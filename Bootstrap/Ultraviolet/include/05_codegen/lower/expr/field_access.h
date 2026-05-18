#pragma once

// =============================================================================
// MIGRATION MAPPING: expr/field_access.h
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16104-16107: (Lower-Expr-FieldAccess)
//     Gamma |- LowerExpr(base) => <IR_b, v_b>    FieldValue(v_b, f) = v_f
//     Gamma |- LowerExpr(FieldAccess(base, f)) => <IR_b, v_f>
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_places.cpp
//   - Lines 379-391: LowerReadPlace for FieldAccessExpr
//   - Lines 647-692: LowerWritePlace for FieldAccessExpr
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/ir/ir_model.h (DerivedValueInfo::Kind::Field)
//   - ultraviolet/include/04_analysis/layout/layout.h (field offset calculation)
//
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// ============================================================================
// LowerReadPlaceFieldAccess - Lower field access expression for reading
// ============================================================================
//
// Implements spec rule: Lower-ReadPlace-Field (Lower-Expr-FieldAccess)
//
// This function lowers a field access expression (base.field) for reading.
// It recursively lowers the base expression and creates a derived value
// representing the field access. The actual field load is deferred to
// LLVM emission where the field offset can be computed.
//
// Parameters:
//   node - The FieldAccessExpr AST node containing base and field name
//   place - The full expression for type lookup purposes
//   ctx - The lowering context
//
// Returns:
//   LowerResult containing:
//     - ir: The IR to evaluate the base expression
//     - value: An IRValue representing the field value (as DerivedValueInfo)
//
// The resulting value is registered with DerivedValueInfo::Kind::Field,
// which stores the base value and field name for later materialization.
// ============================================================================
LowerResult LowerReadPlaceFieldAccess(const ast::FieldAccessExpr& node,
                                       const ast::Expr& place,
                                       LowerCtx& ctx);

// ============================================================================
// LowerWritePlaceFieldAccess - Lower field access expression for writing
// ============================================================================
//
// Implements spec rules: Lower-WritePlace-Field, LowerWriteSub-Field
//
// This function lowers a field access expression (base.field) for writing.
// It computes the address of the base, then the field offset, optionally
// drops the old value, and writes the new value.
//
// Parameters:
//   node - The FieldAccessExpr AST node containing base and field name
//   place - The full expression for type lookup purposes
//   value - The value to write to the field
//   allow_drop - If true, drop the old field value before writing
//   ctx - The lowering context
//
// Returns:
//   IRPtr containing the sequence of IR nodes to perform the write
//
// When allow_drop is true and the binding has responsibility:
//   1. The old field value is read
//   2. Drop is emitted for the old value
//   3. The new value is written
//   4. Binding state is updated (field removed from moved_fields)
//
// When allow_drop is false (subplace write):
//   - Only the write is performed, no drop
// ============================================================================
IRPtr LowerWritePlaceFieldAccess(const ast::FieldAccessExpr& node,
                                  const ast::Expr& place,
                                  const IRValue& value,
                                  bool allow_drop,
                                  LowerCtx& ctx);

// ============================================================================
// IsFieldAccessPlace - Check if expression is a valid field access place
// ============================================================================
//
// Determines whether an expression represents a field access that can be
// used as a place (assignable location).
//
// Parameters:
//   expr - The expression to check
//
// Returns:
//   true if the expression is a FieldAccessExpr (possibly wrapped in
//   AttributedExpr), false otherwise
// ============================================================================
bool IsFieldAccessPlace(const ast::ExprPtr& expr);

// ============================================================================
// BuildFieldAccessPlaceRepr - Build string representation of field access
// ============================================================================
//
// Constructs a human-readable string representation of a field access
// chain for diagnostic and debugging purposes.
//
// Parameters:
//   node - The FieldAccessExpr AST node
//   base_expr - The base expression for recursive traversal
//
// Returns:
//   A string like "x.field" or "x.y.z" representing the access chain
// ============================================================================
std::string BuildFieldAccessPlaceRepr(const ast::FieldAccessExpr& node,
                                       const ast::Expr& base_expr);

}  // namespace ultraviolet::codegen
