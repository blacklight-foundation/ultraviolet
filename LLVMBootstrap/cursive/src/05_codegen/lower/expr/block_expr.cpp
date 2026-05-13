// =============================================================================
// MIGRATION MAPPING: expr/block_expr.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.5 (Statement and Block Lowering)
//   - Lines 16228-16231: (Lower-Expr-Block)
//   - Lines 16731-16739: (Lower-Block-Tail) and (Lower-Block-Unit)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 211-262: LowerBlock
//
// DEPENDENCIES:
//   - cursive/src/05_codegen/ir_model.h (IRBlock, IRValue)
//   - cursive/src/05_codegen/cleanup.h (ComputeCleanupPlanForCurrentScope)
//
// =============================================================================

#include "05_codegen/lower/expr/block_expr.h"

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/ir/ir_control_flow.h"
#include "05_codegen/lower/lower_stmt.h"

namespace cursive::codegen {

namespace {

// RAII scope guard for parallel collect depth tracking.
// Increments parallel_collect_depth on entry (if collection is active),
// and decrements it on scope exit.
struct ParallelCollectScope {
  LowerCtx& ctx;
  bool active = false;

  explicit ParallelCollectScope(LowerCtx& ctx_in) : ctx(ctx_in) {
    active = ctx.parallel_collect != nullptr;
    if (active) {
      ctx.parallel_collect_depth += 1;
    }
  }

  ~ParallelCollectScope() {
    if (active) {
      ctx.parallel_collect_depth -= 1;
    }
  }

  // Non-copyable
  ParallelCollectScope(const ParallelCollectScope&) = delete;
  ParallelCollectScope& operator=(const ParallelCollectScope&) = delete;
};

// Check if a dispatch expression has a reduce option.
bool DispatchHasReduce(const ast::DispatchExpr& expr) {
  for (const auto& opt : expr.opts) {
    if (opt.kind == ast::DispatchOptionKind::Reduce) {
      return true;
    }
  }
  return false;
}

// Check if an expression is collectable for parallel block result collection.
// Spawn expressions and dispatch expressions with reduce options are collectable.
// Sets needs_wait to true for spawn expressions (which require wait).
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

// =============================================================================
// LowerBlock - Lower a block expression
// =============================================================================
//
// A block consists of:
//   - A sequence of statements
//   - An optional tail expression whose value becomes the block's value
//
// Lowering steps:
//   1. Push a new scope for cleanup tracking
//   2. Lower all statements in order
//   3. Lower the tail expression (if present) to get the block's value
//   4. Compute and emit cleanup for variables declared in this scope
//   5. Pop the scope
//   6. Return the combined IR and result value
//
// Special handling for parallel blocks:
//   - If we're in parallel collect mode (depth 1), spawn/dispatch tail
//     expressions are collected rather than immediately producing results.

LowerResult LowerBlock(const ast::Block& block, LowerCtx& ctx) {
  // Push a new scope for this block
  ctx.PushScope(false, false);
  ParallelCollectScope collect_scope(ctx);

  // Lower all statements
  IRPtr stmts_ir = LowerStmtList(block.stmts, ctx);

  // Lower the tail expression if present
  IRPtr tail_ir = EmptyIR();
  IRValue result_value;

  if (block.tail_opt) {
    SPEC_RULE("Lower-Block-Tail");
    bool needs_wait = false;
    const bool collect_tail =
        ctx.parallel_collect && ctx.parallel_collect_depth == 1 &&
        IsCollectableParallelExpr(*block.tail_opt, needs_wait);
    auto prev_suppress = ctx.suppress_temp_at_depth;
    ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
    auto tail_result = LowerExpr(*block.tail_opt, ctx);
    ctx.suppress_temp_at_depth = prev_suppress;
    if (collect_tail) {
      ParallelCollectItem item;
      item.value = tail_result.value;
      item.needs_wait = needs_wait;
      item.value_type = InferParallelCollectedType(*block.tail_opt, ctx, needs_wait);
      ctx.parallel_collect->push_back(std::move(item));
    }
    tail_ir = tail_result.ir;
    result_value = tail_result.value;
  } else {
    SPEC_RULE("Lower-Block-Unit");
    // No tail expression - block produces unit
    result_value = ctx.FreshTempValue("unit");
    ctx.RegisterValueType(result_value, analysis::MakeTypePrim("()"));
  }

  // Section 6.8: Emit cleanup for variables in this scope
  IRPtr scope_enter_ir = EmptyIR();
  ctx.RegisterRuntimeScopeExitIfRequired();
  if (ctx.CurrentScopeRequiresRuntime()) {
    if (const auto scope_id = ctx.CurrentRuntimeScopeId()) {
      scope_enter_ir = EmitRuntimeScopeEnter(*scope_id, ctx);
    }
  }
  CleanupPlan cleanup_plan = ComputeCleanupPlanForCurrentScope(ctx);
  CleanupPlan remainder =
      ComputeCleanupPlanRemainder(CleanupTarget::CurrentScope, ctx);
  IRPtr cleanup_ir = EmitCleanupWithRemainder(cleanup_plan, remainder, ctx);
  ctx.PopScope();

  IRBlock block_ir;
  block_ir.setup = SeqIR({scope_enter_ir, stmts_ir});
  block_ir.body = IRFlowDefinitelyTerminates(tail_ir)
                      ? tail_ir
                      : SeqIR({tail_ir, cleanup_ir});
  block_ir.value = result_value;

  return LowerResult{MakeIR(std::move(block_ir)), result_value};
}

}  // namespace cursive::codegen
