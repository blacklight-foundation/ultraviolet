// =================================================================
// File: 04_analysis/typing/expr/array_repeat.cpp
// Construct: Array Repeat Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Array-Literal-Segments
// =================================================================
//
// MIGRATED FROM: cursive-bootstrap/src/03_analysis/types/expr/array_repeat.cpp
//
// Type checks array repeat expressions [value; count].
// The value must be Bitcopy, and count must evaluate to a constant.
//
// =================================================================

#include "04_analysis/typing/expr/array_repeat.h"

#include "00_core/assert_spec.h"
#include "04_analysis/typing/expr/expr_common.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/types.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsArrayRepeat() {
  SPEC_DEF("T-Array-Literal-Segments", "5.2.6");
}

}  // namespace

ExprTypeResult TypeArrayRepeatExprImpl(const ScopeContext& ctx,
                                       const ast::ArrayRepeatExpr& expr,
                                       TypeExprFn type_expr) {
  SpecDefsArrayRepeat();
  SPEC_RULE("T-Array-Literal-Segments");
  ExprTypeResult r;
  if (!expr.value || !expr.count) {
    return r;
  }
  // Type check the value expression
  const auto value_type = type_expr(expr.value);
  if (!value_type.ok) {
    r.diag_id = value_type.diag_id;
    return r;
  }
  // Type check the count expression
  const auto count_type = type_expr(expr.count);
  if (!count_type.ok) {
    r.diag_id = count_type.diag_id;
    return r;
  }
  // Count must be usize or compatible integer type
  const auto count_prim = GetPrimName(count_type.type);
  if (!count_prim.has_value() ||
      (!IsIntType(*count_prim) && *count_prim != "usize")) {
    r.diag_id = "E-TYP-1812";
    return r;
  }
  // Element type must be Bitcopy for repeat initialization
  if (!BitcopyType(ctx, value_type.type)) {
    r.diag_id = "E-UNS-0107";
    return r;
  }
  // Evaluate count as compile-time constant
  const auto len = ConstLen(ctx, expr.count);
  if (!len.ok || !len.value.has_value()) {
    r.diag_id = len.diag_id.value_or("E-TYP-1812");
    return r;
  }
  r.ok = true;
  r.type = MakeTypeArray(value_type.type, *len.value);
  return r;
}

}  // namespace cursive::analysis::expr

