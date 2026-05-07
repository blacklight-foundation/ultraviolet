// =============================================================================
// MIGRATION MAPPING: expr/binary.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16163-16176: Short-circuit and non-short-circuit binary ops
//     - (Lower-Expr-Bin-And): && with short-circuit
//     - (Lower-Expr-Bin-Or): || with short-circuit
//     - (Lower-Expr-Binary): All other operators
//   - Lines 16328-16336: (Lower-BinOp-Ok) and (Lower-BinOp-Panic)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/lower/lower_expr_calls.cpp
//   - BinaryExpr visitor with operator dispatch
//   - Short-circuit lowering produces IRIf for && and ||
//   - Panic checks for div/mod by zero, overflow, shift
//
// DEPENDENCIES:
//   - cursive/src/05_codegen/ir/ir_model.h (IRBinaryOp, IRIf, IRCheckOp)
//   - cursive/src/05_codegen/checks/checks.h (PanicReason, PanicReasonString)
//
// BINARY OPERATORS:
//   Arithmetic: + - * / % **
//   Comparison: == != < <= > >=
//   Logical: && ||
//   Bitwise: & | ^ << >>
//
// =============================================================================

#include "05_codegen/lower/expr/binary.h"

#include <algorithm>
#include <initializer_list>
#include <utility>
#include <unordered_set>
#include <vector>
#include <iostream>

#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "05_codegen/checks/checks.h"

