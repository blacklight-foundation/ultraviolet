// =============================================================================
// expr_stmt.cpp - Expression statement typing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 5.2: Static Semantics
//   - Expression statements (expr;)
//   - S_ExprStmt set (line 377)
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/types/type_stmt.cpp
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "00_core/assert_spec.h"
#include "02_source/ast/ast.h"
#include "04_analysis/modal/modal_transitions.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/expr/path.h"
#include "04_analysis/typing/type_expr.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsExprStmt() {
  SPEC_DEF("T-ExprStmt", "5.2.11");
  SPEC_DEF("S_ExprStmt", "5.2.11");
}

static std::string ExprKindName(const ast::ExprPtr& expr) {
  if (!expr) {
    return "null";
  }
  return std::visit(
      [&](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          std::string kind = "AttributedExpr";
          kind += node.expr ? "(inner-present)" : "(inner-null)";
          if (node.expr) {
            kind += "->";
            kind += ExprKindName(node.expr);
          }
          return kind;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return "IfExpr";
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          return "BlockExpr";
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
          return "ComptimeExpr";
        } else if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return "IdentifierExpr(" + node.name + ")";
        } else if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          return "LiteralExpr";
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return "BinaryExpr";
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          return "CallExpr";
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          return "MethodCallExpr";
        } else {
          return "Expr";
        }
      },
      expr->node);
}

static ExprTypeResult TypeExprWithCurrentEnv(const ScopeContext& ctx,
                                             const StmtTypeContext& type_ctx,
                                             const TypeEnv& env,
                                             const ExprTypeFn& type_expr,
                                             const ast::ExprPtr& expr) {
  if (!expr) {
    return {};
  }

  const auto via_env = TypeExpr(ctx, type_ctx, expr, env);
  if (via_env.ok || via_env.diag_id.has_value()) {
    return via_env;
  }
  const auto via_callback = type_expr(expr);
  if (via_callback.ok) {
    return via_callback;
  }
  return via_callback;
}

static TypeRef StripPermDeepLocal(const TypeRef& type) {
  TypeRef cur = type;
  while (cur) {
    if (const auto* perm = std::get_if<TypePerm>(&cur->node)) {
      cur = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&cur->node)) {
      cur = refine->base;
      continue;
    }
    break;
  }
  return cur;
}

static TypeRef ReapplyQualifiers(const TypeRef& original,
                                 const TypeRef& transitioned_base) {
  if (!original) {
    return transitioned_base;
  }
  if (const auto* perm = std::get_if<TypePerm>(&original->node)) {
    return MakeTypePerm(perm->perm,
                        ReapplyQualifiers(perm->base, transitioned_base));
  }
  if (const auto* refine = std::get_if<TypeRefine>(&original->node)) {
    return MakeTypeRefine(ReapplyQualifiers(refine->base, transitioned_base),
                          refine->predicate);
  }
  return transitioned_base;
}

static std::optional<TypeRef> TransitionTypeForMethod(
    const ScopeContext& ctx,
    const TypeRef& current_type,
    std::string_view method_name) {
  const TypeRef stripped = StripPermDeepLocal(current_type);
  if (!stripped) {
    return std::nullopt;
  }
  const auto* modal = std::get_if<TypeModalState>(&stripped->node);
  if (!modal) {
    return std::nullopt;
  }

  PathKey key;
  key.reserve(modal->path.size());
  for (const auto& seg : modal->path) {
    key.push_back(seg);
  }
  const auto type_it = ctx.sigma.types.find(key);
  if (type_it == ctx.sigma.types.end()) {
    return std::nullopt;
  }
  const auto* modal_decl = std::get_if<ast::ModalDecl>(&type_it->second);
  if (!modal_decl) {
    return std::nullopt;
  }
  const auto* transition =
      LookupTransitionDecl(*modal_decl, modal->state, method_name);
  if (!transition) {
    return std::nullopt;
  }
  const TypeRef transitioned_base =
      MakeTypeModalState(modal->path, transition->target_state, modal->generic_args);
  return ReapplyQualifiers(current_type, transitioned_base);
}

