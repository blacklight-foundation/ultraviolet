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

  // (T-Outcome-Intro) §13.1.4: a bare RHS assigned to an Outcome place
  // introduces implicitly as Outcome::Value/Error. No-op otherwise.
  IRValue rhs_value = rhs_result.value;
  if (ctx.expr_type && stmt.value && stmt.place) {
    analysis::TypeRef value_type = ctx.LookupValueType(rhs_value);
    if (!value_type) {
      value_type = ctx.expr_type(*stmt.value);
    }
    const analysis::TypeRef place_type = ctx.expr_type(*stmt.place);
    rhs_value = MaybeWrapImplicitOutcome(rhs_value, value_type, place_type, ctx);
  }

  // Write to the place
  IRPtr write_ir = LowerWritePlace(*stmt.place, rhs_value, ctx);

  core::Conformance::Record(
      "rule.18.Lower-Stmt-Assign",
      std::nullopt,
      "source=LowerAssignStmt;ir_form=SeqIR;components=LowerExpr,LowerWritePlace;"
      "rhs_value_written=true");

  return SeqIR({rhs_result.ir, write_ir});
}

}  // namespace ultraviolet::codegen
