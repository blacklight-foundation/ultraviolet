#pragma once

// =============================================================================
// Frame Statement Lowering
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Lines 16664-16672
//   - Lower-Stmt-Frame-Implicit: FrameIR(none, IR_b, v_b)
//   - Lower-Stmt-Frame-Explicit: SeqIR(IR_r, FrameIR(v_r, IR_b, v_b))
//
// =============================================================================

#include "05_codegen/lower/lower_stmt.h"

namespace ultraviolet::codegen {

// Lower frame statement - subdivides region with mark/reset
IRPtr LowerFrameStmt(const ast::FrameStmt& stmt, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
