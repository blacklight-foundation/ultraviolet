// =================================================================
// File: 04_analysis/typing/expr/sizeof.cpp
// Construct: Sizeof Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Sizeof
// =================================================================
//
// MIGRATED FROM: ultraviolet-bootstrap/src/03_analysis/types/expr/sizeof.cpp
//
// Type checks sizeof expressions which compute the size of a type.
// The result type is always usize.
//
// =================================================================

#include "04_analysis/typing/expr/sizeof.h"

#include "00_core/assert_spec.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_wf.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsSizeof() {
  SPEC_DEF("T-Sizeof", "5.2.12");
}

}  // namespace

ExprTypeResult TypeSizeofExprImpl(const ScopeContext& ctx,
                                  const ast::SizeofExpr& expr) {
  SpecDefsSizeof();
  SPEC_RULE("T-Sizeof");
  ExprTypeResult r;
  if (!expr.type) {
    return r;
  }
  const auto lowered = LowerType(ctx, expr.type);
  if (!lowered.ok) {
    r.diag_id = lowered.diag_id;
    return r;
  }
  const auto wf = TypeWF(ctx, lowered.type);
  if (!wf.ok) {
    r.diag_id = wf.diag_id;
    return r;
  }
  r.ok = true;
  r.type = MakeTypePrim("usize");
  return r;
}

}  // namespace ultraviolet::analysis::expr
