#pragma once

// =============================================================================
// Break Statement Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Lines 16683-16690
//   - Lower-Stmt-Break: SeqIR(IR_e, BreakIR(v))
//   - Lower-Stmt-Break-Unit: BreakIR(none)
//   - TempCleanupIR must be emitted before BreakIR
//
// =============================================================================

#include <vector>

#include "05_codegen/lower/lower_stmt.h"

namespace ultraviolet::codegen {

// Lower break statement - exits enclosing loop with optional value
IRPtr LowerBreakStmt(const ast::BreakStmt& stmt,
                     LowerCtx& ctx,
                     const std::vector<TempValue>& temps);

}  // namespace ultraviolet::codegen
