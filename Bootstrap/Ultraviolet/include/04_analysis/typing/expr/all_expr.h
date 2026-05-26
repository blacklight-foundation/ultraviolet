// =================================================================
// File: 04_analysis/typing/expr/all_expr.h
// Construct: All Expression Type Checking (concurrent async execution)
// Spec Section: 17.3.7
// Spec Rules: T-All
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/expr/expr_common.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// T-All: Wait for all async expressions to complete
// Result is tuple of success types | union of error types
ExprTypeResult TypeAllExprImpl(const ScopeContext& ctx,
                               const StmtTypeContext& type_ctx,
                               const ast::AllExpr& expr,
                               const TypeEnv& env,
                               const TypeExprFn& type_expr);

}  // namespace ultraviolet::analysis::expr
