#pragma once

// =============================================================================
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 16.8.6 (Expression Lowering)
//   - (Lower-Internal-AllocExpr)
//     Gamma |- LowerExpr(value) => <IR_v, v>
//     Gamma |- LookupRegion(region_opt) => r
//     Gamma |- Alloc(r, v) => ptr
//     ------------------------------------------------------------------
//     Gamma |- LowerExpr(Alloc(region_opt, value)) => <SeqIR(IR_v, AllocIR(r, v)), ptr>
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// Lower an internal allocation expression to IR.
// Implements the (Lower-Internal-AllocExpr) rule from the spec.
//
// Allocation expressions allocate a value in a region and return the stored value.
//
// Parameters:
//   expr  - The full expression (for type lookup)
//   alloc - The allocation expression node
//   ctx   - Lowering context
//
// Returns:
//   LowerResult with IR for evaluating the value and allocating it,
//   and an IRValue representing the stored allocation result.
LowerResult LowerAllocExpr(const ast::Expr& expr,
                           const ast::AllocExpr& alloc,
                           LowerCtx& ctx);

}  // namespace ultraviolet::codegen
