/*
 * =============================================================================
 * prov_stmt.cpp - Statement Provenance Tracking
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - CursiveSpecification.md, Section 21.4.3 "Statement Provenance" (lines 25190-25260)
 *   - CursiveSpecification.md, Section 10.5 "Memory Provenance" (lines 22510-22600)
 *   - CursiveSpecification.md, Section 6.6 "Statement Execution" (lines 16410-16500)
 *
 * DIAGNOSTIC CODES:
 *   - E-PROV-0020: Provenance invalidated before use
 *   - E-MEM-3020: Return of local provenance
 *   - E-PROV-0022: Move of expired provenance
 *
 * =============================================================================
 */

#include "04_analysis/memory/regions.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/composite/function_types.h"
#include "04_analysis/resolve/resolve_items.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_pattern.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

// =============================================================================
// Internal Provenance Types
// =============================================================================

enum class ProvKind {
  Global,
  Stack,
  Heap,
  Region,
  Bottom,
  Param,
};

struct ProvTag {
  ProvKind kind = ProvKind::Bottom;
  std::size_t scope_id = 0;
  IdKey region;
  std::size_t param_index = 0;
};

struct ProvScope {
  std::size_t id = 0;
  std::unordered_map<IdKey, ProvTag> map;
};

struct RegionEntry {
  IdKey tag;
  IdKey target;
};

struct ProvEnv {
  std::vector<ProvScope> scopes;
  std::vector<RegionEntry> regions;
  std::size_t next_scope_id = 0;
};

struct ProvFlow {
  std::vector<ProvTag> results;
  std::vector<ProvTag> breaks;
  bool break_void = false;
};

struct ProvExprResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvTag prov;
};

struct ProvStmtResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvEnv env;
  TypeEnv gamma;
  ProvFlow flow;
};

static inline void SpecDefsStmtProv() {
  SPEC_DEF("ProvStmtJudg", "5.2.17");
  SPEC_DEF("BindProv", "5.2.17");
  SPEC_DEF("AssignProvOk", "5.2.17");
}

// =============================================================================
// Provenance Tag Constructors
// =============================================================================

static ProvTag BottomTag() {
  return ProvTag{ProvKind::Bottom, 0, IdKey{}, 0};
}

static ProvTag GlobalTag() {
  return ProvTag{ProvKind::Global, 0, IdKey{}, 0};
}

static ProvTag StackTag(std::size_t scope_id) {
  return ProvTag{ProvKind::Stack, scope_id, IdKey{}, 0};
}

static ProvTag RegionTag(const IdKey& name) {
  return ProvTag{ProvKind::Region, 0, name, 0};
}

// =============================================================================
// Provenance Comparison and Merge
// =============================================================================

static bool ProvEq(const ProvTag& lhs, const ProvTag& rhs) {
  if (lhs.kind != rhs.kind) {
    return false;
  }
  switch (lhs.kind) {
    case ProvKind::Stack:
      return lhs.scope_id == rhs.scope_id;
    case ProvKind::Region:
      return lhs.region == rhs.region;
    case ProvKind::Param:
      return lhs.param_index == rhs.param_index;
    default:
      return true;
  }
}

static std::optional<std::size_t> RegionIndex(const ProvEnv& env,
                                              const IdKey& name) {
  for (std::size_t i = 0; i < env.regions.size(); ++i) {
    if (env.regions[i].tag == name) {
      return i;
    }
  }
  return std::nullopt;
}

static bool RegionNesting(const ProvEnv& env,
                          const IdKey& inner,
                          const IdKey& outer) {
  const auto inner_idx = RegionIndex(env, inner);
  const auto outer_idx = RegionIndex(env, outer);
  if (!inner_idx.has_value() || !outer_idx.has_value()) {
    return false;
  }
  return *inner_idx > *outer_idx;
}

static int ProvRank(const ProvTag& tag) {
  switch (tag.kind) {
    case ProvKind::Region: return 0;
    case ProvKind::Stack: return 1;
    case ProvKind::Heap: return 2;
    case ProvKind::Global: return 3;
    case ProvKind::Bottom: return 4;
    case ProvKind::Param: return -1;
  }
  return -1;
}

