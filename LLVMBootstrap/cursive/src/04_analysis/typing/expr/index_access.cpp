// =============================================================================
// File: 04_analysis/typing/expr/index_access.cpp
// Index Access Expression Typing
// Spec Section: 5.2.6
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.2.6: Arrays and Slices (lines 8922-9055)
//   - T-Index-Array (lines 8931-8934): Array indexing with const index
//   - T-Index-Array-Dynamic (lines 8936-8939): Dynamic array indexing
//   - T-Index-Array-Perm (lines 8941-8944): Permission-qualified array
//   - T-Index-Slice (lines 8961-8964): Slice indexing
//   - T-Slice-From-Array (lines 8971-8974): Range indexing produces slice
//
// =============================================================================

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "00_core/assert_spec.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/memory/calls.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {
ExprTypeResult TypeExpr(const ScopeContext& ctx,
                        const StmtTypeContext& type_ctx,
                        const ast::ExprPtr& expr,
                        const TypeEnv& env);
PlaceTypeResult TypePlace(const ScopeContext& ctx,
                          const StmtTypeContext& type_ctx,
                          const ast::ExprPtr& expr,
                          const TypeEnv& env);
}

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsIndexAccess() {
  SPEC_DEF("T-Index-Array", "5.2.6");
  SPEC_DEF("T-Index-Array-Dynamic", "5.2.6");
  SPEC_DEF("T-Index-Array-Perm", "5.2.6");
  SPEC_DEF("T-Index-Array-Perm-Dynamic", "5.2.6");
  SPEC_DEF("T-Index-Slice", "5.2.6");
  SPEC_DEF("T-Index-Slice-Perm", "5.2.6");
  SPEC_DEF("T-Slice-From-Array", "5.2.6");
  SPEC_DEF("T-Slice-From-Array-Perm", "5.2.6");
  SPEC_DEF("T-Slice-From-Slice", "5.2.6");
  SPEC_DEF("T-Slice-From-Slice-Perm", "5.2.6");
  SPEC_DEF("P-Index-Array", "5.2.6");
  SPEC_DEF("P-Index-Array-Perm", "5.2.6");
  SPEC_DEF("P-Index-Array-Dynamic", "5.2.6");
  SPEC_DEF("P-Index-Array-Perm-Dynamic", "5.2.6");
  SPEC_DEF("P-Index-Slice", "5.2.6");
  SPEC_DEF("P-Index-Slice-Perm", "5.2.6");
  SPEC_DEF("P-Slice-From-Array", "5.2.6");
  SPEC_DEF("P-Slice-From-Array-Perm", "5.2.6");
  SPEC_DEF("P-Slice-From-Slice", "5.2.6");
  SPEC_DEF("P-Slice-From-Slice-Perm", "5.2.6");
  SPEC_DEF("Index-Array-OOB-Err", "5.2.6");
  SPEC_DEF("Index-Array-NonConst-Err", "5.2.6");
  SPEC_DEF("Index-Array-NonUsize", "5.2.6");
  SPEC_DEF("Index-Slice-NonUsize", "5.2.6");
  SPEC_DEF("Index-NonIndexable", "5.2.6");
  SPEC_DEF("Coerce-Array-Slice", "5.2.6");
}

