// =============================================================================
// MIGRATION MAPPING: stmt/return_stmt.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Lines 16674-16681
//   - Lower-Stmt-Return: SeqIR(IR_e, ReturnIR(v))
//   - Lower-Stmt-Return-Unit: ReturnIR(())
//   - TempCleanupIR must be emitted before ReturnIR
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 462-524: LowerReturnStmt function
//   - Handles async procedure returns (wraps in @Completed)
//   - Drops temporaries before cleanup
//   - Computes cleanup plan to function root
//
// DEPENDENCIES:
//   - ultraviolet/src/05_codegen/ir_model.h (IRReturn, IRAsyncComplete)
//   - ultraviolet/src/05_codegen/cleanup.h (ComputeCleanupPlanToFunctionRoot)
//   - ultraviolet/src/04_analysis/types.h (IsAsyncType)
//
// =============================================================================

#include "05_codegen/lower/stmt/return_stmt.h"

#include "00_core/assert_spec.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_stmt.h"
#include "00_core/process_config.h"
#include "00_core/spec_trace.h"

#include <iostream>
#include <string_view>

namespace ultraviolet::codegen {

namespace {

bool IsUnitType(const analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  const auto* prim = std::get_if<analysis::TypePrim>(&type->node);
  return prim && prim->name == "()";
}

// Snapshot a return value into a dedicated local so contract checks and the
// eventual return read the exact same materialized value.
IRPtr SnapshotReturnValueForPostcondition(IRValue& return_value,
                                          const analysis::TypeRef& value_type,
                                          bool force_snapshot,
                                          LowerCtx& ctx) {
  if (!ctx.active_contract_postcondition && !force_snapshot) {
    return nullptr;
  }
  if (!value_type || IsUnitType(value_type)) {
    return nullptr;
  }
  if (return_value.kind == IRValue::Kind::Immediate) {
    return nullptr;
  }

  const std::string snapshot_name = ctx.FreshTempValue("return_snapshot").name;

  IRBindVar bind;
  bind.name = snapshot_name;
  bind.value = return_value;
  bind.type = value_type;

  ctx.RegisterVar(snapshot_name,
                  value_type,
                  false,
                  true,
                  analysis::ProvenanceKind::Bottom);
  bind.stable_name = ctx.StableBindingName(snapshot_name);

  IRValue snapshot_value;
  snapshot_value.kind = IRValue::Kind::Local;
  snapshot_value.name = snapshot_name;
  ctx.RegisterValueType(snapshot_value, value_type);

  return_value = snapshot_value;
  return MakeIR(std::move(bind));
}

bool ExprNeedsDynamicRefinementCheck(const ast::ExprPtr& expr,
                                     const LowerCtx& ctx) {
  if (!expr || !ctx.dynamic_refine_checks) {
    return false;
  }
  return ctx.dynamic_refine_checks->find(expr.get()) !=
         ctx.dynamic_refine_checks->end();
}

bool SameIRValue(const IRValue& lhs, const IRValue& rhs) {
  return lhs.kind == rhs.kind &&
         lhs.name == rhs.name &&
         lhs.bytes == rhs.bytes &&
         lhs.literal_id == rhs.literal_id &&
         lhs.vtable_sym == rhs.vtable_sym;
}

std::vector<TempValue> TempsExcludingValue(const std::vector<TempValue>& temps,
                                           const IRValue& value) {
  std::vector<TempValue> filtered;
  filtered.reserve(temps.size());
  for (const TempValue& temp : temps) {
    if (SameIRValue(temp.value, value)) {
      continue;
    }
    filtered.push_back(temp);
  }
  return filtered;
}

}  // namespace

IRPtr EmitDynamicPostconditionCheckForReturn(const IRValue& return_value,
                                             LowerCtx& ctx) {
  if (!ctx.active_contract_postcondition) {
    return nullptr;
  }

  const auto prev_result = ctx.contract_result_value;
  const bool prev_lowering_contract_postcondition =
      ctx.lowering_contract_postcondition;
  ctx.contract_result_value = return_value;
  ctx.lowering_contract_postcondition = true;
  auto post_result = LowerExpr(*ctx.active_contract_postcondition, ctx);
  ctx.lowering_contract_postcondition = prev_lowering_contract_postcondition;
  ctx.contract_result_value = prev_result;

  if (core::IsDebugEnabled("return")) {
    std::cerr << "[return-post-debug] proc="
              << ctx.current_proc_symbol.value_or("<unknown>")
              << " cond_kind=" << static_cast<int>(post_result.value.kind)
              << " cond_name=" << post_result.value.name << "\n";
  }

  IRIf check;
  check.cond = post_result.value;
  check.then_ir = nullptr;
  check.else_ir = LowerContractViolation(ContractKind::Post,
                                         ctx,
                                         ctx.active_contract_postcondition,
                                         ctx.active_contract_postcondition->span);
  check.result = ctx.FreshTempValue("post_check");
  ctx.RegisterValueType(check.result, analysis::MakeTypePrim("bool"));

  return SeqIR({post_result.ir, MakeIR(std::move(check))});
}

IRPtr LowerReturnStmt(const ast::ReturnStmt& stmt,
                      LowerCtx& ctx,
                      const std::vector<TempValue>& temps) {
  std::vector<IRPtr> ir_parts;

  // Lower the return expression if present
  IRValue return_value;
  analysis::TypeRef value_type;
  if (stmt.value_opt) {
    SPEC_RULE("Lower-Stmt-Return");
    auto prev_suppress = ctx.suppress_temp_at_depth;
    ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
    auto expr_result = LowerExpr(*stmt.value_opt, ctx);
    ctx.suppress_temp_at_depth = prev_suppress;
    ir_parts.push_back(expr_result.ir);
    return_value = expr_result.value;
    // Try to get the type from the lowered value first
    value_type = ctx.LookupValueType(return_value);
    // Fall back to expression type from type checker for immediate values
    if (!value_type && ctx.expr_type) {
      value_type = ctx.expr_type(*stmt.value_opt);
    }
  } else {
    SPEC_RULE("Lower-Stmt-Return-Unit");
    return_value = ctx.FreshTempValue("unit");
    value_type = analysis::MakeTypePrim("()");
  }

  // Preserve return expression typing for ABI return coercion.
  // Without this, codegen can lose the source member type for union returns
  // and pack the wrong variant at runtime.
  if (value_type) {
    ctx.RegisterValueType(return_value, value_type);
  }

  // Section 19.1.3 Async procedure return handling
  // If the procedure returns an async type, wrap the return value in @Completed
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  if (analysis::AsyncSigOf(scope, ctx.proc_ret_type).has_value()) {
    SPEC_RULE("Lower-AsyncReturn");

    // Create IRAsyncComplete to wrap the value in @Completed state
    IRAsyncComplete async_complete;
    async_complete.value = return_value;
    async_complete.result = ctx.FreshTempValue("async_result");
    async_complete.async_type = ctx.proc_ret_type;
    async_complete.result_type = value_type;
    ctx.RegisterValueType(async_complete.result, ctx.proc_ret_type);

    // Save result before move - accessing async_complete after move is UB
    IRValue async_result = async_complete.result;
    ir_parts.push_back(MakeIR(std::move(async_complete)));
    return_value = async_result;
  }

  const bool needs_refine_snapshot =
      stmt.value_opt && ExprNeedsDynamicRefinementCheck(stmt.value_opt, ctx);
  if (IRPtr snapshot_ir =
          SnapshotReturnValueForPostcondition(return_value,
                                              value_type ? value_type : ctx.proc_ret_type,
                                              needs_refine_snapshot,
                                              ctx)) {
    ir_parts.push_back(snapshot_ir);
  }

  if (stmt.value_opt) {
    if (IRPtr refine_ir = EmitDynamicRefinementChecksForExpr(
            *stmt.value_opt,
            return_value,
            value_type ? value_type : ctx.proc_ret_type,
            ctx)) {
      ir_parts.push_back(refine_ir);
    }
  }

  if (IRPtr postcheck_ir = EmitDynamicPostconditionCheckForReturn(return_value, ctx)) {
    ir_parts.push_back(postcheck_ir);
  }

  // Drop statement-scoped temporaries before unwinding scopes.
  const std::vector<TempValue> cleanup_temps =
      TempsExcludingValue(temps, return_value);
  if (!cleanup_temps.empty()) {
    ir_parts.push_back(CleanupList(cleanup_temps, ctx));
  }

  // Section 6.8 Emit cleanup for all variables from current scope to function root
  CleanupPlan cleanup_plan = ComputeCleanupPlanToFunctionRoot(ctx);
  ir_parts.push_back(EmitCleanup(cleanup_plan, ctx));

  // Emit the return
  IRReturn ret;
  ret.value = return_value;
  ir_parts.push_back(MakeIR(std::move(ret)));

  return SeqIR(std::move(ir_parts));
}

}  // namespace ultraviolet::codegen
