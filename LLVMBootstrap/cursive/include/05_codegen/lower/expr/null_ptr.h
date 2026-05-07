#pragma once

// =============================================================================
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16151-16153: (Lower-Expr-PtrNull)
//     Gamma |- LowerExpr(PtrNull) => <epsilon, NullPtrValue>
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

// Lower a null pointer expression to IR.
// Implements the (Lower-Expr-PtrNull) rule from the spec.
//
// Parameters:
//   expr - The null pointer expression node
//   ctx  - Lowering context
//
// Returns:
//   LowerResult with EmptyIR and a null pointer value.
LowerResult LowerPtrNull(const ast::PtrNullExpr& expr, LowerCtx& ctx);

}  // namespace cursive::codegen
