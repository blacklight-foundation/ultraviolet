// =================================================================
// File: 04_analysis/typing/expr/yield_expr.cpp
// Construct: Yield Expression Type Checking
// Spec Section: 17.3.3
// Spec Rules: T-Yield
// =================================================================
#include "00_core/assert_spec.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsYield() {
  SPEC_DEF("T-Yield", "17.3.3");
  SPEC_DEF("rule.21.T-Yield", "21.2.4");
  SPEC_DEF("rule.21.Yield-NotAsync-Err", "21.2.4");
  SPEC_DEF("rule.21.Yield-Out-Err", "21.2.4");
}

}  // namespace

// Section 17.3.3 Yield Expression Typing
//
// Typing rule (T-Yield):
// AsyncSig(R) = (Out, In, Result, E)
// Gamma |- expr : Out
// --------------------------------------------------
// Gamma |- yield expr : In
//
// yield forms:
// - yield expr: yield value and suspend
// - yield release expr: release keys, yield, suspend
//
// yield is the core async primitive. It:
// 1. Suspends execution
// 2. Yields a value of type Out to the caller
// 3. Resumes with a value of type In from the caller
//
// CRITICAL: yield MUST NOT occur while keys are held
// UNLESS: yield release form is used, which explicitly releases keys
// E-CON-0213: yield inside key block without release
//
ExprTypeResult TypeYieldExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::YieldExpr& expr,
                            const TypeEnv& env,
                            const ExprTypeFn& type_expr,
                            const IdentTypeFn& /*type_ident*/,
                            const PlaceTypeFn& /*type_place*/) {
  SpecDefsYield();
  ExprTypeResult result;

  // Verify we're in an async procedure by extracting async signature
  const auto async_sig = AsyncSigOf(ctx, type_ctx.return_type);
  if (!async_sig.has_value()) {
    SPEC_RULE("rule.21.Yield-NotAsync-Err");
    result.diag_id = "E-CON-0210";  // yield outside async context
    return result;
  }

  // Type the yielded value
  const auto value_result = type_expr(expr.value);
  if (!value_result.ok) {
    result.diag_id = value_result.diag_id;
    return result;
  }

  // Check yielded value type is subtype of async's Out type
  const auto sub = Subtyping(ctx, value_result.type, async_sig->out);
  if (!sub.ok) {
    result.diag_id = sub.diag_id;
    return result;
  }
  if (!sub.subtype) {
    SPEC_RULE("rule.21.Yield-Out-Err");
    result.diag_id = "E-CON-0211";  // yield type mismatch
    return result;
  }

  // Check key constraint
  // yield is ill-formed when keys are held, unless release modifier is present
  SPEC_RULE("requirement.21.SuspensionKeyRestrictionsReference");
  if (type_ctx.keys_held && !expr.release) {
    SPEC_RULE("requirement.21.AsyncKeyRestrictions");
    result.diag_id = "E-CON-0213";  // yield inside key block without release
    return result;
  }
  if (expr.release && type_ctx.env_ref) {
    MarkSharedDerivedBindingsStale(*type_ctx.env_ref);
  }

  // Result type is the In type from async signature (received on resume)
  SPEC_RULE("requirements.21.AsyncCapabilityRequirements");
  SPEC_RULE("T-Yield");
  SPEC_RULE("rule.21.T-Yield");
  result.ok = true;
  result.type = async_sig->in;
  return result;
}

}  // namespace ultraviolet::analysis::expr
