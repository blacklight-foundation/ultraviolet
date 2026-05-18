#pragma once

// =============================================================================
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16240-16242: (Lower-Expr-Move)
//     Gamma |- LowerMovePlace(place) => <IR, v>
//     ------------------------------------------------------------------
//     Gamma |- LowerExpr(Move(place)) => <IR, v>
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_places.cpp
//   - Lines 1159-1185: LowerMovePlace
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// Lower a move expression to IR.
// Implements the (Lower-Expr-Move) rule from the spec.
//
// This function reads the value from the place and marks the binding as moved
// in the lowering context. For partial moves (field access), only the field
// is marked as moved.
//
// Parameters:
//   place - The place expression to move from
//   ctx   - Lowering context
//
// Returns:
//   LowerResult with the IR to read the value and update move state.
//
// Note: After this operation, the source binding (or field) is in the
// Moved state and cannot be used again.
LowerResult LowerMovePlace(const ast::Expr& place, LowerCtx& ctx);
LowerResult LowerMovePlaceAsRef(const ast::Expr& place, LowerCtx& ctx);
LowerResult LowerCopyExpr(const ast::CopyExpr& expr, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