static bool ProvLeq(const ProvEnv& env, const ProvTag& lhs, const ProvTag& rhs) {
  if (ProvEq(lhs, rhs)) {
    return true;
  }
  if (lhs.kind == ProvKind::Param || rhs.kind == ProvKind::Param) {
    return false;
  }
  if (lhs.kind == ProvKind::Region && rhs.kind == ProvKind::Region) {
    return RegionNesting(env, lhs.region, rhs.region);
  }
  const int lhs_rank = ProvRank(lhs);
  const int rhs_rank = ProvRank(rhs);
  if (lhs_rank < 0 || rhs_rank < 0) {
    return false;
  }
  return lhs_rank < rhs_rank;
}

static bool ProvLess(const ProvEnv& env, const ProvTag& lhs, const ProvTag& rhs) {
  if (lhs.kind == ProvKind::Param || rhs.kind == ProvKind::Param) {
    return false;
  }
  if (lhs.kind == ProvKind::Region && rhs.kind == ProvKind::Region) {
    return RegionNesting(env, lhs.region, rhs.region);
  }
  if (lhs.kind == ProvKind::Region && rhs.kind == ProvKind::Stack) {
    return true;
  }
  if (lhs.kind == ProvKind::Stack && rhs.kind == ProvKind::Heap) {
    return true;
  }
  if (lhs.kind == ProvKind::Heap && rhs.kind == ProvKind::Global) {
    return true;
  }
  if (lhs.kind == ProvKind::Global && rhs.kind == ProvKind::Bottom) {
    return true;
  }
  return false;
}

// =============================================================================
// Environment Operations
// =============================================================================

static std::optional<ProvTag> Lookup_pi(const ProvEnv& env, std::string_view name) {
  const auto key = IdKeyOf(name);
  for (auto it = env.scopes.rbegin(); it != env.scopes.rend(); ++it) {
    const auto found = it->map.find(key);
    if (found != it->map.end()) {
      return found->second;
    }
  }
  return std::nullopt;
}

static ProvEnv Intro_pi(const ProvEnv& env, std::string_view name,
                        const ProvTag& tag) {
  ProvEnv out = env;
  if (out.scopes.empty()) {
    return out;
  }
  out.scopes.back().map[IdKeyOf(name)] = tag;
  return out;
}

static ProvEnv ShadowIntro_pi(const ProvEnv& env, std::string_view name,
                              const ProvTag& tag) {
  ProvEnv out = env;
  const auto key = IdKeyOf(name);
  for (auto it = out.scopes.rbegin(); it != out.scopes.rend(); ++it) {
    const auto found = it->map.find(key);
    if (found != it->map.end()) {
      found->second = tag;
      return out;
    }
  }
  if (!out.scopes.empty()) {
    out.scopes.back().map[key] = tag;
  }
  return out;
}

static ProvEnv IntroAll_pi(const ProvEnv& env,
                           const std::vector<std::string>& names,
                           const ProvTag& tag) {
  ProvEnv current = env;
  for (const auto& name : names) {
    current = Intro_pi(current, name, tag);
  }
  return current;
}

static ProvEnv ShadowAll_pi(const ProvEnv& env,
                            const std::vector<std::string>& names,
                            const ProvTag& tag) {
  ProvEnv current = env;
  for (const auto& name : names) {
    current = ShadowIntro_pi(current, name, tag);
  }
  return current;
}

static ProvTag StackProv(const ProvEnv& env) {
  if (env.scopes.empty()) {
    return BottomTag();
  }
  return StackTag(env.scopes.back().id);
}

static ProvTag BindingSeedTag(const ProvEnv& env, const TypeBinding& binding) {
  switch (binding.provenance_kind) {
    case BindingProvenanceSeedKind::Global:
      return GlobalTag();
    case BindingProvenanceSeedKind::Stack:
      return StackProv(env);
    case BindingProvenanceSeedKind::Heap:
      return ProvTag{ProvKind::Heap, 0, IdKey{}, 0};
    case BindingProvenanceSeedKind::Region:
      if (binding.provenance_region.has_value()) {
        return RegionTag(*binding.provenance_region);
      }
      return BottomTag();
    case BindingProvenanceSeedKind::Bottom:
      return BottomTag();
    case BindingProvenanceSeedKind::Param:
      return ProvTag{ProvKind::Param, 0, IdKey{}, 0};
  }
  return BottomTag();
}

