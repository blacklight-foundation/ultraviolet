// =============================================================================
// Expression Lowering: TransmuteExpr
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16183-16186: (Lower-Expr-Transmute)
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 1583-1608: TransmuteExpr visitor
//
// =============================================================================

#include "05_codegen/lower/expr/transmute_expr.h"
#include "05_codegen/checks/checks.h"
#include "04_analysis/layout/layout.h"
#include "00_core/assert_spec.h"

namespace ultraviolet::codegen {

// =============================================================================
// LowerTransmuteExpr - Lower a transmute expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Transmute)
//   Transmute expressions reinterpret bits from one type to another.
//   They require:
//   - Source and target types to have the same size
//   - Must appear within unsafe block
//
// The actual size checking is delegated to LowerTransmute in checks.cpp,
// which handles the size validation and emits the IRTransmute node.
// =============================================================================

LowerResult LowerTransmuteExpr(const ast::Expr& expr,
                               const ast::TransmuteExpr& transmute,
                               LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Transmute");

    // Resolve the target type from the syntax
    analysis::TypeRef to_type;
    analysis::TypeRef from_type;

    if (ctx.sigma) {
        const analysis::ScopeContext& scope = ScopeForLowering(ctx);

        // Lower the target type from AST
        if (transmute.to) {
            if (auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, transmute.to)) {
                to_type = *lowered;
            }
        }

        // Lower the source type from AST (if explicitly provided)
        if (transmute.from) {
            if (auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, transmute.from)) {
                from_type = *lowered;
            }
        }
    }

    // Fallback: infer types from expression typing if not explicitly provided
    if (!to_type && ctx.expr_type) {
        to_type = ctx.expr_type(expr);
    }
    if (!from_type && ctx.expr_type) {
        from_type = ctx.expr_type(*transmute.value);
    }

    // Delegate to LowerTransmute which handles size checking and IR emission
    return LowerTransmute(from_type, to_type, *transmute.value, ctx);
}

}  // namespace ultraviolet::codegen
