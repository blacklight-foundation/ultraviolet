// =============================================================================
// Continue Statement Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 16692-16694 (Lower-Stmt-Continue)
//   - ContinueIR
//   - TempCleanupIR must be emitted before ContinueIR
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 572-593: LowerContinueStmt function
//
// =============================================================================

#include "05_codegen/lower/stmt/continue_stmt.h"

#include <vector>

#include "00_core/assert_spec.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_stmt.h"

namespace cursive::codegen {

// ============================================================================
// Lower-Stmt-Continue
// ============================================================================
//
// Per the spec (Lines 16692-16694):
//   TempCleanupIR(temps) => IR_t
//   CleanupPlanToLoopScope => IR_c
//   Result: SeqIR(IR_t, IR_c, ContinueIR)
//
// The implementation handles:
//   - Dropping temporaries before unwinding
//   - Cleanup from current scope to loop scope
//
IRPtr LowerContinueStmt(const ast::ContinueStmt& /*stmt*/,
                        LowerCtx& ctx,
                        const std::vector<TempValue>& temps) {
  SPEC_RULE("Lower-Stmt-Continue");

  std::vector<IRPtr> ir_parts;

  // Drop statement-scoped temporaries before unwinding scopes.
  if (!temps.empty()) {
    ir_parts.push_back(CleanupList(temps, ctx));
  }

  // Section 6.8: Emit cleanup for variables from current scope to loop scope
  CleanupPlan cleanup_plan = ComputeCleanupPlanToLoopScope(ctx);
  CleanupPlan remainder = ComputeCleanupPlanRemainder(CleanupTarget::ToLoopScope, ctx);
  ir_parts.push_back(EmitCleanupWithRemainder(cleanup_plan, remainder, ctx));

  // Emit the continue
  IRContinue cont;
  ir_parts.push_back(MakeIR(std::move(cont)));

  return SeqIR(std::move(ir_parts));
}

}  // namespace cursive::codegen
