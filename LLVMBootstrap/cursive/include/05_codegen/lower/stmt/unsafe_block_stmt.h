#pragma once

// =============================================================================
// Unsafe Block Statement Lowering
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 16696-16699 (Lower-Stmt-UnsafeBlock)
//
// =============================================================================

#include "05_codegen/lower/lower_stmt.h"

namespace cursive::codegen {

// Lower unsafe block statement - lowered as regular block
IRPtr LowerUnsafeBlockStmt(const ast::UnsafeBlockStmt& stmt, LowerCtx& ctx);

}  // namespace cursive::codegen
