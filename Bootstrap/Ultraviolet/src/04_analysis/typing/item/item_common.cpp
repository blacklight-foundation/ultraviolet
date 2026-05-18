// =============================================================================
// MIGRATION: item/item_common.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   Section 5.3: Declarations
//   - Common item processing utilities
//   - Declaration environment management
//
// SOURCE: ultraviolet-bootstrap/src/03_analysis/types/type_decls.cpp
//
// =============================================================================

#include "04_analysis/typing/type_decls.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/memory/borrow_bind.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/composite/enums.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/composite/records.h"
#include "04_analysis/composite/record_methods.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/if_case_check.h"
#include "04_analysis/typing/type_pattern.h"
#include "04_analysis/typing/type_stmt.h"
#include "../typecheck_diag_lookup.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsDeclTyping() {
  SPEC_DEF("DeclJudg", "5.2.14");
  SPEC_DEF("DeclTyping", "5.2.14");
  SPEC_DEF("DeclTypingMod", "5.2.14");
  SPEC_DEF("DeclTypingItem", "5.2.14");
  SPEC_DEF("ProvBindCheck", "5.2.14");
  SPEC_DEF("ParamBinds", "5.2.14");
  SPEC_DEF("ProcReturn", "5.2.14");
  SPEC_DEF("ReturnAnnOk", "5.2.14");
  SPEC_DEF("StaticVisOk", "5.2.14");
  SPEC_DEF("VisRank", "5.2.14");
  SPEC_DEF("FieldVisOk", "5.2.14");
  SPEC_DEF("StateMemberVisOk", "5.2.14");
  SPEC_DEF("MainDecls", "5.2.14");
  SPEC_DEF("MainGeneric", "5.2.14");
  SPEC_DEF("MainSigOk", "5.2.14");
}

// =============================================================================
// DIAGNOSTIC HELPERS
// =============================================================================

static void EmitTypecheckDiag(core::DiagnosticStream& diags,
                              std::string_view diag_id,
                              const std::optional<core::Span>& span,
                              const std::string& detail = {}) {
  EmitResolvedTypecheckDiagnostic(diags, diag_id, span, detail);
}

// =============================================================================
// TYPE LOWERING UTILITIES
// =============================================================================

// Local variant of LowerType that adds TypeWF checking
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

static LowerTypeResult LowerReturnType(const ScopeContext& ctx,
                                       const std::shared_ptr<ast::Type>& type_opt) {
  if (!type_opt) {
    return {true, std::nullopt, MakeTypePrim("()")};
  }
  return LowerTypeWithWF(ctx, type_opt);
}

// =============================================================================
// PATH AND NAME UTILITIES
// =============================================================================

static TypePath TypePathForItem(const ast::ModulePath& module_path,
                                std::string_view name) {
  TypePath out;
  out.reserve(module_path.size() + 1);
  for (const auto& part : module_path) {
    out.push_back(part);
  }
  out.push_back(std::string(name));
  return out;
}

static bool DistinctNames(const std::vector<IdKey>& names) {
  if (names.size() < 2) {
    return true;
  }
  std::vector<IdKey> sorted = names;
  std::sort(sorted.begin(), sorted.end());
  return std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end();
}

static std::vector<IdKey> ParamNames(const std::vector<ast::Param>& params) {
  std::vector<IdKey> names;
  names.reserve(params.size());
  for (const auto& param : params) {
    names.push_back(IdKeyOf(param.name));
  }
  return names;
}

// =============================================================================
// BINDING UTILITIES
// =============================================================================

static bool AddBinding(TypeEnv& env,
                       std::string_view name,
                       const TypeRef& type) {
  if (env.scopes.empty()) {
    env.scopes.emplace_back();
  }
  const auto key = IdKeyOf(name);
  if (env.scopes.back().find(key) != env.scopes.back().end()) {
    return false;
  }
  env.scopes.back()[key] = {ast::Mutability::Let, type};
  return true;
}

// =============================================================================
// VISIBILITY UTILITIES
// =============================================================================

static bool StaticVisOk(ast::Visibility vis, ast::Mutability mut) {
  SPEC_RULE("StaticVisOk");
  return !(vis == ast::Visibility::Public && mut == ast::Mutability::Var);
}

