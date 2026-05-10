#pragma once

// =============================================================================
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16198-16201: (Lower-Expr-Range)
//     Gamma |- LowerRangeExpr(Range(kind, lo_opt, hi_opt)) => <IR, v>
//     ----------------------------------------------------------------
//     Gamma |- LowerExpr(Range(kind, lo_opt, hi_opt)) => <IR, v>
//
// Range expressions lower to a RangeVal containing the range kind and
// optional lo/hi bound values. The IR contains instructions to evaluate
// any bound expressions present.
// =============================================================================

#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/checks/checks.h"

namespace cursive::codegen {

// Lower a range expression to LowerResult.
// This is the entry point for use in the main LowerExpr dispatch.
//
// Parameters:
//   expr       - The full expression (for context)
//   range_expr - The range expression node
//   ctx        - Lowering context
//
// Returns:
//   LowerResult with IR for bound evaluation and an IRValue representing
//   the range. The range is registered as a DerivedValueInfo::Kind::RangeLit.
LowerResult LowerRange(const ast::Expr& expr,
                       const ast::RangeExpr& range_expr,
                       LowerCtx& ctx);

}  // namespace cursive::codegen
