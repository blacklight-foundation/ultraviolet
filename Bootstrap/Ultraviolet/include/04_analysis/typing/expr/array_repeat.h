// =================================================================
// File: 03_analysis/types/expr/array_repeat.h
// Construct: Array Repeat Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Array-Literal-Segments
// =================================================================
#pragma once

#include <functional>

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// Type function for recursive expression typing
using TypeExprFn = std::function<ExprTypeResult(const ast::ExprPtr&)>;

// Type check an array repeat expression [value; count]
ExprTypeResult TypeArrayRepeatExprImpl(const ScopeContext& ctx,
                                       const ast::ArrayRepeatExpr& expr,
                                       TypeExprFn type_expr);

}  // namespace ultraviolet::analysis::expr
