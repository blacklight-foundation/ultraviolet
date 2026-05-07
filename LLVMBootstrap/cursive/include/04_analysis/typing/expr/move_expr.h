// =================================================================
// File: 03_analysis/types/expr/move.h
// Construct: Move Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Move
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

// §5.2.12 Move Expression Typing
ExprTypeResult TypeMoveExprImpl(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::MoveExpr& expr,
                                const TypeEnv& env);

}  // namespace cursive::analysis::expr
