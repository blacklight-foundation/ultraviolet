#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// =============================================================================
// Call Expression Lowering
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16143-16151: (Lower-Expr-Call-PanicOut) and (Lower-Expr-Call-NoPanicOut)
//     With panic out: appends PanicOutName to args, adds PanicCheck after
//     Without panic out: direct call
//   - Lines 16153-16156: (Lower-Expr-MethodCall)
//     Method call lowering with receiver argument handling
//
// CALL TYPES:
//   - Direct procedure calls
//   - Method calls on receivers
//   - Record constructor calls (zero-arg)
//   - Builtin capability method calls
//   - Dynamic dispatch calls (via vtable)

// Forward declarations
struct LowerCtx;
struct LowerResult;

// =============================================================================
// Section 6.4 LowerCallExpr - Call expression lowering
// =============================================================================

// (Lower-Expr-Call-PanicOut)
// Gamma |- LowerExpr(callee) => <IR_c, v_c>
// Gamma |- LowerArgs(Params(Call(callee, args)), args) => <IR_a, vec_v>
// NeedsPanicOut(callee)
// => <SeqIR(IR_c, IR_a, CallIR(v_c, vec_v ++ [PanicOutName]), PanicCheck), v_call>
//
// (Lower-Expr-Call-NoPanicOut)
// Gamma |- LowerExpr(callee) => <IR_c, v_c>
// Gamma |- LowerArgs(Params(Call(callee, args)), args) => <IR_a, vec_v>
// not NeedsPanicOut(callee)
// => <SeqIR(IR_c, IR_a, CallIR(v_c, vec_v)), v_call>

LowerResult LowerCallExpr(const ast::Expr& expr_wrapper,
                          const ast::CallExpr& expr,
                          LowerCtx& ctx);

// =============================================================================
// Anchor function for SPEC_RULE markers
// =============================================================================

void AnchorCallLoweringRules();

}  // namespace ultraviolet::codegen
