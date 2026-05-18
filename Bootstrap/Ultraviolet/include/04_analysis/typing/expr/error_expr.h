// =================================================================
// File: 04_analysis/typing/expr/error_expr.h
// Construct: Error Expression Type Checking
// Spec Section: (Compiler internal - not in spec)
// Spec Rules: T-ErrorExpr
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/expr/expr_common.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// T-ErrorExpr: Error recovery for parse errors
//
// Error expressions are produced by the parser for invalid syntax.
// They propagate through the type system using the never type (!)
// as a "poison" type that doesn't cause additional cascading errors.
//
// TYPING (T-ErrorExpr):
//   ErrorExpr produced by parser
//   --------------------------------------------------
//   Gamma |- error_expr : !
//
// The never type (!) is used because:
//   1. It's a subtype of all types (coerces to anything)
//   2. It prevents cascading type errors
//   3. It signals that this path has already reported an error

ExprTypeResult TypeErrorExprImpl(const ScopeContext& ctx,
                                 const ast::ErrorExpr& expr);

}  // namespace ultraviolet::analysis::expr
