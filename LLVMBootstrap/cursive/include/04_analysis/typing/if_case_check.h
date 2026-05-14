#pragma once

#include "04_analysis/memory/calls.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

ExprTypeResult TypeIfIsExpr(const ScopeContext& ctx,
                           const StmtTypeContext& type_ctx,
                           const ast::IfIsExpr& expr,
                           const TypeEnv& env);

ExprTypeResult TypeIfCaseExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::IfCaseExpr& expr,
                             const TypeEnv& env);

CheckResult CheckIfIsExpr(const ScopeContext& ctx,
                          const StmtTypeContext& type_ctx,
                          const ast::IfIsExpr& expr,
                          const TypeEnv& env,
                          const TypeRef& expected);

CheckResult CheckIfCaseExpr(const ScopeContext& ctx,
                           const StmtTypeContext& type_ctx,
                           const ast::IfCaseExpr& expr,
                           const TypeEnv& env,
                           const TypeRef& expected);

}  // namespace cursive::analysis
