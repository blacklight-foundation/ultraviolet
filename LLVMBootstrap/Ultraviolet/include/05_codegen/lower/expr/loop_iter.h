#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// =============================================================================
// Section 6.5 Iterator Loop Lowering
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.5 (Statement and Block Lowering)
//   - Lines 16751-16754: (Lower-Loop-Iter)
//
// (Lower-Loop-Iter):
//   Gamma |- LowerExpr(iter) => <IR_i, v_iter>
//   Gamma |- LowerBlock(body) => <IR_b, v_b>
//   ---
//   Gamma |- LowerLoop(LoopIter(pat, ty_opt, iter, body)) =>
//       <LoopIR(LoopIter, pat, ty_opt, IR_i, v_iter, IR_b, v_b), v_loop>
//
// The iterator loop expression produces:
// - IR: IRLoop node with kind Iter containing iterator IR and body IR
// - Value: Fresh temporary holding the loop result
//
// Key behaviors:
// - Loop scope is pushed for break/continue cleanup tracking
// - Iterator expression is lowered first
// - Pattern bindings are registered with element type from iterator
// - Body is lowered with pattern bindings in scope
// - Loop scope is popped after body lowering
// =============================================================================

/// Lower an iterator loop expression to IR
///
/// Per (Lower-Loop-Iter), lowers:
/// - The iterator expression
/// - The loop body with pattern bindings in scope
///
/// Returns IRLoop with kind Iter containing:
/// - pattern: The loop variable pattern
/// - type_opt: Optional type annotation on the pattern
/// - iter_ir: IR for computing the iterator
/// - iter_value: Value of the iterator expression
/// - body_ir: IR for the loop body
/// - body_value: Value of the loop body
/// - result: Fresh temporary for the loop result
LowerResult LowerLoopIter(const ast::Expr& expr,
                          const ast::LoopIterExpr& loop_expr,
                          LowerCtx& ctx);

}  // namespace ultraviolet::codegen
