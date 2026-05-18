#pragma once

// =============================================================================
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Line 16071-16073: (Lower-Expr-Error)
//     Error expressions represent unreachable code paths that should have been
//     caught by semantic analysis. They lower to a panic with ErrorExpr reason.
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 1203-1211: LowerError function
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// Lower an error expression to IR.
// Implements the (Lower-Expr-Error) rule from the spec.
//
// Parameters:
//   expr - The error expression node
//   ctx  - Lowering context
//
// Returns:
//   LowerResult with panic IR and an opaque value.
//   Error expressions should never be executed in a well-formed program.
LowerResult LowerError(const ast::ErrorExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
