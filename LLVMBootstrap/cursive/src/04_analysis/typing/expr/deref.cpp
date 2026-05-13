// =================================================================
// File: 04_analysis/typing/expr/deref.cpp
// Construct: Dereference Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Deref-Ptr, T-Deref-Raw, P-Deref-Ptr, P-Deref-Raw-Imm,
//             P-Deref-Raw-Mut, Deref-Null, Deref-Expired, Deref-Raw-Unsafe
// =================================================================

#include "04_analysis/typing/expr/deref.h"

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/typecheck.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsDeref() {
  SPEC_DEF("T-Deref-Ptr", "5.2.12");
  SPEC_DEF("T-Deref-Raw", "5.2.12");
  SPEC_DEF("P-Deref-Ptr", "5.2.12");
  SPEC_DEF("P-Deref-Raw-Imm", "5.2.12");
  SPEC_DEF("P-Deref-Raw-Mut", "5.2.12");
  SPEC_DEF("Deref-Null", "5.2.12");
  SPEC_DEF("Deref-Expired", "5.2.12");
  SPEC_DEF("Deref-Raw-Unsafe", "5.2.12");
  SPEC_DEF("ValueUse-NonBitcopyPlace", "5.2.12");
  SPEC_DEF("GpuPtr-Deref-Err", "20.2.5");
}

static TypeRef StripPermDeepLocal(const TypeRef& type) {
  TypeRef cur = type;
  while (cur) {
    if (const auto* perm = std::get_if<TypePerm>(&cur->node)) {
      cur = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&cur->node)) {
      cur = refine->base;
      continue;
    }
    break;
  }
  return cur;
}

static bool IsGpuDomainType(const TypeRef& type) {
  const auto stripped = StripPermDeepLocal(type);
  if (!stripped) {
    return false;
  }
  if (const auto* dyn = std::get_if<TypeDynamic>(&stripped->node)) {
    return IsGpuDomainTypePath(dyn->path);
  }
  if (const auto* path = std::get_if<TypePathType>(&stripped->node)) {
    return IsGpuDomainTypePath(path->path);
  }
  return false;
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

static AliasExpandResult NormalizeDerefBaseType(const ScopeContext& ctx,
                                                const TypeRef& type) {
  AliasExpandResult out;
  out.type = type;
  for (int i = 0; i < 16; ++i) {
    if (!out.type) {
      return out;
    }
    const auto* path = std::get_if<TypePathType>(&out.type->node);
    if (!path) {
      return out;
    }
    const auto expanded = ExpandTypeAliasApply(ctx, *path);
    if (!expanded.ok) {
      out.ok = false;
      out.diag_id = expanded.diag_id;
      return out;
    }
    if (!expanded.expanded) {
      return out;
    }
    out.type = StripPermDeepLocal(expanded.type);
    out.expanded = true;
  }
  return out;
}

static bool IsCapturedOuterBinding(const TypeEnv& env, std::string_view name) {
  if (env.scopes.empty()) {
    return false;
  }
  const auto key = IdKeyOf(name);
  const auto& current_scope = env.scopes.back();
  if (current_scope.find(key) != current_scope.end()) {
    return false;
  }
  for (auto it = env.scopes.rbegin() + 1; it != env.scopes.rend(); ++it) {
    if (it->find(key) != it->end()) {
      return true;
    }
  }
  return false;
}

static bool IsGpuHostPointerDeref(const TypeEnv& env,
                                  const ast::ExprPtr& value) {
  if (!GpuContext(env)) {
    return false;
  }
  if (!value) {
    return false;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&value->node)) {
    return IsCapturedOuterBinding(env, ident->name);
  }
  return false;
}

}  // namespace

