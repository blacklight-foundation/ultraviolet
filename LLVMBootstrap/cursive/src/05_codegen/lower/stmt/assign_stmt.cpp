// =============================================================================
// MIGRATION MAPPING: stmt/assign_stmt.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 16639-16642 (Lower-Stmt-Assign)
//   - LowerExpr(expr) produces IR_e, v
//   - LowerWritePlace(place, v) produces IR_w
//   - Result: SeqIR(IR_e, IR_w)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 366-376: LowerAssignStmt function
//   - Simple RHS lowering then write to place
//
// DEPENDENCIES:
//   - cursive/src/05_codegen/ir/ir_model.h (IRWriteVar, IRWriteField, etc.)
//   - cursive/src/05_codegen/lower/lower_expr.h (LowerExpr, LowerWritePlace)
//
// =============================================================================

#include "05_codegen/lower/stmt/assign_stmt.h"

#include "00_core/assert_spec.h"

namespace cursive::codegen {

IRPtr LowerAssignStmt(const ast::AssignStmt& stmt, LowerCtx& ctx) {
  SPEC_RULE("Lower-Stmt-Assign");

  // Lower the RHS value
  auto rhs_result = LowerExpr(*stmt.value, ctx);

  // Write to the place
  IRPtr write_ir = LowerWritePlace(*stmt.place, rhs_result.value, ctx);

  return SeqIR({rhs_result.ir, write_ir});
}

}  // namespace cursive::codegen
