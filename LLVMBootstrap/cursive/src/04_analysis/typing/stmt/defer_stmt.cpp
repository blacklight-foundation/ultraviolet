// =============================================================================
// defer_stmt.cpp - Defer statement typing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.2.11: Statement Typing
//   - defer { block } executes block at scope exit
//   - T-DeferStmt: Defer statement typing
//   - Defer-NonUnit-Err: Non-unit deferred block (warning)
//   - Defer-NonLocal-Err: Non-local control flow in defer
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/types/type_stmt.cpp
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/type_expr.h"

#include <optional>
#include <string_view>

#include "00_core/assert_spec.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

// Forward declaration - defined in stmt_common.cpp
bool DeferSafe(const ast::Block& block);

namespace {

static inline void SpecDefsDeferStmt() {
  SPEC_DEF("T-DeferStmt", "5.2.11");
  SPEC_DEF("Defer-NonUnit-Err", "5.2.11");
  SPEC_DEF("Defer-NonLocal-Err", "5.2.11");
  SPEC_DEF("DeferSafe", "5.2.11");
}

// Check for non-local control flow in a block
static bool HasNonLocalCtrlStmt(const ast::Stmt& stmt, bool in_loop);
static bool HasNonLocalCtrlExpr(const ast::ExprPtr& expr, bool in_loop);

static bool HasNonLocalCtrlBlock(const ast::Block& block, bool in_loop) {
  for (const auto& stmt : block.stmts) {
    if (HasNonLocalCtrlStmt(stmt, in_loop)) {
      return true;
    }
  }
  if (block.tail_opt && HasNonLocalCtrlExpr(block.tail_opt, in_loop)) {
    return true;
  }
  return false;
}

static bool HasNonLocalCtrlStmt(const ast::Stmt& stmt, bool in_loop) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          return !in_loop;
        } else if constexpr (std::is_same_v<T, ast::ContinueStmt>) {
          return !in_loop;
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          if (node.body && HasNonLocalCtrlBlock(*node.body, in_loop)) {
            return true;
          }
          return false;
        } else {
          return false;
        }
      },
      stmt);
}

static bool HasNonLocalCtrlExpr(const ast::ExprPtr& expr, bool in_loop) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr> ||
                      std::is_same_v<T, ast::LoopConditionalExpr> ||
                      std::is_same_v<T, ast::LoopIterExpr>) {
          // Loops establish their own context
          if (node.body && HasNonLocalCtrlBlock(*node.body, true)) {
            return true;
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          if (node.block && HasNonLocalCtrlBlock(*node.block, in_loop)) {
            return true;
          }
          return false;
        } else {
          return false;
        }
      },
      expr->node);
}

static bool LocalDeferSafe(const ast::Block& block) {
  return !HasNonLocalCtrlBlock(block, false);
}

static bool LocalExprNeedsKeyAccess(const ScopeContext& ctx,
                                    const StmtTypeContext& type_ctx,
                                    const ast::ExprPtr& expr,
                                    const TypeEnv& env);

static bool LocalExprHasSharedPermission(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::ExprPtr& expr,
                                         const TypeEnv& env) {
  if (!expr) {
    return false;
  }
  const auto place = TypePlace(ctx, type_ctx, expr, env);
  if (place.ok && place.type &&
      PermOfType(place.type) == Permission::Shared) {
    return true;
  }
  const auto value = TypeExpr(ctx, type_ctx, expr, env);
  return value.ok && value.type &&
         PermOfType(value.type) == Permission::Shared;
}

static bool LocalStmtNeedsKeyAccess(const ScopeContext& ctx,
                                    const StmtTypeContext& type_ctx,
                                    const ast::Stmt& stmt,
                                    const TypeEnv& env);

static bool LocalBlockNeedsKeyAccess(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::Block& block,
                                     const TypeEnv& env) {
  for (const auto& stmt : block.stmts) {
    if (LocalStmtNeedsKeyAccess(ctx, type_ctx, stmt, env)) {
      return true;
    }
  }
  return LocalExprNeedsKeyAccess(ctx, type_ctx, block.tail_opt, env);
}

static bool LocalStmtNeedsKeyAccess(const ScopeContext& ctx,
                                    const StmtTypeContext& type_ctx,
                                    const ast::Stmt& stmt,
                                    const TypeEnv& env) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          return LocalExprNeedsKeyAccess(ctx, type_ctx, node.binding.init, env);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                             std::is_same_v<T, ast::CompoundAssignStmt>) {
          return LocalExprHasSharedPermission(ctx, type_ctx, node.place, env) ||
                 LocalExprNeedsKeyAccess(ctx, type_ctx, node.place, env) ||
                 LocalExprNeedsKeyAccess(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          return LocalExprNeedsKeyAccess(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::CtStmt>) {
          return node.body && LocalBlockNeedsKeyAccess(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          return LocalExprNeedsKeyAccess(ctx, type_ctx, node.opts_opt, env) ||
                 (node.body &&
                  LocalBlockNeedsKeyAccess(ctx, type_ctx, *node.body, env));
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          return node.body && LocalBlockNeedsKeyAccess(ctx, type_ctx, *node.body, env);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          return LocalExprNeedsKeyAccess(ctx, type_ctx, node.value_opt, env);
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          return true;
        }
        return false;
      },
      stmt);
}

