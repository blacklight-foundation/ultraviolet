// =============================================================================
// MIGRATION MAPPING: tuples.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
// - Section 5.2.5 "Tuples" (lines 8874-8920)
//   - T-Unit-Literal (lines 8876-8878)
//   - T-Tuple-Literal (lines 8880-8883)
//   - T-Tuple-Index (lines 8885-8888)
//   - T-Tuple-Index-Perm (lines 8890-8893)
//   - P-Tuple-Index (lines 8895-8898)
//   - P-Tuple-Index-Perm (lines 8900-8903)
//   - ConstTupleIndex (line 8905)
//   - TupleIndex-NonConst (lines 8907-8910)
//   - TupleIndex-OOB (lines 8912-8915)
//   - TupleAccess-NotTuple (lines 8917-8920)
// - Section 5.11 "Foundational Predicates" for BitcopyType
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/composite/tuples.cpp
// - Lines 1-421 (entire file)
//
// Key source functions to migrate:
// - TypeTupleExpr (lines 278-304): Tuple literal typing
// - TypeTupleAccessValue (lines 306-366): Tuple element access (value context)
// - TypeTupleAccessPlace (lines 368-418): Tuple element access (place context)
//
// Supporting helpers:
// - Tuple alias normalization
// - Canonical StripPerm and BitcopyType predicates from type_predicates
//
// DEPENDENCIES:
// - ultraviolet/src/04_analysis/resolve/scopes.h (ScopeContext)
// - ultraviolet/src/00_core/int128.h (UInt128 operations)
// - ultraviolet/src/00_core/assert_spec.h (SPEC_DEF, SPEC_RULE)
//
// REFACTORING NOTES:
// 1. Tuple index must be a compile-time constant (statically resolved)
// =============================================================================

#include "04_analysis/composite/tuples.h"

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/int128.h"
#include "00_core/numeric_literals.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_predicates.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsTuples() {
  SPEC_DEF("ConstTupleIndex", "5.2.5");
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

static AliasExpandResult NormalizeTupleBaseType(const ScopeContext& ctx,
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

ExprTypeResult TypeTupleExpr(const ScopeContext& ctx,
                             const ast::TupleExpr& expr,
                             const ExprTypeFn& type_expr) {
  SpecDefsTuples();
  (void)ctx;
  ExprTypeResult result;
  if (expr.elements.empty()) {
    SPEC_RULE("T-Unit-Literal");
    result.ok = true;
    result.type = MakeTypePrim("()");
    return result;
  }
  std::vector<TypeRef> elements;
  elements.reserve(expr.elements.size());
  for (const auto& element : expr.elements) {
    const auto elem_type = type_expr(element);
    if (!elem_type.ok) {
      result.diag_id = elem_type.diag_id;
      return result;
    }
    elements.push_back(elem_type.type);
  }
  SPEC_RULE("T-Tuple-Literal");
  result.ok = true;
  result.type = MakeTypeTuple(std::move(elements));
  return result;
}

ExprTypeResult TypeTupleAccessValue(const ScopeContext& ctx,
                                    const ast::TupleAccessExpr& expr,
                                    const ExprTypeFn& type_expr) {
  SpecDefsTuples();
  ExprTypeResult result;
  if (!expr.base) {
    return result;
  }

  const auto base_type = type_expr(expr.base);
  if (!base_type.ok) {
    result.diag_id = base_type.diag_id;
    return result;
  }
  const auto stripped = StripPerm(base_type.type);
  const auto normalized = NormalizeTupleBaseType(ctx, stripped);
  if (!normalized.ok) {
    result.diag_id = normalized.diag_id;
    return result;
  }
  const auto* tuple =
      normalized.type ? std::get_if<TypeTuple>(&normalized.type->node) : nullptr;
  if (!tuple) {
    if (normalized.type && std::holds_alternative<TypeUnion>(normalized.type->node)) {
      SPEC_RULE("Union-DirectAccess-Err");
      result.diag_id = "Union-DirectAccess-Err";
      return result;
    }
    SPEC_RULE("TupleAccess-NotTuple");
    result.diag_id = "TupleAccess-NotTuple";
    return result;
  }

  const auto index = ast::TupleIndexToSize(expr.index);
  if (!index.has_value() || *index >= tuple->elements.size()) {
    SPEC_RULE("TupleIndex-OOB");
    result.diag_id = "TupleIndex-OOB";
    return result;
  }

  const auto element = tuple->elements[*index];
  const auto* perm = std::get_if<TypePerm>(&base_type.type->node);
  TypeRef out_type = element;
  if (perm) {
    out_type = MakeTypePerm(perm->perm, element);
    if (!BitcopyType(ctx, out_type)) {
      result.diag_id = "ValueUse-NonBitcopyPlace";
      return result;
    }
    SPEC_RULE("T-Tuple-Index-Perm");
  } else {
    if (!BitcopyType(ctx, out_type)) {
      result.diag_id = "ValueUse-NonBitcopyPlace";
      return result;
    }
    SPEC_RULE("T-Tuple-Index");
  }

  result.ok = true;
  result.type = std::move(out_type);
  return result;
}

PlaceTypeResult TypeTupleAccessPlace(const ScopeContext& ctx,
                                     const ast::TupleAccessExpr& expr,
                                     const PlaceTypeFn& type_place) {
  SpecDefsTuples();
  (void)ctx;
  PlaceTypeResult result;
  if (!expr.base) {
    return result;
  }

  const auto base_type = type_place(expr.base);
  if (!base_type.ok) {
    result.diag_id = base_type.diag_id;
    return result;
  }
  const auto stripped = StripPerm(base_type.type);
  const auto normalized = NormalizeTupleBaseType(ctx, stripped);
  if (!normalized.ok) {
    result.diag_id = normalized.diag_id;
    return result;
  }
  const auto* tuple =
      normalized.type ? std::get_if<TypeTuple>(&normalized.type->node) : nullptr;
  if (!tuple) {
    if (normalized.type && std::holds_alternative<TypeUnion>(normalized.type->node)) {
      SPEC_RULE("Union-DirectAccess-Err");
      result.diag_id = "Union-DirectAccess-Err";
      return result;
    }
    SPEC_RULE("TupleAccess-NotTuple");
    result.diag_id = "TupleAccess-NotTuple";
    return result;
  }

  const auto index = ast::TupleIndexToSize(expr.index);
  if (!index.has_value() || *index >= tuple->elements.size()) {
    SPEC_RULE("TupleIndex-OOB");
    result.diag_id = "TupleIndex-OOB";
    return result;
  }

  const auto element = tuple->elements[*index];
  const auto* perm = std::get_if<TypePerm>(&base_type.type->node);
  if (perm) {
    SPEC_RULE("P-Tuple-Index-Perm");
    result.type = MakeTypePerm(perm->perm, element);
  } else {
    SPEC_RULE("P-Tuple-Index");
    result.type = element;
  }
  result.ok = true;
  return result;
}

}  // namespace ultraviolet::analysis
