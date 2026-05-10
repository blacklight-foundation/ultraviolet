#pragma once

// =============================================================================
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16146-16149: (Lower-Expr-Alloc)
//     Gamma |- LowerExpr(value) => <IR_v, v>
//     Gamma |- LookupRegion(region_opt) => r
//     Gamma |- Alloc(r, v) => ptr
//     ------------------------------------------------------------------
//     Gamma |- LowerExpr(Alloc(region_opt, value)) => <SeqIR(IR_v, AllocIR(r, v)), ptr>
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

// Lower an allocation expression to IR.
// Implements the (Lower-Expr-Alloc) rule from the spec.
//
// Allocation expressions (^value or region^value) allocate a value in a region.
// The result is a pointer (Ptr<T>@Valid) to the allocated value.
//
// Parameters:
//   expr  - The full expression (for type lookup)
//   alloc - The allocation expression node
//   ctx   - Lowering context
//
// Returns:
//   LowerResult with IR for evaluating the value and allocating it,
//   and an IRValue representing the pointer to the allocated memory.
LowerResult LowerAllocExpr(const ast::Expr& expr,
                           const ast::AllocExpr& alloc,
                           LowerCtx& ctx);

}  // namespace cursive::codegen
