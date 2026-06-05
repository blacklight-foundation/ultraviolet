// =============================================================================
// MIGRATION MAPPING: expr/loop_infinite.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.5 (Statement and Block Lowering)
//   - Lines 16741-16744: (Lower-Loop-Infinite)
//     Gamma |- LowerBlock(body) => <IR_b, v_b>
//     ---
//     Gamma |- LowerLoop(LoopInfinite(body)) => <LoopIR(LoopInfinite, IR_b, v_b), v_loop>
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 902-924: LowerLoopInfinite
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRLoop, IRLoopKind::Infinite)
//   - ultraviolet/include/05_codegen/lower/lower_expr.h (LowerCtx, LowerResult, MakeIR)
//   - ultraviolet/include/05_codegen/lower/lower_stmt.h (LowerBlock)
//
// =============================================================================

#include "05_codegen/lower/expr/loop_infinite.h"

#include <string>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/spec_trace.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/lower/lower_stmt.h"

namespace ultraviolet::codegen {

namespace {

IRPtr EmitLoopInvariantCheck(const ast::LoopInvariant& invariant,
                             const char* position,
                             const char* temp_prefix,
                             LowerCtx& ctx) {
  if (!ctx.dynamic_checks || !invariant.predicate) {
    return EmptyIR();
  }

  analysis::StaticProofContext proof_ctx;
  const auto proof = analysis::StaticProof(proof_ctx, invariant.predicate);
  if (proof.provable) {
    return EmptyIR();
  }

  SPEC_RULE("def.15.LoopInvariantEnforcementPoints");
  SPEC_RULE("req.15.InvariantVerificationModeRules");
  if (core::Conformance::Enabled()) {
    std::string payload =
        "source=EmitLoopInvariantCheck;kind=LoopInv;position=";
    payload += position;
    payload +=
        ";dynamic_checks=true;static_proof=false;lowering=ContractCheck";
    core::Conformance::Record(
        "def.15.LoopInvariantEnforcementPoints",
        std::nullopt,
        payload);
    core::Conformance::Record(
        "req.15.InvariantVerificationModeRules",
        std::nullopt,
        payload);
  }

  const analysis::TypeRef bool_type = analysis::MakeTypePrim("bool");
  auto pred_result = LowerExpr(*invariant.predicate, ctx);

  std::vector<IRPtr> parts;
  parts.push_back(pred_result.ir);
  ctx.RegisterValueType(pred_result.value, bool_type);

  IRIf check;
  check.cond = pred_result.value;
  check.then_ir = EmptyIR();
  check.else_ir = LowerContractViolation(ContractKind::LoopInv,
                                         ctx,
                                         invariant.predicate.get(),
                                         invariant.predicate->span);
  check.result = ctx.FreshTempValue(temp_prefix);
  ctx.RegisterValueType(check.result, bool_type);
  parts.push_back(MakeIR(std::move(check)));

  return SeqIR(std::move(parts));
}

}  // namespace

// =============================================================================
// Section 6.5 LowerLoopInfinite - lower infinite loop expression
// =============================================================================
//
// Per (Lower-Loop-Infinite):
//   Gamma |- LowerBlock(body) => <IR_b, v_b>
//   ---
//   Gamma |- LowerLoop(LoopInfinite(body)) => <LoopIR(LoopInfinite, IR_b, v_b), v_loop>
//
// An infinite loop is lowered by:
// 1. Push a loop scope for break/continue cleanup tracking
// 2. Lower the body block
// 3. Pop the loop scope
// 4. Create an IRLoop node with kind Infinite containing the body IR and value
//
// The loop result value is typed as the union of break values (or never if no
// break with value exists).
// =============================================================================

LowerResult LowerLoopInfinite(const ast::Expr& expr,
                              const ast::LoopInfiniteExpr& loop_expr,
                              LowerCtx& ctx) {
  SPEC_RULE("Lower-Loop-Infinite");

  // Push a loop scope for break/continue cleanup tracking.
  // First parameter: is_loop = true (this is a loop scope)
  // Second parameter: is_region = false (not a region scope)
  ctx.PushScope(true, false);

  // Lower the body block
  LowerResult body_result = LowerBlock(*loop_expr.body, ctx);

  IRPtr invariant_entry_ir;
  IRPtr invariant_backedge_ir;
  if (loop_expr.invariant_opt.has_value()) {
    invariant_entry_ir =
        EmitLoopInvariantCheck(*loop_expr.invariant_opt, "entry", "loop_inv_init", ctx);
    invariant_backedge_ir =
        EmitLoopInvariantCheck(*loop_expr.invariant_opt,
                               "backedge",
                               "loop_inv_maint",
                               ctx);
  }

  // Pop the loop scope
  ctx.PopScope();

  // Create infinite loop IR
  IRLoop loop;
  loop.kind = IRLoopKind::Infinite;
  loop.body_ir = body_result.ir;
  loop.invariant_entry_ir = std::move(invariant_entry_ir);
  loop.invariant_backedge_ir = std::move(invariant_backedge_ir);
  loop.body_value = body_result.value;

  // Generate fresh result value for the loop expression
  IRValue result = ctx.FreshTempValue("loop");
  if (ctx.expr_type) {
    if (analysis::TypeRef result_type = ctx.expr_type(expr)) {
      ctx.RegisterValueType(result, result_type);
    }
  }
  loop.result = result;
  IRPtr loop_ir = MakeIR(std::move(loop));
  core::Conformance::Record(
      "def.18.BlockLoopLoweringTotality",
      std::nullopt,
      "source=LowerLoopInfinite;form=LoopInfinite;ir_form=IRLoop;"
      "body_lowered=true");
  core::Conformance::Record(
      "rule.18.Lower-Loop-Infinite",
      std::nullopt,
      "source=LowerLoopInfinite;ir_form=IRLoop;kind=Infinite;"
      "body_lowered=true");

  return LowerResult{loop_ir, result};
}

}  // namespace ultraviolet::codegen
