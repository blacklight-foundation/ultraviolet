// =================================================================
// File: 03_analysis/types/expr/field_access.h
// Construct: Field Access Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Field-Record, T-Field-Record-Perm, P-Field-Record,
//             P-Field-Record-Perm, FieldAccess-Unknown, FieldAccess-NotVisible
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

// §5.2.12 Field Access Expression Typing
ExprTypeResult TypeFieldAccessExprImpl(const ScopeContext& ctx,
                                       const StmtTypeContext& type_ctx,
                                       const ast::FieldAccessExpr& expr,
                                       const TypeEnv& env);

// §5.2.12 Field Access Place Typing
PlaceTypeResult TypeFieldAccessPlaceImpl(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::FieldAccessExpr& expr,
                                         const TypeEnv& env);

}  // namespace cursive::analysis::expr
