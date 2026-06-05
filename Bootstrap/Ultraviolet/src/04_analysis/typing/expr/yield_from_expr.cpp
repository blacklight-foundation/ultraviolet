// =================================================================
// File: 04_analysis/typing/expr/yield_from_expr.cpp
// Construct: Yield From Expression Type Checking
// Spec Section: 17.3.3
// Spec Rules: T-Yield-From
// =================================================================
#include "00_core/assert_spec.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsYieldFrom() {
  SPEC_DEF("T-Yield-From", "17.3.3");
  SPEC_DEF("rule.21.T-Yield-From", "21.2.4");
  SPEC_DEF("rule.21.YieldFrom-NotAsync-Err", "21.2.4");
  SPEC_DEF("rule.21.YieldFrom-Out-Err", "21.2.4");
  SPEC_DEF("rule.21.YieldFrom-In-Err", "21.2.4");
  SPEC_DEF("rule.21.YieldFrom-ErrType-Err", "21.2.4");
}

}  // namespace

// Section 17.3.3 Yield From Expression Typing
//
// Typing rule (T-Yield-From):
// AsyncSig(R) = (Out_r, In_r, Result_r, E_r)
// Gamma |- expr : Async<Out_i, In_i, Result_i, E_i>
// Out_i <: Out_r       (inner yields subtype of outer yields)
// In_r <: In_i         (outer inputs subtype of inner inputs - contravariant)
// E_i <: E_r           (inner errors subtype of outer errors)
// --------------------------------------------------
// Gamma |- yield from expr : Result_i
//
// yield from forms:
// - yield from expr: delegate to inner async
// - yield release from expr: release keys, then delegate
//
// yield from enables async composition/delegation:
// - All values from inner async are yielded to outer
// - All inputs from outer async are forwarded to inner
// - Errors from inner propagate to outer
// - Result of inner is returned when inner completes
//
// CRITICAL: yield from MUST NOT occur while keys are held
// UNLESS: yield release from is used
// E-CON-0224: yield from inside key block without release
//
ExprTypeResult TypeYieldFromExpr(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::YieldFromExpr& expr,
                                 const TypeEnv& env,
                                 const ExprTypeFn& type_expr,
                                 const IdentTypeFn& /*type_ident*/,
                                 const PlaceTypeFn& /*type_place*/) {
  SpecDefsYieldFrom();
  ExprTypeResult result;

  // Verify we're in an async procedure by extracting async signature
  const auto async_sig = AsyncSigOf(ctx, type_ctx.return_type);
  if (!async_sig.has_value()) {
    SPEC_RULE("rule.21.YieldFrom-NotAsync-Err");
    result.diag_id = "E-CON-0220";  // yield from outside async context
    return result;
  }

  // Type the delegated async expression
  const auto value_result = type_expr(expr.value);
  if (!value_result.ok) {
    result.diag_id = value_result.diag_id;
    return result;
  }

  // Extract async signature from delegated expression
  const auto child_sig = AsyncSigOf(ctx, value_result.type);
  if (!child_sig.has_value()) {
    SPEC_RULE("rule.21.YieldFrom-Out-Err");
    result.diag_id = "E-CON-0221";  // not an async type
    return result;
  }

  // Check Out compatibility: inner Out must equal outer Out
  const auto out_eq = TypeEquiv(async_sig->out, child_sig->out);
  if (!out_eq.ok) {
    result.diag_id = out_eq.diag_id;
    return result;
  }
  if (!out_eq.equiv) {
    SPEC_RULE("rule.21.YieldFrom-Out-Err");
    result.diag_id = "E-CON-0221";  // out type mismatch
    return result;
  }

  // Check In compatibility: outer In must equal inner In
  const auto in_eq = TypeEquiv(async_sig->in, child_sig->in);
  if (!in_eq.ok) {
    result.diag_id = in_eq.diag_id;
    return result;
  }
  if (!in_eq.equiv) {
    SPEC_RULE("rule.21.YieldFrom-In-Err");
    result.diag_id = "E-CON-0222";  // in type mismatch
    return result;
  }

  // Check Error compatibility: inner E must be subtype of outer E
  const auto err_sub = Subtyping(ctx, child_sig->err, async_sig->err);
  if (!err_sub.ok) {
    result.diag_id = err_sub.diag_id;
    return result;
  }
  if (!err_sub.subtype) {
    SPEC_RULE("rule.21.YieldFrom-ErrType-Err");
    result.diag_id = "E-CON-0225";  // error type mismatch
    return result;
  }

  // Check key constraint
  // yield from is ill-formed when keys are held, unless release modifier
  SPEC_RULE("requirement.21.SuspensionKeyRestrictionsReference");
  if (type_ctx.keys_held && !expr.release) {
    SPEC_RULE("requirement.21.AsyncKeyRestrictions");
    result.diag_id = "E-CON-0224";  // yield from inside key block without release
    return result;
  }
  if (expr.release && type_ctx.env_ref) {
    MarkSharedDerivedBindingsStale(*type_ctx.env_ref);
  }

  // Result type is the Result type from the delegated async
  SPEC_RULE("requirements.21.AsyncCapabilityRequirements");
  SPEC_RULE("T-Yield-From");
  SPEC_RULE("rule.21.T-Yield-From");
  result.ok = true;
  result.type = child_sig->result;
  return result;
}

}  // namespace ultraviolet::analysis::expr
