// =============================================================================
// Defer Statement Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Lines 16655-16657 (Lower-Stmt-Defer)
//   - DeferIR - deferred block marker stored for cleanup
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 695-701: DeferStmt case in LowerStmt dispatch
//
// =============================================================================

#include "05_codegen/lower/stmt/defer_stmt.h"

#include "00_core/assert_spec.h"
#include "00_core/spec_trace.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_stmt.h"

namespace ultraviolet::codegen {

// ============================================================================
// Lower-Stmt-Defer
// ============================================================================
//
// Per the spec (Lines 16655-16657):
//   DeferIR marks the deferred block registration point.
//
// The implementation:
//   - Creates an IRDefer marker node
//   - Registers the defer with the context for cleanup tracking
//   - Defers are executed in reverse order at scope exit
//
IRPtr LowerDeferStmt(const ast::DeferStmt& stmt, LowerCtx& ctx) {
  SPEC_RULE("Lower-Stmt-Defer");

  // Lower the deferred block now and register its IR for execution during
  // cleanup at scope exit.
  LowerResult deferred_body = LowerBlock(*stmt.body, ctx);
  ctx.RegisterDefer(deferred_body.ir);

  core::Conformance::Record(
      "req.18.DeferCleanupSmallStep",
      std::nullopt,
      "source=LowerDeferStmt;operation=RegisterDefer;cleanup_stack=true;"
      "statement_site_execution=false");
  core::Conformance::Record(
      "req.18.DeferCleanupBigStep",
      std::nullopt,
      "source=LowerDeferStmt;operation=RegisterDefer;scope_exit_cleanup=true;"
      "deferred_body_lowered=true");
  core::Conformance::Record(
      "rule.18.Lower-Stmt-Defer",
      std::nullopt,
      "source=LowerDeferStmt;ir_form=IRDefer;body_lowered=LowerBlock;"
      "registered_for_cleanup=true");

  // Runtime behavior is driven by cleanup expansion, not immediate execution
  // at the statement site.
  return MakeIR(IRDefer{});
}

}  // namespace ultraviolet::codegen
