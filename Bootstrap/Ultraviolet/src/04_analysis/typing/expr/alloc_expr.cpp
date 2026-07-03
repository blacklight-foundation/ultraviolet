// =================================================================
// File: 04_analysis/typing/expr/alloc_expr.cpp
// Construct: Region Allocation Expression Type Checking
// Spec Section: 16.8.4
// Spec Rules: T-Internal-Alloc-Explicit, T-New-CurrentRegion, Alloc-Region-NotFound-Err,
//             New-NoActiveRegion-Err
// =================================================================

#include "04_analysis/typing/expr/alloc_expr.h"

#include "00_core/assert_spec.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/typecheck.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsAlloc() {
  SPEC_DEF("T-Internal-Alloc-Explicit", "16.8.4");
  SPEC_DEF("T-New-CurrentRegion", "16.8.4");
  SPEC_DEF("Alloc-Region-NotFound-Err", "16.8.4");
  SPEC_DEF("New-NoActiveRegion-Err", "16.8.4");
}

}  // namespace

ExprTypeResult TypeAllocExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::AllocExpr& expr,
                                 const TypeEnv& env) {
  SpecDefsAlloc();
  ExprTypeResult result;

  // Allocation is impure (affects memory)
  if (type_ctx.require_pure) {
    result.diag_id = "E-SEM-2802";
    return result;
  }

  if (!expr.value) {
    return result;
  }

  // Explicit region allocation by region handle.
  if (expr.region_opt.has_value()) {
    const auto binding = BindOf(env, *expr.region_opt);
    if (!binding.has_value()) {
      result.diag_id = "ResolveExpr-Ident-Err";
      return result;
    }
    if (!RegionActiveType(binding->type)) {
      SPEC_RULE("diag.16.EffectfulCoreExpressions");
      SPEC_RULE("Alloc-Region-NotFound-Err");
      result.diag_id = "E-MEM-1206";
      return result;
    }
    const auto inner = TypeExpr(ctx, type_ctx, expr.value, env);
    if (!inner.ok) {
      result.diag_id = inner.diag_id;
      return result;
    }
    SPEC_RULE("T-Internal-Alloc-Explicit");
    SPEC_RULE("rule.16.T-Internal-Alloc-Explicit");
    result.ok = true;
    result.type = inner.type;
    return result;
  }

  const auto region = InnermostActiveRegion(env);
  if (!region.has_value()) {
    SPEC_RULE("diag.16.EffectfulCoreExpressions");
    SPEC_RULE("New-NoActiveRegion-Err");
    result.diag_id = "E-MEM-3021";
    return result;
  }

  const auto inner = TypeExpr(ctx, type_ctx, expr.value, env);
  if (!inner.ok) {
    result.diag_id = inner.diag_id;
    return result;
  }
  SPEC_RULE("T-New-CurrentRegion");
  SPEC_RULE("rule.16.T-New-CurrentRegion");
  result.ok = true;
  result.type = inner.type;
  return result;
}

}  // namespace ultraviolet::analysis::expr
