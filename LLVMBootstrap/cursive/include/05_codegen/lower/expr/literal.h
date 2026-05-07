#pragma once

// =============================================================================
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Line 16048-16051: (Lower-Expr-Literal)
//     T = ExprType(Literal(l))    LiteralValue(l, T) = v
//     Gamma |- LowerExpr(Literal(l)) => <epsilon, v>
//
// Literal lowering produces an immediate IRValue with no accompanying IR.
// The value's bytes field contains the encoded literal representation.
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

// Lower a literal expression to IR.
// Implements the (Lower-Expr-Literal) rule from the spec.
//
// Parameters:
//   expr - The full expression (for type lookup)
//   lit  - The literal expression node
//   ctx  - Lowering context
//
// Returns:
//   LowerResult with EmptyIR() and an IRValue containing the encoded literal.
//
// Encoding rules:
//   - Integers: encoded via EncodeConst if type info available, else big-endian
//   - Floats: IEEE 754 encoding (f16, f32, f64)
//   - Bool: 1 byte (0x00 for false, 0x01 for true)
//   - Char: 4-byte UTF-32 codepoint (little-endian)
//   - String: UTF-8 bytes after escape sequence decoding
//   - Null: single zero byte
LowerResult LowerLiteral(const ast::Expr& expr,
                         const ast::LiteralExpr& lit,
                         LowerCtx& ctx);

}  // namespace cursive::codegen