static void SeedMinimalProvEnv(const TypeEnv& gamma, ProvEnv& env) {
  env.next_scope_id = 0;

  ProvScope scope;
  scope.id = env.next_scope_id++;
  for (const auto& type_scope : gamma.scopes) {
    for (const auto& [key, bind] : type_scope) {
      if (RegionActiveType(bind.type)) {
        const auto tag = bind.provenance_region.value_or(key);
        env.regions.push_back(RegionEntry{tag, key});
        scope.map[key] = RegionTag(tag);
        continue;
      }
      scope.map[key] = BindingSeedTag(env, bind);
    }
  }
  env.scopes.push_back(std::move(scope));
}

static ProvTag BindProv(const ProvEnv& env, const ProvTag& init) {
  SpecDefsStmtProv();
  if (init.kind == ProvKind::Bottom) {
    SPEC_RULE("BindProv-Stack");
    return StackProv(env);
  }
  SPEC_RULE("BindProv-Init");
  return init;
}

static bool IsFreshRegionExpr(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          return node.path.size() == 1 && IdEq(node.path[0], "Region") &&
                 IdEq(node.name, "new_scoped");
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          if (!node.callee) {
            return false;
          }
          if (const auto* path = std::get_if<ast::PathExpr>(&node.callee->node)) {
            return path->path.size() == 1 && IdEq(path->path[0], "Region") &&
                   IdEq(path->name, "new_scoped");
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return IsFreshRegionExpr(node.expr);
        }
        return false;
      },
      expr->node);
}

// =============================================================================
// Type Lowering Helpers
// =============================================================================

static TypeRef StripPermOnce(const TypeRef& type) {
  if (!type) {
    return type;
  }
  if (const auto* perm = std::get_if<TypePerm>(&type->node)) {
    return perm->base;
  }
  if (const auto* refine = std::get_if<TypeRefine>(&type->node)) {
    return refine->base;
  }
  return type;
}

struct LocalTypeLowerResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
};

static LocalTypeLowerResult LocalLowerType(const ScopeContext& ctx,
                                           const std::shared_ptr<ast::Type>& type) {
  if (!type) {
    return {false, std::nullopt, {}};
  }
  const auto result = LowerType(ctx, type);
  return {result.ok, result.diag_id, result.type};
}

static std::vector<std::pair<std::string, TypeRef>> PatternBindings(
    const ScopeContext& ctx,
    const ast::PatternPtr& pat,
    const TypeRef& expected) {
  std::vector<std::pair<std::string, TypeRef>> bindings;
  if (!pat || !expected) {
    return bindings;
  }
  const auto typed = TypePattern(ctx, pat, expected);
  if (typed.ok) {
    bindings = typed.bindings;
    return bindings;
  }
  const auto names = PatNames(pat);
  bindings.reserve(names.size());
  for (const auto& name : names) {
    bindings.emplace_back(name, expected);
  }
  return bindings;
}

static void AddBindingsToTypeEnv(TypeEnv& env,
                                 const std::vector<std::pair<std::string, TypeRef>>& binds,
                                 ast::Mutability mut,
                                 bool shadow) {
  if (env.scopes.empty()) {
    env.scopes.emplace_back();
  }
  for (const auto& [name, type] : binds) {
    const auto key = IdKeyOf(name);
    if (shadow) {
      bool updated = false;
      for (auto it = env.scopes.rbegin(); it != env.scopes.rend(); ++it) {
        const auto found = it->find(key);
        if (found != it->end()) {
          found->second = TypeBinding{mut, type};
          updated = true;
          break;
        }
      }
      if (!updated) {
        env.scopes.back()[key] = TypeBinding{mut, type};
      }
    } else {
      env.scopes.back()[key] = TypeBinding{mut, type};
    }
  }
}

static std::optional<TypeRef> BindingType(const ScopeContext& ctx,
                                          const ast::Binding& binding,
                                          const TypeEnv& env) {
  const auto ann_type = ast::BindingAnnotationTypeOpt(binding);
  if (ann_type) {
    const auto lowered = LocalLowerType(ctx, ann_type);
    if (!lowered.ok) {
      return std::nullopt;
    }
    return lowered.type;
  }

  StmtTypeContext type_ctx;
  type_ctx.return_type = {};
  type_ctx.loop_flag = LoopFlag::None;
  type_ctx.in_unsafe = false;
  type_ctx.diags = nullptr;

  auto type_expr_fn = [&](const ast::ExprPtr& expr) {
    return TypeExpr(ctx, type_ctx, expr, env);
  };
  auto type_ident_fn = [&](std::string_view name) -> ExprTypeResult {
    return TypeIdentifierExpr(ctx, ast::IdentifierExpr{std::string(name)}, env);
  };
  auto type_place_fn = [&](const ast::ExprPtr& expr) {
    return TypePlace(ctx, type_ctx, expr, env);
  };

  const auto inferred = InferExpr(ctx, binding.init, type_expr_fn, type_place_fn, type_ident_fn);
  if (!inferred.ok) {
    return std::nullopt;
  }
  return inferred.type;
}

