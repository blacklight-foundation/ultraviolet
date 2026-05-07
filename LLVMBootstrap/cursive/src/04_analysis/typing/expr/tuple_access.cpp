// =================================================================
// File: 04_analysis/typing/expr/tuple_access.cpp
// Construct: Tuple Access Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: P-Tuple-Index, P-Tuple-Index-Perm
// =================================================================
#include "04_analysis/typing/expr/tuple_access.h"

#include "00_core/assert_spec.h"
#include "04_analysis/composite/tuples.h"
#include "04_analysis/typing/type_expr.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsTupleAccess() {
  SPEC_DEF("P-Tuple-Index", "5.2.12");
  SPEC_DEF("P-Tuple-Index-Perm", "5.2.12");
}

}  // namespace

// Section 5.2.12 Tuple Access Expression Typing
//
// Typing rule (P-Tuple-Index):
// Gamma |- tuple : TypeTuple([T_0, T_1, ..., T_n])
// 0 <= index < n
// --------------------------------------------------
// Gamma |- tuple.index : T_index
//
// Typing rule (P-Tuple-Index-Perm):
// Gamma |- tuple : TypePerm(p, TypeTuple([T_0, ..., T_n]))
// 0 <= index < n
// --------------------------------------------------
// Gamma |- tuple.index : TypePerm(p, T_index)
//
// Tuple access uses numeric indices (.0, .1, .2, etc.)
// The index must be a valid compile-time constant within bounds.
// Permissions are propagated from the tuple to the element.
//
ExprTypeResult TypeTupleAccessExprImpl(const ScopeContext& ctx,
                                       const StmtTypeContext& type_ctx,
                                       const ast::TupleAccessExpr& expr,
                                       const TypeEnv& env) {
  auto type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, SuppressSharedAccessCheck(type_ctx), inner, env);
  };
  return TypeTupleAccessValue(ctx, expr, type_expr);
}

// Place typing for tuple access (allows assignment to tuple elements)
PlaceTypeResult TypeTupleAccessPlaceImpl(const ScopeContext& ctx,
                                         const StmtTypeContext& type_ctx,
                                         const ast::TupleAccessExpr& expr,
                                         const TypeEnv& env) {
  PlaceTypeFn type_place = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, SuppressSharedAccessCheck(type_ctx), inner, env);
  };
  return TypeTupleAccessPlace(ctx, expr, type_place);
}

}  // namespace cursive::analysis::expr
