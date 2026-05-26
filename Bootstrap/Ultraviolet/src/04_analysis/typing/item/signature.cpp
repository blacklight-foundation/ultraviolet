// =============================================================================
// MIGRATION: item/signature.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 5.3.1: Procedure Declarations - Signatures
//   - Parameter list processing
//   - Receiver shorthands (~, ~!, ~%)
//   - Return type
//   - Function type construction
//
// SOURCE: ultraviolet-bootstrap/src/03_analysis/types/type_decls.cpp
//
// =============================================================================

#include "04_analysis/typing/type_decls.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/composite/record_methods.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_wf.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsSignature() {
  SPEC_DEF("Sig-Params", "5.3.1");
  SPEC_DEF("Sig-Return", "5.3.1");
  SPEC_DEF("Sig-Recv", "5.3.1");
  SPEC_DEF("Recv-Const", "5.3.1");
  SPEC_DEF("Recv-Unique", "5.3.1");
  SPEC_DEF("Recv-Shared", "5.3.1");
  SPEC_DEF("Recv-Explicit", "5.3.1");
  SPEC_DEF("ParamMode-Move", "5.3.1");
  SPEC_DEF("TypeFunc-Construct", "5.3.1");
  SPEC_DEF("InlineParamConstraint-Self-Err", "14.8.7");
}

static LowerTypeResult LowerTypeWithWF(const ScopeContext& ctx,
                                       const std::shared_ptr<ast::Type>& type) {
  const auto lowered = LowerType(ctx, type);
  if (!lowered.ok) {
    return lowered;
  }
  const auto wf = TypeWF(ctx, lowered.type);
  if (!wf.ok) {
    return {false, wf.diag_id, {}};
  }
  return lowered;
}

