// =================================================================
// File: 03_analysis/types/expr/if.h
// Construct: If Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-If, T-If-No-Else, Chk-If, Chk-If-No-Else
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// §5.2.12 If Expression Typing
ExprTypeResult TypeIfExprImpl(const ScopeContext& ctx,
                              const StmtTypeContext& type_ctx,
                              const ast::IfExpr& expr,
                              const TypeEnv& env);

// §5.2.12 If Expression Checking (against expected type)
CheckResult CheckIfExprImpl(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::IfExpr& expr,
                            const TypeRef& expected,
                            const TypeEnv& env);

}  // namespace ultraviolet::analysis::expr
