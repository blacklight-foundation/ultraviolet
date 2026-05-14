// =============================================================================
// Expression Lowering: YieldFromExpr
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 11.2 (Yield-From Expression)
//   - yield from e delegates to another async
//   - release modifier releases keys before suspend
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 2589-2627: YieldFromExpr visitor
//
// =============================================================================

#include "05_codegen/lower/expr/yield_from_expr.h"
#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_concurrency.h"

namespace cursive::codegen {

// =============================================================================
// LowerYieldFromExpr - Lower a yield-from expression to IR
// =============================================================================
// SPEC: (Lower-Expr-YieldFrom)
//   Gamma |- LowerExpr(source) => <IR_s, v_source>
//   --------------------------------------------------------
//   Gamma |- LowerExpr(yield from source) => <SeqIR(IR_s, Bind, IRYieldFrom), v_result>
//
// The yield-from expression:
// 1. Evaluates the source async expression
// 2. Binds it to a local for iteration
// 3. Delegates all yields from the source to the current async
// 4. Returns the source's final result
//
// With release modifier:
//   yield release from source - releases keys before each suspend
// =============================================================================

LowerResult LowerYieldFromExpr(const ast::YieldFromExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-YieldFrom");

    // Lower the source expression
    auto source_result = LowerExpr(*expr.value, ctx);

    // Get the source type
    analysis::TypeRef source_type;
    if (ctx.expr_type) {
        source_type = ctx.expr_type(*expr.value);
    }
    if (!source_type) {
        source_type = ctx.LookupValueType(source_result.value);
    }

    // Bind the source to a local variable for iteration
    const std::string source_name = ctx.FreshTempValue("yield_from_source").name;
    IRBindVar bind_source;
    bind_source.name = source_name;
    bind_source.value = source_result.value;
    bind_source.type = source_type;

    // Create local reference to the source
    IRValue source_local;
    source_local.kind = IRValue::Kind::Local;
    source_local.name = source_name;
    if (source_type) {
        ctx.RegisterValueType(source_local, source_type);
    }

    // Create the yield-from IR node
    IRYieldFrom yf;
    yf.release = expr.release;
    yf.source = source_local;
    yf.result = ctx.FreshTempValue("yield_from_result");
    yf.source_type = source_type;
    // state_index will be assigned during async procedure transformation
    yf.state_index = 0;

    IRValue yf_result = yf.result;

    // Preserve the delegated completion type on the synthetic result value.
    // Relying only on the generic LowerExpr fallback can lose precision in
    // transformed/reconstructed expression contexts.
    analysis::TypeRef result_type;
    const analysis::ScopeContext& scope = ScopeForLowering(ctx);
    if (const auto source_sig = analysis::AsyncSigOf(scope, source_type)) {
        result_type = source_sig->result;
    }
    if (!result_type && ctx.expr_type) {
        ast::Expr wrapped;
        wrapped.node = expr;
        result_type = ctx.expr_type(wrapped);
    }
    if (result_type) {
        ctx.RegisterValueType(yf_result, result_type);
    }

    return LowerResult{SeqIR({source_result.ir,
                              MakeIR(std::move(bind_source)),
                              MakeIR(std::move(yf))}),
                       yf_result};
}

}  // namespace cursive::codegen
