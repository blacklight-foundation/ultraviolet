// =================================================================
// File: 04_analysis/typing/expr/tuple_literal.cpp
// Construct: Tuple Literal Expression Type Checking
// Spec Section: 5.2.5
// Spec Rules: T-Unit-Literal, T-Tuple-Literal, Syn-Unit, Syn-Tuple
// =================================================================
#include "00_core/assert_spec.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_expr.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsTupleLiteral() {
  SPEC_DEF("T-Unit-Literal", "5.2.5");
  SPEC_DEF("T-Tuple-Literal", "5.2.5");
  SPEC_DEF("Syn-Unit", "5.2.5");
  SPEC_DEF("Syn-Tuple", "5.2.5");
}

}  // namespace

// Section 5.2.5 Tuple Literal Expression Typing
//
// Typing rule (T-Unit-Literal):
// --------------------------------------------------
// Gamma |- () : TypePrim("()")
//
// Typing rule (T-Tuple-Literal):
// n >= 1
// Gamma |- e_i : T_i for all i in 1..n
// --------------------------------------------------
// Gamma |- (e_1, ..., e_n) : TypeTuple([T_1, ..., T_n])
//
// Empty tuple () has type unit (primitive type "()")
// Non-empty tuple (e1, e2, ...) has type TypeTuple([T1, T2, ...])
// Single-element tuple requires (e;) syntax to distinguish from parenthesized expr
//
ExprTypeResult TypeTupleExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::TupleExpr& expr,
                                 const TypeEnv& env) {
  ExprTypeResult result;

  // Handle empty tuple - unit literal
  if (expr.elements.empty()) {
    SPEC_RULE("T-Unit-Literal");
    SPEC_RULE("Syn-Unit");
    result.ok = true;
    result.type = MakeTypePrim("()");
    return result;
  }

  // Type check each element
  std::vector<TypeRef> element_types;
  element_types.reserve(expr.elements.size());

  for (const auto& elem : expr.elements) {
    const auto elem_result = TypeExpr(ctx, type_ctx, elem, env);
    if (!elem_result.ok) {
      result.diag_id = elem_result.diag_id;
      return result;
    }
    element_types.push_back(elem_result.type);
  }

  SPEC_RULE("T-Tuple-Literal");
  SPEC_RULE("Syn-Tuple");
  result.ok = true;
  result.type = MakeTypeTuple(std::move(element_types));
  return result;
}

}  // namespace cursive::analysis::expr
