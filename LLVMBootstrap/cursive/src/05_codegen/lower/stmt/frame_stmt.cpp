// =============================================================================
// Frame Statement Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 16664-16672
//   - Lower-Stmt-Frame-Implicit: FrameIR(none, IR_b, v_b)
//   - Lower-Stmt-Frame-Explicit: SeqIR(IR_r, FrameIR(v_r, IR_b, v_b))
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 747-854: FrameStmt case in LowerStmt dispatch
//
// =============================================================================

#include "05_codegen/lower/stmt/frame_stmt.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/ir/ir_control_flow.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_stmt.h"
#include "05_codegen/lower/stmt/stmt_common.h"

namespace cursive::codegen {

namespace {

// Check if a dispatch expression has a reduce option
bool DispatchHasReduce(const ast::DispatchExpr& expr) {
  for (const auto& opt : expr.opts) {
    if (opt.kind == ast::DispatchOptionKind::Reduce) {
      return true;
    }
  }
  return false;
}

// Check if an expression is a collectable parallel expression
bool IsCollectableParallelExpr(const ast::Expr& expr, bool& needs_wait) {
  if (std::holds_alternative<ast::SpawnExpr>(expr.node)) {
    needs_wait = true;
    return true;
  }
  if (const auto* dispatch = std::get_if<ast::DispatchExpr>(&expr.node)) {
    if (DispatchHasReduce(*dispatch)) {
      needs_wait = false;
      return true;
    }
  }
  return false;
}

analysis::TypeRef InferParallelCollectedType(const ast::Expr& expr,
                                             LowerCtx& ctx,
                                             bool needs_wait) {
  if (!ctx.expr_type) {
    return nullptr;
  }
  analysis::TypeRef expr_type = ctx.expr_type(expr);
  analysis::TypeRef stripped = analysis::StripPerm(expr_type);
  if (!stripped) {
    stripped = expr_type;
  }
  if (!stripped) {
    return nullptr;
  }
  if (!needs_wait) {
    return stripped;
  }
  if (const auto inner = analysis::ExtractSpawnedInner(stripped)) {
    return *inner;
  }
  return nullptr;
}

}  // namespace

// ============================================================================
// Lower-Stmt-Frame-Implicit / Lower-Stmt-Frame-Explicit
// ============================================================================
//
// Per the spec (Lines 16664-16672):
//   Frame-Implicit: Uses current active region
//   Frame-Explicit: Uses specified region
//
// The implementation:
//   - Calls Region::mark to record the region state
//   - Lowers the body block
//   - Registers Region::reset_to as a defer for cleanup
//   - Allocations within the frame are released when the frame exits
//
IRPtr LowerFrameStmt(const ast::FrameStmt& stmt, LowerCtx& ctx) {
  if (!stmt.body) {
    return EmptyIR();
  }

  std::optional<IRValue> region_value;
  IRPtr region_ir = EmptyIR();

  if (stmt.target_opt.has_value()) {
    // Explicit frame: frame target_region { ... }
    SPEC_RULE("Lower-Stmt-Frame-Explicit");
    ast::IdentifierExpr ident;
    ident.name = *stmt.target_opt;
    ast::Expr region_expr;
    region_expr.span = stmt.span;
    region_expr.node = ident;
    auto region_result = LowerExpr(region_expr, ctx);
    region_ir = region_result.ir;
    region_value = region_result.value;
  } else {
    // Implicit frame: frame { ... }
    SPEC_RULE("Lower-Stmt-Frame-Implicit");
    if (!ctx.active_region_aliases.empty()) {
      IRValue value;
      value.kind = IRValue::Kind::Local;
      value.name = ctx.active_region_aliases.back();
      region_value = value;
    }
  }

  if (!region_value.has_value()) {
    // No active region - frame is a no-op
    return EmptyIR();
  }

  // Create a temp for the mark value (records region state)
  IRValue mark_value = ctx.FreshTempValue("frame_mark");
  ctx.RegisterValueType(mark_value, analysis::MakeTypePrim("usize"));

  // Call Region::mark after the runtime scope enter so the runtime can
  // associate the frame entry with the pushed frame scope.
  IRCall mark_call;
  mark_call.callee.kind = IRValue::Kind::Symbol;
  mark_call.callee.name = BuiltinModalSymRegionMark();
  mark_call.args.push_back(*region_value);
  mark_call.result = mark_value;
  IRPtr mark_ir = MakeIR(std::move(mark_call));

  // Create the reset call (will be registered as a defer)
  IRCall reset_call;
  reset_call.callee.kind = IRValue::Kind::Symbol;
  reset_call.callee.name = BuiltinModalSymRegionResetTo();
  reset_call.args.push_back(*region_value);
  reset_call.args.push_back(mark_value);
  IRValue reset_value = ctx.FreshTempValue("frame_reset");
  reset_call.result = reset_value;
  ctx.RegisterValueType(reset_value, analysis::MakeTypePrim("()"));
  IRPtr reset_ir = MakeIR(std::move(reset_call));

  // Push a new scope for the frame
  ctx.PushScope(false, false);
  ctx.RequireCurrentRuntimeScope();
  ctx.RegisterRuntimeScopeExit();
  IRPtr scope_enter_ir = EmptyIR();
  if (const auto scope_id = ctx.CurrentRuntimeScopeId()) {
    scope_enter_ir = EmitRuntimeScopeEnter(*scope_id, ctx);
  }
  const std::string frame_tag = ctx.FreshRegionAlias();
  ctx.ReserveRegionTag(frame_tag);

  // Set up parallel collection scope
  ParallelCollectScope collect_scope(ctx);

  // Register the reset as a defer to ensure it runs on scope exit
  // This ensures reset runs after defers/drops in this frame scope.
  ctx.RegisterDefer(reset_ir);

  // Lower the body statements
  IRPtr stmts_ir = LowerStmtList(stmt.body->stmts, ctx);

  // Lower the tail expression if present
  IRPtr tail_ir = EmptyIR();
  IRValue result_value;
  if (stmt.body->tail_opt) {
    SPEC_RULE("Lower-Block-Tail");
    bool needs_wait = false;
    const bool collect_tail =
        ctx.parallel_collect && ctx.parallel_collect_depth == 1 &&
        IsCollectableParallelExpr(*stmt.body->tail_opt, needs_wait);
    auto prev_suppress = ctx.suppress_temp_at_depth;
    if (collect_tail) {
      ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
    }
    auto tail_result = LowerExpr(*stmt.body->tail_opt, ctx);
    if (collect_tail) {
      ctx.suppress_temp_at_depth = prev_suppress;
      ParallelCollectItem item;
      item.value = tail_result.value;
      item.needs_wait = needs_wait;
      item.value_type = InferParallelCollectedType(*stmt.body->tail_opt, ctx, needs_wait);
      ctx.parallel_collect->push_back(std::move(item));
    } else {
      ctx.suppress_temp_at_depth = prev_suppress;
    }
    tail_ir = tail_result.ir;
    result_value = tail_result.value;
  } else {
    SPEC_RULE("Lower-Block-Unit");
    result_value = ctx.FreshTempValue("unit");
    ctx.RegisterValueType(result_value, analysis::MakeTypePrim("()"));
  }

  // Compute cleanup for this scope
  CleanupPlan cleanup_plan = ComputeCleanupPlanForCurrentScope(ctx);
  CleanupPlan remainder = ComputeCleanupPlanRemainder(CleanupTarget::CurrentScope, ctx);
  IRPtr cleanup_ir = EmitCleanupWithRemainder(cleanup_plan, remainder, ctx);

  // Pop the frame scope
  ctx.PopScope();

  // Assemble the block IR
  IRBlock block_ir;
  block_ir.setup = SeqIR({region_ir, scope_enter_ir, mark_ir, stmts_ir});
  block_ir.body = IRFlowDefinitelyTerminates(tail_ir)
                      ? tail_ir
                      : SeqIR({tail_ir, cleanup_ir});
  block_ir.value = result_value;

  // Register result as temp if needed
  if (ctx.temp_sink) {
    analysis::TypeRef result_type;
    if (stmt.body->tail_opt && ctx.expr_type) {
      result_type = ctx.expr_type(*stmt.body->tail_opt);
    } else if (!stmt.body->tail_opt) {
      result_type = analysis::MakeTypePrim("()");
    }
    ctx.RegisterTempValue(result_value, result_type);
  }

  // Create the frame IR
  IRFrame frame;
  frame.region = region_value;
  frame.body = MakeIR(std::move(block_ir));
  frame.value = result_value;

  return MakeIR(std::move(frame));
}

}  // namespace cursive::codegen