static bool UpdateBindingType(TypeEnv& env,
                              std::string_view name,
                              const TypeRef& type) {
  const auto key = IdKeyOf(name);
  for (auto it = env.scopes.rbegin(); it != env.scopes.rend(); ++it) {
    const auto found = it->find(key);
    if (found == it->end()) {
      continue;
    }
    found->second.type = type;
    found->second.storage_type = type;
    return true;
  }
  return false;
}

static void MergeFlowInfo(FlowInfo& dst, const FlowInfo& src) {
  dst.results.insert(dst.results.end(), src.results.begin(), src.results.end());
  dst.breaks.insert(dst.breaks.end(), src.breaks.begin(), src.breaks.end());
  dst.break_void = dst.break_void || src.break_void;
}

static FlowInfo CollectExprStmtFlow(const ScopeContext& ctx,
                                    const StmtTypeContext& type_ctx,
                                    const TypeEnv& env,
                                    const ExprTypeFn& type_expr,
                                    const ast::ExprPtr& expr);

static FlowInfo CollectStmtFlow(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const TypeEnv& env,
                                const ExprTypeFn& type_expr,
                                const ast::Stmt& stmt);

static FlowInfo CollectBlockFlow(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const TypeEnv& env,
                                 const ExprTypeFn& type_expr,
                                 const ast::Block& block);

static FlowInfo CollectBlockExprFlow(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const TypeEnv& env,
                                     const ExprTypeFn& type_expr,
                                     const ast::Block& block) {
  return CollectBlockFlow(ctx, type_ctx, env, type_expr, block);
}

static FlowInfo CollectBlockExprFlow(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const TypeEnv& env,
                                     const ExprTypeFn& type_expr,
                                     const ast::BlockPtr& block) {
  if (!block) {
    return {};
  }
  return CollectBlockExprFlow(ctx, type_ctx, env, type_expr, *block);
}

static FlowInfo CollectExprStmtFlow(const ScopeContext& ctx,
                                    const StmtTypeContext& type_ctx,
                                    const TypeEnv& env,
                                    const ExprTypeFn& type_expr,
                                    const ast::ExprPtr& expr) {
  FlowInfo flow;
  if (!expr) {
    return flow;
  }

  return std::visit(
      [&](const auto& node) -> FlowInfo {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return CollectExprStmtFlow(ctx, type_ctx, env, type_expr, node.expr);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                             std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return CollectBlockExprFlow(ctx, type_ctx, env, type_expr,
                                      node.block);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          FlowInfo out;
          MergeFlowInfo(out,
                        CollectExprStmtFlow(ctx, type_ctx, env, type_expr,
                                            node.then_expr));
          MergeFlowInfo(out,
                        CollectExprStmtFlow(ctx, type_ctx, env, type_expr,
                                            node.else_expr));
          return out;
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          FlowInfo out;
          MergeFlowInfo(out,
                        CollectExprStmtFlow(ctx, type_ctx, env, type_expr,
                                            node.then_expr));
          MergeFlowInfo(out,
                        CollectExprStmtFlow(ctx, type_ctx, env, type_expr,
                                            node.else_expr));
          return out;
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          FlowInfo out;
          for (const auto& case_clause : node.cases) {
            MergeFlowInfo(out,
                          CollectExprStmtFlow(ctx, type_ctx, env, type_expr,
                                              case_clause.body));
          }
          MergeFlowInfo(out,
                        CollectExprStmtFlow(ctx, type_ctx, env, type_expr,
                                            node.else_expr));
          return out;
        } else {
          return {};
        }
      },
      expr->node);
}

