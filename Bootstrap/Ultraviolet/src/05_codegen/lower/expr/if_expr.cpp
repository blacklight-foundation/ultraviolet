// =============================================================================
// MIGRATION MAPPING: expr/if_expr.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16203-16206: (Lower-Expr-If)
//     Gamma |- LowerExpr(cond) => <IR_c, v_c>
//     Gamma |- LowerBlock(b_1) => <IR_1, v_1>
//     Gamma |- LowerBlock(b_2) => <IR_2, v_2>
//     ---
//     Gamma |- LowerExpr(IfExpr(cond, b_1, b_2)) => <SeqIR(IR_c, IfIR(...)), v_if>
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_calls.cpp
//   - IfExpr visitor produces IRIf
//
// DEPENDENCIES:
//   - ultraviolet/src/05_codegen/ir_model.h (IRIf, IRValue)
//   - ultraviolet/src/05_codegen/cleanup/cleanup.h (CleanupPlan, EmitCleanupWithRemainder)
//   - ultraviolet/src/05_codegen/cleanup/unwind.h (IsNoopIR)
//
// =============================================================================

#include "05_codegen/lower/expr/if_expr.h"

#include <algorithm>
#include <initializer_list>
#include <utility>
#include <unordered_set>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/cleanup/unwind.h"
#include "05_codegen/ir/ir_control_flow.h"

