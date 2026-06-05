// =============================================================================
// MIGRATION MAPPING: stmt/assign_stmt.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Lines 16639-16642 (Lower-Stmt-Assign)
//   - LowerExpr(expr) produces IR_e, v
//   - LowerWritePlace(place, v) produces IR_w
//   - Result: SeqIR(IR_e, IR_w)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 366-376: LowerAssignStmt function
//   - Simple RHS lowering then write to place
//
// DEPENDENCIES:
//   - ultraviolet/src/05_codegen/ir/ir_model.h (IRWriteVar, IRWriteField, etc.)
//   - ultraviolet/src/05_codegen/lower/lower_expr.h (LowerExpr, LowerWritePlace)
//
// =============================================================================

#include "05_codegen/lower/stmt/assign_stmt.h"

#include "00_core/assert_spec.h"
#include "00_core/spec_trace.h"

namespace ultraviolet::codegen {

IRPtr LowerAssignStmt(const ast::AssignStmt& stmt, LowerCtx& ctx) {
  SPEC_RULE("Lower-Stmt-Assign");

  // Lower the RHS value
  auto rhs_result = LowerExpr(*stmt.value, ctx);

  // Write to the place
  IRPtr write_ir = LowerWritePlace(*stmt.place, rhs_result.value, ctx);

  core::Conformance::Record(
      "rule.18.Lower-Stmt-Assign",
      std::nullopt,
      "source=LowerAssignStmt;ir_form=SeqIR;components=LowerExpr,LowerWritePlace;"
      "rhs_value_written=true");

  return SeqIR({rhs_result.ir, write_ir});
}

}  // namespace ultraviolet::codegen