namespace cursive::codegen {

// Create a boolean immediate value for short-circuit results
// This is the canonical location; declared in other files that need it.
static IRValue BoolImmediate(bool value) {
  IRValue v;
  v.kind = IRValue::Kind::Immediate;
  v.name = value ? "true" : "false";
  v.bytes = {static_cast<std::uint8_t>(value ? 1 : 0)};
  return v;
}

namespace {

LowerCtx MakeBranchCtx(LowerCtx& base) {
  auto saved_value_types = std::move(base.values.value_types);
  auto saved_derived_values = std::move(base.values.derived_values);
  auto saved_drop_glue_types = std::move(base.values.drop_glue_types);

  LowerCtx branch = base;

  base.values.value_types = std::move(saved_value_types);
  base.values.derived_values = std::move(saved_derived_values);
  base.values.drop_glue_types = std::move(saved_drop_glue_types);

  branch.values.value_types.clear();
  branch.values.derived_values.clear();
  branch.values.drop_glue_types.clear();
  branch.values.parent = &base;
  return branch;
}

// Merge value type info from branch context into base context
// Used when lowering short-circuit operators where only one branch may execute
void MergeLowerCtxTemps(LowerCtx& base, const LowerCtx& branch) {
  for (const auto& [name, type] : branch.values.value_types) {
    if (!base.values.value_types.count(name)) {
      base.values.value_types.emplace(name, type);
    }
  }
  for (const auto& [name, info] : branch.values.derived_values) {
    if (!base.values.derived_values.count(name)) {
      base.values.derived_values.emplace(name, info);
    }
  }
  for (const auto& [name, type] : branch.values.static_types) {
    if (!base.values.static_types.count(name)) {
      base.values.static_types.emplace(name, type);
    }
  }
  for (const auto& [name, type] : branch.values.drop_glue_types) {
    if (!base.values.drop_glue_types.count(name)) {
      base.values.drop_glue_types.emplace(name, type);
    }
  }
  *base.temp_counter = std::max(*base.temp_counter, *branch.temp_counter);
}

// Merge move states from multiple branch contexts into a single base context
// A binding is considered moved if it was moved in any branch
void MergeMoveStates(LowerCtx& base, std::initializer_list<const LowerCtx*> branches) {
  for (auto& [name, states] : base.binding_states) {
    if (states.empty()) {
      continue;
    }
    auto& state = states.back();

    // Collect move state from all branches
    bool moved_any = false;
    std::unordered_set<std::string> fields;
    for (const LowerCtx* ctx : branches) {
      auto it = ctx->binding_states.find(name);
      if (it == ctx->binding_states.end() || it->second.empty()) {
        continue;
      }
      const BindingState* bstate = &it->second.back();
      if (!bstate) {
        continue;
      }
      if (bstate->is_moved) {
        moved_any = true;
      } else if (!moved_any) {
        fields.insert(bstate->moved_fields.begin(), bstate->moved_fields.end());
      }
    }

    if (moved_any) {
      state.is_moved = true;
      state.moved_fields.clear();
    } else {
      state.is_moved = false;
      state.moved_fields.assign(fields.begin(), fields.end());
    }
  }
}

// Merge failure flags from branch context into base context
void MergeFailures(LowerCtx& base, const LowerCtx& branch) {
  if (branch.resolve_failed) {
    base.resolve_failed = true;
  }
  if (branch.codegen_failed) {
    base.codegen_failed = true;
  }
  for (const auto& name : branch.resolve_failures) {
    if (std::find(base.resolve_failures.begin(), base.resolve_failures.end(), name) ==
        base.resolve_failures.end()) {
      base.resolve_failures.push_back(name);
    }
  }
}

// Determine panic reason for binary operator
// Some operators can trigger panics: division by zero, overflow, invalid shift
PanicReason BinOpPanicReason(const std::string& op) {
  if (op == "/" || op == "%") {
    return PanicReason::DivZero;  // Or Overflow for INT_MIN / -1
  }
  if (op == "<<" || op == ">>") {
    return PanicReason::Shift;
  }
  if (op == "+" || op == "-" || op == "*" || op == "**") {
    return PanicReason::Overflow;
  }
  return PanicReason::Other;
}

bool IsBoolBinOp(const std::string& op) {
  return op == "==" || op == "===" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
         op == ">=" || op == "&&" || op == "||";
}

}  // namespace

// =============================================================================
// Section 6.4 LowerBinOp - lower binary operator (non-short-circuit)
// =============================================================================
//
// (Lower-BinOp-Ok)
// Gamma |- LowerExpr(e_1) => <IR_1, v_1>    Gamma |- LowerExpr(e_2) => <IR_2, v_2>    BinOp(op, v_1, v_2) => v
// -----------------------------------------------------------------------------------------------------------------
// Gamma |- LowerBinOp(op, e_1, e_2) => <SeqIR(IR_1, IR_2), v>
//
// (Lower-BinOp-Panic)
// Gamma |- LowerExpr(e_1) => <IR_1, v_1>    Gamma |- LowerExpr(e_2) => <IR_2, v_2>
// BinOp(op, v_1, v_2) undefined    OpPanicReason(op, v_1, v_2) = r    Gamma |- LowerPanic(r) => IR_k
// -----------------------------------------------------------------------------------------------------------------
// Gamma |- LowerBinOp(op, e_1, e_2) => <SeqIR(IR_1, IR_2, IR_k), v_unreach>

LowerResult LowerBinOp(const std::string& op,
                       const ast::Expr& lhs,
                       const ast::Expr& rhs,
                       LowerCtx& ctx) {
  SPEC_RULE("Lower-BinOp-Ok");
  SPEC_RULE("Lower-BinOp-Panic");

  auto lhs_result = LowerExpr(lhs, ctx);
  auto rhs_result = LowerExpr(rhs, ctx);

  IRValue result_value = ctx.FreshTempValue("binop");

  std::vector<IRPtr> parts;
  parts.push_back(lhs_result.ir);
  parts.push_back(rhs_result.ir);

  // Check if this operator can panic and needs a runtime check
  bool needs_check =
      (op == "/" || op == "%" || op == "<<" || op == ">>" || op == "+" ||
       op == "-" || op == "*" || op == "**");

  if (needs_check) {
    IRCheckOp check;
    check.op = op;
    check.reason = PanicReasonString(BinOpPanicReason(op));
    check.lhs = lhs_result.value;
    check.rhs = rhs_result.value;
    parts.push_back(MakeIR(std::move(check)));
    parts.push_back(PanicCheck(ctx));
  }

  IRBinaryOp binop;
  binop.op = op;
  binop.lhs = lhs_result.value;
  binop.rhs = rhs_result.value;
  binop.result = result_value;
  parts.push_back(MakeIR(std::move(binop)));

  if (IsBoolBinOp(op)) {
    ctx.RegisterValueType(result_value, analysis::MakeTypePrim("bool"));
  } else if (analysis::TypeRef lhs_type = ctx.LookupValueType(lhs_result.value)) {
    ctx.RegisterValueType(result_value, lhs_type);
  } else if (analysis::TypeRef rhs_type = ctx.LookupValueType(rhs_result.value)) {
    ctx.RegisterValueType(result_value, rhs_type);
  }

  return LowerResult{SeqIR(std::move(parts)), result_value};
}

// =============================================================================
// Section 6.4 Lower-Expr-Bin-And (short-circuit logical AND)
// =============================================================================
//
// (Lower-Expr-Bin-And)
// Gamma |- LowerExpr(e_1) => <IR_1, v_1>    Gamma |- LowerExpr(e_2) => <IR_2, v_2>
// -----------------------------------------------------------------------------------------
// Gamma |- LowerExpr(Binary("&&", e_1, e_2)) => <SeqIR(IR_1, IfIR(v_1, IR_2, v_2, epsilon, false)), v_and>
//
// Short-circuit: if lhs is false, result is false (don't evaluate rhs)

LowerResult LowerBinAnd(const ast::Expr& lhs,
                        const ast::Expr& rhs,
                        LowerCtx& ctx) {
  SPEC_RULE("Lower-Expr-Bin-And");

  auto lhs_result = LowerExpr(lhs, ctx);

  // Create a copy of context for RHS evaluation (may not execute)
  LowerCtx rhs_ctx = MakeBranchCtx(ctx);
  auto rhs_result = LowerExpr(rhs, rhs_ctx);

  // Merge context info from branch back to base
  MergeLowerCtxTemps(ctx, rhs_ctx);
  MergeMoveStates(ctx, {&rhs_ctx});
  MergeFailures(ctx, rhs_ctx);

  // Short-circuit: if lhs is false, result is false (don't evaluate rhs)
  IRValue result_value = ctx.FreshTempValue("and");

  IRIf if_ir;
  if_ir.cond = lhs_result.value;
  if_ir.then_ir = rhs_result.ir;      // If true, evaluate RHS
  if_ir.then_value = rhs_result.value;
  if_ir.else_ir = EmptyIR();          // If false, skip RHS
  if_ir.else_value = BoolImmediate(false);
  if_ir.result = result_value;

  ctx.RegisterValueType(result_value, analysis::MakeTypePrim("bool"));

  return LowerResult{SeqIR({lhs_result.ir, MakeIR(std::move(if_ir))}),
                     result_value};
}

// =============================================================================
// Section 6.4 Lower-Expr-Bin-Or (short-circuit logical OR)
// =============================================================================
//
// (Lower-Expr-Bin-Or)
// Gamma |- LowerExpr(e_1) => <IR_1, v_1>    Gamma |- LowerExpr(e_2) => <IR_2, v_2>
// -----------------------------------------------------------------------------------------
// Gamma |- LowerExpr(Binary("||", e_1, e_2)) => <SeqIR(IR_1, IfIR(v_1, epsilon, true, IR_2, v_2)), v_or>
//
// Short-circuit: if lhs is true, result is true (don't evaluate rhs)

LowerResult LowerBinOr(const ast::Expr& lhs,
                       const ast::Expr& rhs,
                       LowerCtx& ctx) {
  SPEC_RULE("Lower-Expr-Bin-Or");

  auto lhs_result = LowerExpr(lhs, ctx);

  // Create a copy of context for RHS evaluation (may not execute)
  LowerCtx rhs_ctx = MakeBranchCtx(ctx);
  auto rhs_result = LowerExpr(rhs, rhs_ctx);

  // Merge context info from branch back to base
  MergeLowerCtxTemps(ctx, rhs_ctx);
  MergeMoveStates(ctx, {&rhs_ctx});
  MergeFailures(ctx, rhs_ctx);

  // Short-circuit: if lhs is true, result is true (don't evaluate rhs)
  IRValue result_value = ctx.FreshTempValue("or");

  IRIf if_ir;
  if_ir.cond = lhs_result.value;
  if_ir.then_ir = EmptyIR();          // If true, skip RHS
  if_ir.then_value = BoolImmediate(true);
  if_ir.else_ir = rhs_result.ir;      // If false, evaluate RHS
  if_ir.else_value = rhs_result.value;
  if_ir.result = result_value;

  ctx.RegisterValueType(result_value, analysis::MakeTypePrim("bool"));

  return LowerResult{SeqIR({lhs_result.ir, MakeIR(std::move(if_ir))}),
                     result_value};
}

// =============================================================================
// Section 6.4 LowerBinaryExpr - main binary expression dispatcher
// =============================================================================
//
// (Lower-Expr-Binary)
// op not in {"&&", "||"}    Gamma |- LowerBinOp(op, e_1, e_2) => <IR, v>
// -----------------------------------------------------------------------------------------
// Gamma |- LowerExpr(Binary(op, e_1, e_2)) => <IR, v>

LowerResult LowerBinaryExpr(const ast::BinaryExpr& expr, LowerCtx& ctx) {
  const std::string& op = expr.op;

  // Handle short-circuit operators specially
  if (op == "&&") {
    return LowerBinAnd(*expr.lhs, *expr.rhs, ctx);
  }
  if (op == "||") {
    return LowerBinOr(*expr.lhs, *expr.rhs, ctx);
  }

  // All other operators use LowerBinOp
  SPEC_RULE("Lower-Expr-Binary");
  return LowerBinOp(op, *expr.lhs, *expr.rhs, ctx);
}

}  // namespace cursive::codegen







