// =================================================================
// File: 03_analysis/types/expr/cast.h
// Construct: Cast Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Cast, CastValid
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

// §5.2.12 Cast Expression Typing
ExprTypeResult TypeCastExprImpl(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::CastExpr& expr,
                                const TypeEnv& env);

}  // namespace cursive::analysis::expr