namespace ultraviolet::codegen {

namespace {

LowerCtx MakeBranchCtx(LowerCtx& base) {
  auto saved_value_types = std::move(base.values.value_types);
  auto saved_derived_values = std::move(base.values.derived_values);
  auto saved_drop_glue_types = std::move(base.values.drop_glue_types);

  LowerCtx branch = base;

  base.values.value_types = std::move(saved_value_types);
  base.values.derived_values = std::move(saved_derived_values);
  base.values.drop_glue_types = std::move(saved_drop_glue_types);

  branch.values.value_types.clear();
  branch.values.derived_values.clear();
  branch.values.drop_glue_types.clear();
  branch.values.parent = &base;
  branch.extra_procs.clear();
  return branch;
}

// ---------------------------------------------------------------------------
// Helper: CleanupTemps
// ---------------------------------------------------------------------------
// Emit cleanup IR for temporary values accumulated during expression lowering.
// Temporaries are dropped in reverse order of creation.

IRPtr CleanupTemps(const std::vector<TempValue>& temps, LowerCtx& ctx) {
  if (temps.empty()) {
    return EmptyIR();
  }

  CleanupPlan plan;
  plan.reserve(temps.size());
  for (auto it = temps.rbegin(); it != temps.rend(); ++it) {
    CleanupAction action;
    action.kind = CleanupAction::Kind::DropTemp;
    action.type = it->type;
    action.value = it->value;
    plan.push_back(std::move(action));
  }
  CleanupPlan remainder = ComputeCleanupPlanToFunctionRoot(ctx);
  return EmitCleanupWithRemainder(plan, remainder, ctx);
}

// ---------------------------------------------------------------------------
// Helper: MergeLowerCtxTemps
// ---------------------------------------------------------------------------
// Merge value type info from branch context into base context.
// Used when lowering branching constructs where only one branch executes.

void MergeLowerCtxTemps(LowerCtx& base, const LowerCtx& branch) {
  for (const auto& [name, type] : branch.values.value_types) {
    if (!base.values.value_types.count(name)) {
      base.values.value_types.emplace(name, type);
    }
  }
  for (const auto& [name, info] : branch.values.derived_values) {
    if (!base.values.derived_values.count(name)) {
      base.values.derived_values.emplace(name, info);
    }
  }
  for (const auto& [name, type] : branch.values.static_types) {
    if (!base.values.static_types.count(name)) {
      base.values.static_types.emplace(name, type);
    }
  }
  for (const auto& [name, type] : branch.values.drop_glue_types) {
    if (!base.values.drop_glue_types.count(name)) {
      base.values.drop_glue_types.emplace(name, type);
    }
  }
  *base.temp_counter = std::max(*base.temp_counter, *branch.temp_counter);
}

// ---------------------------------------------------------------------------
// Helper: MergeMoveStates
// ---------------------------------------------------------------------------
// Merge move states from multiple branch contexts into a single base context.
// A binding is considered moved if it was moved in any branch.

void MergeMoveStates(LowerCtx& base, std::initializer_list<const LowerCtx*> branches) {
  for (auto& [name, states] : base.binding_states) {
    if (states.empty()) {
      continue;
    }
    auto& state = states.back();

    // Collect move state from all branches
    bool moved_any = false;
    std::unordered_set<std::string> fields;
    for (const LowerCtx* ctx : branches) {
      auto it = ctx->binding_states.find(name);
      if (it == ctx->binding_states.end() || it->second.empty()) {
        continue;
      }
      const BindingState* bstate = &it->second.back();
      if (!bstate) {
        continue;
      }
      if (bstate->is_moved) {
        moved_any = true;
      } else if (!moved_any) {
        fields.insert(bstate->moved_fields.begin(), bstate->moved_fields.end());
      }
    }

    if (moved_any) {
      state.is_moved = true;
      state.moved_fields.clear();
    } else {
      state.is_moved = false;
      state.moved_fields.assign(fields.begin(), fields.end());
    }
  }
}

// ---------------------------------------------------------------------------
// Helper: MergeFailures
// ---------------------------------------------------------------------------
// Merge failure flags from branch context into base context.

void MergeFailures(LowerCtx& base, const LowerCtx& branch) {
  if (branch.resolve_failed) {
    base.resolve_failed = true;
  }
  if (branch.codegen_failed) {
    base.codegen_failed = true;
  }
  for (const auto& name : branch.resolve_failures) {
    if (std::find(base.resolve_failures.begin(), base.resolve_failures.end(), name) ==
        base.resolve_failures.end()) {
      base.resolve_failures.push_back(name);
    }
  }
}

}  // namespace

// =============================================================================
// Section 6.4 LowerIfExpr - lower if expression
// =============================================================================
//
// Per (Lower-Expr-If):
//   Gamma |- LowerExpr(cond) => <IR_c, v_c>
//   Gamma |- LowerBlock(b_1) => <IR_1, v_1>
//   Gamma |- LowerBlock(b_2) => <IR_2, v_2>
//   ---
//   Gamma |- LowerExpr(IfExpr(cond, b_1, b_2)) => <SeqIR(IR_c, IfIR(...)), v_if>
//
// The if expression is lowered to:
// 1. Lower the condition, capturing temporaries for cleanup
// 2. Lower the then branch in a fresh context, capturing temporaries
// 3. Lower the else branch (or produce unit) in a fresh context
// 4. Merge move states and failures from both branches
// 5. Build IRIf node with condition value, branch IR, and branch values
// =============================================================================

LowerResult LowerIfExpr(const ast::Expr& expr,
                        const ast::IfExpr& if_expr,
                        LowerCtx& ctx) {
  SPEC_RULE("Lower-Expr-If");

  // Lower condition
  auto* prev_sink = ctx.temp_sink;
  std::vector<TempValue> cond_temps;
  ctx.temp_sink = &cond_temps;
  auto prev_suppress = ctx.suppress_temp_at_depth;
  ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
  auto cond_result = LowerExpr(*if_expr.cond, ctx);
  ctx.suppress_temp_at_depth = prev_suppress;
  ctx.temp_sink = prev_sink;
  IRPtr cond_cleanup = CleanupTemps(cond_temps, ctx);
  if (IsNoopIR(cond_cleanup)) {
    cond_cleanup = EmptyIR();
  }

  // Lower then branch
  LowerCtx then_ctx = MakeBranchCtx(ctx);
  std::vector<TempValue> then_temps;
  then_ctx.temp_sink = &then_temps;
  auto then_prev_suppress = then_ctx.suppress_temp_at_depth;
  then_ctx.suppress_temp_at_depth = then_ctx.temp_depth + 1;
  auto then_result = LowerExpr(*if_expr.then_expr, then_ctx);
  then_ctx.suppress_temp_at_depth = then_prev_suppress;
  IRPtr then_cleanup = CleanupTemps(then_temps, then_ctx);
  if (!IsNoopIR(then_cleanup) && IRFlowMayFallThrough(then_result.ir)) {
    then_result.ir = SeqIR({then_result.ir, then_cleanup});
  }

  // Lower else branch (if present)
  LowerCtx else_ctx = MakeBranchCtx(ctx);
  *else_ctx.temp_counter = std::max(*else_ctx.temp_counter, *then_ctx.temp_counter);
  LowerResult else_result;
  if (if_expr.else_expr) {
    std::vector<TempValue> else_temps;
    else_ctx.temp_sink = &else_temps;
    auto else_prev_suppress = else_ctx.suppress_temp_at_depth;
    else_ctx.suppress_temp_at_depth = else_ctx.temp_depth + 1;
    else_result = LowerExpr(*if_expr.else_expr, else_ctx);
    else_ctx.suppress_temp_at_depth = else_prev_suppress;
    IRPtr else_cleanup = CleanupTemps(else_temps, else_ctx);
    if (!IsNoopIR(else_cleanup) && IRFlowMayFallThrough(else_result.ir)) {
      else_result.ir = SeqIR({else_result.ir, else_cleanup});
    }
  } else {
    else_result.ir = EmptyIR();
    else_result.value = ctx.FreshTempValue("unit");
    ctx.RegisterValueType(else_result.value, analysis::MakeTypePrim("()"));
  }

  MergeLowerCtxTemps(ctx, then_ctx);
  MergeLowerCtxTemps(ctx, else_ctx);
  ctx.MergeGeneratedProcsFrom(then_ctx);
  ctx.MergeGeneratedProcsFrom(else_ctx);
  MergeMoveStates(ctx, {&then_ctx, &else_ctx});
  MergeFailures(ctx, then_ctx);
  MergeFailures(ctx, else_ctx);

  // Create if IR
  IRValue result_value = ctx.FreshTempValue("if");
  analysis::TypeRef result_type;
  if (ctx.expr_type) {
    result_type = ctx.expr_type(expr);
  }
  if (!result_type && !if_expr.else_expr) {
    result_type = analysis::MakeTypePrim("()");
  }
  if (result_type) {
    ctx.RegisterValueType(result_value, result_type);
  }

  IRIf if_ir;
  if_ir.cond = cond_result.value;
  if_ir.then_ir = then_result.ir;
  if_ir.then_value = then_result.value;
  if_ir.else_ir = else_result.ir;
  if_ir.else_value = else_result.value;
  if_ir.result = result_value;

  return LowerResult{SeqIR({cond_result.ir, cond_cleanup, MakeIR(std::move(if_ir))}),
                     result_value};
}

}  // namespace ultraviolet::codegen
