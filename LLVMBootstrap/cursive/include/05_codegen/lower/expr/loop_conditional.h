#pragma once

// =============================================================================
// loop_conditional.h - Conditional loop expression lowering
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.5 (Statement and Block Lowering)
//   - Lines 16746-16754: (Lower-Loop-Cond)
//
// This header declares the lowering function for conditional loop expressions
// (loop condition { body }). The conditional loop is lowered to an IRLoop node
// with IRLoopKind::Conditional, containing both the condition expression IR
// and the body block IR.
//
// =============================================================================

#include "02_source/ast/ast.h"
#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

// =============================================================================
// Section 6.5 LowerLoopConditional
// =============================================================================
//
// Lower a conditional loop expression to IR.
//
// Per (Lower-Loop-Cond):
//   Gamma |- LowerExpr(cond) => <IR_c, v_c>
//   Gamma |- LowerBlock(body) => <IR_b, v_b>
//   ---
//   Gamma |- LowerLoop(LoopCond(cond, body)) =>
//            <LoopIR(LoopCond, IR_c, v_c, IR_b, v_b), v_loop>
//
// Parameters:
//   expr - The LoopConditionalExpr AST node to lower
//   ctx  - The lowering context (modified during lowering)
//
// Returns:
//   LowerResult containing the IRLoop node and result value
//
// =============================================================================
LowerResult LowerLoopConditional(const ast::Expr& expr,
                                 const ast::LoopConditionalExpr& loop_expr,
                                 LowerCtx& ctx);

}  // namespace cursive::codegen
