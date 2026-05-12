// =============================================================================
// break_stmt.cpp - Break statement typing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.2.11: Loop Control Statements
//   - T-Break-Value (line 9609): Break with value
//   - T-Break-Unit (line 9614): Break without value
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/types/type_stmt.cpp
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <optional>
#include <string_view>

#include "00_core/assert_spec.h"
#include "02_source/ast/ast.h"
#include "04_analysis/typing/type_expr.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsBreakStmt() {
  SPEC_DEF("T-Break-Value", "5.2.11");
  SPEC_DEF("T-Break-Unit", "5.2.11");
  SPEC_DEF("Break-Outside-Loop", "5.2.11");
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

}  // namespace

StmtTypeResult TypeBreakStmt(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::BreakStmt& node,
                             const TypeEnv& env,
                             const ExprTypeFn& type_expr) {
  SpecDefsBreakStmt();

  // Break must be inside a loop
  if (type_ctx.loop_flag != LoopFlag::Loop) {
    SPEC_RULE("Break-Outside-Loop");
    return {false, "E-SEM-3162", {}, {}};
  }

  FlowInfo flow;
  if (node.value_opt) {
    // Break with a value - type the expression and add to breaks
    const auto typed =
        TypeExprWithCurrentEnv(ctx, type_ctx, env, type_expr, node.value_opt);
    if (!typed.ok) {
      return {false, typed.diag_id, {}, {}, typed.diag_detail};
    }
    SPEC_RULE("T-Break-Value");
    flow.breaks.push_back(typed.type);
  } else {
    // Break without value - set break_void flag
    SPEC_RULE("T-Break-Unit");
    flow.break_void = true;
  }

  return {true, std::nullopt, env, std::move(flow)};
}

}  // namespace cursive::analysis
