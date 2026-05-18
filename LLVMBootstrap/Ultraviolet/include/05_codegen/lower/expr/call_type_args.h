#pragma once

// =============================================================================
// Expression Lowering: Call with Type Arguments
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Generic call instantiation with type arguments
//   - Monomorphization of generic procedures
//
// This header declares functions for lowering calls with explicit type
// arguments (e.g., foo<i32, bool>(x, y)).
//
// =============================================================================

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// =============================================================================
// LowerCallWithTypeArgs - lower call expression with explicit type arguments
// =============================================================================
//
// When a call has explicit type arguments, the callee symbol must be
// monomorphized by incorporating the type arguments into the symbol name.
//
// Example:
//   foo<i32, bool>(x, y)
//   ->  IRCall with callee symbol "foo::inst::P::i32::P::bool"
//
// Parameters:
//   expr - The CallExpr AST node with non-empty generic_args
//   ctx  - The lowering context
//
// Returns:
//   LowerResult containing:
//   - ir: The IR to evaluate arguments and perform the call
//   - value: The result value of the call
//
LowerResult LowerCallWithTypeArgs(const ast::CallExpr& expr, LowerCtx& ctx);

// =============================================================================
// HasExplicitTypeArgs - check if a call has explicit type arguments
// =============================================================================
//
// Returns true if the call expression has non-empty generic_args.
// Use this to determine whether to dispatch to LowerCallWithTypeArgs
// or the standard LowerCallExpr.
//
bool HasExplicitTypeArgs(const ast::CallExpr& expr);

// =============================================================================
// Spec Rule Anchors
// =============================================================================

// Emits SPEC_RULE anchors for call with type args lowering rules.
void AnchorCallTypeArgsRules();

}  // namespace ultraviolet::codegen
