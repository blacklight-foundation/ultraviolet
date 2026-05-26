// =================================================================
// File: 03_analysis/types/expr/tuple_access.h
// Construct: Tuple Access Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Tuple-Access, P-Tuple-Access
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// §5.2.12 Tuple Access Expression Typing
ExprTypeResult TypeTupleAccessExprImpl(const ScopeContext& ctx,
                                       const StmtTypeContext& type_ctx,
                                       const ast::TupleAccessExpr& expr,
                                       const TypeEnv& env);

PlaceTypeResult TypeTupleAccessPlaceImpl(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::TupleAccessExpr& expr,
                                         const TypeEnv& env);

}  // namespace ultraviolet::analysis::expr
