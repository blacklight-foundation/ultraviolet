#pragma once

// =============================================================================
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16270-16275: (Lower-Expr-QualifiedApply)
//     Gamma |- ResolveType(T) => tau
//     Gamma |- LowerCallWith(tau::f, args) => <IR, v>
//     ------------------------------------------------------------------
//     Gamma |- LowerExpr(QualifiedApply(T, f, args)) => <IR, v>
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// Lower a qualified apply expression to IR.
// Implements the (Lower-Expr-QualifiedApply) rule from the spec.
//
// A qualified apply is a method call using qualified syntax: T~>method(args)
// where T is a type path and method is resolved in the type's namespace.
//
// Parameters:
//   expr - The qualified apply expression node
//   ctx  - Lowering context
//
// Returns:
//   LowerResult with the IR for the qualified method call.
LowerResult LowerQualifiedApply(const ast::QualifiedApplyExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
