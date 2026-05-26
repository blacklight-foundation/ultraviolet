#pragma once

// =============================================================================
// loop_infinite.h - Infinite loop expression lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.5 (Statement and Block Lowering)
//   - Lines 16741-16744: (Lower-Loop-Infinite)
//
// This header declares the lowering function for infinite loop expressions
// (loop { body }). The infinite loop is lowered to an IRLoop node with
// IRLoopKind::Infinite.
//
// =============================================================================

#include "02_source/ast/ast.h"
#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// =============================================================================
// Section 6.5 LowerLoopInfinite
// =============================================================================
//
// Lower an infinite loop expression to IR.
//
// Per (Lower-Loop-Infinite):
//   Gamma |- LowerBlock(body) => <IR_b, v_b>
//   ---
//   Gamma |- LowerLoop(LoopInfinite(body)) => <LoopIR(LoopInfinite, IR_b, v_b), v_loop>
//
// Parameters:
//   expr - The LoopInfiniteExpr AST node to lower
//   ctx  - The lowering context (modified during lowering)
//
// Returns:
//   LowerResult containing the IRLoop node and result value
//
// =============================================================================
LowerResult LowerLoopInfinite(const ast::Expr& expr,
                              const ast::LoopInfiniteExpr& loop_expr,
                              LowerCtx& ctx);

}  // namespace ultraviolet::codegen
