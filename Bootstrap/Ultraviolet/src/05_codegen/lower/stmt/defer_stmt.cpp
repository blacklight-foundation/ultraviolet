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

  // Runtime behavior is driven by cleanup expansion, not immediate execution
  // at the statement site.
  return MakeIR(IRDefer{});
}

}  // namespace ultraviolet::codegen
