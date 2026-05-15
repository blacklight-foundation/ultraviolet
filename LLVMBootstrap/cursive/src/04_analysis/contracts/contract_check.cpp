/*
 * =============================================================================
 * MIGRATION MAPPING: contract_check.cpp
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - CursiveSpecification.md, Section 14 "Contracts" (line 23181)
 *   - CursiveSpecification.md, Section 14.4 "Contract Syntax" (line 23183)
 *   - CursiveSpecification.md, Section 14.5 "Preconditions and Postconditions" (line 23279)
 *   - CursiveSpecification.md, Section 14.6 "Contract Well-Formedness" (lines 23400-23500)
 *
 * SOURCE FILE:
 *   - cursive-bootstrap/src/03_analysis/contracts/contract_check.cpp (lines 1-263)
 *
 * FUNCTIONS MIGRATED:
 *   - CheckContractWellFormed(Contract* contract) -> bool
 *       Validate contract syntax and semantics
 *   - CheckPrecondition(Precond* pre, ProcDecl* proc) -> bool
 *       Validate precondition references valid parameters
 *   - CheckPostcondition(Postcond* post, ProcDecl* proc) -> bool
 *       Validate postcondition with @result and @entry
 *   - ValidateContractExpression(Expr* expr) -> bool
 *       Ensure contract expression is well-typed boolean
 *   - CheckContractScope(Contract* contract, ProcDecl* proc) -> bool
 *       Validate contract only references in-scope names
 *
 * DEPENDENCIES:
 *   - Contract, Precondition, Postcondition AST nodes
 *   - Expression type checking
 *   - Name resolution
 *
 * REFACTORING NOTES:
 *   1. Contract syntax: |: P (precond), |: P => Q (pre+post), |: => Q (postcond only)
 *   2. @result references return value (postcondition only)
 *   3. @entry(expr) captures entry/old value of expression
 *   4. @entry requires BitcopyType
 *   5. Contracts must be boolean expressions
 *   6. Contract predicates must be PURE (see contract_purity.cpp)
 *
 * CONTRACT FORMS:
 *   |: precondition
 *   |: precondition => postcondition
 *   |: => postcondition
 *
 * INTRINSICS:
 *   - @result: Return value (postcondition context only)
 *   - @entry(expr): Captured entry value
 *
 * DIAGNOSTIC CODES:
 *   - E-SEM-2802: Impure expression in contract predicate
 *   - E-SEM-2805: @entry() result type not BitcopyType
 *   - E-SEM-2806: @result used outside postcondition
 *   - E-CON-0001: Invalid contract syntax
 *   - E-CON-0002: @result outside postcondition
 *   - E-CON-0003: @entry with non-copyable type
 *   - WF-Contract: Contract predicate is not boolean
 *   - E-CON-0005: Undefined name in contract
 *
 * =============================================================================
 */

#include "04_analysis/contracts/contract_check.h"

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <string>
#include <unordered_set>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_requirements.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/composite/record_methods.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/modal/modal_fields.h"
#include "04_analysis/modal/modal_transitions.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_lookup.h"

namespace cursive::analysis
{

  namespace
  {

    static inline void SpecDefsContractCheck()
    {
      SPEC_DEF("WF-Contract", "C0X.5.W");
      SPEC_DEF("ContractPure", "C0X.5.W");
      SPEC_DEF("PreContext", "C0X.5.W");
      SPEC_DEF("PostContext", "C0X.5.W");
      SPEC_DEF("TypeInvariant", "C0X.5.W");
      SPEC_DEF("LoopInvariant", "C0X.5.W");
      SPEC_DEF("LSP", "C0X.5.W");
    }

    // Check if expression contains @result
    bool ContainsResult(const ast::ExprPtr &expr)
    {
      if (!expr)
        return false;

      if (std::holds_alternative<ast::ResultExpr>(expr->node))
      {
        return true;
      }

      // Recursively check sub-expressions
      return std::visit(
          [](const auto &node) -> bool
          {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::BinaryExpr>)
            {
              return ContainsResult(node.lhs) || ContainsResult(node.rhs);
            }
            else if constexpr (std::is_same_v<T, ast::UnaryExpr>)
            {
              return ContainsResult(node.value);
            }
            else if constexpr (std::is_same_v<T, ast::PipelineExpr>)
            {
              return ContainsResult(node.lhs) || ContainsResult(node.rhs);
            }
            else if constexpr (std::is_same_v<T, ast::CallExpr>)
            {
              if (ContainsResult(node.callee))
                return true;
              for (const auto &arg : node.args)
              {
                if (ContainsResult(arg.value))
                  return true;
              }
              return false;
            }
            else if constexpr (std::is_same_v<T, ast::MethodCallExpr>)
            {
              if (ContainsResult(node.receiver))
                return true;
              for (const auto &arg : node.args)
              {
                if (ContainsResult(arg.value))
                  return true;
              }
              return false;
            }
            return false;
          },
          expr->node);
    }

