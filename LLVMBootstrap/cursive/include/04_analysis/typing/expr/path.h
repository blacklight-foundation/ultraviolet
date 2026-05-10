// =================================================================
// File: 03_analysis/types/expr/path.h
// Construct: Path and Identifier Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Ident, P-Ident, T-Path-Value, ValueUse-NonBitcopyPlace
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

// (T-Ident) Identifier Expression Value Typing
ExprTypeResult TypeIdentifierExprImpl(const ScopeContext& ctx,
                                      const ast::IdentifierExpr& expr,
                                      const TypeEnv& env);

// (P-Ident) Identifier Expression Place Typing
PlaceTypeResult TypeIdentifierPlaceImpl(const ScopeContext& ctx,
                                        const ast::IdentifierExpr& expr,
                                        const TypeEnv& env);

// (T-Path-Value) Path Expression Typing
ExprTypeResult TypePathExprImpl(const ScopeContext& ctx,
                                const ast::PathExpr& expr,
                                const TypeEnv& env);

}  // namespace cursive::analysis::expr
