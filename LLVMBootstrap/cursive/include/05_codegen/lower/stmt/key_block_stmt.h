#pragma once

// =============================================================================
// Key Block Statement Lowering
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 7.6 (Key Blocks)
//   - #path { body } acquires key for synchronized access
//   - Modes: read, write (default)
//   - Modifiers: dynamic, speculative, release
//
// =============================================================================

#include "05_codegen/lower/lower_stmt.h"

namespace cursive::codegen {

// Lower key block statement - synchronized access block
IRPtr LowerKeyBlockStmt(const ast::KeyBlockStmt& stmt, LowerCtx& ctx);

}  // namespace cursive::codegen
