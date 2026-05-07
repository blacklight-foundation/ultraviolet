#pragma once

// =============================================================================
// Block Expression Lowering
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.5 (Statement and Block Lowering)
//   - Lines 16228-16231: (Lower-Expr-Block)
//   - Lines 16731-16739: (Lower-Block-Tail) and (Lower-Block-Unit)
//
// This header declares the LowerBlock function for lowering block expressions.
// Block expressions consist of a sequence of statements followed by an optional
// tail expression whose value becomes the block's value.
//
// =============================================================================

#include "05_codegen/lower/lower_expr.h"
#include "02_source/ast/ast.h"

namespace cursive::codegen {

// =============================================================================
// LowerBlock - Lower a block expression
// =============================================================================
//
// A block consists of:
//   - A sequence of statements
//   - An optional tail expression whose value becomes the block's value
//
// If the tail expression is present, the block's value is the tail expression's
// value. Otherwise, the block produces unit.
//
// Lowering includes proper cleanup of variables declared within the block scope.
//
// Note: This function is also declared in lower_expr.h and lower_stmt.h for
// different contexts. All declarations refer to the same implementation in
// block_expr.cpp.

LowerResult LowerBlock(const ast::Block& block, LowerCtx& ctx);

}  // namespace cursive::codegen
