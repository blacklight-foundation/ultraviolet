// =============================================================================
// Expression Lowering: YieldExpr
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 11.2 (Yield Expression)
//   - yield value suspends async and produces output
//   - yield release value releases keys before suspending
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - YieldExpr visitor produces IRYield with state index
//
// =============================================================================

#include "05_codegen/lower/expr/yield_expr.h"
#include "00_core/assert_spec.h"

namespace cursive::codegen {

// =============================================================================
// LowerYieldExpr - Lower a yield expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Yield)
//   Gamma |- LowerExpr(value) => <IR_v, v>
//   --------------------------------------------------------
//   Gamma |- LowerExpr(yield value) => <SeqIR(IR_v, IRYield), v_unit>
//
// The yield expression:
// 1. Evaluates the value to yield
// 2. Suspends the async procedure
// 3. Returns unit (the async resumes with an input value)
//
// With release modifier:
//   yield release value - releases held keys before suspending
//
// CRITICAL: yield MUST NOT occur while keys are held unless 'release' is used
// =============================================================================

LowerResult LowerYieldExpr(const ast::YieldExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Yield");

    // Lower the value to yield
    auto value_result = LowerExpr(*expr.value, ctx);

    // Create the yield IR node
    IRYield yield;
    yield.value = value_result.value;
    yield.release = expr.release;  // true if "yield release"
    yield.result = ctx.FreshTempValue("yield_input");

    // `yield e` evaluates to the async input type (`In`) at resume points.
    analysis::TypeRef yield_type = analysis::MakeTypePrim("()");
    if (const auto sig = analysis::GetAsyncSig(ctx.proc_ret_type)) {
        if (sig->in) {
            yield_type = sig->in;
        }
    }
    ctx.RegisterValueType(yield.result, yield_type);

    IRValue yield_result = yield.result;
    return LowerResult{
        SeqIR({value_result.ir, MakeIR(std::move(yield))}),
        yield_result
    };
}

}  // namespace cursive::codegen
