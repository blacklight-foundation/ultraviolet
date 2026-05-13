// =============================================================================
// Error Statement Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 16701-16703 (Lower-Stmt-Error)
//   - LowerPanic(ErrorStmt(span))
//   - Error statements trigger panic IR
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 626-628: ErrorStmt case in LowerStmt dispatch
//
// =============================================================================

#include "05_codegen/lower/stmt/error_stmt.h"

#include "00_core/assert_spec.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

// ============================================================================
// Lower-Stmt-Error
// ============================================================================
//
// Per the spec (Lines 16701-16703):
//   LowerPanic(ErrorStmt(span))
//
// Error statements represent syntax errors that were recovered during parsing.
// They are lowered to panic IR since the program should never actually execute
// error statements - they indicate malformed code.
//
IRPtr LowerErrorStmt(const ast::ErrorStmt& /*stmt*/, LowerCtx& ctx) {
  SPEC_RULE("Lower-Stmt-Error");
  ctx.codegen_failed = true;

  // Error statements emit a panic with the ErrorStmt reason
  return LowerPanic(PanicReason::ErrorStmt, ctx);
}

}  // namespace cursive::codegen
