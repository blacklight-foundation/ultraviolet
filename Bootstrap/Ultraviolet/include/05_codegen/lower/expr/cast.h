#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// =============================================================================
// Cast Expression Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16178-16181: (Lower-Expr-Cast)
//   - Lines 16338-16346: (Lower-Cast) and (Lower-Cast-Panic)
//
// CAST KINDS:
//   - Numeric widening (i32 -> i64)
//   - Numeric narrowing (i64 -> i32) - may panic on overflow
//   - Pointer casts
//   - Bool to int, int to bool
//   - Dynamic class casts ($ClassName) - creates fat pointer (data_ptr + vtable)

// Forward declarations
struct LowerCtx;
struct LowerResult;

// Section 6.4 LowerCastExpr - main cast expression lowering entry point
// Lowers a CastExpr AST node to IR, handling:
// - Type resolution from AST type annotation
// - Fallback to type inference if annotation missing
// - Dynamic class type casts (produces fat pointer via DynPack)
// - Regular casts with runtime check for validity
LowerResult LowerCastExpr(const ast::CastExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