static bool LocalExprNeedsKeyAccess(const ScopeContext& ctx,
                                    const StmtTypeContext& type_ctx,
                                    const ast::ExprPtr& expr,
                                    const TypeEnv& env) {
  if (!expr) {
    return false;
  }
  if (LocalExprHasSharedPermission(ctx, type_ctx, expr, env)) {
    return true;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (LocalExprNeedsKeyAccess(ctx, type_ctx, node.callee, env)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (LocalExprNeedsKeyAccess(ctx, type_ctx, arg.value, env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          if (LocalExprNeedsKeyAccess(ctx, type_ctx, node.receiver, env)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (LocalExprNeedsKeyAccess(ctx, type_ctx, arg.value, env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr> ||
                             std::is_same_v<T, ast::PipelineExpr>) {
          return LocalExprNeedsKeyAccess(ctx, type_ctx, node.lhs, env) ||
                 LocalExprNeedsKeyAccess(ctx, type_ctx, node.rhs, env);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr> ||
                             std::is_same_v<T, ast::CastExpr> ||
                             std::is_same_v<T, ast::DerefExpr> ||
                             std::is_same_v<T, ast::PropagateExpr> ||
                             std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr>) {
          return LocalExprNeedsKeyAccess(ctx, type_ctx, node.value, env);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr> ||
                             std::is_same_v<T, ast::MoveExpr>) {
          return LocalExprNeedsKeyAccess(ctx, type_ctx, node.place, env);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                             std::is_same_v<T, ast::TupleAccessExpr>) {
          return LocalExprNeedsKeyAccess(ctx, type_ctx, node.base, env);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return LocalExprNeedsKeyAccess(ctx, type_ctx, node.base, env) ||
                 LocalExprNeedsKeyAccess(ctx, type_ctx, node.index, env);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            if (LocalExprNeedsKeyAccess(ctx, type_ctx, elem, env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          bool found = false;
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            found = found || LocalExprNeedsKeyAccess(ctx, type_ctx, elem, env);
          });
          return found;
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          return LocalExprNeedsKeyAccess(ctx, type_ctx, node.value, env) ||
                 LocalExprNeedsKeyAccess(ctx, type_ctx, node.count, env);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            if (LocalExprNeedsKeyAccess(ctx, type_ctx, field.value, env)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          return LocalExprNeedsKeyAccess(ctx, type_ctx, node.cond, env) ||
                 LocalExprNeedsKeyAccess(ctx, type_ctx, node.then_expr, env) ||
                 LocalExprNeedsKeyAccess(ctx, type_ctx, node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          if (LocalExprNeedsKeyAccess(ctx, type_ctx, node.scrutinee, env)) {
            return true;
          }
          for (const auto& arm : node.cases) {
            if (LocalExprNeedsKeyAccess(ctx, type_ctx, arm.body, env)) {
              return true;
            }
          }
          return LocalExprNeedsKeyAccess(ctx, type_ctx, node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          return LocalExprNeedsKeyAccess(ctx, type_ctx, node.scrutinee, env) ||
                 LocalExprNeedsKeyAccess(ctx, type_ctx, node.then_expr, env) ||
                 LocalExprNeedsKeyAccess(ctx, type_ctx, node.else_expr, env);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                             std::is_same_v<T, ast::UnsafeBlockExpr>) {
          return node.block && LocalBlockNeedsKeyAccess(ctx, type_ctx, *node.block, env);
        }
        return false;
      },
      expr->node);
}

}  // namespace

StmtTypeResult TypeDeferStmt(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::DeferStmt& node,
                             const TypeEnv& env,
                             const ExprTypeFn& type_expr,
                             const IdentTypeFn& type_ident,
                             const PlaceTypeFn& type_place) {
  SpecDefsDeferStmt();

  if (!node.body) {
    return {false, std::nullopt, {}, {}};
  }

  if (type_ctx.in_speculative) {
    return {false, "E-CON-0093", {}, {}};
  }

  if (LocalBlockNeedsKeyAccess(ctx, type_ctx, *node.body, env)) {
    return {false, "E-CON-0006", {}, {}};
  }

  // Type check the deferred block - it should have unit type
  const auto check = CheckBlock(ctx, type_ctx, *node.body, env,
                                MakeTypePrim("()"), type_expr,
                                type_ident, type_place,
                                type_ctx.env_ref);
  if (!check.ok) {
    if (!check.diag_id.has_value()) {
      SPEC_RULE("Defer-NonUnit-Err");
      return {false, "Defer-NonUnit-Err", {}, {}};
    }
    return {false, check.diag_id, {}, {}};
  }

  // Check that defer block doesn't contain non-local control flow
  if (!LocalDeferSafe(*node.body)) {
    SPEC_RULE("Defer-NonLocal-Err");
    return {false, "Defer-NonLocal-Err", {}, {}};
  }

  SPEC_RULE("T-DeferStmt");
  return {true, std::nullopt, env, {}};
}

}  // namespace cursive::analysis
