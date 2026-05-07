#pragma once

// =============================================================================
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16062-16065: (Lower-Expr-Ident-Path)
//   - Lines 16067-16070: (Lower-Expr-Path)
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

// Lower a path expression to IR.
// Implements the (Lower-Expr-Path) rule from the spec.
//
// Parameters:
//   expr - The path expression node
//   ctx  - Lowering context
//
// Returns:
//   LowerResult with a symbol value for the resolved path.
LowerResult LowerPath(const ast::PathExpr& expr, LowerCtx& ctx);

}  // namespace cursive::codegen
