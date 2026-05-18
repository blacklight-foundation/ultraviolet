// =============================================================================
// MIGRATION MAPPING: expr/unary.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16158-16161: (Lower-Expr-Unary)
//   - Lines 16318-16326: (Lower-UnOp-Ok) and (Lower-UnOp-Panic)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_calls.cpp
//   - UnaryExpr visitor with operator dispatch
//   - Panic for undefined operations (e.g., negation overflow)
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRUnaryOp, IRCheckOp)
//   - ultraviolet/include/05_codegen/checks/checks.h (PanicReason, PanicReasonString)
//
// UNARY OPERATORS: ! (not), - (negate)
//
// =============================================================================

#include "05_codegen/lower/expr/unary.h"

#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/typing/type_predicates.h"
#include "05_codegen/checks/checks.h"

namespace ultraviolet::codegen {

namespace {

// Determine panic reason for unary operator
// Negation can overflow for signed integers (e.g., -(-128i8) overflows)
PanicReason UnOpPanicReason(const std::string& op) {
  if (op == "-") {
    return PanicReason::Overflow;
  }
  return PanicReason::Other;
}

analysis::TypeRef StripPermType(const analysis::TypeRef& type) {
  if (!type) {
    return nullptr;
  }
  if (analysis::TypeRef stripped = analysis::StripPerm(type)) {
    return stripped;
  }
  return type;
}

analysis::TypeRef InferUnaryResultType(std::string_view op,
                                       const analysis::TypeRef& operand_type) {
  analysis::TypeRef stripped = StripPermType(operand_type);
  if (!stripped) {
    return nullptr;
  }

  if (op == "widen") {
    if (const auto* modal_state =
            std::get_if<analysis::TypeModalState>(&stripped->node)) {
      return analysis::MakeTypePath(modal_state->path,
                                    modal_state->generic_args);
    }
    return nullptr;
  }

  if (op == "!" || op == "-" || op == "~") {
    return stripped;
  }

  return nullptr;
}

bool IsSignedIntegerOperandType(const analysis::TypeRef& type) {
  const analysis::TypeRef stripped = StripPermType(type);
  if (!stripped) {
    return false;
  }
  const auto* prim = std::get_if<analysis::TypePrim>(&stripped->node);
  if (!prim) {
    return false;
  }
  const std::string_view name = prim->name;
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
         name == "i128" || name == "isize";
}

}  // namespace

// =============================================================================
// Section 6.4 LowerUnOp - lower unary operator
// =============================================================================
//
// (Lower-UnOp-Ok)
// Gamma |- LowerExpr(e) => <IR_e, v>    UnOp(op, v) => v'
// ---------------------------------------------------------------------------
// Gamma |- LowerUnOp(op, e) => <IR_e, v'>
//
// (Lower-UnOp-Panic)
// Gamma |- LowerExpr(e) => <IR_e, v>    UnOp(op, v) undefined
// OpPanicReason(op, v) = r    Gamma |- LowerPanic(r) => IR_k
// ---------------------------------------------------------------------------
// Gamma |- LowerUnOp(op, e) => <SeqIR(IR_e, IR_k), v_unreach>
//
// The unary operator is lowered as follows:
// 1. Lower the operand expression to get IR and value
// 2. If the operator can panic (negation), emit a check operation
// 3. Emit the unary operation itself
// 4. Return the sequence of IR and the result value

LowerResult LowerUnOp(const std::string& op,
                      const ast::Expr& operand,
                      LowerCtx& ctx) {
  SPEC_RULE("Lower-UnOp-Ok");
  SPEC_RULE("Lower-UnOp-Panic");

  // The operand value is consumed by the unary operator, so it should not be
  // tracked as a temporary requiring cleanup.
  auto prev_suppress = ctx.suppress_temp_at_depth;
  ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
  auto operand_result = LowerExpr(operand, ctx);
  ctx.suppress_temp_at_depth = prev_suppress;

  analysis::TypeRef operand_type = ctx.LookupValueType(operand_result.value);
  if (!operand_type && ctx.expr_type) {
    operand_type = ctx.expr_type(operand);
  }
  if (operand_type) {
    ctx.RegisterValueType(operand_result.value, operand_type);
  }

  IRValue result_value = ctx.FreshTempValue("unop");
  analysis::TypeRef result_type = InferUnaryResultType(op, operand_type);

  std::vector<IRPtr> parts;
  parts.push_back(operand_result.ir);

  // Check if this operator can panic and needs a runtime check.
  // Only signed-integer negation can overflow (e.g., -INT_MIN).
  bool needs_check = (op == "-") && IsSignedIntegerOperandType(operand_type);
  if (needs_check) {
    IRCheckOp check;
    check.op = op;
    check.reason = PanicReasonString(UnOpPanicReason(op));
    check.lhs = operand_result.value;
    // rhs is not set for unary operators (std::optional remains empty)
    parts.push_back(MakeIR(std::move(check)));
    parts.push_back(PanicFollowup(ctx));
  }

  // Emit the unary operation
  IRUnaryOp unop;
  unop.op = op;
  unop.operand = operand_result.value;
  unop.result = result_value;
  unop.operand_type = operand_type;
  unop.result_type = result_type;
  parts.push_back(MakeIR(std::move(unop)));

  if (result_type) {
    ctx.RegisterValueType(result_value, result_type);
  }

  return LowerResult{SeqIR(std::move(parts)), result_value};
}

// =============================================================================
// Section 6.4 LowerUnaryExpr - main unary expression entry point
// =============================================================================
//
// (Lower-Expr-Unary)
// Gamma |- LowerUnOp(op, e) => <IR, v>
// ---------------------------------------------------------------------------
// Gamma |- LowerExpr(Unary(op, e)) => <IR, v>
//
// This function extracts the operator and operand from the UnaryExpr AST node
// and delegates to LowerUnOp for the actual lowering.

LowerResult LowerUnaryExpr(const ast::UnaryExpr& expr, LowerCtx& ctx) {
  SPEC_RULE("Lower-Expr-Unary");
  return LowerUnOp(expr.op, *expr.value, ctx);
}

}  // namespace ultraviolet::codegen
