// =================================================================
// File: 04_analysis/typing/expr/all_expr.cpp
// Construct: All Expression Type Checking (concurrent async execution)
// Spec Section: 17.3.7
// Spec Rules: T-All
// =================================================================

#include "04_analysis/typing/expr/all_expr.h"

#include "00_core/assert_spec.h"
#include "04_analysis/composite/unions.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/typecheck.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsAllExpr() {
  SPEC_DEF("T-All", "17.3.7");
}

}  // namespace

// T-All: Wait for all async expressions to complete
// all { async1, async2, ... } : (T_1, T_2, ...) | E_1 | E_2 | ...
ExprTypeResult TypeAllExprImpl(const ScopeContext& ctx,
                               const StmtTypeContext& type_ctx,
                               const ast::AllExpr& expr,
                               const TypeEnv& env,
                               const TypeExprFn& type_expr) {
  SpecDefsAllExpr();
  SPEC_RULE("T-All");
  ExprTypeResult result;

  std::vector<TypeRef> result_types;
  std::vector<TypeRef> error_types;
  result_types.reserve(expr.exprs.size());
  error_types.reserve(expr.exprs.size());

  for (const auto& elem : expr.exprs) {
    const auto elem_result = type_expr(elem);
    if (!elem_result.ok) {
      result.diag_id = elem_result.diag_id;
      return result;
    }

    // Each expression must be an async with Future-like signature
    const auto async_sig = AsyncSigOf(ctx, elem_result.type);
    if (!async_sig.has_value() || !IsPrimType(async_sig->out, "()")) {
      result.diag_id = "E-CON-0270";
      return result;
    }
    if (!IsPrimType(async_sig->in, "()")) {
      result.diag_id = "E-CON-0271";
      return result;
    }

    result_types.push_back(async_sig->result);
    error_types.push_back(async_sig->err);
  }

  // Result is tuple of all success types | union of all error types
  const auto tuple_type = MakeTypeTuple(std::move(result_types));
  std::vector<TypeRef> members;
  members.reserve(1 + error_types.size());
  members.push_back(tuple_type);
  members.insert(members.end(), error_types.begin(), error_types.end());
  const auto union_type = MakeTypeUnion(std::move(members));
  result.ok = true;
  if (union_type && std::holds_alternative<TypeUnion>(union_type->node)) {
    const auto intro = TypeUnionIntro(ctx, tuple_type, union_type);
    if (!intro.ok) {
      result.diag_id = intro.diag_id;
      return result;
    }
    result.type = intro.type;
  } else {
    result.type = union_type;
  }
  return result;
}

}  // namespace ultraviolet::analysis::expr
