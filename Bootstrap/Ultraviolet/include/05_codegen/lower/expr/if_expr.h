#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// =============================================================================
// Section 6.4 If Expression Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16203-16206: (Lower-Expr-If)
//
// (Lower-Expr-If):
//   Gamma |- LowerExpr(cond) => <IR_c, v_c>
//   Gamma |- LowerBlock(b_1) => <IR_1, v_1>
//   Gamma |- LowerBlock(b_2) => <IR_2, v_2>
//   ---
//   Gamma |- LowerExpr(IfExpr(cond, b_1, b_2)) => <SeqIR(IR_c, IfIR(...)), v_if>
//
// The if expression produces:
// - IR: Sequence of condition IR, condition cleanup, and IRIf node
// - Value: Fresh temporary holding the result from whichever branch executes
//
// Key behaviors:
// - Condition temporaries are cleaned up before branching
// - Branch temporaries are cleaned up within each branch (unless it terminates)
// - Move states from both branches are merged (conservatively moved if any branch moves)
// - Failure flags are propagated from both branches
// =============================================================================

/// Lower an if expression to IR
///
/// Per (Lower-Expr-If), lowers:
/// - The condition expression
/// - The then branch
/// - The else branch (or produces unit if absent)
///
/// Returns IR sequence containing condition lowering, condition cleanup,
/// and an IRIf node, plus a fresh temporary for the result value.
LowerResult LowerIfExpr(const ast::Expr& expr,
                       const ast::IfExpr& if_expr,
                       LowerCtx& ctx);

}  // namespace ultraviolet::codegen
