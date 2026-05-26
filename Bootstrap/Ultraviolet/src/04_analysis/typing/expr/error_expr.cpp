// =================================================================
// File: 04_analysis/typing/expr/error_expr.cpp
// Construct: Error Expression Type Checking
// Spec Section: (Compiler internal - not in spec)
// Spec Rules: T-ErrorExpr
// =================================================================
//
// ERROR EXPRESSION HANDLING:
//   Error expressions are produced by the parser for invalid syntax.
//   They propagate as type errors using the never type (!) which:
//   1. Is a subtype of all types (universal coercion)
//   2. Prevents cascading type errors
//   3. Signals the path has already reported an error
//
// IMPLEMENTATION STRATEGY:
//   - Return success with never type (!)
//   - The parser should have already reported the syntax error
//   - Using never type allows type checking to continue
//   - Any operation on never type produces never type
//
// =================================================================

#include "04_analysis/typing/expr/error_expr.h"

#include "00_core/assert_spec.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsErrorExpr() {
  SPEC_DEF("T-ErrorExpr", "internal");
}

}  // namespace

ExprTypeResult TypeErrorExprImpl(const ScopeContext& /*ctx*/,
                                 const ast::ErrorExpr& /*expr*/) {
  SpecDefsErrorExpr();
  ExprTypeResult result;

  // Error expressions use the never type (!) as a "poison" type.
  // This prevents cascading errors since ! is a subtype of all types.
  // The parser has already reported the actual syntax error.
  SPEC_RULE("T-ErrorExpr");
  result.ok = true;
  result.type = MakeTypePrim("!");
  return result;
}

}  // namespace ultraviolet::analysis::expr
