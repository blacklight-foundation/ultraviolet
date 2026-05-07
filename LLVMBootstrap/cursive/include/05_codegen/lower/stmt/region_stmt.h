#pragma once

// =============================================================================
// Region Statement Lowering
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 16659-16662 (Lower-Stmt-Region)
//
// =============================================================================

#include "05_codegen/lower/lower_stmt.h"

namespace cursive::codegen {

// Lower region statement - creates scoped memory region
IRPtr LowerRegionStmt(const ast::RegionStmt& stmt, LowerCtx& ctx);

}  // namespace cursive::codegen
