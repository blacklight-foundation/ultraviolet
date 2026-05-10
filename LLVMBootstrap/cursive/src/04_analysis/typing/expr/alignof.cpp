// =================================================================
// File: 04_analysis/typing/expr/alignof.cpp
// Construct: Alignof Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Alignof
// =================================================================
//
// MIGRATED FROM: cursive-bootstrap/src/03_analysis/types/expr/alignof.cpp
//
// Type checks alignof expressions which compute the alignment of a type.
// The result type is always usize.
//
// =================================================================

#include "04_analysis/typing/expr/alignof.h"

#include "00_core/assert_spec.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_wf.h"
#include "04_analysis/typing/types.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsAlignof() {
  SPEC_DEF("T-Alignof", "5.2.12");
}

}  // namespace

ExprTypeResult TypeAlignofExprImpl(const ScopeContext& ctx,
                                   const ast::AlignofExpr& expr) {
  SpecDefsAlignof();
  SPEC_RULE("T-Alignof");
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

}  // namespace cursive::analysis::expr
