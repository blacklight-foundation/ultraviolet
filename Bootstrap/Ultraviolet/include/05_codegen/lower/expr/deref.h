#pragma once

// =============================================================================
// MIGRATION MAPPING: expr/deref.h
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16248-16251: (Lower-Expr-Deref)
//     Gamma |- LowerExpr(e) => <IR_e, v_ptr>    Gamma |- LowerRawDeref(v_ptr) => <IR_d, v>
//     ------------------------------------------------------------------------------------
//     Gamma |- LowerExpr(Deref(e)) => <SeqIR(IR_e, IR_d), v>
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_places.cpp
//   - Lines 467-486: LowerReadPlace for DerefExpr
//   - LowerRawDeref helper for raw pointer dereference (in checks.cpp)
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRReadPtr, IRWritePtr, IRValue)
//   - ultraviolet/include/05_codegen/checks/checks.h (LowerRawDeref)
//
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// ============================================================================
// LowerReadPlaceDeref - Lower dereference expression for reading
// ============================================================================
//
// Implements spec rule: Lower-Expr-Deref (Lower-ReadPlace-Deref)
//
// This function lowers a dereference expression (*ptr) for reading.
// It first lowers the pointer expression to get the pointer value,
// then uses LowerRawDeref to generate the appropriate read IR based
// on the pointer type (safe Ptr<T> vs raw *T).
//
// Parameters:
//   node - The DerefExpr AST node containing the pointer expression
//   place - The full expression for type lookup purposes
//   ctx - The lowering context
//
// Returns:
//   LowerResult containing:
//     - ir: SeqIR(IR_ptr, IR_deref) where IR_ptr evaluates the pointer
//           and IR_deref reads through it
//     - value: An IRValue representing the dereferenced value
//
// For safe pointers (Ptr<T>@Valid, Ptr<T>@Null, Ptr<T>@Expired):
//   - @Valid: Emit direct read (Lower-RawDeref-Safe)
//   - @Null: Emit panic for null dereference (Lower-RawDeref-Null)
//   - @Expired: Emit panic for expired pointer (Lower-RawDeref-Expired)
//
// For raw pointers (*imm T, *mut T):
//   - Emit unchecked read (Lower-RawDeref-Raw)
//   - Note: This must be in an unsafe block per language rules
//
// If no type information is available, falls back to a simple IRReadPtr.
// ============================================================================
LowerResult LowerReadPlaceDeref(const ast::DerefExpr& node,
                                 const ast::Expr& place,
                                 LowerCtx& ctx);

// ============================================================================
// LowerWritePlaceDeref - Lower dereference expression for writing
// ============================================================================
//
// Implements spec rules: Lower-WritePlace-Deref, LowerWriteSub-Deref
//
// This function lowers a dereference expression (*ptr) for writing.
// It evaluates the pointer expression to get the address, then writes
// the new value through the pointer.
//
// Parameters:
//   node - The DerefExpr AST node containing the pointer expression
//   place - The full expression (available for type lookups, currently unused)
//   value - The value to write through the pointer
//   allow_drop - Controls SPEC_RULE emitted (unused for actual behavior)
//   ctx - The lowering context
//
// Returns:
//   IRPtr containing: SeqIR(IR_ptr, IRWritePtr{ptr, value})
//
// Note: Unlike field/tuple writes, deref writes do NOT perform drop-on-assign.
// This is because:
// - The pointer's target ownership is typically managed elsewhere
// - For raw pointers (*mut T), the user is responsible for drop
// - For safe pointers (Ptr<T>), the region manages the memory
//
// The allow_drop parameter only affects which SPEC_RULE is recorded:
// - true: "Lower-WritePlace-Deref"
// - false: "LowerWriteSub-Deref"
// ============================================================================
IRPtr LowerWritePlaceDeref(const ast::DerefExpr& node,
                            const ast::Expr& place,
                            const IRValue& value,
                            bool allow_drop,
                            LowerCtx& ctx);

// ============================================================================
// IsDerefPlace - Check if expression is a valid dereference place
// ============================================================================
//
// Determines whether an expression represents a dereference that can be
// used as a place (assignable location).
//
// Parameters:
//   expr - The expression to check
//
// Returns:
//   true if the expression is a DerefExpr (possibly wrapped in
//   AttributedExpr), false otherwise
// ============================================================================
bool IsDerefPlace(const ast::ExprPtr& expr);

}  // namespace ultraviolet::codegen
