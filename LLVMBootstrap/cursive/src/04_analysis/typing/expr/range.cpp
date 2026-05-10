// =================================================================
// File: 04_analysis/typing/expr/range.cpp
// Construct: Range Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: Range-Full, Range-To, Range-ToInclusive, Range-From,
//             Range-Exclusive, Range-Inclusive
// =================================================================
#include "04_analysis/typing/expr/range.h"

#include "00_core/assert_spec.h"
#include "04_analysis/typing/expr/expr_common.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsRange() {
  SPEC_DEF("T-Range-Lift", "5.2.12");
  SPEC_DEF("Range-Full", "5.2.12");
  SPEC_DEF("Range-To", "5.2.12");
  SPEC_DEF("Range-ToInclusive", "5.2.12");
  SPEC_DEF("Range-From", "5.2.12");
  SPEC_DEF("Range-Exclusive", "5.2.12");
  SPEC_DEF("Range-Inclusive", "5.2.12");
}

}  // namespace

// §5.2.12 Range Expression Typing
//
// Range forms:
// - ..: full range (all elements)
// - ..n: range to (exclusive)
// - ..=n: range to (inclusive)
// - n..: range from
// - n..m: exclusive range
// - n..=m: inclusive range
//
ExprTypeResult TypeRangeExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::RangeExpr& expr,
                                 const TypeEnv& env) {
  ExprTypeResult result;
  TypeRef lhs_type;
  TypeRef rhs_type;

  if (expr.lhs) {
    const auto lhs = TypeExpr(ctx, type_ctx, expr.lhs, env);
    if (!lhs.ok) {
      result.diag_id = lhs.diag_id;
      return result;
    }
    lhs_type = lhs.type;
  }

  if (expr.rhs) {
    const auto rhs = TypeExpr(ctx, type_ctx, expr.rhs, env);
    if (!rhs.ok) {
      result.diag_id = rhs.diag_id;
      return result;
    }
    rhs_type = rhs.type;
  }

  // For two-sided ranges, bounds must have equivalent types.
  if (lhs_type && rhs_type) {
    const auto eq = TypeEquiv(lhs_type, rhs_type);
    if (!eq.ok) {
      result.diag_id = eq.diag_id;
      return result;
    }
    if (!eq.equiv) {
      result.diag_id = "E-SEM-3133";
      return result;
    }
  }

  // Determine range kind and apply appropriate spec rule
  switch (expr.kind) {
    case ast::RangeKind::Full:
      SPEC_RULE("Range-Full");
      result.type = MakeTypeRangeFull();
      break;
    case ast::RangeKind::To:
      SPEC_RULE("Range-To");
      result.type = rhs_type ? MakeTypeRangeTo(rhs_type) : nullptr;
      break;
    case ast::RangeKind::ToInclusive:
      SPEC_RULE("Range-ToInclusive");
      result.type = rhs_type ? MakeTypeRangeToInclusive(rhs_type) : nullptr;
      break;
    case ast::RangeKind::From:
      SPEC_RULE("Range-From");
      result.type = lhs_type ? MakeTypeRangeFrom(lhs_type) : nullptr;
      break;
    case ast::RangeKind::Exclusive:
      SPEC_RULE("Range-Exclusive");
      result.type = lhs_type ? MakeTypeRange(lhs_type) : nullptr;
      break;
    case ast::RangeKind::Inclusive:
      SPEC_RULE("Range-Inclusive");
      result.type = lhs_type ? MakeTypeRangeInclusive(lhs_type) : nullptr;
      break;
  }

  if (!result.type) {
    return result;
  }
  SPEC_RULE("T-Range-Lift");
  result.ok = true;
  return result;
}

}  // namespace cursive::analysis::expr
