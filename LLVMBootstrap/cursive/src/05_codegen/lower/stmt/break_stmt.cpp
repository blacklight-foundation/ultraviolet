// =============================================================================
// Break Statement Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 16683-16690
//   - Lower-Stmt-Break: SeqIR(IR_e, BreakIR(v))
//   - Lower-Stmt-Break-Unit: BreakIR(none)
//   - TempCleanupIR must be emitted before BreakIR
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 530-566: LowerBreakStmt function
//
// =============================================================================

#include "05_codegen/lower/stmt/break_stmt.h"

#include <vector>

#include "00_core/assert_spec.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_stmt.h"

namespace cursive::codegen {

// ============================================================================
// Lower-Stmt-Break / Lower-Stmt-Break-Unit
// ============================================================================
//
// Per the spec (Lines 16683-16690):
//   If value present:
//     LowerExpr(value) => <IR_e, v>
//     TempCleanupIR(temps) => IR_t
//     CleanupPlanToLoopScope => IR_c
//     Result: SeqIR(IR_e, IR_t, IR_c, BreakIR(v))
//   If no value:
//     Result: SeqIR(IR_t, IR_c, BreakIR(unit))
//
// The implementation handles:
//   - Optional break value expression
//   - Dropping temporaries before unwinding
//   - Cleanup from current scope to loop scope
//
IRPtr LowerBreakStmt(const ast::BreakStmt& stmt,
                     LowerCtx& ctx,
                     const std::vector<TempValue>& temps) {
  std::vector<IRPtr> ir_parts;

  // Lower the break value if present
  std::optional<IRValue> break_value;
  if (stmt.value_opt) {
    SPEC_RULE("Lower-Stmt-Break");
    auto prev_suppress = ctx.suppress_temp_at_depth;
    ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
    auto expr_result = LowerExpr(*stmt.value_opt, ctx);
    ctx.suppress_temp_at_depth = prev_suppress;
    ir_parts.push_back(expr_result.ir);
    break_value = expr_result.value;
  } else {
    SPEC_RULE("Lower-Stmt-Break-Unit");
    break_value = std::nullopt;
  }

  // Drop statement-scoped temporaries before unwinding scopes.
  if (!temps.empty()) {
    ir_parts.push_back(CleanupList(temps, ctx));
  }

  // Section 6.8: Emit cleanup for variables from current scope to loop scope
  CleanupPlan cleanup_plan = ComputeCleanupPlanToLoopScope(ctx);
  CleanupPlan remainder = ComputeCleanupPlanRemainder(CleanupTarget::ToLoopScope, ctx);
  ir_parts.push_back(EmitCleanupWithRemainder(cleanup_plan, remainder, ctx));

  // Emit the break
  IRBreak brk;
  brk.value = break_value;
  ir_parts.push_back(MakeIR(std::move(brk)));

  return SeqIR(std::move(ir_parts));
}

}  // namespace cursive::codegen
