#pragma once

// =============================================================================
// Defer Statement Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Lines 16655-16657 (Lower-Stmt-Defer)
//
// =============================================================================

#include "05_codegen/lower/lower_stmt.h"

namespace ultraviolet::codegen {

// Lower defer statement - deferred block stored for cleanup
IRPtr LowerDeferStmt(const ast::DeferStmt& stmt, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
