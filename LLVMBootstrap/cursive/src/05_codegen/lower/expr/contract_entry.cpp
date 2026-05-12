// =============================================================================
// Expression Lowering: EntryExpr
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 5.4 (Contracts)
//   - @entry(expr) intrinsic captures entry/old value in postconditions
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//
// =============================================================================

#include "05_codegen/lower/expr/contract_entry.h"
#include "00_core/assert_spec.h"

namespace cursive::codegen {

// =============================================================================
// LowerEntryExpr - Lower an @entry(expr) expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Entry)
//   @entry(expr) in a postcondition refers to the value of expr at procedure
//   entry. During contract lowering, the expression is evaluated at entry
//   and stored, then this node references that stored value.
//
// In postcondition evaluation:
//   1. At procedure entry, @entry expressions are evaluated and saved
//   2. At procedure exit, @entry(expr) refers to the saved value
//
// Requirements:
//   - The result type of @entry(expr) must satisfy Bitcopy
// =============================================================================

LowerResult LowerEntryExpr(const ast::EntryExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Entry");
    if (const auto it = ctx.contract_entry_values.find(&expr);
        it != ctx.contract_entry_values.end()) {
        return LowerResult{EmptyIR(), it->second};
    }

    // Fallback for malformed/missing capture setup.
    if (expr.expr) {
        return LowerExpr(*expr.expr, ctx);
    }
    ctx.ReportCodegenFailure();
    return LowerResult{EmptyIR(), ctx.FreshTempValue("entry_val_missing")};
}

}  // namespace cursive::codegen
