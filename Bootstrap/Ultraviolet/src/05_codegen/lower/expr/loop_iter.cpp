// =============================================================================
// MIGRATION MAPPING: expr/loop_iter.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.5 (Statement and Block Lowering)
//   - Lines 16751-16754: (Lower-Loop-Iter)
//     Gamma |- LowerExpr(iter) => <IR_i, v_iter>
//     Gamma |- LowerBlock(body) => <IR_b, v_b>
//     ---
//     Gamma |- LowerLoop(LoopIter(pat, ty_opt, iter, body)) =>
//         <LoopIR(LoopIter, pat, ty_opt, IR_i, v_iter, IR_b, v_b), v_loop>
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 955-992: LowerLoopIter
//
// DEPENDENCIES:
//   - ultraviolet/src/05_codegen/ir/ir_model.h (IRLoop, IRLoopKind::Iter)
//   - ultraviolet/src/05_codegen/lower/pattern/*.h (for pattern binding)
//
// =============================================================================

#include "05_codegen/lower/expr/loop_iter.h"

#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/lower/lower_pat.h"
#include "05_codegen/lower/lower_stmt.h"
#include "05_codegen/lower/pattern/ir_pattern.h"

namespace ultraviolet::codegen {

namespace {

// ---------------------------------------------------------------------------
// Helper: ProvInfo
// ---------------------------------------------------------------------------
// Provenance information for bindings.

struct ProvInfo {
  analysis::ProvenanceKind kind = analysis::ProvenanceKind::Bottom;
  std::optional<std::string> region;
  std::optional<std::string> region_tag;
  bool fresh_region = false;
};

// ---------------------------------------------------------------------------
// Helper: ExprProvInfo
// ---------------------------------------------------------------------------
// Extract provenance information from an expression using the context's
// provenance maps populated during analysis.

ProvInfo ExprProvInfo(const ast::Expr& expr, const LowerCtx& ctx) {
  ProvInfo info;
  if (auto prov = ctx.LookupExprProv(expr)) {
    info.kind = *prov;
  }
  if (info.kind == analysis::ProvenanceKind::Region) {
    info.region = ctx.LookupExprRegion(expr);
    info.region_tag = ctx.LookupExprRegionTag(expr);
  }
  return info;
}

// ---------------------------------------------------------------------------
// Helper: BindProvInfo
// ---------------------------------------------------------------------------
// Compute the provenance for a binding initialized from an expression.
// If the initializer has no provenance (Bottom), the binding gets Stack
// provenance. Otherwise, the binding inherits the initializer's provenance.

ProvInfo BindProvInfo(const ProvInfo& init) {
  if (init.kind == analysis::ProvenanceKind::Bottom) {
    return ProvInfo{analysis::ProvenanceKind::Stack, std::nullopt, std::nullopt, false};
  }
  return init;
}

ProvInfo StableRegionProv(const ProvInfo& info, const LowerCtx& ctx) {
  ProvInfo stable = info;
  if (stable.kind == analysis::ProvenanceKind::Region &&
      stable.region.has_value() && !stable.region->empty()) {
    stable.region = ctx.StableBindingName(*stable.region);
  }
  return stable;
}

// ---------------------------------------------------------------------------
// Helper: LoopPatternType
// ---------------------------------------------------------------------------
// Extract the element type from an iterator expression's type.
// For arrays and slices, returns the element type.
// For other types, returns nullptr (type inference will handle it).

analysis::TypeRef LoopPatternType(const analysis::TypeRef& iter_type) {
  if (!iter_type) {
    return nullptr;
  }
  if (std::holds_alternative<analysis::TypeArray>(iter_type->node)) {
    return std::get<analysis::TypeArray>(iter_type->node).element;
  }
  if (std::holds_alternative<analysis::TypeSlice>(iter_type->node)) {
    return std::get<analysis::TypeSlice>(iter_type->node).element;
  }
  if (std::holds_alternative<analysis::TypeRange>(iter_type->node)) {
    return std::get<analysis::TypeRange>(iter_type->node).base;
  }
  if (std::holds_alternative<analysis::TypeRangeInclusive>(iter_type->node)) {
    return std::get<analysis::TypeRangeInclusive>(iter_type->node).base;
  }
  if (std::holds_alternative<analysis::TypeRangeFrom>(iter_type->node)) {
    return std::get<analysis::TypeRangeFrom>(iter_type->node).base;
  }
  if (std::holds_alternative<analysis::TypeRangeTo>(iter_type->node)) {
    return std::get<analysis::TypeRangeTo>(iter_type->node).base;
  }
  if (std::holds_alternative<analysis::TypeRangeToInclusive>(iter_type->node)) {
    return std::get<analysis::TypeRangeToInclusive>(iter_type->node).base;
  }
  return nullptr;
}

bool IsNoOpIR(const IRPtr& ir) {
  return !ir || std::holds_alternative<IROpaque>(ir->node);
}

IRPtr EmitLoopInvariantCheck(const ast::LoopInvariant& invariant,
                             const char* temp_prefix,
                             LowerCtx& ctx) {
  if (!ctx.dynamic_checks || !invariant.predicate) {
    return EmptyIR();
  }

  analysis::StaticProofContext proof_ctx;
  const auto proof = analysis::StaticProof(proof_ctx, invariant.predicate);
  if (proof.provable) {
    return EmptyIR();
  }

  const analysis::TypeRef bool_type = analysis::MakeTypePrim("bool");
  auto pred_result = LowerExpr(*invariant.predicate, ctx);

  std::vector<IRPtr> parts;
  parts.push_back(pred_result.ir);
  ctx.RegisterValueType(pred_result.value, bool_type);

  IRIf check;
  check.cond = pred_result.value;
  check.then_ir = EmptyIR();
  check.else_ir = LowerContractViolation(ContractKind::LoopInv,
                                         ctx,
                                         invariant.predicate.get(),
                                         invariant.predicate->span);
  check.result = ctx.FreshTempValue(temp_prefix);
  ctx.RegisterValueType(check.result, bool_type);
  parts.push_back(MakeIR(std::move(check)));

  return SeqIR(std::move(parts));
}

}  // namespace

// =============================================================================
// Section 6.5 LowerLoopIter - lower iterator loop expression
// =============================================================================
//
// Per (Lower-Loop-Iter):
//   Gamma |- LowerExpr(iter) => <IR_i, v_iter>
//   Gamma |- LowerBlock(body) => <IR_b, v_b>
//   ---
//   Gamma |- LowerLoop(LoopIter(pat, ty_opt, iter, body)) =>
//       <LoopIR(LoopIter, pat, ty_opt, IR_i, v_iter, IR_b, v_b), v_loop>
//
// The iterator loop is lowered to:
// 1. Push a loop scope for break/continue cleanup tracking
// 2. Lower the iterator expression
// 3. Determine the pattern type from the iterator's element type
// 4. Register pattern bindings with appropriate provenance
// 5. Lower the loop body
// 6. Pop the loop scope
// 7. Create IRLoop with kind Iter
// =============================================================================

LowerResult LowerLoopIter(const ast::Expr& expr,
                          const ast::LoopIterExpr& loop_expr,
                          LowerCtx& ctx) {
  SPEC_RULE("Lower-Loop-Iter");

  // Push a loop scope for break/continue cleanup tracking
  ctx.PushScope(true, false);

  // Lower the iterator expression
  auto iter_result = LowerExpr(*loop_expr.iter, ctx);

  // Determine the pattern type from the iterator's element type
  analysis::TypeRef pattern_type;
  if (ctx.expr_type) {
    pattern_type = LoopPatternType(ctx.expr_type(*loop_expr.iter));
  }

  // Compute provenance for the loop binding
  const ProvInfo iter_prov = ExprProvInfo(*loop_expr.iter, ctx);
  const ProvInfo bind_prov = StableRegionProv(BindProvInfo(iter_prov), ctx);

  // Register pattern bindings with the computed provenance
  RegisterPatternBindings(*loop_expr.pattern, pattern_type, ctx, false,
                          bind_prov.kind, bind_prov.region,
                          bind_prov.region_tag);

  // Lower the body
  LowerResult body_result = LowerBlock(*loop_expr.body, ctx);

  if (loop_expr.invariant_opt.has_value()) {
    IRPtr maintenance_check =
        EmitLoopInvariantCheck(*loop_expr.invariant_opt, "for_inv_maint", ctx);
    if (!IsNoOpIR(maintenance_check)) {
      body_result.ir = SeqIR({body_result.ir, maintenance_check});
    }
  }

  IRPatternPtr loop_pattern = LowerIRPattern(*loop_expr.pattern, ctx);

  // Pop the loop scope
  ctx.PopScope();

  // Create iter loop IR
  IRLoop loop;
  loop.kind = IRLoopKind::Iter;
  loop.pattern = loop_pattern;
  loop.iter_ir = iter_result.ir;
  loop.iter_value = iter_result.value;
  loop.body_ir = body_result.ir;
  loop.body_value = body_result.value;

  IRValue result = ctx.FreshTempValue("for");
  if (ctx.expr_type) {
    if (analysis::TypeRef result_type = ctx.expr_type(expr)) {
      ctx.RegisterValueType(result, result_type);
    }
  }
  loop.result = result;
  IRPtr loop_ir = MakeIR(std::move(loop));

  if (loop_expr.invariant_opt.has_value()) {
    IRPtr init_check =
        EmitLoopInvariantCheck(*loop_expr.invariant_opt, "for_inv_init", ctx);
    if (!IsNoOpIR(init_check)) {
      return LowerResult{SeqIR({init_check, loop_ir}), result};
    }
  }

  return LowerResult{loop_ir, result};
}

}  // namespace ultraviolet::codegen
