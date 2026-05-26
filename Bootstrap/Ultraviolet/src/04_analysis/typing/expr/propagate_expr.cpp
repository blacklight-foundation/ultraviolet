// =================================================================
// File: 04_analysis/typing/expr/propagate_expr.cpp
// Construct: Propagate Expression Type Checking (?)
// Spec Section: 5.2.12
// Spec Rules: T-Propagate
// =================================================================
#include "04_analysis/typing/expr/propagate_expr.h"

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/outcome.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_stmt.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsPropagate() {
  SPEC_DEF("T-Propagate", "5.2.12");
  SPEC_DEF("T-Propagate-Outcome", "5.2.12");
  SPEC_DEF("T-Async-Try", "5.2.12");
  SPEC_DEF("T-Async-Try-Outcome", "5.2.12");
  SPEC_DEF("Async-Try-Infallible-Err", "5.2.12");
}

struct AliasExpandResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  TypeRef type = nullptr;
  bool expanded = false;
};

static const ast::TypeAliasDecl* LookupTypeAliasDecl(const ScopeContext& ctx,
                                                     const TypePath& path) {
  if (path.empty()) {
    return nullptr;
  }
  if (path.size() > 1) {
    ast::Path full;
    full.reserve(path.size());
    for (const auto& seg : path) {
      full.push_back(seg);
    }
    const auto it = ctx.sigma.types.find(PathKeyOf(full));
    if (it == ctx.sigma.types.end()) {
      return nullptr;
    }
    return std::get_if<ast::TypeAliasDecl>(&it->second);
  }

  const auto ent = ResolveTypeName(ctx, path[0]);
  if (!ent.has_value() || !ent->origin_opt.has_value()) {
    return nullptr;
  }

  ast::Path resolved = *ent->origin_opt;
  const std::string resolved_name =
      ent->target_opt.has_value() ? *ent->target_opt : path[0];
  resolved.push_back(resolved_name);
  const auto resolved_it = ctx.sigma.types.find(PathKeyOf(resolved));
  if (resolved_it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::TypeAliasDecl>(&resolved_it->second);
}

static AliasExpandResult ExpandTypeAliasApply(const ScopeContext& ctx,
                                              const TypePathType& applied) {
  AliasExpandResult result;
  const auto* alias = LookupTypeAliasDecl(ctx, applied.path);
  if (!alias) {
    return result;
  }

  const auto lowered = LowerType(ctx, alias->type);
  if (!lowered.ok) {
    result.ok = false;
    result.diag_id = lowered.diag_id;
    return result;
  }

  if (!alias->generic_params.has_value()) {
    if (!applied.generic_args.empty()) {
      return result;
    }
    result.type = lowered.type;
    result.expanded = true;
    return result;
  }

  const auto& params = alias->generic_params->params;
  if (applied.generic_args.size() > params.size()) {
    return result;
  }

  const auto subst = BuildSubstitution(params, applied.generic_args);
  result.type = InstantiateType(lowered.type, subst);
  result.expanded = result.type != nullptr;
  return result;
}

static AliasExpandResult NormalizeAliasTopLevel(const ScopeContext& ctx,
                                                const TypeRef& type) {
  AliasExpandResult out;
  out.type = type;
  for (int i = 0; i < 16; ++i) {
    if (!out.type) {
      return out;
    }
    const auto* path = AppliedTypePath(*out.type);
    const auto* args = AppliedTypeArgs(*out.type);
    if (!path || !args) {
      return out;
    }
    const auto expanded = ExpandTypeAliasApply(ctx, TypePathType{*path, *args});
    if (!expanded.ok) {
      out.ok = false;
      out.diag_id = expanded.diag_id;
      return out;
    }
    if (!expanded.expanded) {
      return out;
    }
    out.type = expanded.type;
    out.expanded = true;
  }
  return out;
}

}  // namespace

// (T-Propagate)
// The propagate operator (?) extracts the success type from a union,
// propagating failure cases to the caller via early return.
//
// Typing rule:
// Gamma |- expr : Success | Failure
// Return type includes Failure
// --------------------------------------------------
// Gamma |- expr? : Success
// (early returns Failure if not success)
ExprTypeResult TypePropagateExprImpl(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::PropagateExpr& expr,
                                     const TypeEnv& env) {
  ExprTypeResult result;

  // Type the inner expression
  const auto inner = TypeExpr(ctx, type_ctx, expr.value, env);
  if (!inner.ok) {
    result.diag_id = inner.diag_id;
    return result;
  }

  // Strip permission to get the underlying type
  const auto stripped = StripPerm(inner.type);
  if (!stripped) {
    return result;
  }
  const auto normalized = NormalizeAliasTopLevel(ctx, stripped);
  if (!normalized.ok) {
    result.diag_id = normalized.diag_id;
    return result;
  }
  const TypeRef source_type = normalized.type;
  if (!source_type) {
    return result;
  }

  // Need a return type context to check what can be propagated.
  // For async procedures, propagate (?) routes through the async error channel.
  if (!type_ctx.return_type) {
    return result;
  }

  TypeRef propagate_target = type_ctx.return_type;
  bool async_try = false;
  if (const auto async_sig = AsyncSigOf(ctx, type_ctx.return_type);
      async_sig.has_value() && IsPrimType(async_sig->err, "!")) {
    SPEC_RULE("Async-Try-Infallible-Err");
    result.diag_id = "E-CON-0230";
    return result;
  } else if (async_sig.has_value()) {
    async_try = true;
    propagate_target = async_sig->err;
  }

  if (!propagate_target) {
    return result;
  }

  if (const auto outcome_sig = OutcomeSigOf(source_type)) {
    TypeRef error_target = nullptr;
    if (async_try) {
      error_target = propagate_target;
    } else if (const auto return_outcome = OutcomeSigOf(type_ctx.return_type)) {
      error_target = return_outcome->error;
    }

    if (!error_target) {
      return result;
    }

    const auto sub = Subtyping(ctx, outcome_sig->error, error_target);
    if (!sub.ok) {
      result.diag_id = sub.diag_id;
      return result;
    }
    if (!sub.subtype) {
      return result;
    }

    SPEC_RULE(async_try ? "T-Async-Try-Outcome" : "T-Propagate-Outcome");
    result.ok = true;
    result.type = outcome_sig->value;
    return result;
  }

  const auto* union_type = std::get_if<TypeUnion>(&source_type->node);
  if (!union_type || union_type->members.empty()) {
    return result;
  }

  // Find the success type: the member that is NOT a subtype of the propagate
  // target. All other members (failures) must be subtypes of that target.
  std::optional<TypeRef> success;
  for (const auto& member : union_type->members) {
    const auto sub = Subtyping(ctx, member, propagate_target);
    if (!sub.ok) {
      result.diag_id = sub.diag_id;
      return result;
    }
    if (sub.subtype) {
      // This member can be propagated to the return type (failure case)
      continue;
    }
    // This member cannot be propagated - it's the success type
    if (success.has_value()) {
      // Multiple success types - cannot determine which to propagate
      return result;
    }
    success = member;
  }

  if (!success.has_value()) {
    // All members are subtypes of return - no success type to extract
    return result;
  }

  if (async_try) {
    SPEC_RULE("T-Async-Try");
  } else {
    SPEC_RULE("T-Propagate");
  }
  result.ok = true;
  result.type = *success;
  return result;
}

}  // namespace ultraviolet::analysis::expr
