// =============================================================================
// Compound Assignment Statement Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 16644-16647 (Lower-Stmt-CompoundAssign)
//   - LowerReadPlace(place) produces IR_p, v_old
//   - LowerExpr(expr) produces IR_e, v_rhs
//   - BinOp(op, v_old, v_rhs) then LowerWritePlace
//   - Result: SeqIR(IR_p, IR_e, IR_op, IR_w)
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 382-430: LowerCompoundAssignStmt function
//
// =============================================================================

#include "05_codegen/lower/stmt/compound_assign_stmt.h"

#include <optional>
#include <string>
#include <vector>

#include "00_core/assert_spec.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

// ============================================================================
// Lower-Stmt-CompoundAssign
// ============================================================================
//
// Per the spec (Lines 16644-16647):
//   LowerReadPlace(place) => <IR_p, v_old>
//   LowerExpr(expr) => <IR_e, v_rhs>
//   Compute new value via binary operation
//   LowerWritePlace(place, v_new) => IR_w
//   Result: SeqIR(IR_p, IR_e, IR_op, IR_w)
//
// The implementation handles:
//   - Stripping '=' suffix from compound operators (e.g., "+=" -> "+")
//   - Panic checks for operations that can fail (div, mod, shift, overflow)
//   - Writing the result back to the place
//
IRPtr LowerCompoundAssignStmt(const ast::CompoundAssignStmt& stmt,
                              LowerCtx& ctx) {
  SPEC_RULE("Lower-Stmt-CompoundAssign");

  // Read the current value from the place
  auto lhs_result = LowerReadPlace(*stmt.place, ctx);

  // Lower the RHS expression
  auto rhs_result = LowerExpr(*stmt.value, ctx);

  // Extract the operator (strip trailing '=')
  std::string op = stmt.op;
  if (!op.empty() && op.back() == '=') {
    op.pop_back();
  }

  // Create a fresh temp for the binary operation result
  IRValue new_value = ctx.FreshTempValue("binop");

  std::vector<IRPtr> op_parts;

  // Determine if this operation needs runtime checks
  auto panic_reason = [&]() -> std::optional<PanicReason> {
    if (op == "/" || op == "%") {
      return PanicReason::DivZero;
    }
    if (op == "<<" || op == ">>") {
      return PanicReason::Shift;
    }
    if (op == "+" || op == "-" || op == "*" || op == "**") {
      return PanicReason::Overflow;
    }
    return std::nullopt;
  }();

  // Emit check IR if needed
  if (panic_reason.has_value()) {
    IRCheckOp check;
    check.op = op;
    check.reason = PanicReasonString(*panic_reason);
    check.lhs = lhs_result.value;
    check.rhs = rhs_result.value;
    op_parts.push_back(MakeIR(std::move(check)));
    op_parts.push_back(PanicFollowup(ctx));
  }

  // Emit the binary operation
  IRBinaryOp binop;
  binop.op = op;
  binop.lhs = lhs_result.value;
  binop.rhs = rhs_result.value;
  binop.result = new_value;
  op_parts.push_back(MakeIR(std::move(binop)));

  IRPtr op_ir = SeqIR(std::move(op_parts));

  // Write the new value back to the place
  IRPtr write_ir = LowerWritePlace(*stmt.place, new_value, ctx);

  return SeqIR({lhs_result.ir, rhs_result.ir, op_ir, write_ir});
}

}  // namespace cursive::codegen
