// =============================================================================
// Expression Lowering: SyncExpr
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 11.3 (Sync Expression)
//   - sync e runs async to completion synchronously
//   - Constraints: Out = (), In = ()
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 2629-2650: SyncExpr visitor
//
// =============================================================================

#include "05_codegen/lower/expr/sync_expr.h"
#include "04_analysis/typing/type_expr.h"
#include "00_core/assert_spec.h"

namespace cursive::codegen {

// =============================================================================
// LowerSyncExpr - Lower a sync expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Sync)
//   Gamma |- LowerExpr(async_expr) => <IR_a, v_async>
//   --------------------------------------------------------
//   Gamma |- LowerExpr(sync async_expr) => <SeqIR(IR_a, IRSync), v_result>
//
// The sync expression:
// 1. Evaluates the async expression
// 2. Runs it to completion synchronously (blocking)
// 3. Returns the result (or propagates error)
//
// CONSTRAINTS:
// - Only allowed in non-async context
// - The async must have Out = () and In = ()
// =============================================================================

LowerResult LowerSyncExpr(const ast::SyncExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Sync");

    // Lower the async expression
    auto async_result = LowerExpr(*expr.value, ctx);

    // Create the sync IR node
    IRSync sync;
    sync.async_value = async_result.value;
    sync.result = ctx.FreshTempValue("sync_result");

    // Get async type info for emission
    if (ctx.expr_type) {
        sync.async_type = ctx.expr_type(*expr.value);
        // Extract result_type and error_type from async signature
        if (auto sig = analysis::GetAsyncSig(sync.async_type)) {
            sync.result_type = sig->result;
            sync.error_type = sig->err;
        }
    }

    // Preserve typed result metadata for LLVM emission.
    analysis::TypeRef sync_value_type = sync.result_type;
    if (ctx.expr_type) {
        ast::Expr sync_expr_wrapper;
        sync_expr_wrapper.node = expr;
        analysis::TypeRef typed = ctx.expr_type(sync_expr_wrapper);
        if (typed) {
            sync_value_type = typed;
        }
    }
    if (!sync_value_type) {
        sync_value_type = analysis::MakeTypePrim("()");
    }
    ctx.RegisterValueType(sync.result, sync_value_type);

    IRValue sync_result = sync.result;
    return LowerResult{SeqIR({async_result.ir, MakeIR(std::move(sync))}),
                       sync_result};
}

}  // namespace cursive::codegen
