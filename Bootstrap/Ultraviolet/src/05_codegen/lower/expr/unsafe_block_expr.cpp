// =============================================================================
// Expression Lowering: UnsafeBlockExpr
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16233-16236: (Lower-Expr-UnsafeBlock)
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 1637-1639: UnsafeBlockExpr visitor
//
// =============================================================================

#include "05_codegen/lower/expr/unsafe_block_expr.h"
#include "05_codegen/lower/expr/block_expr.h"
#include "00_core/assert_spec.h"

namespace ultraviolet::codegen {

// =============================================================================
// LowerUnsafeBlockExpr - Lower an unsafe block expression to IR
// =============================================================================
// SPEC: (Lower-Expr-UnsafeBlock)
//   Gamma |- LowerBlock(block) => <IR_b, v>
//   ----------------------------------------
//   Gamma |- LowerExpr(unsafe { block }) => <IR_b, v>
//
// Unsafe blocks in Ultraviolet allow operations that are normally disallowed:
// - Raw pointer dereference
// - Transmute operations
// - Calling extern functions
//
// At the IR level, unsafe blocks are lowered exactly like regular blocks.
// The "unsafe" aspect is only relevant during type checking and safety
// verification, not during code generation.
// =============================================================================

LowerResult LowerUnsafeBlockExpr(const ast::UnsafeBlockExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-UnsafeBlock");

    // An unsafe block is lowered exactly like a regular block
    // The safety implications are handled during semantic analysis
    return LowerBlock(*expr.block, ctx);
}

}  // namespace ultraviolet::codegen
