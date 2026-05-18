// =============================================================================
// Expression Lowering: ErrorExpr
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Line 16071-16073: (Lower-Expr-Error)
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 1203-1211: LowerError function
//
// =============================================================================

#include "05_codegen/lower/expr/error_expr.h"
#include "05_codegen/checks/checks.h"
#include "00_core/assert_spec.h"

namespace ultraviolet::codegen {

// =============================================================================
// LowerError - Lower an error expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Error)
//   Error expressions represent unreachable code paths that should have been
//   caught by semantic analysis. They lower to a panic with ErrorExpr reason.
//
// Error expressions are produced by the parser for malformed constructs.
// In a well-formed program, they should never be executed, so we emit a panic.
// =============================================================================

LowerResult LowerError(const ast::ErrorExpr& /*expr*/, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Error");
    ctx.codegen_failed = true;

    // Create an opaque value for the unreachable result
    IRValue value = ctx.FreshTempValue("unreachable");

    // Emit panic IR for the error expression
    return LowerResult{LowerPanic(PanicReason::ErrorExpr, ctx), value};
}

}  // namespace ultraviolet::codegen
