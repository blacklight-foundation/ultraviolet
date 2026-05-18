// =================================================================
// File: 03_analysis/types/expr/propagate.h
// Construct: Propagate Expression Type Checking (?)
// Spec Section: 5.2.12
// Spec Rules: T-Propagate
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// §5.2.12 Propagate Expression Typing
ExprTypeResult TypePropagateExprImpl(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::PropagateExpr& expr,
                                     const TypeEnv& env);

}  // namespace ultraviolet::analysis::expr
