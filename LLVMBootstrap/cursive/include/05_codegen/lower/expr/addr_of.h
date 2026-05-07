#pragma once

// =============================================================================
// MIGRATION MAPPING: expr/addr_of.h
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16243-16246: (Lower-Expr-AddressOf)
//   - Lines 16428-16477: LowerAddrOf rules for all place forms
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/lower/lower_expr_places.cpp
//   - Lines 888-1153: LowerAddrOf
//   - Lines 1159-1185: LowerMovePlace
//
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

// ============================================================================
// LowerAddrOf - Lower address-of expression
// ============================================================================
//
// Implements spec rules:
//   - Lower-AddrOf-Ident-Local: Address of local binding
//   - Lower-AddrOf-Ident-Path: Address of static/module-level binding
//   - Lower-AddrOf-Field: Address of record field
//   - Lower-AddrOf-Tuple: Address of tuple element
//   - Lower-AddrOf-Index: Address of array/slice element
//   - Lower-AddrOf-Index-OOB: Out-of-bounds index handling
//   - Lower-AddrOf-Deref: Address from dereferenced pointer
//   - Lower-AddrOf-Deref-Null: Null pointer dereference handling
//   - Lower-AddrOf-Deref-Expired: Expired pointer dereference handling
//   - Lower-AddrOf-Deref-Raw: Raw pointer dereference handling
//
// Parameters:
//   place - The place expression to take the address of
//   ctx - The lowering context
//
// Returns:
//   LowerResult containing the IR to compute the address and the resulting
//   pointer value
//
// Note: The resulting pointer type is Ptr<T>@Valid where T is the type
// of the place expression.
// ============================================================================

// Declaration in lower_expr.h:
// LowerResult LowerAddrOf(const ast::Expr& place, LowerCtx& ctx);

// ============================================================================
// LowerMovePlace - Move value out of a place
// ============================================================================
//
// Implements spec rule: Lower-MovePlace
//
// Reads the value from the place and marks the binding as moved in the
// lowering context. For partial moves (field access), only the field
// is marked as moved.
//
// Parameters:
//   place - The place expression to move from
//   ctx - The lowering context
//
// Returns:
//   LowerResult containing the IR to read the value and update move state
//
// Note: After this operation, the source binding (or field) is in the
// Moved state and cannot be used again.
// ============================================================================

// Declaration in lower_expr.h:
// LowerResult LowerMovePlace(const ast::Expr& place, LowerCtx& ctx);

}  // namespace cursive::codegen
