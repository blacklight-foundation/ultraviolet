// =============================================================================
// MIGRATION MAPPING: expr/loop_conditional.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.5 (Statement and Block Lowering)
//   - Lines 16746-16754: (Lower-Loop-Cond)
//     Gamma |- LowerExpr(cond) => <IR_c, v_c>
//     Gamma |- LowerBlock(body) => <IR_b, v_b>
//     ---
//     Gamma |- LowerLoop(LoopCond(cond, body)) =>
//              <LoopIR(LoopCond, IR_c, v_c, IR_b, v_b), v_loop>
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 926-952: LowerLoopConditional
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRLoop, IRLoopKind::Conditional)
//   - ultraviolet/include/05_codegen/lower/lower_expr.h (LowerCtx, LowerResult, MakeIR)
//   - ultraviolet/include/05_codegen/lower/lower_stmt.h (LowerBlock)
//
// =============================================================================

#include "05_codegen/lower/expr/loop_conditional.h"

#include <variant>
#include <vector>

#include "04_analysis/contracts/verification.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/checks/checks.h"
#include "00_core/spec_trace.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

namespace {

bool IsNoOpIR(const IRPtr& ir) {
  return !ir || std::holds_alternative<IROpaque>(ir->node);
}

IRPtr EmitLoopInvariantCheck(const ast::LoopInvariant& invariant,
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
// Section 6.5 LowerLoopConditional - lower conditional loop expression
// =============================================================================
//
// Per (Lower-Loop-Cond):
//   Gamma |- LowerExpr(cond) => <IR_c, v_c>
//   Gamma |- LowerBlock(body) => <IR_b, v_b>
//   ---
//   Gamma |- LowerLoop(LoopCond(cond, body)) =>
//            <LoopIR(LoopCond, IR_c, v_c, IR_b, v_b), v_loop>
//
// A conditional loop (loop condition { body }) is lowered by:
// 1. Push a loop scope for break/continue cleanup tracking
// 2. Lower the condition expression
// 3. Lower the body block
// 4. Pop the loop scope
// 5. Create an IRLoop node with kind Conditional containing both the
//    condition IR/value and the body IR/value
//
// The loop result value is typed as the union of break values (or unit if
// no break with value exists, since the loop may not execute at all).
// =============================================================================

LowerResult LowerLoopConditional(const ast::Expr& expr,
                                 const ast::LoopConditionalExpr& loop_expr,
                                 LowerCtx& ctx) {
  SPEC_RULE("Lower-Loop-Cond");

  // Push a loop scope for break/continue cleanup tracking.
  // First parameter: is_loop = true (this is a loop scope)
  // Second parameter: is_region = false (not a region scope)
  ctx.PushScope(true, false);

  // Lower the condition expression
  auto cond_result = LowerExpr(*loop_expr.cond, ctx);

  // Lower the body block
  LowerResult body_result = LowerBlock(*loop_expr.body, ctx);

  if (loop_expr.invariant_opt.has_value()) {
    IRPtr maintenance_check =
        EmitLoopInvariantCheck(*loop_expr.invariant_opt, "while_inv_maint", ctx);
    if (!IsNoOpIR(maintenance_check)) {
      body_result.ir = SeqIR({body_result.ir, maintenance_check});
    }
  }

  // Pop the loop scope
  ctx.PopScope();

  // Create conditional loop IR
  IRLoop loop;
  loop.kind = IRLoopKind::Conditional;
  loop.cond_ir = cond_result.ir;
  loop.cond_value = cond_result.value;
  loop.body_ir = body_result.ir;
  loop.body_value = body_result.value;

  // Generate fresh result value for the loop expression
  IRValue result = ctx.FreshTempValue("while");
  if (ctx.expr_type) {
    if (analysis::TypeRef result_type = ctx.expr_type(expr)) {
      ctx.RegisterValueType(result, result_type);
    }
  }
  loop.result = result;
  IRPtr loop_ir = MakeIR(std::move(loop));

  if (loop_expr.invariant_opt.has_value()) {
    IRPtr init_check =
        EmitLoopInvariantCheck(*loop_expr.invariant_opt, "while_inv_init", ctx);
    if (!IsNoOpIR(init_check)) {
      return LowerResult{SeqIR({init_check, loop_ir}), result};
    }
  }

  return LowerResult{loop_ir, result};
}

}  // namespace ultraviolet::codegen