// (T-Deref-Ptr), (T-Deref-Raw) - Value context dereference
ExprTypeResult TypeDerefExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::DerefExpr& expr,
                                 const TypeEnv& env,
                                 const core::Span& span) {
  SpecDefsDeref();
  ExprTypeResult result;

  const auto ptr = TypeExpr(ctx, type_ctx, expr.value, env);
  if (!ptr.ok) {
    result.diag_id = ptr.diag_id;
    return result;
  }

  if (IsGpuHostPointerDeref(env, expr.value)) {
    SPEC_RULE("GpuPtr-Deref-Err");
    result.diag_id = "E-CON-0150";
    return result;
  }

  const auto stripped = StripPermDeepLocal(ptr.type);
  const auto normalized = NormalizeDerefBaseType(ctx, stripped);
  if (!normalized.ok) {
    result.diag_id = normalized.diag_id;
    return result;
  }
  const auto stripped_base = normalized.type;
  if (!stripped_base) {
    return result;
  }

  // Safe pointer dereference
  if (const auto* type_ptr = std::get_if<TypePtr>(&stripped_base->node)) {
    if (type_ptr->state == PtrState::Null) {
      SPEC_RULE("Deref-Null");
      result.diag_id = "Deref-Null";
      return result;
    }
    if (type_ptr->state == PtrState::Expired) {
      SPEC_RULE("Deref-Expired");
      result.diag_id = "Deref-Expired";
      return result;
    }
    // Value use requires Bitcopy type
    if (!BitcopyType(ctx, type_ptr->element)) {
      SPEC_RULE("ValueUse-NonBitcopyPlace");
      result.diag_id = "ValueUse-NonBitcopyPlace";
      return result;
    }
    SPEC_RULE("T-Deref-Ptr");
    result.ok = true;
    result.type = type_ptr->element;
    return result;
  }

  // Raw pointer dereference (requires unsafe)
  if (const auto* raw_ptr = std::get_if<TypeRawPtr>(&stripped_base->node)) {
    if (!IsInUnsafeSpan(ctx, span)) {
      SPEC_RULE("Deref-Raw-Unsafe");
      result.diag_id = "Deref-Raw-Unsafe";
      return result;
    }
    // Value use requires Bitcopy type
    if (!BitcopyType(ctx, raw_ptr->element)) {
      SPEC_RULE("ValueUse-NonBitcopyPlace");
      result.diag_id = "ValueUse-NonBitcopyPlace";
      return result;
    }
    SPEC_RULE("T-Deref-Raw");
    result.ok = true;
    result.type = raw_ptr->element;
    return result;
  }

  return result;
}

// (P-Deref-Ptr), (P-Deref-Raw-Imm), (P-Deref-Raw-Mut) - Place context dereference
PlaceTypeResult TypeDerefPlaceImpl(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::DerefExpr& expr,
                                   const TypeEnv& env,
                                   const core::Span& span) {
  SpecDefsDeref();
  PlaceTypeResult result;

  const auto ptr = TypeExpr(ctx, type_ctx, expr.value, env);
  if (!ptr.ok) {
    result.diag_id = ptr.diag_id;
    return result;
  }

  if (IsGpuHostPointerDeref(env, expr.value)) {
    SPEC_RULE("GpuPtr-Deref-Err");
    result.diag_id = "E-CON-0150";
    return result;
  }

  const auto stripped = StripPermDeepLocal(ptr.type);
  const auto normalized = NormalizeDerefBaseType(ctx, stripped);
  if (!normalized.ok) {
    result.diag_id = normalized.diag_id;
    return result;
  }
  const auto stripped_base = normalized.type;
  if (!stripped_base) {
    return result;
  }

  // Safe pointer place
  if (const auto* type_ptr = std::get_if<TypePtr>(&stripped_base->node)) {
    if (type_ptr->state == PtrState::Null) {
      SPEC_RULE("Deref-Null");
      result.diag_id = "Deref-Null";
      return result;
    }
    if (type_ptr->state == PtrState::Expired) {
      SPEC_RULE("Deref-Expired");
      result.diag_id = "Deref-Expired";
      return result;
    }
    SPEC_RULE("P-Deref-Ptr");
    result.ok = true;
    result.type = type_ptr->element;
    return result;
  }

  // Raw pointer place (requires unsafe)
  if (const auto* raw_ptr = std::get_if<TypeRawPtr>(&stripped_base->node)) {
    if (!IsInUnsafeSpan(ctx, span)) {
      SPEC_RULE("Deref-Raw-Unsafe");
      result.diag_id = "Deref-Raw-Unsafe";
      return result;
    }
    // *imm T produces const T place
    if (raw_ptr->qual == RawPtrQual::Imm) {
      SPEC_RULE("P-Deref-Raw-Imm");
      result.ok = true;
      result.type = MakeTypePerm(Permission::Const, raw_ptr->element);
      return result;
    }
    // *mut T produces unique T place
    SPEC_RULE("P-Deref-Raw-Mut");
    result.ok = true;
    result.type = MakeTypePerm(Permission::Unique, raw_ptr->element);
    return result;
  }

  return result;
}

}  // namespace cursive::analysis::expr