    // Check if expression contains @entry
    bool ContainsEntry(const ast::ExprPtr &expr)
    {
      if (!expr)
        return false;

      if (std::holds_alternative<ast::EntryExpr>(expr->node))
      {
        return true;
      }

      return std::visit(
          [](const auto &node) -> bool
          {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::BinaryExpr>)
            {
              return ContainsEntry(node.lhs) || ContainsEntry(node.rhs);
            }
            else if constexpr (std::is_same_v<T, ast::UnaryExpr>)
            {
              return ContainsEntry(node.value);
            }
            else if constexpr (std::is_same_v<T, ast::PipelineExpr>)
            {
              return ContainsEntry(node.lhs) || ContainsEntry(node.rhs);
            }
            else if constexpr (std::is_same_v<T, ast::CallExpr>)
            {
              if (ContainsEntry(node.callee))
                return true;
              for (const auto &arg : node.args)
              {
                if (ContainsEntry(arg.value))
                  return true;
              }
              return false;
            }
            return false;
          },
          expr->node);
    }

    void AddImplicationFacts(StaticProofContext &proof_ctx,
                             const ast::ExprPtr &predicate,
                             const core::Span &target_span)
    {
      if (!predicate)
      {
        return;
      }
      if (const auto *binary = std::get_if<ast::BinaryExpr>(&predicate->node);
          binary && binary->op == "&&")
      {
        AddImplicationFacts(proof_ctx, binary->lhs, target_span);
        AddImplicationFacts(proof_ctx, binary->rhs, target_span);
        return;
      }
      // Implication checks are not tied to CFG dominance in a concrete function
      // body, so use the target span for all facts in the proof context.
      AddFact(proof_ctx, predicate, target_span);
    }

    bool PredicateImplies(const ast::ExprPtr &antecedent,
                          const ast::ExprPtr &consequent)
    {
      if (!consequent)
      {
        return true;
      }

      StaticProofContext proof_ctx;
      if (antecedent)
      {
        AddImplicationFacts(proof_ctx, antecedent, consequent->span);
      }

      const auto proof =
          StaticProofAt(proof_ctx, consequent ? consequent->span : core::Span{},
                        consequent);
      return proof.provable;
    }

    const ast::ASTModule *FindModuleByPath(const ScopeContext &ctx,
                                           const ast::ModulePath &path)
    {
      for (const auto &mod : ctx.sigma.mods)
      {
        if (mod.path == path)
        {
          return &mod;
        }
      }
      return nullptr;
    }

    const ast::ProcedureDecl *FindProcedureInModule(const ast::ASTModule &module,
                                                    std::string_view name)
    {
      for (const auto &item : module.items)
      {
        if (const auto *proc = std::get_if<ast::ProcedureDecl>(&item))
        {
          if (IdEq(proc->name, name))
          {
            return proc;
          }
        }
      }
      return nullptr;
    }

    const ast::ComptimeProcedureDecl *FindComptimeProcedureInModule(
        const ast::ASTModule &module,
        std::string_view name)
    {
      for (const auto &proc : module.comptime_procedures)
      {
        if (IdEq(proc.name, name))
        {
          return &proc;
        }
      }
      for (const auto &item : module.items)
      {
        if (const auto *proc = std::get_if<ast::ComptimeProcedureDecl>(&item))
        {
          if (IdEq(proc->name, name))
          {
            return proc;
          }
        }
      }
      return nullptr;
    }

    struct ContractProcedureLookupResult
    {
      const ast::ProcedureDecl *proc = nullptr;
      const ast::ComptimeProcedureDecl *comptime_proc = nullptr;
      ast::ModulePath origin;
    };

    ast::ModulePath ModulePathForOwnedType(const TypePath &type_path)
    {
      ast::ModulePath module_path;
      if (type_path.empty())
      {
        return module_path;
      }
      module_path.reserve(type_path.size() - 1);
      for (std::size_t index = 0; index + 1 < type_path.size(); ++index)
      {
        module_path.push_back(type_path[index]);
      }
      return module_path;
    }

    struct ScopedCurrentModule
    {
      ScopeContext &ctx;
      ast::ModulePath saved_module;

      ScopedCurrentModule(const ScopeContext &scope_ctx,
                          const ast::ModulePath &module_path)
          : ctx(const_cast<ScopeContext &>(scope_ctx)),
            saved_module(ctx.current_module)
      {
        ctx.current_module = module_path;
      }

      ~ScopedCurrentModule()
      {
        ctx.current_module = saved_module;
      }
    };

    std::optional<ContractProcedureLookupResult> LookupProcedureForCallee(
        const ScopeContext &ctx,
        const ast::ExprPtr &callee)
    {
      if (!callee)
      {
        return std::nullopt;
      }

      std::string name;
      std::optional<ast::ModulePath> origin;

      if (const auto *ident = std::get_if<ast::IdentifierExpr>(&callee->node))
      {
        const auto ent = ResolveValueName(ctx, ident->name);
        if (ent && ent->origin_opt.has_value())
        {
          origin = *ent->origin_opt;
          name = ent->target_opt.value_or(std::string(ident->name));
        }
        else
        {
          origin = ctx.current_module;
          name = ident->name;
        }
      }
      else if (const auto *qualified =
                   std::get_if<ast::QualifiedNameExpr>(&callee->node))
      {
        origin = qualified->path;
        name = qualified->name;
      }
      else if (const auto *path_expr = std::get_if<ast::PathExpr>(&callee->node))
      {
        origin = path_expr->path.empty() ? ctx.current_module : path_expr->path;
        name = path_expr->name;
      }
      else
      {
        return std::nullopt;
      }

      if (!origin.has_value())
      {
        return std::nullopt;
      }
      const auto *module = FindModuleByPath(ctx, *origin);
      if (!module)
      {
        return std::nullopt;
      }
      const auto *proc = FindProcedureInModule(*module, name);
      if (proc)
      {
        return ContractProcedureLookupResult{proc, nullptr, *origin};
      }
      const auto *comptime_proc = FindComptimeProcedureInModule(*module, name);
      if (!comptime_proc)
      {
        return std::nullopt;
      }
      return ContractProcedureLookupResult{nullptr, comptime_proc, *origin};
    }

    bool HasCapabilityParams(const ast::ProcedureDecl &proc)
    {
      for (const auto &param : proc.params)
      {
        if (!param.type)
        {
          continue;
        }
        if (!InferCapabilitiesFromAstType(*param.type).IsEmpty())
        {
          return true;
        }
      }
      return false;
    }

    bool HasCapabilityParams(const std::vector<ast::Param> &params)
    {
      for (const auto &param : params)
      {
        if (!param.type)
        {
          continue;
        }
        if (!InferCapabilitiesFromAstType(*param.type).IsEmpty())
        {
          return true;
        }
      }
      return false;
    }

    bool QualifiedPathIs(const ast::ModulePath &path,
                         std::initializer_list<std::string_view> expected)
    {
      if (path.size() != expected.size())
      {
        return false;
      }
      std::size_t index = 0;
      for (const auto segment : expected)
      {
        if (!IdEq(path[index], segment))
        {
          return false;
        }
        ++index;
      }
      return true;
    }

    bool IsPureBuiltinQualifiedCall(const ast::QualifiedApplyExpr &expr)
    {
      if (!std::holds_alternative<ast::ParenArgs>(expr.args))
      {
        return false;
      }
      if (QualifiedPathIs(expr.path, {"string"}))
      {
        return IdEq(expr.name, "length") ||
               IdEq(expr.name, "as_view") ||
               IdEq(expr.name, "slice");
      }
      if (QualifiedPathIs(expr.path, {"bytes"}))
      {
        return IdEq(expr.name, "view_string") ||
               IdEq(expr.name, "as_slice");
      }
      return false;
    }

    bool IsPureBuiltinQualifiedName(const ast::ModulePath &path,
                                    std::string_view name)
    {
      if (QualifiedPathIs(path, {"string"}))
      {
        return IdEq(name, "length") ||
               IdEq(name, "as_view") ||
               IdEq(name, "slice");
      }
      if (QualifiedPathIs(path, {"bytes"}))
      {
        return IdEq(name, "view_string") ||
               IdEq(name, "as_slice");
      }
      return false;
    }

    bool IsPureBuiltinCall(const ast::CallExpr &expr)
    {
      if (!expr.callee)
      {
        return false;
      }
      if (const auto *qualified =
              std::get_if<ast::QualifiedNameExpr>(&expr.callee->node))
      {
        return IsPureBuiltinQualifiedName(qualified->path, qualified->name);
      }
      if (const auto *path = std::get_if<ast::PathExpr>(&expr.callee->node))
      {
        return IsPureBuiltinQualifiedName(path->path, path->name);
      }
      return false;
    }

    struct PurityStack
    {
      std::unordered_set<const ast::ProcedureDecl *> procedures;
      std::unordered_set<const ast::MethodDecl *> record_methods;
      std::unordered_set<const ast::ClassMethodDecl *> class_methods;
      std::unordered_set<const ast::StateMethodDecl *> state_methods;
    };

    bool IsImpureExpr(ContractContext *ctx,
                      const ast::ExprPtr &expr,
                      PurityStack &purity_stack);

    void CollectPatternBindingNames(const ast::PatternPtr &pattern,
                                    std::unordered_set<std::string> &out);

    void CollectFieldPatternBindingNames(const ast::FieldPattern &field,
                                         std::unordered_set<std::string> &out)
    {
      if (field.pattern_opt)
      {
        CollectPatternBindingNames(field.pattern_opt, out);
      }
      else if (!IdEq(field.name, "_"))
      {
        out.insert(field.name);
      }
    }

    void CollectPatternBindingNames(const ast::PatternPtr &pattern,
                                    std::unordered_set<std::string> &out)
    {
      if (!pattern)
      {
        return;
      }
      std::visit(
          [&](const auto &node)
          {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::IdentifierPattern>)
            {
              if (!IdEq(node.name, "_"))
              {
                out.insert(node.name);
              }
            }
            else if constexpr (std::is_same_v<T, ast::TypedPattern>)
            {
              if (!IdEq(node.name, "_"))
              {
                out.insert(node.name);
              }
            }
            else if constexpr (std::is_same_v<T, ast::TuplePattern>)
            {
              for (const auto &elem : node.elements)
              {
                CollectPatternBindingNames(elem, out);
              }
            }
            else if constexpr (std::is_same_v<T, ast::RecordPattern>)
            {
              for (const auto &field : node.fields)
              {
                CollectFieldPatternBindingNames(field, out);
              }
            }
            else if constexpr (std::is_same_v<T, ast::EnumPattern>)
            {
              if (!node.payload_opt.has_value())
              {
                return;
              }
              std::visit(
                  [&](const auto &payload)
                  {
                    using P = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>)
                    {
                      for (const auto &elem : payload.elements)
                      {
                        CollectPatternBindingNames(elem, out);
                      }
                    }
                    else
                    {
                      for (const auto &field : payload.fields)
                      {
                        CollectFieldPatternBindingNames(field, out);
                      }
                    }
                  },
                  *node.payload_opt);
            }
            else if constexpr (std::is_same_v<T, ast::ModalPattern>)
            {
              if (!node.fields_opt.has_value())
              {
                return;
              }
              for (const auto &field : node.fields_opt->fields)
              {
                CollectFieldPatternBindingNames(field, out);
              }
            }
            else if constexpr (std::is_same_v<T, ast::RangePattern>)
            {
              CollectPatternBindingNames(node.lo, out);
              CollectPatternBindingNames(node.hi, out);
            }
          },
          pattern->node);
    }

    void IntroduceLocalBindings(ContractContext *ctx,
                                const ast::PatternPtr &pattern)
    {
      if (!ctx)
      {
        return;
      }
      CollectPatternBindingNames(pattern, ctx->local_bindings);
    }

    std::optional<std::string> AssignmentRootName(const ast::ExprPtr &expr)
    {
      if (!expr)
      {
        return std::nullopt;
      }
      return std::visit(
          [&](const auto &node) -> std::optional<std::string>
          {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::IdentifierExpr>)
            {
              return node.name;
            }
            else if constexpr (std::is_same_v<T, ast::PathExpr>)
            {
              if (node.path.empty())
              {
                return node.name;
              }
              return std::nullopt;
            }
            else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>)
            {
              return AssignmentRootName(node.base);
            }
            else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>)
            {
              return AssignmentRootName(node.base);
            }
            else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>)
            {
              return AssignmentRootName(node.base);
            }
            return std::nullopt;
          },
          expr->node);
    }

    bool AssignmentTargetsLocalBinding(const ContractContext *ctx,
                                       const ast::ExprPtr &place)
    {
      const auto root = AssignmentRootName(place);
      return ctx && root.has_value() &&
             ctx->local_bindings.find(*root) != ctx->local_bindings.end();
    }

    TypeRef LookupExprTypeFromContext(const ContractContext *ctx,
                                      const ast::ExprPtr &expr)
    {
      if (!ctx || !ctx->scope_ctx || !ctx->scope_ctx->expr_types || !expr)
      {
        return nullptr;
      }
      const auto it = ctx->scope_ctx->expr_types->find(expr.get());
      if (it == ctx->scope_ctx->expr_types->end())
      {
        return nullptr;
      }
      return it->second;
    }

    TypeRef LookupContractBindingType(const ContractContext *ctx,
                                      std::string_view name)
    {
      if (!ctx)
      {
        return nullptr;
      }
      if (IdEq(name, "self"))
      {
        return ctx->receiver_type;
      }
      const auto it = ctx->params.find(std::string(name));
      if (it != ctx->params.end())
      {
        return it->second;
      }
      return nullptr;
    }

    const ast::TypeAliasDecl *LookupContractTypeAliasDecl(const ScopeContext &ctx,
                                                          const TypePath &path)
    {
      ast::Path syntax_path;
      syntax_path.reserve(path.size());
      for (const auto &segment : path)
      {
        syntax_path.push_back(segment);
      }

      const auto it = ctx.sigma.types.find(PathKeyOf(syntax_path));
      if (it != ctx.sigma.types.end())
      {
        return std::get_if<ast::TypeAliasDecl>(&it->second);
      }

      if (path.size() != 1)
      {
        return nullptr;
      }

      const auto ent = ResolveTypeName(ctx, path[0]);
      if (!ent.has_value() || !ent->origin_opt.has_value())
      {
        return nullptr;
      }

      ast::Path resolved = *ent->origin_opt;
      const std::string resolved_name =
          ent->target_opt.has_value() ? *ent->target_opt : path[0];
      resolved.push_back(resolved_name);

      const auto resolved_it = ctx.sigma.types.find(PathKeyOf(resolved));
      if (resolved_it == ctx.sigma.types.end())
      {
        return nullptr;
      }

      return std::get_if<ast::TypeAliasDecl>(&resolved_it->second);
    }

    TypeRef NormalizeContractAliasType(const ContractContext *ctx,
                                       const TypeRef &type,
                                       std::size_t depth = 0)
    {
      if (!ctx || !ctx->scope_ctx || !type || depth > 16)
      {
        return type;
      }

      if (const auto *perm = std::get_if<TypePerm>(&type->node))
      {
        return MakeTypePerm(
            perm->perm,
            NormalizeContractAliasType(ctx, perm->base, depth + 1));
      }
      if (const auto *refine = std::get_if<TypeRefine>(&type->node))
      {
        return MakeTypeRefine(
            NormalizeContractAliasType(ctx, refine->base, depth + 1),
            refine->predicate);
      }
      if (const auto *union_type = std::get_if<TypeUnion>(&type->node))
      {
        std::vector<TypeRef> members;
        members.reserve(union_type->members.size());
        for (const auto &member : union_type->members)
        {
          members.push_back(NormalizeContractAliasType(ctx, member, depth + 1));
        }
        return MakeTypeUnion(std::move(members));
      }

      const TypePath *path_name = AppliedTypePath(*type);
      const std::vector<TypeRef> *path_args = AppliedTypeArgs(*type);
      if (!path_name || path_name->empty())
      {
        return type;
      }

      const ast::TypeAliasDecl *alias =
          LookupContractTypeAliasDecl(*ctx->scope_ctx, *path_name);
      if (!alias)
      {
        return type;
      }

      const auto lowered = LowerType(*ctx->scope_ctx, alias->type);
      if (!lowered.ok || !lowered.type)
      {
        return type;
      }

      TypeRef instantiated = lowered.type;
      if (alias->generic_params.has_value() && path_args)
      {
        const auto &params = alias->generic_params->params;
        if (path_args->size() > params.size())
        {
          return type;
        }
        const auto subst = BuildSubstitution(params, *path_args);
        instantiated = InstantiateType(instantiated, subst);
        if (!instantiated)
        {
          return type;
        }
      }

      return NormalizeContractAliasType(ctx, instantiated, depth + 1);
    }

    TypeRef StripPermOrSelf(const TypeRef &type);

    TypeRef InferContractExprType(const ContractContext *ctx,
                                  const ast::ExprPtr &expr)
    {
      if (!expr)
      {
        return nullptr;
      }

      if (const auto *ident = std::get_if<ast::IdentifierExpr>(&expr->node))
      {
        if (TypeRef binding_type = LookupContractBindingType(ctx, ident->name))
        {
          return NormalizeContractAliasType(ctx, binding_type);
        }
      }
      if (std::holds_alternative<ast::ResultExpr>(expr->node))
      {
        return NormalizeContractAliasType(
            ctx,
            (ctx && ctx->is_postcondition) ? ctx->return_type : nullptr);
      }

      if (TypeRef from_map = LookupExprTypeFromContext(ctx, expr))
      {
        return NormalizeContractAliasType(ctx, from_map);
      }

      return std::visit(
          [&](const auto &node) -> TypeRef
          {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::IdentifierExpr>)
            {
              return NormalizeContractAliasType(
                  ctx, LookupContractBindingType(ctx, node.name));
            }
            else if constexpr (std::is_same_v<T, ast::ResultExpr>)
            {
              return NormalizeContractAliasType(
                  ctx,
                  (ctx && ctx->is_postcondition) ? ctx->return_type : nullptr);
            }
            else if constexpr (std::is_same_v<T, ast::EntryExpr>)
            {
              return InferContractExprType(ctx, node.expr);
            }
            else if constexpr (std::is_same_v<T, ast::AttributedExpr>)
            {
              return InferContractExprType(ctx, node.expr);
            }
            else if constexpr (std::is_same_v<T, ast::CastExpr>)
            {
              if (!ctx || !ctx->scope_ctx || !node.type)
              {
                return nullptr;
              }
              const auto lowered = LowerType(*ctx->scope_ctx, node.type);
              return lowered.ok ? lowered.type : nullptr;
            }
            else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>)
            {
              if (!ctx || !ctx->scope_ctx)
              {
                return nullptr;
              }

              TypeRef base =
                  StripPermOrSelf(InferContractExprType(ctx, node.base));
              base = StripPermOrSelf(NormalizeContractAliasType(ctx, base));
              if (!base)
              {
                return nullptr;
              }

              if (const TypePath *path = AppliedTypePath(*base))
              {
                const auto *record =
                    LookupRecordDecl(*ctx->scope_ctx, *path);
                if (!record)
                {
                  return nullptr;
                }
                const auto *path_args = AppliedTypeArgs(*base);
                const auto field =
                    FieldType(*record, node.name, *ctx->scope_ctx,
                              path_args ? *path_args
                                        : std::vector<TypeRef>{});
                return field.has_value()
                    ? NormalizeContractAliasType(ctx, *field)
                    : nullptr;
              }

              if (const auto *modal =
                      std::get_if<TypeModalState>(&base->node))
              {
                const ast::ModalDecl *decl =
                    LookupModalDecl(*ctx->scope_ctx, modal->path);
                if (!decl)
                {
                  return nullptr;
                }
                const ast::StateFieldDecl *field =
                    LookupModalFieldDecl(*decl, modal->state, node.name);
                if (!field || !field->type)
                {
                  return nullptr;
                }
                const auto lowered = LowerType(*ctx->scope_ctx, field->type);
                if (!lowered.ok || !lowered.type)
                {
                  return nullptr;
                }

                TypeRef field_type = lowered.type;
                if (decl->generic_params.has_value())
                {
                  const TypeSubst subst = BuildModalRefSubstitution(
                      decl->generic_params->params, modal->generic_args);
                  field_type = InstantiateType(field_type, subst);
                }
                return NormalizeContractAliasType(ctx, field_type);
              }
              return nullptr;
            }
            return nullptr;
      },
      expr->node);
    }

    bool ReceiverIsConst(const ast::Receiver &receiver, const ScopeContext *scope_ctx)
    {
      return std::visit(
          [&](const auto &recv) -> bool
          {
            using R = std::decay_t<decltype(recv)>;
            if constexpr (std::is_same_v<R, ast::ReceiverShorthand>)
            {
              return recv.perm == ast::ReceiverPerm::Const;
            }
            else
            {
              if (!scope_ctx || !recv.type || recv.mode_opt.has_value())
              {
                return false;
              }
              const auto lowered = LowerType(*scope_ctx, recv.type);
              if (!lowered.ok || !lowered.type)
              {
                return false;
              }
              return PermOfType(lowered.type) == Permission::Const;
            }
          },
          receiver);
    }

    TypeRef StripPermOrSelf(const TypeRef &type)
    {
      if (!type)
      {
        return nullptr;
      }
      TypeRef stripped = StripPerm(type);
      return stripped ? stripped : type;
    }

    TypeRef ModalStateMemberForPattern(const ContractContext *ctx,
                                       const TypeRef &type,
                                       std::string_view state_name)
    {
      const TypeRef stripped =
          StripPermOrSelf(NormalizeContractAliasType(ctx, type));
      if (!stripped)
      {
        return nullptr;
      }
      if (const auto *modal = std::get_if<TypeModalState>(&stripped->node))
      {
        return IdEq(modal->state, state_name) ? stripped : nullptr;
      }
      if (const auto *union_type = std::get_if<TypeUnion>(&stripped->node))
      {
        for (const auto &member : union_type->members)
        {
          TypeRef matched = ModalStateMemberForPattern(ctx, member, state_name);
          if (matched)
          {
            return matched;
          }
        }
      }
      return nullptr;
    }

    TypeRef ContractModalStateFieldType(const ContractContext *ctx,
                                        const TypeRef &state_type,
                                        std::string_view field_name)
    {
      if (!ctx || !ctx->scope_ctx || !state_type)
      {
        return nullptr;
      }
      TypeRef stripped =
          StripPermOrSelf(NormalizeContractAliasType(ctx, state_type));
      if (!stripped)
      {
        return nullptr;
      }
      const auto *modal = std::get_if<TypeModalState>(&stripped->node);
      if (!modal)
      {
        return nullptr;
      }
      const ast::ModalDecl *decl =
          LookupModalDecl(*ctx->scope_ctx, modal->path);
      if (!decl)
      {
        return nullptr;
      }
      const ast::StateFieldDecl *field =
          LookupModalFieldDecl(*decl, modal->state, field_name);
      if (!field || !field->type)
      {
        return nullptr;
      }
      const auto lowered = LowerType(*ctx->scope_ctx, field->type);
      if (!lowered.ok || !lowered.type)
      {
        return nullptr;
      }
      TypeRef field_type = lowered.type;
      if (decl->generic_params.has_value())
      {
        const TypeSubst subst = BuildModalRefSubstitution(
            decl->generic_params->params, modal->generic_args);
        field_type = InstantiateType(field_type, subst);
      }
      return NormalizeContractAliasType(ctx, field_type);
    }

    TypeRef ContractRecordFieldType(const ContractContext *ctx,
                                    const TypeRef &record_type,
                                    std::string_view field_name)
    {
      if (!ctx || !ctx->scope_ctx || !record_type)
      {
        return nullptr;
      }
      TypeRef stripped =
          StripPermOrSelf(NormalizeContractAliasType(ctx, record_type));
      if (!stripped)
      {
        return nullptr;
      }
      const TypePath *path = AppliedTypePath(*stripped);
      if (!path)
      {
        return nullptr;
      }
      const ast::RecordDecl *record =
          LookupRecordDecl(*ctx->scope_ctx, *path);
      if (!record)
      {
        return nullptr;
      }
      const auto *path_args = AppliedTypeArgs(*stripped);
      const auto field =
          FieldType(*record, field_name, *ctx->scope_ctx,
                    path_args ? *path_args : std::vector<TypeRef>{});
      return field.has_value()
          ? NormalizeContractAliasType(ctx, *field)
          : nullptr;
    }

    void BindPatternTypesFromExpected(ContractContext *ctx,
                                      const ast::PatternPtr &pattern,
                                      const TypeRef &expected_type)
    {
      if (!ctx || !pattern || !expected_type)
      {
        return;
      }
      const TypeRef normalized =
          NormalizeContractAliasType(ctx, expected_type);
      std::visit(
          [&](const auto &node)
          {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::IdentifierPattern>)
            {
              if (!IdEq(node.name, "_"))
              {
                ctx->params[node.name] = normalized;
                ctx->local_bindings.insert(node.name);
              }
            }
            else if constexpr (std::is_same_v<T, ast::TypedPattern>)
            {
              if (IdEq(node.name, "_"))
              {
                return;
              }
              if (!node.type || !ctx->scope_ctx)
              {
                ctx->params[node.name] = normalized;
                ctx->local_bindings.insert(node.name);
                return;
              }
              const auto lowered = LowerType(*ctx->scope_ctx, node.type);
              ctx->params[node.name] =
                  (lowered.ok && lowered.type)
                      ? NormalizeContractAliasType(ctx, lowered.type)
                      : normalized;
              ctx->local_bindings.insert(node.name);
            }
            else if constexpr (std::is_same_v<T, ast::TuplePattern>)
            {
              const TypeRef stripped = StripPermOrSelf(normalized);
              const auto *tuple =
                  stripped ? std::get_if<TypeTuple>(&stripped->node) : nullptr;
              if (!tuple)
              {
                return;
              }
              const std::size_t count =
                  std::min(node.elements.size(), tuple->elements.size());
              for (std::size_t index = 0; index < count; ++index)
              {
                BindPatternTypesFromExpected(ctx, node.elements[index],
                                             tuple->elements[index]);
              }
            }
            else if constexpr (std::is_same_v<T, ast::RecordPattern>)
            {
              for (const auto &field : node.fields)
              {
                TypeRef field_type =
                    ContractRecordFieldType(ctx, normalized, field.name);
                if (!field_type)
                {
                  continue;
                }
                if (field.pattern_opt)
                {
                  BindPatternTypesFromExpected(ctx, field.pattern_opt,
                                               field_type);
                }
                else if (!IdEq(field.name, "_"))
                {
                  ctx->params[field.name] = field_type;
                  ctx->local_bindings.insert(field.name);
                }
              }
            }
            else if constexpr (std::is_same_v<T, ast::ModalPattern>)
            {
              TypeRef state_type =
                  ModalStateMemberForPattern(ctx, normalized, node.state);
              if (!state_type || !node.fields_opt.has_value())
              {
                return;
              }
              for (const auto &field : node.fields_opt->fields)
              {
                TypeRef field_type =
                    ContractModalStateFieldType(ctx, state_type, field.name);
                if (!field_type)
                {
                  continue;
                }
                if (field.pattern_opt)
                {
                  BindPatternTypesFromExpected(ctx, field.pattern_opt,
                                               field_type);
                }
                else if (!IdEq(field.name, "_"))
                {
                  ctx->params[field.name] = field_type;
                  ctx->local_bindings.insert(field.name);
                }
              }
            }
          },
          pattern->node);
    }

    TypeRef BindingDeclaredOrInferredType(ContractContext *ctx,
                                          const ast::Binding &binding)
    {
      if (!ctx)
      {
        return nullptr;
      }
      if (ast::TypePtr annotation = ast::BindingAnnotationTypeOpt(binding))
      {
        if (!ctx->scope_ctx)
        {
          return nullptr;
        }
        const auto lowered = LowerType(*ctx->scope_ctx, annotation);
        return lowered.ok ? NormalizeContractAliasType(ctx, lowered.type)
                          : nullptr;
      }
      return InferContractExprType(ctx, binding.init);
    }

    std::optional<std::string> ScrutineeBindingName(const ast::ExprPtr &expr)
    {
      if (!expr)
      {
        return std::nullopt;
      }
      if (const auto *ident = std::get_if<ast::IdentifierExpr>(&expr->node))
      {
        return ident->name;
      }
      if (const auto *path = std::get_if<ast::PathExpr>(&expr->node);
          path && path->path.empty())
      {
        return path->name;
      }
      return std::nullopt;
    }

    ContractContext NarrowContractContextForPattern(
        const ContractContext *ctx,
        const ast::ExprPtr &scrutinee,
        const ast::PatternPtr &pattern)
    {
      ContractContext narrowed = ctx ? *ctx : ContractContext{};
      if (!ctx || !pattern)
      {
        return narrowed;
      }
      const auto *modal_pattern = std::get_if<ast::ModalPattern>(&pattern->node);
      if (!modal_pattern)
      {
        return narrowed;
      }
      const auto binding_name = ScrutineeBindingName(scrutinee);
      if (!binding_name.has_value())
      {
        return narrowed;
      }
      TypeRef scrutinee_type = LookupContractBindingType(ctx, *binding_name);
      if (!scrutinee_type)
      {
        scrutinee_type = InferContractExprType(ctx, scrutinee);
      }
      if (!scrutinee_type)
      {
        return narrowed;
      }
      TypeRef state_type =
          ModalStateMemberForPattern(ctx, scrutinee_type, modal_pattern->state);
      if (!state_type)
      {
        return narrowed;
      }
      if (IdEq(*binding_name, "self"))
      {
        narrowed.receiver_type = state_type;
      }
      else
      {
        narrowed.params[*binding_name] = state_type;
      }
      BindPatternTypesFromExpected(&narrowed, pattern, state_type);
      return narrowed;
    }

    bool IsPureProcedure(const ScopeContext &scope_ctx,
                         const ast::ModulePath &owner_module,
                         const ast::ProcedureDecl &proc,
                         PurityStack &purity_stack);

    bool IsPureRecordMethod(const ScopeContext &scope_ctx,
                            const ast::MethodDecl &method,
                            const TypeRef &receiver_type,
                            const ast::ModulePath &owner_module,
                            PurityStack &purity_stack);

    bool IsPureClassMethod(const ScopeContext &scope_ctx,
                           const ast::ClassMethodDecl &method,
                           const TypeRef &receiver_type,
                           const ast::ModulePath &owner_module,
                           PurityStack &purity_stack);

    bool IsPureStateMethod(const ScopeContext &scope_ctx,
                           const ast::StateMethodDecl &method,
                           const TypeRef &receiver_type,
                           const ast::ModulePath &owner_module,
                           PurityStack &purity_stack);

    bool IsImpureStmt(ContractContext *ctx,
                      const ast::Stmt &stmt,
                      PurityStack &purity_stack)
    {
      return std::visit(
          [&](const auto &node) -> bool
          {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::LetStmt> ||
                          std::is_same_v<T, ast::VarStmt>)
            {
              const bool init_impure =
                  IsImpureExpr(ctx, node.binding.init, purity_stack);
              TypeRef binding_type =
                  BindingDeclaredOrInferredType(ctx, node.binding);
              IntroduceLocalBindings(ctx, node.binding.pat);
              BindPatternTypesFromExpected(ctx, node.binding.pat, binding_type);
              return init_impure;
            }
            else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>)
            {
              // UsingLocalStmt is a compile-time alias; no runtime expression.
              (void)node;
              return false;
            }
            else if constexpr (std::is_same_v<T, ast::ExprStmt>)
            {
              return IsImpureExpr(ctx, node.value, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::ReturnStmt>)
            {
              return IsImpureExpr(ctx, node.value_opt, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::AssignStmt>)
            {
              return IsImpureExpr(ctx, node.place, purity_stack) ||
                     IsImpureExpr(ctx, node.value, purity_stack) ||
                     !AssignmentTargetsLocalBinding(ctx, node.place);
            }
            else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>)
            {
              return IsImpureExpr(ctx, node.place, purity_stack) ||
                     IsImpureExpr(ctx, node.value, purity_stack) ||
                     !AssignmentTargetsLocalBinding(ctx, node.place);
            }
            else if constexpr (std::is_same_v<T, ast::BreakStmt> ||
                               std::is_same_v<T, ast::ContinueStmt>)
            {
              return false;
            }
            else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                               std::is_same_v<T, ast::RegionStmt> ||
                               std::is_same_v<T, ast::FrameStmt> ||
                               std::is_same_v<T, ast::KeyBlockStmt> ||
                               std::is_same_v<T, ast::UnsafeBlockStmt>)
            {
              return true;
            }
            return false;
          },
          stmt);
    }

    bool IsImpureBlock(ContractContext *ctx,
                       const ast::Block &block,
                       PurityStack &purity_stack)
    {
      for (const auto &stmt : block.stmts)
      {
        if (IsImpureStmt(ctx, stmt, purity_stack))
        {
          return true;
        }
      }
      return IsImpureExpr(ctx, block.tail_opt, purity_stack);
    }

    bool IsPureProcedure(const ScopeContext &scope_ctx,
                         const ast::ModulePath &owner_module,
                         const ast::ProcedureDecl &proc,
                         PurityStack &purity_stack)
    {
      if (HasCapabilityParams(proc) || !proc.body)
      {
        return false;
      }
      if (purity_stack.procedures.find(&proc) != purity_stack.procedures.end())
      {
        // Recursion is allowed for pure procedures. Treat active-cycle calls as
        // provisionally pure and let the enclosing traversal detect concrete
        // impure constructs in the strongly connected body.
        return true;
      }
      purity_stack.procedures.insert(&proc);
      ScopedCurrentModule current_module(scope_ctx, owner_module);
      ContractContext proc_ctx;
      proc_ctx.scope_ctx = &scope_ctx;
      proc_ctx.allow_responsibility_moves = true;
      for (const auto &param : proc.params)
      {
        if (!param.type)
        {
          continue;
        }
        const auto lowered = LowerType(scope_ctx, param.type);
        if (!lowered.ok || !lowered.type)
        {
          continue;
        }
        proc_ctx.params[param.name] = lowered.type;
      }
      const bool pure = !IsImpureBlock(&proc_ctx, *proc.body, purity_stack);
      purity_stack.procedures.erase(&proc);
      return pure;
    }

    bool IsPureRecordMethod(const ScopeContext &scope_ctx,
                            const ast::MethodDecl &method,
                            const TypeRef &receiver_type,
                            const ast::ModulePath &owner_module,
                            PurityStack &purity_stack)
    {
      if (!method.body || HasCapabilityParams(method.params) ||
          !ReceiverIsConst(method.receiver, &scope_ctx))
      {
        return false;
      }
      if (purity_stack.record_methods.find(&method) !=
          purity_stack.record_methods.end())
      {
        return true;
      }
      purity_stack.record_methods.insert(&method);
      ScopedCurrentModule current_module(scope_ctx, owner_module);
      ContractContext method_ctx;
      method_ctx.scope_ctx = &scope_ctx;
      method_ctx.receiver_type = receiver_type;
      method_ctx.allow_responsibility_moves = true;
      for (const auto &param : method.params)
      {
        if (!param.type)
        {
          continue;
        }
        const auto lowered = LowerType(scope_ctx, param.type);
        if (!lowered.ok || !lowered.type)
        {
          continue;
        }
        method_ctx.params[param.name] = lowered.type;
      }
      const bool pure = !IsImpureBlock(&method_ctx, *method.body, purity_stack);
      purity_stack.record_methods.erase(&method);
      return pure;
    }

    bool IsPureClassMethod(const ScopeContext &scope_ctx,
                           const ast::ClassMethodDecl &method,
                           const TypeRef &receiver_type,
                           const ast::ModulePath &owner_module,
                           PurityStack &purity_stack)
    {
      if (!method.body_opt || HasCapabilityParams(method.params) ||
          !ReceiverIsConst(method.receiver, &scope_ctx))
      {
        return false;
      }
      if (purity_stack.class_methods.find(&method) !=
          purity_stack.class_methods.end())
      {
        return true;
      }
      purity_stack.class_methods.insert(&method);
      ScopedCurrentModule current_module(scope_ctx, owner_module);
      ContractContext method_ctx;
      method_ctx.scope_ctx = &scope_ctx;
      method_ctx.receiver_type = receiver_type;
      method_ctx.allow_responsibility_moves = true;
      for (const auto &param : method.params)
      {
        if (!param.type)
        {
          continue;
        }
        const auto lowered = LowerType(scope_ctx, param.type);
        if (!lowered.ok || !lowered.type)
        {
          continue;
        }
        method_ctx.params[param.name] = lowered.type;
      }
      const bool pure =
          !IsImpureBlock(&method_ctx, *method.body_opt, purity_stack);
      purity_stack.class_methods.erase(&method);
      return pure;
    }

    bool IsPureStateMethod(const ScopeContext &scope_ctx,
                           const ast::StateMethodDecl &method,
                           const TypeRef &receiver_type,
                           const ast::ModulePath &owner_module,
                           PurityStack &purity_stack)
    {
      if (!method.body || HasCapabilityParams(method.params) ||
          !ReceiverIsConst(method.receiver, &scope_ctx))
      {
        return false;
      }
      if (purity_stack.state_methods.find(&method) !=
          purity_stack.state_methods.end())
      {
        return true;
      }
      purity_stack.state_methods.insert(&method);
      ScopedCurrentModule current_module(scope_ctx, owner_module);
      ContractContext method_ctx;
      method_ctx.scope_ctx = &scope_ctx;
      method_ctx.receiver_type = receiver_type;
      method_ctx.allow_responsibility_moves = true;
      for (const auto &param : method.params)
      {
        if (!param.type)
        {
          continue;
        }
        const auto lowered = LowerType(scope_ctx, param.type);
        if (!lowered.ok || !lowered.type)
        {
          continue;
        }
        method_ctx.params[param.name] = lowered.type;
      }
      const bool pure = !IsImpureBlock(&method_ctx, *method.body, purity_stack);
      purity_stack.state_methods.erase(&method);
      return pure;
    }

    bool IsImpureExpr(ContractContext *ctx,
                      const ast::ExprPtr &expr,
                      PurityStack &purity_stack)
    {
      if (!expr)
      {
        return false;
      }

      return std::visit(
          [&](const auto &node) -> bool
          {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, ast::LiteralExpr> ||
                          std::is_same_v<T, ast::PtrNullExpr> ||
                          std::is_same_v<T, ast::IdentifierExpr> ||
                          std::is_same_v<T, ast::QualifiedNameExpr> ||
                          std::is_same_v<T, ast::PathExpr> ||
                          std::is_same_v<T, ast::ResultExpr>)
            {
              return false;
            }
            else if constexpr (std::is_same_v<T, ast::BinaryExpr>)
            {
              return IsImpureExpr(ctx, node.lhs, purity_stack) ||
                     IsImpureExpr(ctx, node.rhs, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::RangeExpr>)
            {
              return IsImpureExpr(ctx, node.lhs, purity_stack) ||
                     IsImpureExpr(ctx, node.rhs, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::UnaryExpr>)
            {
              return IsImpureExpr(ctx, node.value, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::CastExpr>)
            {
              return IsImpureExpr(ctx, node.value, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::PipelineExpr>)
            {
              return IsImpureExpr(ctx, node.lhs, purity_stack) ||
                     IsImpureExpr(ctx, node.rhs, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>)
            {
              return IsImpureExpr(ctx, node.base, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>)
            {
              return IsImpureExpr(ctx, node.base, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>)
            {
              return IsImpureExpr(ctx, node.base, purity_stack) ||
                     IsImpureExpr(ctx, node.index, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::AddressOfExpr>)
            {
              return IsImpureExpr(ctx, node.place, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::DerefExpr>)
            {
              return IsImpureExpr(ctx, node.value, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::IfExpr>)
            {
              return IsImpureExpr(ctx, node.cond, purity_stack) ||
                     IsImpureExpr(ctx, node.then_expr, purity_stack) ||
                     IsImpureExpr(ctx, node.else_expr, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::IfCaseExpr>)
            {
              if (IsImpureExpr(ctx, node.scrutinee, purity_stack))
              {
                return true;
              }
              for (const auto &arm : node.cases)
              {
                ContractContext arm_ctx =
                    NarrowContractContextForPattern(ctx, node.scrutinee,
                                                    arm.pattern);
                if (IsImpureExpr(&arm_ctx, arm.body, purity_stack))
                {
                  return true;
                }
              }
              return IsImpureExpr(ctx, node.else_expr, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::IfIsExpr>)
            {
              if (IsImpureExpr(ctx, node.scrutinee, purity_stack))
              {
                return true;
              }
              ContractContext then_ctx =
                  NarrowContractContextForPattern(ctx, node.scrutinee,
                                                  node.pattern);
              return IsImpureExpr(&then_ctx, node.then_expr, purity_stack) ||
                     IsImpureExpr(ctx, node.else_expr, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::TupleExpr>)
            {
              for (const auto &elem : node.elements)
              {
                if (IsImpureExpr(ctx, elem, purity_stack))
                {
                  return true;
                }
              }
              return false;
            }
            else if constexpr (std::is_same_v<T, ast::ArrayExpr>)
            {
              bool has_impure_subexpr = false;
              ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr &elem)
              {
                if (has_impure_subexpr)
                {
                  return;
                }
                if (IsImpureExpr(ctx, elem, purity_stack))
                {
                  has_impure_subexpr = true;
                }
              });
              return has_impure_subexpr;
            }
            else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>)
            {
              return IsImpureExpr(ctx, node.value, purity_stack) ||
                     IsImpureExpr(ctx, node.count, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::RecordExpr>)
            {
              for (const auto &field : node.fields)
              {
                if (IsImpureExpr(ctx, field.value, purity_stack))
                {
                  return true;
                }
              }
              return false;
            }
            else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>)
            {
              if (!node.payload_opt.has_value())
              {
                return false;
              }
              return std::visit(
                  [&](const auto &payload) -> bool
                  {
                    using P = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<P, ast::EnumPayloadParen>)
                    {
                      for (const auto &elem : payload.elements)
                      {
                        if (IsImpureExpr(ctx, elem, purity_stack))
                        {
                          return true;
                        }
                      }
                      return false;
                    }
                    else
                    {
                      for (const auto &field : payload.fields)
                      {
                        if (IsImpureExpr(ctx, field.value, purity_stack))
                        {
                          return true;
                        }
                      }
                      return false;
                    }
                  },
                  *node.payload_opt);
            }
            else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>)
            {
              if (std::holds_alternative<ast::ParenArgs>(node.args))
              {
                const auto &paren = std::get<ast::ParenArgs>(node.args);
                for (const auto &arg : paren.args)
                {
                  if (IsImpureExpr(ctx, arg.value, purity_stack))
                  {
                    return true;
                  }
                }
                return !IsPureBuiltinQualifiedCall(node);
              }
              const auto &brace = std::get<ast::BraceArgs>(node.args);
              for (const auto &field : brace.fields)
              {
                if (IsImpureExpr(ctx, field.value, purity_stack))
                {
                  return true;
                }
              }
              return true;
            }
            else if constexpr (std::is_same_v<T, ast::EntryExpr>)
            {
              return IsImpureExpr(ctx, node.expr, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::MoveExpr>)
            {
              return !ctx || !ctx->allow_responsibility_moves ||
                     IsImpureExpr(ctx, node.place, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::AttributedExpr>)
            {
              return IsImpureExpr(ctx, node.expr, purity_stack);
            }
            else if constexpr (std::is_same_v<T, ast::SizeofExpr> ||
                               std::is_same_v<T, ast::AlignofExpr>)
            {
              return false;
            }
            else if constexpr (std::is_same_v<T, ast::CallExpr>)
            {
              if (IsImpureExpr(ctx, node.callee, purity_stack))
              {
                return true;
              }
              for (const auto &arg : node.args)
              {
                if (IsImpureExpr(ctx, arg.value, purity_stack))
                {
                  return true;
                }
              }

              if (IsPureBuiltinCall(node))
              {
                return false;
              }

              if (!ctx || !ctx->scope_ctx)
              {
                return true;
              }
              const auto callee_proc = LookupProcedureForCallee(*ctx->scope_ctx, node.callee);
              if (!callee_proc.has_value())
              {
                return true;
              }
              if (callee_proc->comptime_proc)
              {
                SPEC_RULE("Pure-Comptime");
                return false;
              }
              if (!callee_proc->proc)
              {
                return true;
              }
              const bool callee_pure =
                  IsPureProcedure(*ctx->scope_ctx, callee_proc->origin,
                                  *callee_proc->proc, purity_stack);
              return !callee_pure;
            }
            else if constexpr (std::is_same_v<T, ast::MethodCallExpr>)
            {
              if (IsImpureExpr(ctx, node.receiver, purity_stack))
              {
                return true;
              }
              for (const auto &arg : node.args)
              {
                if (IsImpureExpr(ctx, arg.value, purity_stack))
                {
                  return true;
                }
              }

              if (!ctx || !ctx->scope_ctx)
              {
                return true;
              }
              TypeRef receiver_type = InferContractExprType(ctx, node.receiver);
              receiver_type = StripPermOrSelf(receiver_type);
              if (!receiver_type)
              {
                return true;
              }

              if (const auto *modal = std::get_if<TypeModalState>(&receiver_type->node))
              {
                const ast::ModalDecl *decl = LookupModalDecl(*ctx->scope_ctx, modal->path);
                if (!decl)
                {
                  return true;
                }
                if (LookupTransitionDecl(*decl, modal->state, node.name))
                {
                  return true;
                }
                const ast::StateMethodDecl *state_method =
                    LookupStateMethodDecl(*decl, modal->state, node.name);
                if (!state_method)
                {
                  return true;
                }
                return !IsPureStateMethod(*ctx->scope_ctx, *state_method,
                                          receiver_type,
                                          ModulePathForOwnedType(modal->path),
                                          purity_stack);
              }

              const auto lookup = LookupMethodStatic(*ctx->scope_ctx, receiver_type, node.name);
              if (!lookup.ok)
              {
                return true;
              }
              if (lookup.record_method)
              {
                return !IsPureRecordMethod(*ctx->scope_ctx, *lookup.record_method,
                                           receiver_type,
                                           ModulePathForOwnedType(lookup.record_path),
                                           purity_stack);
              }
              if (lookup.class_method)
              {
                return !IsPureClassMethod(*ctx->scope_ctx, *lookup.class_method,
                                          receiver_type,
                                          ModulePathForOwnedType(lookup.owner_class),
                                          purity_stack);
              }
              return true;
            }
            else if constexpr (std::is_same_v<T, ast::BlockExpr>)
            {
              return node.block ? IsImpureBlock(ctx, *node.block, purity_stack) : false;
            }
            else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>)
            {
              if (node.invariant_opt.has_value() &&
                  IsImpureExpr(ctx, node.invariant_opt->predicate, purity_stack))
              {
                return true;
              }
              return node.body ? IsImpureBlock(ctx, *node.body, purity_stack) : false;
            }
            else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>)
            {
              if (IsImpureExpr(ctx, node.cond, purity_stack))
              {
                return true;
              }
              if (node.invariant_opt.has_value() &&
                  IsImpureExpr(ctx, node.invariant_opt->predicate, purity_stack))
              {
                return true;
              }
              return node.body ? IsImpureBlock(ctx, *node.body, purity_stack) : false;
            }
            else if constexpr (std::is_same_v<T, ast::LoopIterExpr>)
            {
              if (IsImpureExpr(ctx, node.iter, purity_stack))
              {
                return true;
              }
              if (node.invariant_opt.has_value() &&
                  IsImpureExpr(ctx, node.invariant_opt->predicate, purity_stack))
              {
                return true;
              }
              ContractContext loop_ctx = ctx ? *ctx : ContractContext{};
              IntroduceLocalBindings(&loop_ctx, node.pattern);
              return node.body ? IsImpureBlock(&loop_ctx, *node.body, purity_stack) : false;
            }
            else if constexpr (std::is_same_v<T, ast::AllocExpr> ||
                               std::is_same_v<T, ast::YieldExpr> ||
                               std::is_same_v<T, ast::YieldFromExpr> ||
                               std::is_same_v<T, ast::WaitExpr> ||
                               std::is_same_v<T, ast::SyncExpr> ||
                               std::is_same_v<T, ast::SpawnExpr> ||
                               std::is_same_v<T, ast::ParallelExpr> ||
                               std::is_same_v<T, ast::DispatchExpr> ||
                               std::is_same_v<T, ast::TransmuteExpr> ||
                               std::is_same_v<T, ast::UnsafeBlockExpr> ||
                               std::is_same_v<T, ast::ClosureExpr> ||
                               std::is_same_v<T, ast::FenceExpr> ||
                               std::is_same_v<T, ast::PropagateExpr>)
            {
              return true;
            }
            return false;
          },
          expr->node);
    }

    // Check if expression is observably mutating/impure in contract context.
    bool IsMutating(const ContractContext *ctx, const ast::ExprPtr &expr)
    {
      PurityStack purity_stack;
      ContractContext local_ctx = ctx ? *ctx : ContractContext{};
      return IsImpureExpr(&local_ctx, expr, purity_stack);
    }

  } // namespace

  ContractCheckResult CheckContractWellFormed(
      const ContractContext &ctx,
      const ast::ContractClause &contract)
  {
    SpecDefsContractCheck();
    SPEC_RULE("WF-Contract");

    ContractCheckResult result;

    // Check precondition
    if (contract.precondition)
    {
      auto pre_result = CheckPrecondition(ctx, contract.precondition);
      if (!pre_result.ok)
      {
        return pre_result;
      }
    }

    // Check postcondition
    if (contract.postcondition)
    {
      ContractContext post_ctx = ctx;
      post_ctx.is_postcondition = true;
      auto post_result = CheckPostcondition(post_ctx, contract.postcondition);
      if (!post_result.ok)
      {
        return post_result;
      }
    }

    return result;
  }

  ContractCheckResult CheckPrecondition(
      const ContractContext &ctx,
      const ast::ExprPtr &expr)
  {
    SpecDefsContractCheck();
    SPEC_RULE("Check-Pre");

    ContractCheckResult result;

    // Precondition must not contain @result or @entry
    if (ContainsResult(expr))
    {
      result.ok = false;
      result.diag_id = "E-SEM-2806"; // @result in precondition
      result.span = expr->span;
      return result;
    }

    if (ContainsEntry(expr))
    {
      result.ok = false;
      result.diag_id = "E-SEM-2852"; // @entry outside postcondition scope
      result.span = expr->span;
      return result;
    }

    // Check purity
    auto purity = CheckPurity(ctx, expr);
    if (!purity.ok)
    {
      return purity;
    }

    return result;
  }

  ContractCheckResult CheckPostcondition(
      const ContractContext &ctx,
      const ast::ExprPtr &expr)
  {
    SpecDefsContractCheck();
    SPEC_RULE("Check-Post");

    ContractCheckResult result;

    // Postcondition may contain @result and @entry
    // Check purity
    auto purity = CheckPurity(ctx, expr);
    if (!purity.ok)
    {
      return purity;
    }

    return result;
  }

  ContractCheckResult CheckPurity(const ast::ExprPtr &expr)
  {
    ContractContext empty_ctx;
    return CheckPurity(empty_ctx, expr);
  }

  ContractCheckResult CheckPurity(const ContractContext &ctx,
                                  const ast::ExprPtr &expr)
  {
    SpecDefsContractCheck();
    SPEC_RULE("ContractPure");

    ContractCheckResult result;

    if (IsMutating(&ctx, expr))
    {
      result.ok = false;
      result.diag_id = "E-SEM-2802"; // Impure expression in contract
      if (expr)
      {
        result.span = expr->span;
      }
    }

    return result;
  }

  TypeInvariantResult CheckTypeInvariant(
      const ContractContext &ctx,
      const ast::TypeInvariant &invariant)
  {
    SpecDefsContractCheck();
    SPEC_RULE("TypeInvariant");

    TypeInvariantResult result;

    // @result is invalid in type invariants (non-return context).
    if (ContainsResult(invariant.predicate))
    {
      result.ok = false;
      result.diag_id = "E-SEM-2854";
      return result;
    }

    // Check predicate purity
    auto purity = CheckPurity(ctx, invariant.predicate);
    if (!purity.ok)
    {
      result.ok = false;
      result.diag_id = "E-SEM-3004";
    }

    (void)ctx;
    return result;
  }

  ContractCheckResult CheckLoopInvariant(
      const ContractContext &ctx,
      const ast::LoopInvariant &invariant)
  {
    SpecDefsContractCheck();
    SPEC_RULE("LoopInvariant");

    ContractCheckResult result;

    // Loop invariants may not contain @result
    if (ContainsResult(invariant.predicate))
    {
      result.ok = false;
      result.diag_id = "E-SEM-2854"; // @result in loop invariant
      result.span = invariant.span;
      return result;
    }

    // Check predicate purity
    auto purity = CheckPurity(ctx, invariant.predicate);
    if (!purity.ok)
    {
      result.ok = false;
      result.diag_id = "E-SEM-3004";
      result.span = purity.span.has_value() ? purity.span : std::optional<core::Span>(invariant.span);
    }

    (void)ctx;
    return result;
  }

  BehavioralSubtypingResult CheckBehavioralSubtyping(
      const ast::ContractClause &class_contract,
      const ast::ContractClause &impl_contract)
  {
    SpecDefsContractCheck();
    SPEC_RULE("LSP");

    BehavioralSubtypingResult result;

    // Per §14.8.1 verification strategy:
    // 1) Verify class precondition implies implementation precondition.
    // 2) Verify implementation postcondition implies class postcondition.
    result.precondition_weaker =
        PredicateImplies(class_contract.precondition, impl_contract.precondition);
    if (!result.precondition_weaker)
    {
      result.ok = false;
      result.diag_id = "E-SEM-2803";
      return result;
    }

    result.postcondition_stronger =
        PredicateImplies(impl_contract.postcondition, class_contract.postcondition);
    if (!result.postcondition_stronger)
    {
      result.ok = false;
      result.diag_id = "E-SEM-2804";
      return result;
    }

    return result;
  }

} // namespace cursive::analysis