static int VisRank(ast::Visibility vis) {
  SPEC_RULE("VisRank");
  switch (vis) {
    case ast::Visibility::Public:
      return 4;
    case ast::Visibility::Internal:
      return 3;
    case ast::Visibility::Private:
      return 1;
  }
  return 1;
}

static bool FieldVisOk(const ast::RecordDecl& record) {
  SPEC_RULE("FieldVisOk");
  for (const auto& member : record.members) {
    const auto* field = std::get_if<ast::FieldDecl>(&member);
    if (!field) {
      continue;
    }
    if (VisRank(field->vis) > VisRank(record.vis)) {
      return false;
    }
  }
  return true;
}

static bool StateMemberVisOk(const ast::ModalDecl& modal) {
  SPEC_RULE("StateMemberVisOk");
  for (const auto& state : modal.states) {
    for (const auto& member : state.members) {
      std::optional<ast::Visibility> vis;
      if (const auto* field = std::get_if<ast::StateFieldDecl>(&member)) {
        vis = field->vis;
      } else if (const auto* method = std::get_if<ast::StateMethodDecl>(&member)) {
        vis = method->vis;
      } else if (const auto* transition =
                     std::get_if<ast::TransitionDecl>(&member)) {
        vis = transition->vis;
      }
      if (vis.has_value() && VisRank(*vis) > VisRank(modal.vis)) {
        return false;
      }
    }
  }
  return true;
}

// =============================================================================
// RETURN TYPE VALIDATION
// =============================================================================

static bool ReturnAnnOk(const std::shared_ptr<ast::Type>& ret_opt) {
  SPEC_RULE("ReturnAnnOk");
  return ret_opt != nullptr;
}

// =============================================================================
// MAIN ENTRY POINT VALIDATION
// =============================================================================

static bool BuiltInContextType(const ast::Type& type) {
  const auto* path = std::get_if<ast::TypePathType>(&type.node);
  return path && IsContextTypePath(path->path);
}

static bool MainGeneric(const ast::ProcedureDecl& /*decl*/) {
  SPEC_RULE("MainGeneric");
  // Main cannot be generic in C0
  return false;
}

static bool MainSigOk(const ast::ProcedureDecl& decl) {
  SPEC_RULE("MainSigOk");
  if (decl.vis != ast::Visibility::Public) {
    return false;
  }
  if (decl.params.size() != 1) {
    return false;
  }
  const auto& param = decl.params[0];
  if (param.mode.has_value() && *param.mode != ast::ParamMode::Move) {
    return false;
  }
  if (!param.type || !BuiltInContextType(*param.type)) {
    return false;
  }
  const auto* ret = decl.return_type_opt
                        ? std::get_if<ast::TypePrim>(&decl.return_type_opt->node)
                        : nullptr;
  return ret && ret->name == "i32";
}

// =============================================================================
// RETURN SUBTYPE CHECKING
// =============================================================================

static bool SubtypeReturn(const ScopeContext& ctx,
                          const TypeRef& body_type,
                          const TypeRef& return_type,
                          core::DiagnosticStream& diags,
                          const std::optional<core::Span>& span) {
  if (const auto async_sig = AsyncSigOf(ctx, return_type)) {
    const auto sub = Subtyping(ctx, body_type, async_sig->result);
    if (!sub.ok) {
      if (sub.diag_id.has_value()) {
        EmitTypecheckDiag(diags, *sub.diag_id, span);
      }
      return false;
    }
    if (!sub.subtype) {
      SPEC_RULE("Return-Async-Type-Err");
      EmitTypecheckDiag(diags, "E-CON-0203", span);
      return false;
    }
    return true;
  }

  const auto sub = Subtyping(ctx, body_type, return_type);
  if (!sub.ok) {
    if (sub.diag_id.has_value()) {
      EmitTypecheckDiag(diags, *sub.diag_id, span);
    }
    return false;
  }
  if (!sub.subtype) {
    SPEC_RULE("Return-Type-Err");
    EmitTypecheckDiag(diags, "E-SEM-3161", span);
    return false;
  }
  return true;
}

// =============================================================================
// EXPLICIT RETURN CHECK
// =============================================================================

