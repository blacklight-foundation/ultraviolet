// =============================================================================
// MIGRATION MAPPING: arrays_slices.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
// - Section 5.2.6 "Arrays and Slices" (lines 8922-9054)
//   - Coerce-Array-Slice (lines 9041-9044)
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/composite/arrays_slices.cpp
// - Lines 1-557 (entire file)
//
// Key source function migrated:
// - CoerceArrayToSlice (lines 533-554): Array to slice coercion
//
// Supporting helpers:
// - Type alias expansion helpers used to normalize array aliases before coercion
//
// DEPENDENCIES:
// - ultraviolet/include/04_analysis/typing/context.h (ScopeContext)
// - ultraviolet/include/00_core/assert_spec.h (SPEC_DEF, SPEC_RULE)
//
// =============================================================================

#include "04_analysis/composite/arrays_slices.h"

#include <optional>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_lower.h"

namespace ultraviolet::analysis {

namespace {

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

static AliasExpandResult NormalizeIndexBaseType(const ScopeContext& ctx,
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
    out.type = expanded.type;
    out.expanded = true;
  }
  return out;
}

}  // namespace

ExprTypeResult CoerceArrayToSlice(const ScopeContext& ctx,
                                  const TypeRef& type) {
  ExprTypeResult result;
  if (!type) {
    return result;
  }
  const auto normalized = NormalizeIndexBaseType(ctx, type);
  if (!normalized.ok) {
    result.diag_id = normalized.diag_id;
    return result;
  }
  const auto* perm = normalized.type ? std::get_if<TypePerm>(&normalized.type->node) : nullptr;
  if (!perm || !perm->base) {
    const auto* array = normalized.type ? std::get_if<TypeArray>(&normalized.type->node) : nullptr;
    if (!array) {
      return result;
    }
    SPEC_RULE("Coerce-Array-Slice");
    result.ok = true;
    result.type = MakeTypePerm(Permission::Const, MakeTypeSlice(array->element));
    return result;
  }
  const auto* array = std::get_if<TypeArray>(&perm->base->node);
  if (!array) {
    return result;
  }

  SPEC_RULE("Coerce-Array-Slice");
  result.ok = true;
  result.type = MakeTypePerm(perm->perm, MakeTypeSlice(array->element));
  return result;
}

}  // namespace ultraviolet::analysis
