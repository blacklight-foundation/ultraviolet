#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

// =============================================================================
// Unary Expression Lowering
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16158-16161: (Lower-Expr-Unary)
//   - Lines 16318-16326: (Lower-UnOp-Ok) and (Lower-UnOp-Panic)
//
// UNARY OPERATORS:
//   Logical: ! (not)
//   Arithmetic: - (negate)
//
// For negation of signed integers, overflow is possible when negating the
// minimum value (e.g., -(-128i8) overflows). This triggers a panic per
// Lower-UnOp-Panic.

// Forward declarations
struct LowerCtx;
struct LowerResult;

// Section 6.4 LowerUnaryExpr - main unary expression lowering entry point
// Dispatches to LowerUnOp with the operator string and operand
LowerResult LowerUnaryExpr(const ast::UnaryExpr& expr, LowerCtx& ctx);

}  // namespace cursive::codegen
