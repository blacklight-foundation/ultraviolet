// =================================================================
// File: 04_analysis/typing/expr/dispatch_expr.h
// Construct: Dispatch Expression Type Checking (data parallelism)
// Spec Section: 17.2.3
// Spec Rules: T-Dispatch, T-Dispatch-Reduce
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/expr/expr_common.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

// T-Dispatch: Data parallel iteration
// dispatch pat in range key? opts { body }
//
// TYPING (T-Dispatch):
//   Inside parallel block
//   range : Range
//   pat binds index variable
//   body : T
//   --------------------------------------------------
//   Gamma |- dispatch pat in range {body} : ()
//
// TYPING (T-Dispatch-Reduce):
//   Inside parallel block
//   range : Range
//   reduce operator: +, *, min, max, and, or
//   body : T
//   --------------------------------------------------
//   Gamma |- dispatch pat in range [reduce: op] {body} : T

ExprTypeResult TypeDispatchExprImpl(const ScopeContext& ctx,
                                    const StmtTypeContext& type_ctx,
                                    const ast::DispatchExpr& expr,
                                    const TypeEnv& env,
                                    const TypeExprFn& type_expr,
                                    const TypeIdentFn& type_ident,
                                    const PlaceTypeFn& type_place);

}  // namespace cursive::analysis::expr