// Strip permission qualifiers
static TypeRef StripPermLocal(const TypeRef& type) {
  if (!type) {
    return type;
  }
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

static std::optional<core::Span> ExprSpan(const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  return expr->span;
}

static std::string NonConstArrayIndexDetail() {
  return "fixed-size array index expression is not compile-time constant; "
         "runtime fixed-array indexing requires [[dynamic]], or use a slice "
         "for runtime indexing";
}

// Extract permission if present
static std::optional<Permission> ExtractPerm(const TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
  if (const auto* perm = std::get_if<TypePerm>(&type->node)) {
    return perm->perm;
  }
  return std::nullopt;
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

static AliasExpandResult NormalizeIndexBaseAlias(const ScopeContext& ctx,
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

// Type index access expression (base[index])
ExprTypeResult TypeIndexAccessExpr(const ScopeContext& ctx,
                                    const StmtTypeContext& type_ctx,
                                    const ast::IndexAccessExpr& expr,
                                    const TypeEnv& env,
                                    const ExprTypeFn& type_expr,
                                    const PlaceTypeFn& type_place,
                                    const IdentTypeFn& type_ident) {
  SpecDefsIndexAccess();
  ExprTypeResult result;

  if (!expr.base || !expr.index) {
    return result;
  }

  // 1. Type the base expression
  const auto base_type = ::cursive::analysis::TypeExpr(
      ctx, SuppressSharedAccessCheck(type_ctx), expr.base, env);
  if (!base_type.ok) {
    result.diag_id = base_type.diag_id;
    result.diag_detail = base_type.diag_detail;
    result.diag_span = base_type.diag_span.has_value()
                           ? base_type.diag_span
                           : ExprSpan(expr.base);
    return result;
  }

  // Extract permission and strip to find base type
  const auto perm = ExtractPerm(base_type.type);
  const auto stripped = StripPermLocal(base_type.type);
  const auto normalized = NormalizeIndexBaseAlias(ctx, stripped);
  if (!normalized.ok) {
    result.diag_id = normalized.diag_id;
    result.diag_span = ExprSpan(expr.base);
    return result;
  }
  const auto stripped_base = normalized.type;

  if (!stripped_base) {
    result.diag_id = "Index-NonIndexable";
    result.diag_span = ExprSpan(expr.base);
    return result;
  }

  // 2. Check index against expected usize first (typed-context checking).
  // This enables context-driven integer literal typing (e.g. arr[0]).
  const auto index_check = CheckExpr(
      ctx, expr.index, MakeTypePrim("usize"), type_expr, type_place, type_ident);

  // 3. If usize check failed, this may still be range indexing.
  if (!index_check.ok) {
    const auto index_type = type_expr(expr.index);
    if (!index_type.ok) {
      result.diag_id = index_type.diag_id;
      result.diag_detail = index_type.diag_detail;
      result.diag_span = index_type.diag_span.has_value()
                             ? index_type.diag_span
                             : ExprSpan(expr.index);
      return result;
    }
    const bool base_is_slice =
        stripped_base && std::holds_alternative<TypeSlice>(stripped_base->node);
    const std::string_view non_usize_diag =
        base_is_slice ? "Index-Slice-NonUsize" : "Index-Array-NonUsize";

    if (!::cursive::analysis::IsRangeType(index_type.type) ||
        !::cursive::analysis::IsRangeIndexType(index_type.type)) {
      if (base_is_slice) {
        SPEC_RULE("Index-Slice-NonUsize");
      } else {
        SPEC_RULE("Index-Array-NonUsize");
      }
      result.diag_id = non_usize_diag;
      result.diag_span = ExprSpan(expr.index);
      return result;
    }

    // Range indexing produces a slice
    if (const auto* arr = std::get_if<TypeArray>(&stripped_base->node)) {
      TypeRef out_type = MakeTypeSlice(arr->element);
      if (perm.has_value()) {
        out_type = MakeTypePerm(*perm, out_type);
        if (!BitcopyType(ctx, out_type)) {
          result.diag_id = "ValueUse-NonBitcopyPlace";
          return result;
        }
        SPEC_RULE("T-Slice-From-Array-Perm");
        result.ok = true;
        result.type = out_type;
      } else {
        if (!BitcopyType(ctx, out_type)) {
          result.diag_id = "ValueUse-NonBitcopyPlace";
          return result;
        }
        SPEC_RULE("T-Slice-From-Array");
        result.ok = true;
        result.type = out_type;
      }
      return result;
    }

    if (const auto* slice = std::get_if<TypeSlice>(&stripped_base->node)) {
      TypeRef out_type = MakeTypeSlice(slice->element);
      if (perm.has_value()) {
        out_type = MakeTypePerm(*perm, out_type);
        if (!BitcopyType(ctx, out_type)) {
          result.diag_id = "ValueUse-NonBitcopyPlace";
          return result;
        }
        SPEC_RULE("T-Slice-From-Slice-Perm");
        result.ok = true;
        result.type = out_type;
      } else {
        if (!BitcopyType(ctx, out_type)) {
          result.diag_id = "ValueUse-NonBitcopyPlace";
          return result;
        }
        SPEC_RULE("T-Slice-From-Slice");
        result.ok = true;
        result.type = out_type;
      }
      return result;
    }

    result.diag_id = "Index-NonIndexable";
    result.diag_span = ExprSpan(expr.base);
    return result;
  }

  // 4. Handle array indexing
  if (const auto* arr = std::get_if<TypeArray>(&stripped_base->node)) {
    const auto index_const = ConstLen(ctx, expr.index);
    const bool has_const_index = index_const.ok && index_const.value.has_value();
    if (!has_const_index && !type_ctx.contract_dynamic) {
      SPEC_RULE("Index-Array-NonConst-Err");
      result.diag_id = "E-UNS-0102";
      result.diag_detail = NonConstArrayIndexDetail();
      result.diag_span = ExprSpan(expr.index);
      return result;
    }
    if (has_const_index && *index_const.value >= arr->length) {
      SPEC_RULE("Index-Array-OOB-Err");
      result.diag_id = "E-UNS-0103";
      result.diag_span = ExprSpan(expr.index);
      return result;
    }

    if (perm.has_value()) {
      TypeRef out_type = MakeTypePerm(*perm, arr->element);
      if (!BitcopyType(ctx, out_type)) {
        result.diag_id = "ValueUse-NonBitcopyPlace";
        return result;
      }
      if (has_const_index) {
        SPEC_RULE("T-Index-Array-Perm");
      } else {
        SPEC_RULE("T-Index-Array-Perm-Dynamic");
      }
      result.ok = true;
      result.type = out_type;
    } else {
      if (!BitcopyType(ctx, arr->element)) {
        result.diag_id = "ValueUse-NonBitcopyPlace";
        return result;
      }
      if (has_const_index) {
        SPEC_RULE("T-Index-Array");
      } else {
        SPEC_RULE("T-Index-Array-Dynamic");
      }
      result.ok = true;
      result.type = arr->element;
    }
    return result;
  }

  // 5. Handle slice indexing
  if (const auto* slice = std::get_if<TypeSlice>(&stripped_base->node)) {
    if (perm.has_value()) {
      TypeRef out_type = MakeTypePerm(*perm, slice->element);
      if (!BitcopyType(ctx, out_type)) {
        result.diag_id = "ValueUse-NonBitcopyPlace";
        return result;
      }
      SPEC_RULE("T-Index-Slice-Perm");
      result.ok = true;
      result.type = out_type;
    } else {
      if (!BitcopyType(ctx, slice->element)) {
        result.diag_id = "ValueUse-NonBitcopyPlace";
        return result;
      }
      SPEC_RULE("T-Index-Slice");
      result.ok = true;
      result.type = slice->element;
    }
    return result;
  }

  // Not an indexable type
  SPEC_RULE("Index-NonIndexable");
  result.diag_id = "Index-NonIndexable";
  return result;
}

// Place typing for index access
PlaceTypeResult TypeIndexAccessPlace(const ScopeContext& ctx,
                                      const StmtTypeContext& type_ctx,
                                      const ast::IndexAccessExpr& expr,
                                      const TypeEnv& env,
                                      const ExprTypeFn& type_expr,
                                      const PlaceTypeFn& type_place,
                                      const IdentTypeFn& type_ident) {
  SpecDefsIndexAccess();
  (void)env;
  (void)type_ident;
  PlaceTypeResult result;

  if (!expr.base || !expr.index) {
    return result;
  }

  const auto base_type = ::cursive::analysis::TypePlace(
      ctx, SuppressSharedAccessCheck(type_ctx), expr.base, env);
  if (!base_type.ok) {
    result.diag_id = base_type.diag_id;
    result.diag_detail = base_type.diag_detail;
    result.diag_span = base_type.diag_span.has_value()
                           ? base_type.diag_span
                           : ExprSpan(expr.base);
    return result;
  }

  const auto perm = ExtractPerm(base_type.type);
  const auto stripped = StripPermLocal(base_type.type);
  const auto normalized = NormalizeIndexBaseAlias(ctx, stripped);
  if (!normalized.ok) {
    result.diag_id = normalized.diag_id;
    result.diag_span = ExprSpan(expr.base);
    return result;
  }
  const auto stripped_base = normalized.type;
  if (!stripped_base) {
    result.diag_id = "Index-NonIndexable";
    result.diag_span = ExprSpan(expr.base);
    return result;
  }

  const auto index_check = CheckExpr(
      ctx, expr.index, MakeTypePrim("usize"), type_expr, type_place, type_ident);
  if (!index_check.ok) {
    const auto index_type = type_expr(expr.index);
    if (!index_type.ok) {
      result.diag_id = index_type.diag_id;
      result.diag_detail = index_type.diag_detail;
      result.diag_span = index_type.diag_span.has_value()
                             ? index_type.diag_span
                             : ExprSpan(expr.index);
      return result;
    }
    const bool base_is_slice =
        stripped_base && std::holds_alternative<TypeSlice>(stripped_base->node);
    const std::string_view non_usize_diag =
        base_is_slice ? "Index-Slice-NonUsize" : "Index-Array-NonUsize";

    if (!::cursive::analysis::IsRangeType(index_type.type) ||
        !::cursive::analysis::IsRangeIndexType(index_type.type)) {
      if (base_is_slice) {
        SPEC_RULE("Index-Slice-NonUsize");
      } else {
        SPEC_RULE("Index-Array-NonUsize");
      }
      result.diag_id = non_usize_diag;
      result.diag_span = ExprSpan(expr.index);
      return result;
    }

    if (const auto* arr = std::get_if<TypeArray>(&stripped_base->node)) {
      if (perm.has_value()) {
        SPEC_RULE("P-Slice-From-Array-Perm");
        result.ok = true;
        result.type = MakeTypePerm(*perm, MakeTypeSlice(arr->element));
      } else {
        SPEC_RULE("P-Slice-From-Array");
        result.ok = true;
        result.type = MakeTypeSlice(arr->element);
      }
      return result;
    }

    if (const auto* slice = std::get_if<TypeSlice>(&stripped_base->node)) {
      if (perm.has_value()) {
        SPEC_RULE("P-Slice-From-Slice-Perm");
        result.ok = true;
        result.type = MakeTypePerm(*perm, MakeTypeSlice(slice->element));
      } else {
        SPEC_RULE("P-Slice-From-Slice");
        result.ok = true;
        result.type = MakeTypeSlice(slice->element);
      }
      return result;
    }

    result.diag_id = "Index-NonIndexable";
    result.diag_span = ExprSpan(expr.base);
    return result;
  }

  if (const auto* arr = std::get_if<TypeArray>(&stripped_base->node)) {
    const auto index_const = ConstLen(ctx, expr.index);
    const bool has_const_index = index_const.ok && index_const.value.has_value();
    if (!has_const_index && !type_ctx.contract_dynamic) {
      SPEC_RULE("Index-Array-NonConst-Err");
      result.diag_id = "E-UNS-0102";
      result.diag_detail = NonConstArrayIndexDetail();
      result.diag_span = ExprSpan(expr.index);
      return result;
    }
    if (has_const_index && *index_const.value >= arr->length) {
      SPEC_RULE("Index-Array-OOB-Err");
      result.diag_id = "E-UNS-0103";
      result.diag_span = ExprSpan(expr.index);
      return result;
    }

    if (perm.has_value()) {
      if (has_const_index) {
        SPEC_RULE("P-Index-Array-Perm");
      } else {
        SPEC_RULE("P-Index-Array-Perm-Dynamic");
      }
      result.ok = true;
      result.type = MakeTypePerm(*perm, arr->element);
    } else {
      if (has_const_index) {
        SPEC_RULE("P-Index-Array");
      } else {
        SPEC_RULE("P-Index-Array-Dynamic");
      }
      result.ok = true;
      result.type = arr->element;
    }
    return result;
  }

  if (const auto* slice = std::get_if<TypeSlice>(&stripped_base->node)) {
    if (perm.has_value()) {
      SPEC_RULE("P-Index-Slice-Perm");
      result.ok = true;
      result.type = MakeTypePerm(*perm, slice->element);
    } else {
      SPEC_RULE("P-Index-Slice");
      result.ok = true;
      result.type = slice->element;
    }
    return result;
  }

  SPEC_RULE("Index-NonIndexable");
  result.diag_id = "Index-NonIndexable";
  return result;
}

}  // namespace cursive::analysis::expr