static FlowInfo CollectBreakFlow(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const TypeEnv& env,
                                 const ExprTypeFn& type_expr,
                                 const ast::BreakStmt& stmt) {
  FlowInfo flow;
  if (!stmt.value_opt) {
    flow.break_void = true;
    return flow;
  }

  const auto typed =
      TypeExprWithCurrentEnv(ctx, type_ctx, env, type_expr, stmt.value_opt);
  if (typed.ok) {
    flow.breaks.push_back(typed.type);
  }
  return flow;
}

static FlowInfo CollectBlockFlow(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const TypeEnv& env,
                                 const ExprTypeFn& type_expr,
                                 const ast::Block& block) {
  FlowInfo out;
  for (const auto& stmt : block.stmts) {
    MergeFlowInfo(out, CollectStmtFlow(ctx, type_ctx, env, type_expr, stmt));
  }
  MergeFlowInfo(out,
                CollectExprStmtFlow(ctx, type_ctx, env, type_expr,
                                    block.tail_opt));
  return out;
}

static FlowInfo CollectStmtFlow(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const TypeEnv& env,
                                const ExprTypeFn& type_expr,
                                const ast::Stmt& stmt) {
  return std::visit(
      [&](const auto& node) -> FlowInfo {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          return CollectBreakFlow(ctx, type_ctx, env, type_expr, node);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return CollectExprStmtFlow(ctx, type_ctx, env, type_expr,
                                     node.value);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::RegionStmt> ||
                             std::is_same_v<T, ast::FrameStmt> ||
                             std::is_same_v<T, ast::KeyBlockStmt>) {
          if (!node.body) {
            return {};
          }
          return CollectBlockFlow(ctx, type_ctx, env, type_expr, *node.body);
        } else {
          return {};
        }
      },
      stmt);
}

}  // namespace

StmtTypeResult TypeExprStmt(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::ExprStmt& node,
                            const TypeEnv& env,
                            const ExprTypeFn& type_expr) {
  SpecDefsExprStmt();

  // Type the expression - the result value is discarded
  const auto typed = TypeExprWithCurrentEnv(ctx, type_ctx, env, type_expr, node.value);
  if (!typed.ok) {
    std::string detail = typed.diag_detail;
    if (detail.empty()) {
      detail = "expr-kind=" + ExprKindName(node.value);
    } else {
      detail += "; expr-kind=" + ExprKindName(node.value);
    }
    if (node.value) {
      if (const auto* attributed = std::get_if<ast::AttributedExpr>(&node.value->node)) {
        if (attributed->expr) {
          if (const auto* ident =
                  std::get_if<ast::IdentifierExpr>(&attributed->expr->node)) {
            const auto binding = BindOf(env, ident->name);
            detail += "; ident-binding=";
            detail += binding.has_value() ? "present" : "absent";
          }
        }
      }
    }
    return {false, typed.diag_id, {}, {}, detail};
  }

  TypeEnv out_env = env;
  if (node.value) {
    if (const auto* method = std::get_if<ast::MethodCallExpr>(&node.value->node)) {
      const ast::ExprPtr* receiver = &method->receiver;
      if (method->receiver) {
        if (const auto* moved =
                std::get_if<ast::MoveExpr>(&method->receiver->node)) {
          receiver = &moved->place;
        }
      }
      if (*receiver) {
        if (const auto* ident =
                std::get_if<ast::IdentifierExpr>(&(*receiver)->node)) {
          const auto binding = BindOf(out_env, ident->name);
          if (binding.has_value()) {
            const auto transitioned =
                TransitionTypeForMethod(ctx, binding->type, method->name);
            if (transitioned.has_value()) {
              (void)UpdateBindingType(out_env, ident->name, *transitioned);
            }
          }
        }
      }
    }
  }

  // Expression statement executes for side effects only
  // The result value is discarded
  SPEC_RULE("T-ExprStmt");
  StmtTypeResult result{true, std::nullopt, out_env, {}};
  result.flow = CollectExprStmtFlow(ctx, type_ctx, out_env, type_expr, node.value);
  return result;
}

}  // namespace ultraviolet::analysis
