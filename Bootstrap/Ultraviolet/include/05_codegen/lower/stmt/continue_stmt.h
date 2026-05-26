#pragma once

// =============================================================================
// Continue Statement Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Lines 16692-16694 (Lower-Stmt-Continue)
//   - ContinueIR
//   - TempCleanupIR must be emitted before ContinueIR
//
// =============================================================================

#include <vector>

#include "05_codegen/lower/lower_stmt.h"

namespace ultraviolet::codegen {

// Lower continue statement - skips to next loop iteration
IRPtr LowerContinueStmt(const ast::ContinueStmt& stmt,
                        LowerCtx& ctx,
                        const std::vector<TempValue>& temps);

}  // namespace ultraviolet::codegen
