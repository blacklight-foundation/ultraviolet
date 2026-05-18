// =============================================================================
// resolve_generics.cpp - Generic Parameter Resolution
// =============================================================================
//
// SPEC REFERENCE:
//   Docs/SPECIFICATION.md §5.1.2 "Name Introduction and Shadowing" (Lines 6718-6821)
//   Docs/SPECIFICATION.md §5.1.7 "Resolution Pass" (Lines 7430-7549)
//   Docs/SPECIFICATION.md §7 "Generics and Type Parameters"
//
// SOURCE FILE:
//   Migrated from ultraviolet-bootstrap/src/03_analysis/resolve/resolver_items.cpp
//   (Lines 60-140 for generic params)
//   Migrated from ultraviolet-bootstrap/src/03_analysis/resolve/resolver_types.cpp
//   (Lines 60-100 for generic args)
//
// =============================================================================

#include "04_analysis/resolve/resolver.h"

#include "00_core/assert_spec.h"
#include "04_analysis/generics/generic_params.h"
#include "04_analysis/resolve/scopes.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsResolveGenerics() {
  SPEC_DEF("ResolveGenericParams", "5.1.7");
  SPEC_DEF("ResolveTypeParam", "5.1.7");
  SPEC_DEF("ResolveTypeBound", "5.1.7");
  SPEC_DEF("ResolveWhereClause", "5.1.7");
  SPEC_DEF("ResolveGenericArgs", "5.1.7");
  SPEC_DEF("ResolvePredicate", "5.1.7");
}

}  // namespace

// -----------------------------------------------------------------------------
// ResolveTypeBound
// -----------------------------------------------------------------------------
// Resolves a type bound: T <: ClassName
// Implements (Resolve-Type-Bound):
//   Gamma |- ResolveClassPath(class_path) => ent /\ ClassKind(ent)
//   -> Gamma |- ResolveTypeBound(T <: class_path) => ok

ResolveResult<ast::TypeBound> ResolveTypeBound(
    ResolveContext& ctx,
    const ast::TypeBound& bound) {
  SpecDefsResolveGenerics();
  ResolveResult<ast::TypeBound> result;
  result.ok = true;
  result.value = bound;

  const auto resolved = ResolveClassPath(ctx, bound.class_path);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {}};
  }

  result.value.class_path = resolved.value;
  result.value.generic_args.clear();
  result.value.generic_args.reserve(bound.generic_args.size());
  for (const auto& arg : bound.generic_args) {
    const auto resolved_arg = ResolveType(ctx, arg);
    if (!resolved_arg.ok) {
      return {false, resolved_arg.diag_id, resolved_arg.span, {}};
    }
    result.value.generic_args.push_back(resolved_arg.value);
  }
  SPEC_RULE("ResolveTypeBound");
  return result;
}

// -----------------------------------------------------------------------------
// ResolveTypeBounds
// -----------------------------------------------------------------------------
// Resolves a list of type bounds.

ResolveResult<std::vector<ast::TypeBound>> ResolveTypeBounds(
    ResolveContext& ctx,
    const std::vector<ast::TypeBound>& bounds) {
  SpecDefsResolveGenerics();
  ResolveResult<std::vector<ast::TypeBound>> result;
  result.ok = true;

  if (bounds.empty()) {
    SPEC_RULE("ResolveTypeBounds-Empty");
    return result;
  }

  result.value.reserve(bounds.size());
  for (const auto& bound : bounds) {
    const auto resolved = ResolveTypeBound(ctx, bound);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.push_back(resolved.value);
    SPEC_RULE("ResolveTypeBounds-Cons");
  }

  return result;
}

// -----------------------------------------------------------------------------
// ResolveTypeParam
// -----------------------------------------------------------------------------
// Resolves a single type parameter.
// Implements (Resolve-Type-Param):
//   - Introduces the type parameter name into scope
//   - Resolves bounds (class constraints)
//   - Resolves default type if present