static bool ExprContainsIdentifier(const ast::ExprPtr& expr,
                                   std::string_view name) {
  if (!expr) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return IdEq(node.name, std::string(name));
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprContainsIdentifier(node.lhs, name) ||
                 ExprContainsIdentifier(node.rhs, name);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return ExprContainsIdentifier(node.lhs, name) ||
                 ExprContainsIdentifier(node.rhs, name);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return ExprContainsIdentifier(node.value, name);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return ExprContainsIdentifier(node.value, name);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ExprContainsIdentifier(node.base, name);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return ExprContainsIdentifier(node.base, name);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprContainsIdentifier(node.base, name) ||
                 ExprContainsIdentifier(node.index, name);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (ExprContainsIdentifier(node.callee, name)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprContainsIdentifier(arg.value, name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (ExprContainsIdentifier(node.receiver, name)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprContainsIdentifier(arg.value, name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (ExprContainsIdentifier(elem, name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool found = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            found = found || ExprContainsIdentifier(elem, name);
          });
          return found;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return ExprContainsIdentifier(node.value, name) ||
                 ExprContainsIdentifier(node.count, name);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (ExprContainsIdentifier(field.value, name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprContainsIdentifier(node.cond, name) ||
                 ExprContainsIdentifier(node.then_expr, name) ||
                 ExprContainsIdentifier(node.else_expr, name);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return ExprContainsIdentifier(node.expr, name);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return ExprContainsIdentifier(node.place, name);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return ExprContainsIdentifier(node.place, name);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return ExprContainsIdentifier(node.value, name);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return ExprContainsIdentifier(node.expr, name);
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          return ExprContainsIdentifier(node.lhs, name) ||
                 ExprContainsIdentifier(node.rhs, name);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return ExprContainsIdentifier(node.value, name);
        }
        return false;
      },
      expr->node);
}

static bool TypeContainsInlineParameterSelfConstraint(
    const std::shared_ptr<ast::Type>& type) {
  if (!type) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return ExprContainsIdentifier(node.predicate, "self") ||
                 TypeContainsInlineParameterSelfConstraint(node.base);
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return TypeContainsInlineParameterSelfConstraint(node.base);
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          for (const auto& member : node.types) {
            if (TypeContainsInlineParameterSelfConstraint(member)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          for (const auto& param : node.params) {
            if (TypeContainsInlineParameterSelfConstraint(param.type)) {
              return true;
            }
          }
          return TypeContainsInlineParameterSelfConstraint(node.ret);
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          for (const auto& param : node.params) {
            if (TypeContainsInlineParameterSelfConstraint(param.type)) {
              return true;
            }
          }
          if (TypeContainsInlineParameterSelfConstraint(node.ret)) {
            return true;
          }
          if (node.deps_opt.has_value()) {
            for (const auto& dep : *node.deps_opt) {
              if (TypeContainsInlineParameterSelfConstraint(dep.type)) {
                return true;
              }
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          for (const auto& elem : node.elements) {
            if (TypeContainsInlineParameterSelfConstraint(elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          return TypeContainsInlineParameterSelfConstraint(node.element);
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          return TypeContainsInlineParameterSelfConstraint(node.element);
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          return TypeContainsInlineParameterSelfConstraint(node.element);
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          return TypeContainsInlineParameterSelfConstraint(node.element);
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          for (const auto& arg : node.generic_args) {
            if (TypeContainsInlineParameterSelfConstraint(arg)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          for (const auto& arg : node.args) {
            if (TypeContainsInlineParameterSelfConstraint(arg)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          for (const auto& arg : node.generic_args) {
            if (TypeContainsInlineParameterSelfConstraint(arg)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::SpliceExprNode>) {
          return ExprContainsIdentifier(node.expr, "self");
        } else if constexpr (std::is_same_v<T, ast::TypeRange> ||
                             std::is_same_v<T, ast::TypeRangeInclusive> ||
                             std::is_same_v<T, ast::TypeRangeFrom> ||
                             std::is_same_v<T, ast::TypeRangeTo> ||
                             std::is_same_v<T, ast::TypeRangeToInclusive>) {
          return TypeContainsInlineParameterSelfConstraint(node.base);
        }
        return false;
      },
      type->node);
}

}  // namespace

// LowerParamMode is defined in type_lower.cpp (canonical location)
// RecvTypeForReceiver is defined in record_methods.cpp and declared in record_methods.h
// No duplicate definition here.

// =============================================================================
// EXPORTED: SubstSelfType
// =============================================================================

TypeRef SubstSelfType(const TypeRef& self_type, const TypeRef& type) {
  return SubstSelfType(self_type, type, nullptr);
}

TypeRef SubstSelfType(const TypeRef& self_type,
                      const TypeRef& type,
                      const TypeSubst* assoc_subst) {
  SpecDefsSignature();
  if (!type || !self_type) {
    return type;
  }

  return std::visit(
      [&](const auto& node) -> TypeRef {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, TypePathType>) {
          // Check if this is "Self"
          if (IsSelfVarPath(node.path)) {
            return self_type;
          }
          // Check if this is "Self::AssocType"
          if (node.path.size() == 2 && node.path[0] == "Self" &&
              assoc_subst != nullptr) {
            const auto assoc_it = assoc_subst->find(node.path[1]);
            if (assoc_it != assoc_subst->end()) {
              return assoc_it->second;
            }
          }
          // Recursively substitute in generic args
          if (node.generic_args.empty()) {
            return type;
          }
          std::vector<TypeRef> new_args;
          new_args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            new_args.push_back(SubstSelfType(self_type, arg, assoc_subst));
            }
            return MakeTypePath(node.path, new_args);
          } else if constexpr (std::is_same_v<T, TypeApply>) {
            std::vector<TypeRef> new_args;
            new_args.reserve(node.args.size());
            for (const auto& arg : node.args) {
              new_args.push_back(SubstSelfType(self_type, arg, assoc_subst));
            }
            return MakeTypeApply(node.path, new_args);
          } else if constexpr (std::is_same_v<T, TypePerm>) {
          auto new_base = SubstSelfType(self_type, node.base, assoc_subst);
          return MakeTypePerm(node.perm, new_base);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          std::vector<TypeRef> new_members;
          new_members.reserve(node.members.size());
          for (const auto& member : node.members) {
            new_members.push_back(SubstSelfType(self_type, member, assoc_subst));
          }
          return MakeTypeUnion(new_members);
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          std::vector<TypeFuncParam> new_params;
          new_params.reserve(node.params.size());
          for (const auto& param : node.params) {
            new_params.push_back({
                param.mode,
                SubstSelfType(self_type, param.type, assoc_subst)
            });
          }
          auto new_ret = SubstSelfType(self_type, node.ret, assoc_subst);
          return MakeTypeFunc(new_params, new_ret);
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          std::vector<std::pair<bool, TypeRef>> new_params;
          new_params.reserve(node.params.size());
          for (const auto& param : node.params) {
            new_params.emplace_back(param.first,
                                    SubstSelfType(self_type, param.second,
                                                  assoc_subst));
          }
          auto new_ret = SubstSelfType(self_type, node.ret, assoc_subst);
          std::optional<std::vector<SharedDep>> deps_opt;
          if (node.deps_opt.has_value()) {
            std::vector<SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              SharedDep lowered;
              lowered.name = dep.name;
              lowered.type = SubstSelfType(self_type, dep.type, assoc_subst);
              deps.push_back(std::move(lowered));
            }
            deps_opt = std::move(deps);
          }
          return MakeTypeClosure(std::move(new_params), new_ret,
                                 std::move(deps_opt));
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          std::vector<TypeRef> new_elements;
          new_elements.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            new_elements.push_back(SubstSelfType(self_type, elem, assoc_subst));
          }
          return MakeTypeTuple(new_elements);
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          auto new_elem = SubstSelfType(self_type, node.element, assoc_subst);
          return MakeTypeArray(new_elem, node.length, node.length_expr_text);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          auto new_elem = SubstSelfType(self_type, node.element, assoc_subst);
          return MakeTypeSlice(new_elem);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          auto new_elem = SubstSelfType(self_type, node.element, assoc_subst);
          return MakeTypePtr(new_elem, node.state);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          auto new_elem = SubstSelfType(self_type, node.element, assoc_subst);
          return MakeTypeRawPtr(node.qual, new_elem);
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          std::vector<TypeRef> new_args;
          new_args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            new_args.push_back(SubstSelfType(self_type, arg, assoc_subst));
          }
          return MakeTypeModalState(node.path, node.state, new_args);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          auto new_base = SubstSelfType(self_type, node.base, assoc_subst);
          return MakeTypeRefine(new_base, node.predicate);
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          auto new_base = SubstSelfType(self_type, node.base, assoc_subst);
          return MakeTypeRange(new_base);
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          auto new_base = SubstSelfType(self_type, node.base, assoc_subst);
          return MakeTypeRangeInclusive(new_base);
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          auto new_base = SubstSelfType(self_type, node.base, assoc_subst);
          return MakeTypeRangeFrom(new_base);
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          auto new_base = SubstSelfType(self_type, node.base, assoc_subst);
          return MakeTypeRangeTo(new_base);
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          auto new_base = SubstSelfType(self_type, node.base, assoc_subst);
          return MakeTypeRangeToInclusive(new_base);
        } else if constexpr (std::is_same_v<T, TypeRangeFull>) {
          return MakeTypeRangeFull();
        } else {
          // TypePrim, TypeString, TypeBytes, TypeDynamic, TypeOpaque
          return type;
        }
      },
      type->node);
}

// =============================================================================
// EXPORTED: BuildProcedureSignature
// =============================================================================

SignatureResult BuildProcedureSignature(
    const ScopeContext& ctx,
    const std::vector<ast::Param>& params,
    const std::shared_ptr<ast::Type>& return_type_opt) {
  SpecDefsSignature();
  SignatureResult result;
  result.ok = true;

  // Process parameters
  std::vector<TypeFuncParam> func_params;
  func_params.reserve(params.size());

  for (const auto& param : params) {
    if (TypeContainsInlineParameterSelfConstraint(param.type)) {
      SPEC_RULE("InlineParamConstraint-Self-Err");
      result.ok = false;
      result.diag_id = "E-TYP-1956";
      return result;
    }

    const auto lowered = LowerTypeWithWF(ctx, param.type);
    if (!lowered.ok) {
      result.ok = false;
      result.diag_id = lowered.diag_id;
      return result;
    }

    func_params.push_back({
        LowerParamMode(param.mode),
        lowered.type
    });

    result.bindings.emplace_back(param.name, lowered.type);
  }

  // Process return type
  TypeRef ret_type;
  if (return_type_opt) {
    SPEC_RULE("Sig-Return");
    const auto lowered = LowerTypeWithWF(ctx, return_type_opt);
    if (!lowered.ok) {
      result.ok = false;
      result.diag_id = lowered.diag_id;
      return result;
    }
    ret_type = lowered.type;
  } else {
    // Default return type is unit
    ret_type = MakeTypePrim("()");
  }

  // Construct function type
  SPEC_RULE("TypeFunc-Construct");
  result.func_type = MakeTypeFunc(func_params, ret_type);
  result.return_type = ret_type;

  return result;
}

// =============================================================================
// EXPORTED: BuildMethodSignature
// =============================================================================

SignatureResult BuildMethodSignature(
    const ScopeContext& ctx,
    const TypeRef& self_type,
    const ast::Receiver& receiver,
    const std::vector<ast::Param>& params,
    const std::shared_ptr<ast::Type>& return_type_opt,
    const TypeSubst* assoc_subst) {
  SpecDefsSignature();
  SignatureResult result;
  result.ok = true;

  auto lower_type = [&](const std::shared_ptr<ast::Type>& type) -> LowerTypeResult {
    return LowerTypeWithWF(ctx, type);
  };

  // Process receiver
  const auto recv = RecvTypeForReceiver(ctx, self_type, receiver, lower_type);
  if (!recv.ok) {
    result.ok = false;
    result.diag_id = recv.diag_id;
    return result;
  }

  // Build parameter list
  std::vector<TypeFuncParam> func_params;
  func_params.reserve(params.size() + 1);

  // Add receiver as first parameter if present
  if (recv.type) {
    const auto recv_subst = SubstSelfType(self_type, recv.type, assoc_subst);
    result.bindings.emplace_back("self", recv_subst);
    // Receiver mode depends on type
    std::optional<ParamMode> recv_mode;
    if (const auto* explicit_recv = std::get_if<ast::ReceiverExplicit>(&receiver)) {
      recv_mode = LowerParamMode(explicit_recv->mode_opt);
    }
    func_params.push_back({recv_mode, recv_subst});
  }

  // Process remaining parameters
  for (const auto& param : params) {
    const auto lowered = LowerTypeWithWF(ctx, param.type);
    if (!lowered.ok) {
      result.ok = false;
      result.diag_id = lowered.diag_id;
      return result;
    }

    const auto subst = SubstSelfType(self_type, lowered.type, assoc_subst);
    func_params.push_back({
        LowerParamMode(param.mode),
        subst
    });

    result.bindings.emplace_back(param.name, subst);
  }

  // Process return type
  TypeRef ret_type;
  if (return_type_opt) {
    SPEC_RULE("Sig-Return");
    const auto lowered = LowerTypeWithWF(ctx, return_type_opt);
    if (!lowered.ok) {
      result.ok = false;
      result.diag_id = lowered.diag_id;
      return result;
    }
    ret_type = SubstSelfType(self_type, lowered.type, assoc_subst);
  } else {
    ret_type = MakeTypePrim("()");
  }

  // Construct function type
  SPEC_RULE("TypeFunc-Construct");
  result.func_type = MakeTypeFunc(func_params, ret_type);
  result.return_type = ret_type;

  return result;
}

SignatureResult BuildTransitionSignature(
    const ScopeContext& ctx,
    const TypeRef& source_self_type,
    const TypeRef& target_self_type,
    const std::vector<ast::Param>& params,
    const TypeSubst* assoc_subst) {
  SpecDefsSignature();
  SignatureResult result;
  result.ok = true;

  std::vector<TypeFuncParam> func_params;
  func_params.reserve(params.size() + 1);

  const auto recv_type = MakeTypePerm(Permission::Unique, source_self_type);
  result.bindings.emplace_back("self", recv_type);
  func_params.push_back({ParamMode::Move, recv_type});

  for (const auto& param : params) {
    const auto lowered = LowerTypeWithWF(ctx, param.type);
    if (!lowered.ok) {
      result.ok = false;
      result.diag_id = lowered.diag_id;
      return result;
    }

    const auto subst = SubstSelfType(source_self_type, lowered.type, assoc_subst);
    func_params.push_back({
        LowerParamMode(param.mode),
        subst
    });
    result.bindings.emplace_back(param.name, subst);
  }

  result.func_type = MakeTypeFunc(func_params, target_self_type);
  result.return_type = target_self_type;
  SPEC_RULE("TypeFunc-Construct");
  return result;
}

}  // namespace ultraviolet::analysis
