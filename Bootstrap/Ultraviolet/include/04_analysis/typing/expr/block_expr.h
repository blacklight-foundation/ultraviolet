// =================================================================
// File: 04_analysis/typing/expr/block_expr.h
// Construct: Block Expression Type Checking
// Spec Section: 5.2.11
// Spec Rules: T-Block, StmtSeq-Empty, StmtSeq-Cons
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/expr/expr_common.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// T-Block: Block expression typing
// { stmt_1; stmt_2; ... ; tail_expr } : T where tail_expr : T (or unit if no tail)
ExprTypeResult TypeBlockExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::BlockExpr& expr,
                                 const TypeEnv& env,
                                 const TypeExprFn& type_expr,
                                 const TypeIdentFn& type_ident,
                                 const PlaceTypeFn& type_place);

// Check block expression against expected type
CheckResult CheckBlockExprImpl(const ScopeContext& ctx,
                               const StmtTypeContext& type_ctx,
                               const ast::BlockExpr& expr,
                               const TypeEnv& env,
                               const TypeRef& expected,
                               const TypeExprFn& type_expr,
                               const TypeIdentFn& type_ident,
                               const PlaceTypeFn& type_place);

}  // namespace ultraviolet::analysis::expr
