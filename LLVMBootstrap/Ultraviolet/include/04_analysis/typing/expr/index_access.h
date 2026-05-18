// =============================================================================
// File: 04_analysis/typing/expr/index_access.h
// Index Access Expression Typing Declarations
// =============================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

ExprTypeResult TypeIndexAccessExpr(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::IndexAccessExpr& expr,
                                   const TypeEnv& env,
                                   const ExprTypeFn& type_expr,
                                   const PlaceTypeFn& type_place,
                                   const IdentTypeFn& type_ident);

PlaceTypeResult TypeIndexAccessPlace(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::IndexAccessExpr& expr,
                                     const TypeEnv& env,
                                     const ExprTypeFn& type_expr,
                                     const PlaceTypeFn& type_place,
                                     const IdentTypeFn& type_ident);

}  // namespace ultraviolet::analysis::expr
