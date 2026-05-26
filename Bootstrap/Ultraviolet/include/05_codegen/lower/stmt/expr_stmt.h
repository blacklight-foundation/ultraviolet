#pragma once

// =============================================================================
// Expression Statement Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Lines 16650-16653 (Lower-Stmt-Expr)
//   - LowerExpr(expr) produces IR_e
//   - Result value discarded (side effects only)
//
// =============================================================================

#include "05_codegen/lower/lower_stmt.h"

namespace ultraviolet::codegen {

// Lower expression statement - evaluates for side effects only
IRPtr LowerExprStmt(const ast::ExprStmt& stmt, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
