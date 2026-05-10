#pragma once

// =============================================================================
// Compound Assignment Statement Lowering
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 16644-16647 (Lower-Stmt-CompoundAssign)
//   - LowerReadPlace(place) produces IR_p, v_old
//   - LowerExpr(expr) produces IR_e, v_rhs
//   - BinOp(op, v_old, v_rhs) then LowerWritePlace
//   - Result: SeqIR(IR_p, IR_e, IR_op, IR_w)
//
// =============================================================================

#include "05_codegen/lower/lower_stmt.h"

namespace cursive::codegen {

// Lower compound assignment statement (+=, -=, *=, etc.)
IRPtr LowerCompoundAssignStmt(const ast::CompoundAssignStmt& stmt,
                              LowerCtx& ctx);

}  // namespace cursive::codegen
