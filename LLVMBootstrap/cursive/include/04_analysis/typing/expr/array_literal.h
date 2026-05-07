// =================================================================
// File: 04_analysis/typing/expr/array_literal.h
// Construct: Array Literal Expression Type Checking
// Spec Section: 5.2.6
// Spec Rules: T-Array-Literal-Segments
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/expr/expr_common.h"
#include "04_analysis/typing/type_infer.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

// T-Array-Literal-Segments: Array from explicit and repeated segments
// [e_1, e_2, ..., e_n] : [T; n] where all e_i : T
ExprTypeResult TypeArrayExprImpl(const ScopeContext& ctx,
                                 const ast::ArrayExpr& expr,
                                 const TypeExprFn& type_expr);

}  // namespace cursive::analysis::expr