// Check if a block body has an explicit return statement at the end.
// Required for procedures/methods with non-unit return types.
static bool HasExplicitReturn(const ast::Block& block) {
  auto stmtHasExplicitReturn = [&](const auto& self, const ast::Stmt& stmt) -> bool {
    return std::visit(
        [&](const auto& node) -> bool {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
            return true;
          } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
            return node.body && HasExplicitReturn(*node.body);
          }
          return false;
        },
        stmt);
  };
  // If there's a tail expression, it's not an explicit return
  if (block.tail_opt) {
    return false;
  }
  return !block.stmts.empty() && stmtHasExplicitReturn(stmtHasExplicitReturn, block.stmts.back());
}

// =============================================================================
// TYPE ALIAS CYCLE DETECTION
// =============================================================================

static bool TypeAliasCycleFrom(const ScopeContext& ctx,
                               const PathKey& start,
                               std::set<PathKey>& active,
                               std::set<PathKey>& done);

static void CollectAliasDeps(const ScopeContext& ctx,
                             const std::shared_ptr<ast::Type>& type,
                             std::vector<PathKey>& deps) {
  if (!type) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePathType>) {
          const auto key = PathKeyOf(node.path);
          const auto it = ctx.sigma.types.find(key);
          if (it != ctx.sigma.types.end() &&
              std::holds_alternative<ast::TypeAliasDecl>(it->second)) {
            deps.push_back(key);
          }
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          CollectAliasDeps(ctx, node.base, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          for (const auto& elem : node.types) {
            CollectAliasDeps(ctx, elem, deps);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          for (const auto& param : node.params) {
            CollectAliasDeps(ctx, param.type, deps);
          }
          CollectAliasDeps(ctx, node.ret, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          for (const auto& param : node.params) {
            CollectAliasDeps(ctx, param.type, deps);
          }
          CollectAliasDeps(ctx, node.ret, deps);
          if (node.deps_opt.has_value()) {
            for (const auto& dep : *node.deps_opt) {
              CollectAliasDeps(ctx, dep.type, deps);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          for (const auto& elem : node.elements) {
            CollectAliasDeps(ctx, elem, deps);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          CollectAliasDeps(ctx, node.element, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          CollectAliasDeps(ctx, node.element, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          CollectAliasDeps(ctx, node.element, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          CollectAliasDeps(ctx, node.element, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          CollectAliasDeps(ctx, node.base, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeDynamic>) {
          return;
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          for (const auto& arg : node.generic_args) {
            CollectAliasDeps(ctx, arg, deps);
          }
          return;
        } else if constexpr (std::is_same_v<T, ast::TypePrim> ||
                             std::is_same_v<T, ast::TypeString> ||
                             std::is_same_v<T, ast::TypeBytes>) {
          return;
        }
      },
      type->node);
}

static bool TypeAliasCycleFrom(const ScopeContext& ctx,
                               const PathKey& start,
                               std::set<PathKey>& active,
                               std::set<PathKey>& done) {
  if (done.find(start) != done.end()) {
    return false;
  }
  if (!active.insert(start).second) {
    return true;
  }

  const auto it = ctx.sigma.types.find(start);
  if (it == ctx.sigma.types.end()) {
    active.erase(start);
    done.insert(start);
    return false;
  }
  const auto* alias = std::get_if<ast::TypeAliasDecl>(&it->second);
  if (!alias) {
    active.erase(start);
    done.insert(start);
    return false;
  }

  std::vector<PathKey> deps;
  CollectAliasDeps(ctx, alias->type, deps);
  for (const auto& dep : deps) {
    if (TypeAliasCycleFrom(ctx, dep, active, done)) {
      return true;
    }
  }

  active.erase(start);
  done.insert(start);
  return false;
}

static bool TypeAliasOk(const ScopeContext& ctx, const ast::Path& path) {
  std::set<PathKey> active;
  std::set<PathKey> done;
  return !TypeAliasCycleFrom(ctx, PathKeyOf(path), active, done);
}

// =============================================================================
// CLASS PATH UTILITIES
// =============================================================================

static bool DistinctPaths(const std::vector<ast::ClassPath>& paths) {
  if (paths.size() < 2) {
    return true;
  }
  std::vector<PathKey> keys;
  keys.reserve(paths.size());
  for (const auto& path : paths) {
    keys.push_back(PathKeyOf(path));
  }
  std::sort(keys.begin(), keys.end());
  return std::adjacent_find(keys.begin(), keys.end()) == keys.end();
}

static bool ClassPathOk(const ScopeContext& ctx,
                        const ast::ClassPath& path,
                        core::DiagnosticStream& diags,
                        const core::Span& span) {
  if (ctx.sigma.classes.find(PathKeyOf(path)) == ctx.sigma.classes.end()) {
    SPEC_RULE("WF-ClassPath-Err");
    EmitTypecheckDiag(diags, "Superclass-Undefined", span);
    return false;
  }
  SPEC_RULE("WF-ClassPath");
  return true;
}

// =============================================================================
// EXPRESSION IDENTIFIER UTILITIES
// =============================================================================

static bool ExprContainsIdent(const ast::ExprPtr& expr,
                              std::string_view name) {
  if (!expr) {
    return false;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    return IdEq(ident->name, name);
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ExprContainsIdent(node.lhs, name) ||
                 ExprContainsIdent(node.rhs, name);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return ExprContainsIdent(node.value, name);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ExprContainsIdent(node.base, name);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return ExprContainsIdent(node.base, name);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ExprContainsIdent(node.base, name) ||
                 ExprContainsIdent(node.index, name);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (ExprContainsIdent(node.callee, name)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprContainsIdent(arg.value, name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            const auto& paren = std::get<ast::ParenArgs>(node.args);
            for (const auto& arg : paren.args) {
              if (ExprContainsIdent(arg.value, name)) {
                return true;
              }
            }
            return false;
          }
          const auto& brace = std::get<ast::BraceArgs>(node.args);
          for (const auto& field : brace.fields) {
            if (ExprContainsIdent(field.value, name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (ExprContainsIdent(node.receiver, name)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (ExprContainsIdent(arg.value, name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          return ExprContainsIdent(node.value, name);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          return ExprContainsIdent(node.lhs, name) ||
                 ExprContainsIdent(node.rhs, name);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return ExprContainsIdent(node.value, name);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          return ExprContainsIdent(node.place, name);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return ExprContainsIdent(node.place, name);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          return ExprContainsIdent(node.value, name);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (ExprContainsIdent(elem, name)) {
              return true;
            }
          }
          return false;
      } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
        bool contains_ident = false;
        ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
          if (contains_ident) {
            return;
          }
          if (ExprContainsIdent(elem, name)) {
            contains_ident = true;
          }
        });
        return contains_ident;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return ExprContainsIdent(node.value, name) ||
                 ExprContainsIdent(node.count, name);
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (ExprContainsIdent(field.value, name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (!node.payload_opt.has_value()) {
            return false;
          }
          if (std::holds_alternative<ast::EnumPayloadParen>(*node.payload_opt)) {
            const auto& paren = std::get<ast::EnumPayloadParen>(*node.payload_opt);
            for (const auto& elem : paren.elements) {
              if (ExprContainsIdent(elem, name)) {
                return true;
              }
            }
            return false;
          }
          const auto& brace = std::get<ast::EnumPayloadBrace>(*node.payload_opt);
          for (const auto& field : brace.fields) {
            if (ExprContainsIdent(field.value, name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return ExprContainsIdent(node.cond, name) ||
                 ExprContainsIdent(node.then_expr, name) ||
                 ExprContainsIdent(node.else_expr, name);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (ExprContainsIdent(node.scrutinee, name)) {
            return true;
          }
          for (const auto& arm : node.cases) {
            if (ExprContainsIdent(arm.body, name)) {
              return true;
            }
          }
          return ExprContainsIdent(node.else_expr, name);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return ExprContainsIdent(node.scrutinee, name) ||
                 ExprContainsIdent(node.then_expr, name) ||
                 ExprContainsIdent(node.else_expr, name);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          return ExprContainsIdent(node.value, name);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          return ExprContainsIdent(node.expr, name);
        } else {
          return false;
        }
      },
      expr->node);
}

// =============================================================================
// IDENTIFIER SUBSTITUTION
// =============================================================================

static ast::ExprPtr SubstituteIdent(const ast::ExprPtr& expr,
                                    std::string_view name,
                                    const ast::ExprPtr& replacement);

static ast::ExprPtr MakeExpr(const core::Span& span, ast::ExprNode node) {
  auto out = std::make_shared<ast::Expr>();
  out->span = span;
  out->node = std::move(node);
  return out;
}

static ast::ExprPtr SubstituteIdent(const ast::ExprPtr& expr,
                                    std::string_view name,
                                    const ast::ExprPtr& replacement) {
  if (!expr) {
    return expr;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    if (IdEq(ident->name, name)) {
      return replacement;
    }
    return expr;
  }
  return std::visit(
      [&](const auto& node) -> ast::ExprPtr {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          auto out = node;
          out.lhs = SubstituteIdent(node.lhs, name, replacement);
          out.rhs = SubstituteIdent(node.rhs, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          auto out = node;
          out.value = SubstituteIdent(node.value, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto out = node;
          out.base = SubstituteIdent(node.base, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto out = node;
          out.base = SubstituteIdent(node.base, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto out = node;
          out.base = SubstituteIdent(node.base, name, replacement);
          out.index = SubstituteIdent(node.index, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          auto out = node;
          out.callee = SubstituteIdent(node.callee, name, replacement);
          for (auto& arg : out.args) {
            arg.value = SubstituteIdent(arg.value, name, replacement);
          }
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          auto out = node;
          out.receiver = SubstituteIdent(node.receiver, name, replacement);
          for (auto& arg : out.args) {
            arg.value = SubstituteIdent(arg.value, name, replacement);
          }
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          auto out = node;
          out.value = SubstituteIdent(node.value, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          auto out = node;
          out.lhs = SubstituteIdent(node.lhs, name, replacement);
          out.rhs = SubstituteIdent(node.rhs, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          auto out = node;
          out.value = SubstituteIdent(node.value, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          auto out = node;
          out.place = SubstituteIdent(node.place, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          auto out = node;
          out.place = SubstituteIdent(node.place, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          auto out = node;
          out.value = SubstituteIdent(node.value, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          auto out = node;
          for (auto& elem : out.elements) {
            elem = SubstituteIdent(elem, name, replacement);
          }
          return MakeExpr(expr->span, out);
      } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
        auto out = node;
        for (auto& segment : out.elements) {
          std::visit(
              [&](auto& seg) {
                seg.value = SubstituteIdent(seg.value, name, replacement);
                if constexpr (std::is_same_v<std::decay_t<decltype(seg)>,
                                             ast::ArrayRepeatSegment>) {
                  seg.count = SubstituteIdent(seg.count, name, replacement);
                }
              },
              segment);
        }
        return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          auto out = node;
          out.value = SubstituteIdent(node.value, name, replacement);
          out.count = SubstituteIdent(node.count, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          auto out = node;
          for (auto& field : out.fields) {
            field.value = SubstituteIdent(field.value, name, replacement);
          }
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          auto out = node;
          out.cond = SubstituteIdent(node.cond, name, replacement);
          out.then_expr = SubstituteIdent(node.then_expr, name, replacement);
          out.else_expr = SubstituteIdent(node.else_expr, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          auto out = node;
          out.scrutinee = SubstituteIdent(node.scrutinee, name, replacement);
          for (auto& arm : out.cases) {
            arm.body = SubstituteIdent(arm.body, name, replacement);
          }
          out.else_expr = SubstituteIdent(node.else_expr, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          auto out = node;
          out.scrutinee = SubstituteIdent(node.scrutinee, name, replacement);
          out.then_expr = SubstituteIdent(node.then_expr, name, replacement);
          out.else_expr = SubstituteIdent(node.else_expr, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          auto out = node;
          out.value = SubstituteIdent(node.value, name, replacement);
          return MakeExpr(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          auto out = node;
          out.expr = SubstituteIdent(node.expr, name, replacement);
          return MakeExpr(expr->span, out);
        } else {
          return expr;
        }
      },
      expr->node);
}

}  // namespace

// IdKeyOf, IdEq, PathKeyOf are defined in scopes.cpp (with proper NFC normalization)
// and declared in scopes.h - no duplicate definitions here

}  // namespace ultraviolet::analysis