ResolveResult<ast::TypeParam> ResolveTypeParam(
    ResolveContext& ctx,
    const ast::TypeParam& param) {
  SpecDefsResolveGenerics();
  ResolveResult<ast::TypeParam> result;
  result.ok = true;
  result.value = param;

  // Resolve bounds
  const auto bounds = ResolveTypeBounds(ctx, param.bounds);
  if (!bounds.ok) {
    return {false, bounds.diag_id, bounds.span, {}};
  }
  result.value.bounds = bounds.value;

  // Resolve default type if present
  if (param.default_type) {
    const auto default_type = ResolveType(ctx, param.default_type);
    if (!default_type.ok) {
      return {false, default_type.diag_id, default_type.span, {}};
    }
    result.value.default_type = default_type.value;
    SPEC_RULE("ResolveTypeParam-Default");
  }

  SPEC_RULE("ResolveTypeParam");
  return result;
}

// -----------------------------------------------------------------------------
// ResolveGenericParams
// -----------------------------------------------------------------------------
// Resolves generic parameter declarations and introduces them into scope.
// Implements (Resolve-Generic-Params):
//   forall p in params.
//     Gamma |- Intro(p.name, <Type, T, T, Decl>) => Gamma' /\
//     (p.bound != T -> Gamma' |- ResolveTypeBound(p.bound) => ok) /\
//     (p.default != T -> Gamma' |- ResolveType(p.default) => ok)
//   -> Gamma |- ResolveGenericParams(params) => ok
//
// Type parameters are introduced before resolving bounds/defaults to allow
// forward references within the same generic parameter list.

ResolveResult<ast::GenericParams> ResolveGenericParams(
    ResolveContext& ctx,
    const ast::GenericParams& params) {
  SpecDefsResolveGenerics();
  ResolveResult<ast::GenericParams> result;
  result.ok = true;
  result.value = params;
  result.value.params.clear();
  result.value.params.reserve(params.params.size());

  if (params.params.empty()) {
    SPEC_RULE("ResolveGenericParams-Empty");
    return result;
  }

  ctx.ctx->scopes = BindTypeParams(*ctx.ctx, params);
  SPEC_RULE("ResolveGenericParams-Intro");

  // Second pass: resolve bounds and defaults
  for (const auto& param : params.params) {
    const auto resolved = ResolveTypeParam(ctx, param);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.params.push_back(resolved.value);
    SPEC_RULE("ResolveGenericParams-Cons");
  }

  SPEC_RULE("ResolveGenericParams");
  return result;
}

// -----------------------------------------------------------------------------
// ResolveGenericParamsOpt
// -----------------------------------------------------------------------------
// Resolves an optional generic params block.

ResolveResult<std::optional<ast::GenericParams>> ResolveGenericParamsOpt(
    ResolveContext& ctx,
    const std::optional<ast::GenericParams>& params_opt) {
  SpecDefsResolveGenerics();
  ResolveResult<std::optional<ast::GenericParams>> result;
  result.ok = true;

  if (!params_opt.has_value()) {
    result.value = std::nullopt;
    SPEC_RULE("ResolveGenericParamsOpt-None");
    return result;
  }

  const auto resolved = ResolveGenericParams(ctx, *params_opt);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {}};
  }

  result.value = resolved.value;
  SPEC_RULE("ResolveGenericParamsOpt-Some");
  return result;
}

// -----------------------------------------------------------------------------
// ResolvePredicate
// -----------------------------------------------------------------------------
// Resolves a predicate requirement pair: PredicateReq(pred, ty).
// Implements (ResolvePredicateReq-Predicate):
//   Gamma |- ResolveType(ty) => ty'

ResolveResult<ast::PredicateReq> ResolvePredicate(
    ResolveContext& ctx,
    const ast::PredicateReq& pred) {
  SpecDefsResolveGenerics();
  ResolveResult<ast::PredicateReq> result;
  result.ok = true;
  result.value = pred;

  const auto resolved = ResolveType(ctx, pred.type);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {}};
  }
  result.value.type = resolved.value;

  SPEC_RULE("ResolvePredicate");
  return result;
}

