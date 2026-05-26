// =============================================================================
// Unsafe Block Statement Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Lines 16696-16699 (Lower-Stmt-UnsafeBlock)
//   - LowerBlock(block) produces IR_b, v
//   - Result: IR_b (block IR directly, value tracked as temp)
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 864-873: UnsafeBlockStmt case in LowerStmt dispatch
//
// NOTE: Unsafe context is tracked at analysis time, not codegen. The lowering
// is identical to a regular block - safety restrictions are enforced earlier.
//
// =============================================================================

#include "05_codegen/lower/stmt/unsafe_block_stmt.h"

#include "00_core/assert_spec.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_stmt.h"

namespace ultraviolet::codegen {

// ============================================================================
// Lower-Stmt-UnsafeBlock
// ============================================================================
//
// Per the spec (Lines 16696-16699):
//   LowerBlock(block) => <IR_b, v>
//   Result: IR_b (block IR)
//
// Unsafe blocks are simply lowered as regular blocks. The safety context
// (allowing raw pointer derefs, transmutes, etc.) was tracked during
// type checking. At codegen time, there's no difference.
//
IRPtr LowerUnsafeBlockStmt(const ast::UnsafeBlockStmt& stmt, LowerCtx& ctx) {
  SPEC_RULE("Lower-Stmt-UnsafeBlock");

  if (!stmt.body) {
    return EmptyIR();
  }

  // Lower the body block
  auto body_result = LowerBlock(*stmt.body, ctx);

  // Register the result as a temp if needed
  if (ctx.temp_sink) {
    analysis::TypeRef result_type;
    if (stmt.body->tail_opt && ctx.expr_type) {
      result_type = ctx.expr_type(*stmt.body->tail_opt);
    } else if (!stmt.body->tail_opt) {
      result_type = analysis::MakeTypePrim("()");
    }
    ctx.RegisterTempValue(body_result.value, result_type);
  }

  return body_result.ir;
}

}  // namespace ultraviolet::codegen
