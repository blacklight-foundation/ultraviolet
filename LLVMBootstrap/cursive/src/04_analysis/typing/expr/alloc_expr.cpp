// =================================================================
// File: 04_analysis/typing/expr/alloc_expr.cpp
// Construct: Allocation Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Alloc-Explicit, T-Alloc-Implicit, Alloc-Region-NotFound-Err,
//             Alloc-Implicit-NoRegion-Err
// =================================================================

#include "04_analysis/typing/expr/alloc_expr.h"

#include "00_core/assert_spec.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/typecheck.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsAlloc() {
  SPEC_DEF("T-Alloc-Explicit", "5.2.12");
  SPEC_DEF("T-Alloc-Implicit", "5.2.12");
  SPEC_DEF("Alloc-Region-NotFound-Err", "5.2.12");
  SPEC_DEF("Alloc-Implicit-NoRegion-Err", "5.2.12");
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

  // Explicit region allocation: region ^ value
  if (expr.region_opt.has_value()) {
    const auto binding = BindOf(env, *expr.region_opt);
    if (!binding.has_value()) {
      result.diag_id = "ResolveExpr-Ident-Err";
      return result;
    }
    if (!RegionActiveType(binding->type)) {
      SPEC_RULE("Alloc-Region-NotFound-Err");
      result.diag_id = "E-MEM-1206";
      return result;
    }
    const auto inner = TypeExpr(ctx, type_ctx, expr.value, env);
    if (!inner.ok) {
      result.diag_id = inner.diag_id;
      return result;
    }
    SPEC_RULE("T-Alloc-Explicit");
    result.ok = true;
    result.type = inner.type;
    return result;
  }

  // Implicit region allocation: ^value (inside region block)
  const auto region = InnermostActiveRegion(env);
  if (!region.has_value()) {
    SPEC_RULE("Alloc-Implicit-NoRegion-Err");
    result.diag_id = "E-MEM-3021";
    return result;
  }

  const auto inner = TypeExpr(ctx, type_ctx, expr.value, env);
  if (!inner.ok) {
    result.diag_id = inner.diag_id;
    return result;
  }
  SPEC_RULE("T-Alloc-Implicit");
  result.ok = true;
  result.type = inner.type;
  return result;
}

}  // namespace cursive::analysis::expr
