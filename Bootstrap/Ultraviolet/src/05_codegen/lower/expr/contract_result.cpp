// =============================================================================
// Expression Lowering: ResultExpr
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 5.4 (Contracts)
//   - @result intrinsic for postcondition reference to return value
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//
// =============================================================================

#include "05_codegen/lower/expr/contract_result.h"
#include "00_core/assert_spec.h"

namespace ultraviolet::codegen {

// =============================================================================
// LowerResultExpr - Lower an @result expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Result)
//   @result in a postcondition refers to the procedure's return value.
//   This is only valid in postcondition contexts (right of =>).
//
// In postcondition evaluation:
//   1. At procedure exit, @result evaluates to the return value
//   2. The contract checker verifies predicates involving @result
//
// Context Requirements:
//   - @result may only appear in postcondition context
//   - The type of @result matches the procedure's return type
// =============================================================================

LowerResult LowerResultExpr(const ast::ResultExpr& /*expr*/, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Result");
    if (ctx.contract_result_value.has_value()) {
        if (ctx.proc_ret_type) {
            ctx.RegisterValueType(*ctx.contract_result_value, ctx.proc_ret_type);
        }
        return LowerResult{EmptyIR(), *ctx.contract_result_value};
    }

    // Missing @result binding is a codegen conformance failure.
    ctx.ReportCodegenFailure();
    IRValue fallback = ctx.FreshTempValue("contract_result_missing");
    if (ctx.proc_ret_type) {
        ctx.RegisterValueType(fallback, ctx.proc_ret_type);
    }
    return LowerResult{EmptyIR(), fallback};
}

}  // namespace ultraviolet::codegen