static std::optional<TypeRef> ShadowBindingType(const ScopeContext& ctx,
                                                const ast::ExprPtr& init,
                                                const std::shared_ptr<ast::Type>& type_opt,
                                                const TypeEnv& env) {
  if (type_opt) {
    const auto lowered = LocalLowerType(ctx, type_opt);
    if (!lowered.ok) {
      return std::nullopt;
    }
    return lowered.type;
  }

  StmtTypeContext type_ctx;
  type_ctx.return_type = {};
  type_ctx.loop_flag = LoopFlag::None;
  type_ctx.in_unsafe = false;
  type_ctx.diags = nullptr;

  auto type_expr_fn = [&](const ast::ExprPtr& expr) {
    return TypeExpr(ctx, type_ctx, expr, env);
  };
  auto type_ident_fn = [&](std::string_view name) -> ExprTypeResult {
    return TypeIdentifierExpr(ctx, ast::IdentifierExpr{std::string(name)}, env);
  };
  auto type_place_fn = [&](const ast::ExprPtr& expr) {
    return TypePlace(ctx, type_ctx, expr, env);
  };

  const auto inferred = InferExpr(ctx, init, type_expr_fn, type_place_fn, type_ident_fn);
  if (!inferred.ok) {
    return std::nullopt;
  }
  return inferred.type;
}

// =============================================================================
// Forward Declarations for Mutual Recursion
// =============================================================================

static ProvExprResult ProvExpr(const ScopeContext& ctx,
                               const ast::ExprPtr& expr,
                               const ProvEnv& env,
                               const TypeEnv& gamma);

static ProvExprResult ProvPlace(const ScopeContext& ctx,
                                const ast::ExprPtr& place,
                                const ProvEnv& env,
                                const TypeEnv& gamma);

// =============================================================================
// Simplified Expression Provenance (for statement analysis)
// =============================================================================

static ProvExprResult ProvPlace(const ScopeContext& ctx,
                                const ast::ExprPtr& place,
                                const ProvEnv& env,
                                const TypeEnv& gamma) {
  (void)ctx;
  (void)gamma;
  ProvExprResult result;
  if (!place) {
    result.ok = true;
    result.prov = BottomTag();
    return result;
  }

  return std::visit(
      [&](const auto& node) -> ProvExprResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          const auto lookup = Lookup_pi(env, node.name);
          result.ok = true;
          result.prov = lookup.value_or(BottomTag());
          return result;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ProvPlace(ctx, node.base, env, gamma);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return ProvPlace(ctx, node.base, env, gamma);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return ProvPlace(ctx, node.base, env, gamma);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return ProvExpr(ctx, node.value, env, gamma);
        } else {
          ProvExprResult out;
          out.ok = true;
          out.prov = BottomTag();
          return out;
        }
      },
      place->node);
}

static ProvExprResult ProvExpr(const ScopeContext& ctx,
                               const ast::ExprPtr& expr,
                               const ProvEnv& env,
                               const TypeEnv& gamma) {
  ProvExprResult result;
  if (!expr) {
    result.ok = true;
    result.prov = BottomTag();
    return result;
  }

  // For simplicity, delegate to place provenance if it's a place
  if (IsPlaceExpr(expr)) {
    return ProvPlace(ctx, expr, env, gamma);
  }

  // For other expressions, return bottom
  result.ok = true;
  result.prov = BottomTag();
  return result;
}

}  // namespace

// =============================================================================
// Public API: Statement Provenance Tracking
// =============================================================================

