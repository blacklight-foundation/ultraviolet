#pragma once

// =============================================================================
// Error Statement Lowering
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Lines 16701-16703 (Lower-Stmt-Error)
//
// =============================================================================

#include "05_codegen/lower/lower_stmt.h"

namespace ultraviolet::codegen {

// Lower error statement - emits panic IR
IRPtr LowerErrorStmt(const ast::ErrorStmt& stmt, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
