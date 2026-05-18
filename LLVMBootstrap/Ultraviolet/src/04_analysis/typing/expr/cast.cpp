// =================================================================
// File: 04_analysis/typing/expr/cast.cpp
// Construct: Cast Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Cast, CastValid
// =================================================================

#include "04_analysis/typing/expr/cast.h"

#include "00_core/assert_spec.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/typecheck.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsCast() {
  SPEC_DEF("T-Cast", "16.5.4");
  SPEC_DEF("CastValid", "16.5.4");
  SPEC_DEF("T-Cast-Invalid", "16.5.4");
  SPEC_DEF("T-Dynamic-Form", "14.6.4");
  SPEC_DEF("Dynamic-NonDispatchable", "5.3.1");
}

}  // namespace

ExprTypeResult TypeCastExprImpl(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::CastExpr& expr,
                                const TypeEnv& env) {
  SpecDefsCast();
  ExprTypeResult result;

  if (!expr.value || !expr.type) {
    return result;
  }

  // Type the source expression
  const auto value_result = TypeExpr(ctx, type_ctx, expr.value, env);
  if (!value_result.ok) {
    result.diag_id = value_result.diag_id;
    return result;
  }

  // Lower the target type
  const auto target = LowerType(ctx, expr.type);
  if (!target.ok) {
    result.diag_id = target.diag_id;
    return result;
  }

  // Dynamic class casts have their own diagnostic obligation.
  if (const auto* target_dynamic = std::get_if<TypeDynamic>(&target.type->node)) {
    const auto source_base = StripPerm(value_result.type);
    if (TypeImplementsClass(ctx, source_base, target_dynamic->path)) {
      if (const auto diag_id =
              ClassDispatchabilityDiagnostic(ctx, target_dynamic->path)) {
        SPEC_RULE("Dynamic-NonDispatchable");
        result.diag_id = *diag_id;
        return result;
      }
      if (IsPlaceExpr(expr.value)) {
        SPEC_RULE("T-Dynamic-Form");
        result.ok = true;
        result.type = target.type;
        return result;
      }
    }
  }

  // Check if cast is valid
  if (!CastValid(value_result.type, target.type)) {
    SPEC_RULE("T-Cast-Invalid");
    result.diag_id = "T-Cast-Invalid";
    return result;
  }

  SPEC_RULE("T-Cast");
  result.ok = true;
  result.type = target.type;
  return result;
}

}  // namespace ultraviolet::analysis::expr