ProvStmtTrackResult TrackBindingProvenance(const ScopeContext& ctx,
                                            const ast::Binding& binding,
                                            const TypeEnv& gamma) {
  SpecDefsStmtProv();

  ProvStmtTrackResult result;
  const auto init_prov = TrackExprProvenance(ctx, binding.init, gamma);
  result.ok = init_prov.ok;
  result.diag_id = init_prov.diag_id;
  result.span = init_prov.span;
  if (!result.ok) {
    return result;
  }

  const auto lowered_type = BindingType(ctx, binding, gamma);
  const auto names = PatNames(binding.pat);
  const bool fresh_region_expr =
      lowered_type.has_value() && RegionActiveType(*lowered_type) &&
      IsFreshRegionExpr(binding.init) && names.size() == 1;
  if (fresh_region_expr) {
    result.kind = ProvenanceKind::Region;
    result.region = names.front();
    result.region_target = names.front();
    result.fresh_region = true;
    SPEC_RULE("Prov-LetVar");
    return result;
  }

  switch (init_prov.kind) {
    case ProvenanceKind::Global:
      result.kind = ProvenanceKind::Global;
      break;
    case ProvenanceKind::Stack:
      result.kind = ProvenanceKind::Stack;
      break;
    case ProvenanceKind::Heap:
      result.kind = ProvenanceKind::Heap;
      break;
    case ProvenanceKind::Region:
      result.kind = ProvenanceKind::Region;
      result.region = init_prov.region;
      result.region_target = init_prov.region_target;
      break;
    case ProvenanceKind::Bottom:
      result.kind = ProvenanceKind::Bottom;
      break;
    case ProvenanceKind::Param:
      result.kind = ProvenanceKind::Param;
      break;
  }

  SPEC_RULE("Prov-LetVar");
  return result;
}

ProvAssignCheckResult TrackAssignmentProvenance(const ScopeContext& ctx,
                                                 const ast::ExprPtr& place,
                                                 const ast::ExprPtr& value,
                                                 const TypeEnv& gamma) {
  SpecDefsStmtProv();

  ProvEnv env;
  SeedMinimalProvEnv(gamma, env);

  // Track provenance of place and value
  const auto place_prov = ProvPlace(ctx, place, env, gamma);
  const auto value_prov = ProvExpr(ctx, value, env, gamma);

  ProvAssignCheckResult result;
  result.ok = place_prov.ok && value_prov.ok;

  if (!result.ok) {
    result.diag_id = place_prov.ok ? value_prov.diag_id : place_prov.diag_id;
    result.span = place_prov.ok ? value_prov.span : place_prov.span;
    return result;
  }

  // Check if value provenance can escape to place
  if (ProvLess(env, value_prov.prov, place_prov.prov)) {
    SPEC_RULE("Prov-Escape-Err");
    result.ok = false;
    result.diag_id = "E-MEM-3020";
    result.escapes = true;
    return result;
  }

  SPEC_RULE("Prov-Assign");
  result.escapes = false;
  return result;
}

ProvReturnCheckResult TrackReturnProvenance(const ScopeContext& ctx,
                                             const ast::ExprPtr& value,
                                             const TypeEnv& gamma) {
  SpecDefsStmtProv();

  ProvReturnCheckResult result;

  if (!value) {
    // Unit return - no provenance concerns
    SPEC_RULE("Prov-Return-Unit");
    result.ok = true;
    result.kind = ProvenanceKind::Bottom;
    return result;
  }

  const auto value_prov = TrackExprProvenance(ctx, value, gamma);
  result.ok = value_prov.ok;
  result.diag_id = value_prov.diag_id;
  result.span = value_prov.span;
  if (!result.ok) {
    return result;
  }

  switch (value_prov.kind) {
    case ProvenanceKind::Global:
      result.kind = ProvenanceKind::Global;
      break;
    case ProvenanceKind::Stack:
      result.kind = ProvenanceKind::Stack;
      break;
    case ProvenanceKind::Heap:
      result.kind = ProvenanceKind::Heap;
      break;
    case ProvenanceKind::Region:
      result.kind = ProvenanceKind::Region;
      result.region = value_prov.region;
      result.region_target = value_prov.region_target;
      break;
    case ProvenanceKind::Bottom:
      result.kind = ProvenanceKind::Bottom;
      break;
    case ProvenanceKind::Param:
      result.kind = ProvenanceKind::Param;
      break;
  }

  // Check if return value has local provenance that would escape
  if (value_prov.kind == ProvenanceKind::Stack ||
      value_prov.kind == ProvenanceKind::Region) {
    // Local or region provenance - may be problematic
    // The actual escape check depends on the function's return type
    // and is handled at a higher level
    SPEC_RULE("Prov-Return-Local-Warning");
  }

  SPEC_RULE("Prov-Return");
  return result;
}

}  // namespace cursive::analysis

