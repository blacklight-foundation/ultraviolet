#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// =============================================================================
// Binary Expression Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16163-16176: Short-circuit and non-short-circuit binary ops
//     - (Lower-Expr-Bin-And): && with short-circuit
//     - (Lower-Expr-Bin-Or): || with short-circuit
//     - (Lower-Expr-Binary): All other operators
//   - Lines 16328-16336: (Lower-BinOp-Ok) and (Lower-BinOp-Panic)
//
// BINARY OPERATORS:
//   Arithmetic: + - * / % **
//   Comparison: == != < <= > >=
//   Logical: && ||
//   Bitwise: & | ^ << >>

// Forward declarations
struct LowerCtx;
struct LowerResult;

// Section 6.4 LowerBinaryExpr - main binary expression lowering dispatcher
// Handles both short-circuit (&&, ||) and regular binary operators
LowerResult LowerBinaryExpr(const ast::BinaryExpr& expr, LowerCtx& ctx);

// Section 6.4 Lower-Expr-Bin-And (short-circuit logical AND)
// Lowers && using IRIf: if lhs is false, skip rhs and produce false
LowerResult LowerBinAnd(const ast::Expr& lhs,
                        const ast::Expr& rhs,
                        LowerCtx& ctx);

// Section 6.4 Lower-Expr-Bin-Or (short-circuit logical OR)
// Lowers || using IRIf: if lhs is true, skip rhs and produce true
LowerResult LowerBinOr(const ast::Expr& lhs,
                       const ast::Expr& rhs,
                       LowerCtx& ctx);

}  // namespace ultraviolet::codegen
