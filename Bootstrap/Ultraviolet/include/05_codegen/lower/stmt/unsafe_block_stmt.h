#pragma once

// =============================================================================
// Unsafe Block Statement Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Lines 16696-16699 (Lower-Stmt-UnsafeBlock)
//
// =============================================================================

#include "05_codegen/lower/lower_stmt.h"

namespace ultraviolet::codegen {

// Lower unsafe block statement - lowered as regular block
IRPtr LowerUnsafeBlockStmt(const ast::UnsafeBlockStmt& stmt, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