// -----------------------------------------------------------------------------
// ResolveWhereClause
// -----------------------------------------------------------------------------
// Resolves a where clause.
// Implements (Resolve-Where-Clause):
//   forall pred in predicates.
//     pred = Bitcopy(T) -> T in dom(Gamma)
//     pred = Clone(T) -> T in dom(Gamma)
//     pred = Drop(T) -> T in dom(Gamma)
//     pred = T <: C -> Gamma |- ResolveClassPath(C) => ok
//   -> Gamma |- ResolveWhereClause(predicates) => ok

ResolveResult<ast::PredicateClause> ResolveWhereClause(
    ResolveContext& ctx,
    const ast::PredicateClause& where_clause) {
  SpecDefsResolveGenerics();
  ResolveResult<ast::PredicateClause> result;
  result.ok = true;
  result.value = where_clause;
  result.value.clear();
  result.value.reserve(where_clause.size());

  if (where_clause.empty()) {
    SPEC_RULE("ResolveWhereClause-Empty");
    return result;
  }

  for (const auto& pred : where_clause) {
    const auto resolved = ResolvePredicate(ctx, pred);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.push_back(resolved.value);
    SPEC_RULE("ResolveWhereClause-Cons");
  }

  SPEC_RULE("ResolveWhereClause");
  return result;
}

// -----------------------------------------------------------------------------
// ResolveWhereClauseOpt
// -----------------------------------------------------------------------------
// Resolves an optional where clause.

ResolveResult<std::optional<ast::PredicateClause>> ResolveWhereClauseOpt(
    ResolveContext& ctx,
    const std::optional<ast::PredicateClause>& where_opt) {
  SpecDefsResolveGenerics();
  ResolveResult<std::optional<ast::PredicateClause>> result;
  result.ok = true;

  if (!where_opt.has_value()) {
    result.value = std::nullopt;
    SPEC_RULE("ResolveWhereClauseOpt-None");
    return result;
  }

  const auto resolved = ResolveWhereClause(ctx, *where_opt);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {}};
  }

  result.value = resolved.value;
  SPEC_RULE("ResolveWhereClauseOpt-Some");
  return result;
}

// -----------------------------------------------------------------------------
// ResolveGenericArgs
// -----------------------------------------------------------------------------
// Resolves generic type arguments at instantiation site.
// Implements (Resolve-Generic-Args):
//   forall arg in args. Gamma |- ResolveType(arg) => ok
//   -> Gamma |- ResolveGenericArgs(args) => ok
//
// Note: Arity mismatch detection is deferred to type checking.

ResolveResult<ast::GenericArgs> ResolveGenericArgs(
    ResolveContext& ctx,
    const ast::GenericArgs& args) {
  SpecDefsResolveGenerics();
  ResolveResult<ast::GenericArgs> result;
  result.ok = true;
  result.value = args;
  result.value.args.clear();
  result.value.args.reserve(args.args.size());

  if (args.args.empty()) {
    SPEC_RULE("ResolveGenericArgs-Empty");
    return result;
  }

  for (const auto& arg : args.args) {
    const auto resolved = ResolveType(ctx, arg);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.args.push_back(resolved.value);
    SPEC_RULE("ResolveGenericArgs-Cons");
  }

  SPEC_RULE("ResolveGenericArgs");
  return result;
}

// -----------------------------------------------------------------------------
// ResolveGenericArgsOpt
// -----------------------------------------------------------------------------
// Resolves an optional generic args block.

ResolveResult<std::optional<ast::GenericArgs>> ResolveGenericArgsOpt(
    ResolveContext& ctx,
    const std::optional<ast::GenericArgs>& args_opt) {
  SpecDefsResolveGenerics();
  ResolveResult<std::optional<ast::GenericArgs>> result;
  result.ok = true;

  if (!args_opt.has_value()) {
    result.value = std::nullopt;
    SPEC_RULE("ResolveGenericArgsOpt-None");
    return result;
  }

  const auto resolved = ResolveGenericArgs(ctx, *args_opt);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {}};
  }

  result.value = resolved.value;
  SPEC_RULE("ResolveGenericArgsOpt-Some");
  return result;
}

}  // namespace ultraviolet::analysis
