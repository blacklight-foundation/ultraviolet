/*
 * =============================================================================
 * MIGRATION MAPPING: regions.cpp
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - CursiveSpecification.md, Section 21.1 "Region Model" (line 24725)
 *   - CursiveSpecification.md, Section 21.2 "Region Statements" (lines 24800-24900)
 *   - CursiveSpecification.md, Section 21.3 "Frame Statements" (lines 24910-25000)
 *   - CursiveSpecification.md, Section 21.4 "Provenance Tracking" (lines 25010-25200)
 *   - CursiveSpecification.md, Section 8.8 "E-REG Errors" (lines 21700-21800)
 *
 * SOURCE FILE:
 *   - cursive-bootstrap/src/03_analysis/memory/regions.cpp (lines 1-2036)
 *
 * MIGRATED FROM:
 *   - Namespace: cursive0 -> cursive
 *   - AST namespace: syntax:: -> ast::
 *   - Include paths: cursive0/... -> new structure
 *
 * DIAGNOSTIC CODES:
 *   - E-REG-0001: Allocation outside region
 *   - E-REG-0002: Pointer escapes region
 *   - E-REG-0003: Use of expired pointer
 *   - E-REG-0004: Invalid region nesting
 *   - E-REG-0005: Named region not found
 *   - W-REG-0001: Pointer may escape region
 *
 * =============================================================================
 */

#include "04_analysis/memory/regions.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostics.h"
#include "00_core/process_config.h"
#include "04_analysis/caps/cap_heap.h"
#include "04_analysis/composite/function_types.h"
#include "04_analysis/composite/record_methods.h"
#include "04_analysis/modal/builtin_modal_intrinsics.h"
#include "04_analysis/memory/string_bytes.h"
#include "04_analysis/resolve/resolve_items.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_pattern.h"

namespace cursive::analysis {

namespace {

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
  bool frame_active = false;
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

struct ExprProvInfo {
  ProvTag tag;
  std::optional<IdKey> target;
};

using ExprProvTagMap = std::unordered_map<const ast::Expr*, ExprProvInfo>;

struct ProvPerfStats {
  std::uint64_t body_calls = 0;
  std::uint64_t find_module_calls = 0;
  std::uint64_t find_module_scanned = 0;
  std::uint64_t static_binding_items_scanned = 0;
  std::uint64_t static_bindings_us = 0;
  std::uint64_t block_prov_us = 0;
};

static ProvPerfStats& RegionsPerfStats() {
  static ProvPerfStats stats;
  return stats;
}

static bool RegionsPerfEnabled() {
  return core::IsDebugEnabled("sema") || core::IsDebugEnabled("pipeline") ||
         core::IsDebugEnabled("typeperf");
}

static bool RegionsPerfActive() {
  static const bool enabled = RegionsPerfEnabled();
  return enabled;
}

class ScopedProvTimer {
 public:
  explicit ScopedProvTimer(std::uint64_t* slot)
      : slot_(slot), start_(std::chrono::steady_clock::now()) {}

  ~ScopedProvTimer() {
    if (!slot_) {
      return;
    }
    *slot_ += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_)
            .count());
  }

 private:
  std::uint64_t* slot_ = nullptr;
  std::chrono::steady_clock::time_point start_{};
};

struct ProvStmtResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvEnv env;
  TypeEnv gamma;
  ProvFlow flow;
};

struct ProvSeqResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvEnv env;
  TypeEnv gamma;
  ProvFlow flow;
};

struct BreakProvResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  std::vector<ProvTag> breaks;
  bool break_void = false;
};

static inline void SpecDefsRegions() {
  SPEC_DEF("RegionActiveType", "5.2.17");
  SPEC_DEF("FreshRegion", "5.2.17");
  SPEC_DEF("RegionOptsExpr", "5.2.17");
  SPEC_DEF("RegionBind", "5.2.17");
  SPEC_DEF("InnermostActiveRegion", "5.2.17");
  SPEC_DEF("FrameBind", "5.2.17");
  SPEC_DEF("ProvPlaceJudg", "5.2.17");
  SPEC_DEF("ProvExprJudg", "5.2.17");
  SPEC_DEF("ProvStmtJudg", "5.2.17");
  SPEC_DEF("BlockProvJudg", "5.2.17");
  SPEC_DEF("JoinProv", "5.2.17");
  SPEC_DEF("JoinAllProv", "5.2.17");
  SPEC_DEF("BindProv", "5.2.17");
  SPEC_DEF("StaticBindProv", "5.2.17");
  SPEC_DEF("AssignProvOk", "5.2.17");
  SPEC_DEF("AllocTag", "5.2.17");
  SPEC_DEF("P-Pipeline", "5.2.17");
  SPEC_DEF("Warn-Async-LargeCapture", "5.2.17");
  SPEC_DEF("Warn-Async-LargeCapture-Ok", "5.2.17");
}

struct LocalTypeLowerResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
};

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

static LocalTypeLowerResult LocalLowerType(const ScopeContext& ctx,
                                           const std::shared_ptr<ast::Type>& type) {
  if (!type) {
    return {false, std::nullopt, {}};
  }
  // Delegate to the main LowerType function
  const auto result = LowerType(ctx, type);
  return {result.ok, result.diag_id, result.type};
}

static std::optional<TypeRef> CachedExprTypeOf(const ScopeContext& ctx,
                                              const ast::ExprPtr& expr) {
  if (!expr || !ctx.expr_types) {
    return std::nullopt;
  }
  const auto it = ctx.expr_types->find(expr.get());
  if (it == ctx.expr_types->end()) {
    return std::nullopt;
  }
  if (!it->second) {
    return std::nullopt;
  }
  return it->second;
}

struct ScopedBinding {
  TypeBinding binding;
  std::size_t scope_index = 0;
};

static std::optional<ScopedBinding> LookupBindingWithScope(const TypeEnv& env,
                                                           std::string_view name) {
  const auto key = IdKeyOf(name);
  for (std::size_t i = env.scopes.size(); i-- > 0;) {
    const auto found = env.scopes[i].find(key);
    if (found != env.scopes[i].end()) {
      return ScopedBinding{found->second, i};
    }
  }
  return std::nullopt;
}

static bool IsSharedBindingType(const TypeRef& type) {
  TypeRef current = type;
  while (current) {
    if (const auto* refine = std::get_if<TypeRefine>(&current->node)) {
      current = refine->base;
      continue;
    }
    if (const auto* perm = std::get_if<TypePerm>(&current->node)) {
      return perm->perm == Permission::Shared;
    }
    return false;
  }
  return false;
}

static bool IsCapturedLocalSharedBinding(const TypeEnv& env,
                                         std::string_view name) {
  const auto binding = LookupBindingWithScope(env, name);
  if (!binding.has_value()) {
    return false;
  }
  // The provenance checker seeds scope 0 with static/module bindings and scope
  // 1 with procedure self/parameters. Deeper scopes are local block scopes.
  return binding->scope_index >= 2 && IsSharedBindingType(binding->binding.type);
}

static void ParamTypeMap(const ScopeContext& ctx,
                         const std::vector<ast::Param>& params,
                         const std::optional<BindSelfParam>& self_param,
                         TypeEnv& env) {
  TypeRef self_base;
  if (self_param.has_value()) {
    self_base = StripPermOnce(self_param->type);
    if (env.scopes.empty()) {
      env.scopes.emplace_back();
    }
    env.scopes.back()[IdKeyOf("self")] =
        TypeBinding{ast::Mutability::Let, self_param->type};
  }
  for (const auto& param : params) {
    const auto lowered = LocalLowerType(ctx, param.type);
    if (!lowered.ok) {
      continue;
    }
    TypeRef type = lowered.type;
    if (self_base) {
      type = SubstSelfType(self_base, type);
    }
    if (env.scopes.empty()) {
      env.scopes.emplace_back();
    }
    env.scopes.back()[IdKeyOf(param.name)] =
        TypeBinding{ast::Mutability::Let, type};
  }
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

static void IntroRegionAlias_pi_inplace(ProvEnv& env,
                                        const IdKey& tag,
                                        std::string_view target,
                                        bool frame_active = false) {
  env.regions.push_back(RegionEntry{tag, IdKeyOf(target), frame_active});
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

static void AddRegionBindingsToProvEnv(
    ProvEnv& env,
    const std::vector<std::pair<std::string, TypeRef>>& binds,
    const ProvTag& bind_pi,
    bool fresh_region_expr) {
  for (const auto& [name, type] : binds) {
    if (!RegionActiveType(type)) {
      continue;
    }
    if (bind_pi.kind == ProvKind::Region) {
      IntroRegionAlias_pi_inplace(env, bind_pi.region, name);
      continue;
    }
    if (fresh_region_expr) {
      const auto key = IdKeyOf(name);
      IntroRegionAlias_pi_inplace(env, key, name);
    }
  }
}

static std::optional<TypeRef> BindingType(const ScopeContext& ctx,
                                          const ast::Binding& binding,
                                          const TypeEnv& env,
                                          bool allow_type_infer = true) {
  const auto ann_type = ast::BindingAnnotationTypeOpt(binding);
  if (ann_type) {
    const auto lowered = LocalLowerType(ctx, ann_type);
    if (!lowered.ok) {
      return std::nullopt;
    }
    return lowered.type;
  }
  if (const auto cached = CachedExprTypeOf(ctx, binding.init)) {
    return cached;
  }
  if (!allow_type_infer) {
    return std::nullopt;
  }

  StmtTypeContext type_ctx;
  type_ctx.return_type = {};
  type_ctx.loop_flag = LoopFlag::None;
  type_ctx.in_unsafe = false;
  core::DiagnosticStream async_sig_diags;
  type_ctx.diags = &async_sig_diags;

  auto type_expr = [&](const ast::ExprPtr& expr) {
    return TypeExpr(ctx, type_ctx, expr, env);
  };
  auto type_ident = [&](std::string_view name) -> ExprTypeResult {
    return TypeIdentifierExpr(ctx, ast::IdentifierExpr{std::string(name)},
                              env);
  };
  auto type_place = [&](const ast::ExprPtr& expr) {
    return TypePlace(ctx, type_ctx, expr, env);
  };

  const auto inferred = InferExpr(ctx, binding.init, type_expr, type_place, type_ident);
  if (!inferred.ok) {
    return std::nullopt;
  }
  return inferred.type;
}

static std::optional<TypeRef> ShadowBindingType(const ScopeContext& ctx,
                                                const ast::ExprPtr& init,
                                                const std::shared_ptr<ast::Type>& type_opt,
                                                const TypeEnv& env,
                                                bool allow_type_infer = true) {
  if (type_opt) {
    const auto lowered = LocalLowerType(ctx, type_opt);
    if (!lowered.ok) {
      return std::nullopt;
    }
    return lowered.type;
  }
  if (const auto cached = CachedExprTypeOf(ctx, init)) {
    return cached;
  }
  if (!allow_type_infer) {
    return std::nullopt;
  }

  StmtTypeContext type_ctx;
  type_ctx.return_type = {};
  type_ctx.loop_flag = LoopFlag::None;
  type_ctx.in_unsafe = false;
  type_ctx.diags = nullptr;

  auto type_expr = [&](const ast::ExprPtr& expr) {
    return TypeExpr(ctx, type_ctx, expr, env);
  };
  auto type_ident = [&](std::string_view name) -> ExprTypeResult {
    return TypeIdentifierExpr(ctx, ast::IdentifierExpr{std::string(name)},
                              env);
  };
  auto type_place = [&](const ast::ExprPtr& expr) {
    return TypePlace(ctx, type_ctx, expr, env);
  };

  const auto inferred = InferExpr(ctx, init, type_expr, type_place, type_ident);
  if (!inferred.ok) {
    return std::nullopt;
  }
  return inferred.type;
}

struct StaticBindingInfo {
  std::string name;
  TypeRef type;
  ast::Mutability mut = ast::Mutability::Let;
};

static const ast::ASTModule* FindModuleByPath(
    const ScopeContext& ctx,
    const ast::ModulePath& path) {
  struct ModuleLookupCache {
    const ScopeContext* ctx = nullptr;
    const ast::ASTModule* mods_data = nullptr;
    std::size_t mods_size = 0;
    std::map<PathKey, const ast::ASTModule*> modules_by_path;
  };
  auto cached_modules = [&](const ScopeContext& local_ctx)
      -> const std::map<PathKey, const ast::ASTModule*>& {
    static thread_local ModuleLookupCache cache;
    if (cache.ctx == &local_ctx &&
        cache.mods_data == local_ctx.sigma.mods.data() &&
        cache.mods_size == local_ctx.sigma.mods.size()) {
      return cache.modules_by_path;
    }
    cache.ctx = &local_ctx;
    cache.mods_data = local_ctx.sigma.mods.data();
    cache.mods_size = local_ctx.sigma.mods.size();
    cache.modules_by_path.clear();
    for (const auto& module : local_ctx.sigma.mods) {
      cache.modules_by_path.emplace(PathKeyOf(module.path), &module);
    }
    return cache.modules_by_path;
  };

  auto& perf = RegionsPerfStats();
  const bool perf_on = RegionsPerfActive();
  if (perf_on) {
    ++perf.find_module_calls;
    ++perf.find_module_scanned;
  }
  const auto& modules = cached_modules(ctx);
  const auto it = modules.find(PathKeyOf(path));
  if (it != modules.end()) {
    return it->second;
  }
  return nullptr;
}

static std::vector<StaticBindingInfo> StaticBindings(
    const ScopeContext& ctx,
    const ast::ModulePath& module_path) {
  auto& perf = RegionsPerfStats();
  const bool perf_on = RegionsPerfActive();
  ScopedProvTimer timer(perf_on ? &perf.static_bindings_us : nullptr);
  std::vector<StaticBindingInfo> bindings;
  const auto* module = FindModuleByPath(ctx, module_path);
  if (!module) {
    return bindings;
  }
  for (const auto& item : module->items) {
    if (perf_on) {
      ++perf.static_binding_items_scanned;
    }
    const auto* decl = std::get_if<ast::StaticDecl>(&item);
    if (!decl) {
      continue;
    }
    const auto ann_type = ast::BindingAnnotationTypeOpt(decl->binding);
    if (!ann_type) {
      continue;
    }
    const auto ann = LocalLowerType(ctx, ann_type);
    if (!ann.ok) {
      continue;
    }
    const auto pat_bindings = PatternBindings(ctx, decl->binding.pat, ann.type);
    for (const auto& [name, type] : pat_bindings) {
      bindings.push_back(StaticBindingInfo{name, type, decl->mut});
    }
  }
  return bindings;
}

static ProvTag BottomTag() {
  return ProvTag{ProvKind::Bottom, 0, IdKey{}, 0};
}

static ProvTag GlobalTag() {
  return ProvTag{ProvKind::Global, 0, IdKey{}, 0};
}

static ProvTag StackTag(std::size_t scope_id) {
  return ProvTag{ProvKind::Stack, scope_id, IdKey{}, 0};
}

static ProvTag HeapTag() {
  return ProvTag{ProvKind::Heap, 0, IdKey{}, 0};
}

static ProvTag RegionTag(const IdKey& name) {
  return ProvTag{ProvKind::Region, 0, name, 0};
}

static ProvTag ParamTag(std::size_t idx) {
  return ProvTag{ProvKind::Param, 0, IdKey{}, idx};
}

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

static int ProvRank(const ProvTag& tag) {
  switch (tag.kind) {
    case ProvKind::Region:
      return 0;
    case ProvKind::Stack:
      return 1;
    case ProvKind::Heap:
      return 2;
    case ProvKind::Global:
      return 3;
    case ProvKind::Bottom:
      return 4;
    case ProvKind::Param:
      return -1;
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

static ProvTag JoinProv(const ProvEnv& env, const ProvTag& lhs,
                        const ProvTag& rhs) {
  SpecDefsRegions();
  if (ProvLeq(env, lhs, rhs)) {
    return lhs;
  }
  if (ProvLeq(env, rhs, lhs)) {
    return rhs;
  }
  return BottomTag();
}

static ProvTag JoinAllProv(const ProvEnv& env, const std::vector<ProvTag>& tags) {
  SpecDefsRegions();
  if (tags.empty()) {
    return BottomTag();
  }
  ProvTag current = tags.front();
  for (std::size_t i = 1; i < tags.size(); ++i) {
    current = JoinProv(env, current, tags[i]);
  }
  return current;
}

static void PushScope_pi_inplace(ProvEnv& env) {
  ProvScope scope;
  scope.id = env.next_scope_id++;
  env.scopes.push_back(std::move(scope));
}

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

static void Intro_pi_inplace(ProvEnv& env,
                             std::string_view name,
                             const ProvTag& tag) {
  if (env.scopes.empty()) {
    return;
  }
  env.scopes.back().map[IdKeyOf(name)] = tag;
}

static bool ShadowIntro_pi_inplace(ProvEnv& env,
                                   std::string_view name,
                                   const ProvTag& tag) {
  const auto key = IdKeyOf(name);
  for (auto it = env.scopes.rbegin(); it != env.scopes.rend(); ++it) {
    const auto found = it->map.find(key);
    if (found != it->map.end()) {
      found->second = tag;
      return true;
    }
  }
  if (!env.scopes.empty()) {
    env.scopes.back().map[key] = tag;
    return true;
  }
  return false;
}

static void IntroAll_pi_inplace(ProvEnv& env,
                                const std::vector<std::string>& names,
                                const ProvTag& tag) {
  for (const auto& name : names) {
    Intro_pi_inplace(env, name, tag);
  }
}

static ProvTag StackProv(const ProvEnv& env) {
  SpecDefsRegions();
  if (env.scopes.empty()) {
    return BottomTag();
  }
  return StackTag(env.scopes.back().id);
}

static ProvTag BindProv(const ProvEnv& env, const ProvTag& init) {
  SpecDefsRegions();
  if (init.kind == ProvKind::Bottom) {
    return StackProv(env);
  }
  return init;
}

static ProvEnv InitProvEnv(const std::vector<std::string>& params,
                           const std::vector<ProvTag>& tags,
                           const std::vector<RegionEntry>& regions) {
  ProvEnv env;
  ProvScope scope;
  scope.id = env.next_scope_id++;
  for (std::size_t i = 0; i < params.size() && i < tags.size(); ++i) {
    scope.map.emplace(IdKeyOf(params[i]), tags[i]);
  }
  env.scopes.push_back(std::move(scope));
  env.regions = regions;
  return env;
}

static std::optional<IdKey> AllocTag(const ProvEnv& env,
                                     const std::optional<std::string>& target) {
  SpecDefsRegions();
  if (!target.has_value()) {
    if (env.regions.empty()) {
      return std::nullopt;
    }
    return env.regions.back().tag;
  }
  const auto key = IdKeyOf(*target);
  for (auto it = env.regions.rbegin(); it != env.regions.rend(); ++it) {
    if (it->target == key) {
      return it->tag;
    }
  }
  return std::nullopt;
}

static std::optional<std::string> InnermostActiveRegionName(
    const TypeEnv& env) {
  const auto region = InnermostActiveRegion(env);
  if (!region.has_value()) {
    return std::nullopt;
  }
  return *region;
}

static std::optional<IdKey> InnermostFrameRegion(const ProvEnv& env) {
  for (auto it = env.regions.rbegin(); it != env.regions.rend(); ++it) {
    if (it->frame_active) {
      return it->tag;
    }
  }
  return std::nullopt;
}

static ProvTag FrameProv(const TypeEnv& gamma, const ProvEnv& env) {
  SpecDefsRegions();
  (void)gamma;
  const auto region = InnermostFrameRegion(env);
  if (region.has_value()) {
    return RegionTag(*region);
  }
  return StackProv(env);
}

static std::optional<TypeRef> ExprTypeForProvenance(const ScopeContext& ctx,
                                                    const TypeEnv& env,
                                                    const ast::ExprPtr& expr,
                                                    bool allow_type_infer = true) {
  if (!expr) {
    return std::nullopt;
  }
  if (const auto cached = CachedExprTypeOf(ctx, expr)) {
    return cached;
  }
  if (!allow_type_infer) {
    return std::nullopt;
  }

  StmtTypeContext type_ctx;
  type_ctx.return_type = {};
  type_ctx.loop_flag = LoopFlag::None;
  type_ctx.in_unsafe = false;
  type_ctx.diags = nullptr;

  const auto typed = TypeExpr(ctx, type_ctx, expr, env);
  if (!typed.ok || !typed.type) {
    return std::nullopt;
  }
  return typed.type;
}

static bool IsHeapAllocatorType(const TypeRef& type) {
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return false;
  }
  const auto* dyn = std::get_if<TypeDynamic>(&stripped->node);
  return dyn && dyn->path.size() == 1 && IdEq(dyn->path[0], "HeapAllocator");
}

static bool IsManagedHeapValueType(const TypeRef& type) {
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return false;
  }
  if (const auto* str = std::get_if<TypeString>(&stripped->node)) {
    return str->state == StringState::Managed;
  }
  if (const auto* bytes = std::get_if<TypeBytes>(&stripped->node)) {
    return bytes->state == BytesState::Managed;
  }
  if (const auto* union_ty = std::get_if<TypeUnion>(&stripped->node)) {
    for (const auto& member : union_ty->members) {
      if (IsManagedHeapValueType(member)) {
        return true;
      }
    }
  }
  return false;
}

static bool AcceptsHeapAllocator(
    const std::vector<TypeFuncParam>& params) {
  for (const auto& param : params) {
    if (IsHeapAllocatorType(param.type)) {
      return true;
    }
  }
  return false;
}

static bool CallableIntroducesHeapProvenance(const TypeRef& callable_type) {
  const auto stripped = StripPerm(callable_type);
  if (!stripped) {
    return false;
  }

  if (const auto* fn = std::get_if<TypeFunc>(&stripped->node)) {
    return AcceptsHeapAllocator(fn->params) &&
           IsManagedHeapValueType(fn->ret);
  }

  if (const auto* closure = std::get_if<TypeClosure>(&stripped->node)) {
    bool accepts_heap = false;
    for (const auto& [is_move, type] : closure->params) {
      (void)is_move;
      if (IsHeapAllocatorType(type)) {
        accepts_heap = true;
        break;
      }
    }
    return accepts_heap && IsManagedHeapValueType(closure->ret);
  }

  return false;
}

static std::optional<TypeRef> DirectCalleeTypeForProvenance(
    const ScopeContext& ctx,
    const ast::ExprPtr& callee) {
  if (!callee) {
    return std::nullopt;
  }

  std::optional<ast::ModulePath> origin;
  std::string target_name;
  std::optional<std::string> current_module_fallback_name;

  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&callee->node)) {
    current_module_fallback_name = ident->name;
    const auto ent = ResolveValueName(ctx, ident->name);
    if (ent.has_value() && ent->origin_opt.has_value()) {
      origin = ent->origin_opt;
      target_name = ent->target_opt.value_or(std::string(ident->name));
    } else {
      const auto local_callee =
          ValuePathType(ctx, CurrentModule(ctx), ident->name);
      if (local_callee.ok && local_callee.type) {
        return local_callee.type;
      }
      return std::nullopt;
    }
  } else if (const auto* path_expr = std::get_if<ast::PathExpr>(&callee->node)) {
    origin = path_expr->path;
    target_name = path_expr->name;
    if (path_expr->path.empty()) {
      current_module_fallback_name = path_expr->name;
    }
  } else if (const auto* qualified =
                 std::get_if<ast::QualifiedNameExpr>(&callee->node)) {
    origin = qualified->path;
    target_name = qualified->name;
  } else {
    return std::nullopt;
  }

  if (!origin.has_value()) {
    return std::nullopt;
  }
  const auto callee_path_type = ValuePathType(ctx, *origin, target_name);
  if (callee_path_type.ok && callee_path_type.type) {
    return callee_path_type.type;
  }

  if (current_module_fallback_name.has_value()) {
    const auto fallback =
        ValuePathType(ctx, CurrentModule(ctx), *current_module_fallback_name);
    if (fallback.ok && fallback.type) {
      return fallback.type;
    }
  }

  return std::nullopt;
}

static bool CallExprIntroducesHeapProvenance(const ScopeContext& ctx,
                                             const TypeEnv& env,
                                             const ast::CallExpr& call,
                                             bool allow_type_infer = true) {
  if (const auto callee_type =
          ExprTypeForProvenance(ctx, env, call.callee, allow_type_infer)) {
    if (CallableIntroducesHeapProvenance(*callee_type)) {
      return true;
    }
  }

  if (const auto callee_type = DirectCalleeTypeForProvenance(ctx, call.callee)) {
    if (CallableIntroducesHeapProvenance(*callee_type)) {
      return true;
    }
  }

  return false;
}

static bool LoweredMethodIntroducesHeapProvenance(const ScopeContext& ctx,
                                                  const ast::MethodDecl& method) {
  std::vector<TypeFuncParam> params;
  params.reserve(method.params.size());
  for (const auto& param : method.params) {
    const auto lowered = LocalLowerType(ctx, param.type);
    if (!lowered.ok || !lowered.type) {
      return false;
    }
    params.push_back(TypeFuncParam{LowerParamMode(param.mode), lowered.type});
  }

  TypeRef ret = MakeTypePrim("()");
  if (method.return_type_opt) {
    const auto lowered = LocalLowerType(ctx, method.return_type_opt);
    if (!lowered.ok || !lowered.type) {
      return false;
    }
    ret = lowered.type;
  }

  return AcceptsHeapAllocator(params) && IsManagedHeapValueType(ret);
}

static bool LoweredMethodIntroducesHeapProvenance(
    const ScopeContext& ctx,
    const ast::ClassMethodDecl& method) {
  std::vector<TypeFuncParam> params;
  params.reserve(method.params.size());
  for (const auto& param : method.params) {
    const auto lowered = LocalLowerType(ctx, param.type);
    if (!lowered.ok || !lowered.type) {
      return false;
    }
    params.push_back(TypeFuncParam{LowerParamMode(param.mode), lowered.type});
  }

  TypeRef ret = MakeTypePrim("()");
  if (method.return_type_opt) {
    const auto lowered = LocalLowerType(ctx, method.return_type_opt);
    if (!lowered.ok || !lowered.type) {
      return false;
    }
    ret = lowered.type;
  }

  return AcceptsHeapAllocator(params) && IsManagedHeapValueType(ret);
}

static bool MethodCallIntroducesHeapProvenance(const ScopeContext& ctx,
                                               const TypeEnv& env,
                                               const ast::MethodCallExpr& call,
                                               bool allow_type_infer = true) {
  const auto receiver_type =
      ExprTypeForProvenance(ctx, env, call.receiver, allow_type_infer);
  if (!receiver_type.has_value()) {
    return false;
  }
  const auto receiver_base = StripPerm(*receiver_type);
  if (!receiver_base) {
    return false;
  }

  if (const auto sig =
          LookupStringBytesBuiltinMethodSig(receiver_base, call.name)) {
    return AcceptsHeapAllocator(sig->params) &&
           IsManagedHeapValueType(sig->ret);
  }

  if (const auto* dyn = std::get_if<TypeDynamic>(&receiver_base->node)) {
    if (dyn->path.size() == 1 && IdEq(dyn->path[0], "HeapAllocator")) {
      const auto sig = LookupHeapAllocatorMethodSig(call.name);
      return sig.has_value() &&
             sig->kind == HeapAllocatorMethodKind::AllocRaw;
    }
  }

  const auto lookup = LookupMethodStatic(ctx, receiver_base, call.name);
  if (!lookup.ok) {
    return false;
  }
  if (lookup.record_method) {
    return LoweredMethodIntroducesHeapProvenance(ctx, *lookup.record_method);
  }
  if (lookup.class_method) {
    return LoweredMethodIntroducesHeapProvenance(ctx, *lookup.class_method);
  }
  return false;
}

static std::optional<AsyncSig> AsyncSigForExpr(const ScopeContext& ctx,
                                               const TypeEnv& env,
                                               const ast::ExprPtr& expr,
                                               bool allow_type_infer = true) {
  auto async_sig_from_type = [&](const TypeRef& type) -> std::optional<AsyncSig> {
    if (!type) {
      return std::nullopt;
    }
    const auto stripped = StripPerm(type);
    if (!stripped) {
      return std::nullopt;
    }
    if (const auto* fn = std::get_if<TypeFunc>(&stripped->node)) {
      return AsyncSigOf(ctx, fn->ret);
    }
    if (const auto* closure = std::get_if<TypeClosure>(&stripped->node)) {
      return AsyncSigOf(ctx, closure->ret);
    }
    return AsyncSigOf(ctx, stripped);
  };
  auto async_sig_from_direct_callee =
      [&](const ast::ExprPtr& callee) -> std::optional<AsyncSig> {
    if (!callee) {
      return std::nullopt;
    }

    std::optional<ast::ModulePath> origin;
    std::string target_name;
    std::optional<std::string> current_module_fallback_name;

    if (const auto* ident = std::get_if<ast::IdentifierExpr>(&callee->node)) {
      current_module_fallback_name = ident->name;
      const auto ent = ResolveValueName(ctx, ident->name);
      if (ent.has_value() && ent->origin_opt.has_value()) {
        origin = ent->origin_opt;
        target_name = ent->target_opt.value_or(std::string(ident->name));
      } else {
        const auto local_callee =
            ValuePathType(ctx, CurrentModule(ctx), ident->name);
        if (!local_callee.ok || !local_callee.type) {
          return std::nullopt;
        }
        return async_sig_from_type(local_callee.type);
      }
    } else if (const auto* path_expr = std::get_if<ast::PathExpr>(&callee->node)) {
      origin = path_expr->path;
      target_name = path_expr->name;
      if (path_expr->path.empty()) {
        current_module_fallback_name = path_expr->name;
      }
    } else if (const auto* qualified =
                   std::get_if<ast::QualifiedNameExpr>(&callee->node)) {
      origin = qualified->path;
      target_name = qualified->name;
    } else {
      return std::nullopt;
    }

    if (!origin.has_value()) {
      return std::nullopt;
    }
    const auto callee_path_type = ValuePathType(ctx, *origin, target_name);
    if (callee_path_type.ok && callee_path_type.type) {
      return async_sig_from_type(callee_path_type.type);
    }

    // Resolution may produce a non-usable origin during typecheck provenance.
    // Retry against current module for unqualified calls to keep async-create
    // classification consistent with codegen provenance.
    if (current_module_fallback_name.has_value()) {
      const auto fallback =
          ValuePathType(ctx, CurrentModule(ctx), *current_module_fallback_name);
      if (fallback.ok && fallback.type) {
        return async_sig_from_type(fallback.type);
      }
    }

    return std::nullopt;
  };

  if (!expr) {
    return std::nullopt;
  }
  StmtTypeContext type_ctx;
  type_ctx.return_type = {};
  type_ctx.loop_flag = LoopFlag::None;
  type_ctx.in_unsafe = false;
  type_ctx.diags = nullptr;

  const auto expr_type_cached = CachedExprTypeOf(ctx, expr);
  if (expr_type_cached.has_value()) {
    if (const auto sig = async_sig_from_type(*expr_type_cached)) {
      return sig;
    }
  } else if (allow_type_infer) {
    const auto expr_type = TypeExpr(ctx, type_ctx, expr, env);
    if (expr_type.ok && expr_type.type) {
      if (const auto sig = async_sig_from_type(expr_type.type)) {
        return sig;
      }
    }
  }

  if (const auto* call = std::get_if<ast::CallExpr>(&expr->node)) {
    const auto callee_cached = CachedExprTypeOf(ctx, call->callee);
    if (callee_cached.has_value()) {
      if (const auto sig = async_sig_from_type(*callee_cached)) {
        return sig;
      }
    } else if (allow_type_infer) {
      const auto callee_type = TypeExpr(ctx, type_ctx, call->callee, env);
      if (callee_type.ok && callee_type.type) {
        if (const auto sig = async_sig_from_type(callee_type.type)) {
          return sig;
        }
      }
    }
    if (const auto sig = async_sig_from_direct_callee(call->callee)) {
      return sig;
    }
  }

  return std::nullopt;
}

static bool AsyncCreateExpr(const ScopeContext& ctx,
                            const TypeEnv& env,
                            const ast::ExprPtr& expr,
                            bool allow_type_infer = true) {
  if (!expr) {
    return false;
  }
  const bool is_async_form =
      std::holds_alternative<ast::CallExpr>(expr->node) ||
      std::holds_alternative<ast::MethodCallExpr>(expr->node) ||
      std::holds_alternative<ast::RaceExpr>(expr->node);
  if (!is_async_form) {
    return false;
  }
  const auto sig = AsyncSigForExpr(ctx, env, expr, allow_type_infer);
  return sig.has_value();
}

static std::vector<ast::ExprPtr> AsyncCaptureArgs(
    const ast::ExprPtr& expr) {
  std::vector<ast::ExprPtr> out;
  if (!expr) {
    return out;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::CallExpr>) {
          for (const auto& arg : node.args) {
            out.push_back(arg.value);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          out.push_back(node.receiver);
          for (const auto& arg : node.args) {
            out.push_back(arg.value);
          }
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            out.push_back(arm.expr);
          }
        }
      },
      expr->node);
  return out;
}

static bool WarnAsyncCapture(const std::vector<ast::ExprPtr>& args) {
  // Conservative approximation for conformance instrumentation:
  // treat larger capture sets as potentially expensive captures.
  return args.size() >= 3;
}

static std::optional<TypeRef> LookupTypeForRegion(const ScopeContext& ctx,
                                                  const TypeEnv& env,
                                                  std::string_view name) {
  const auto binding = BindOf(env, name);
  if (binding.has_value()) {
    return binding->type;
  }
  const auto ent = ResolveValueName(ctx, name);
  if (ent.has_value() && ent->origin_opt.has_value()) {
    const auto target_name = ent->target_opt.value_or(std::string(name));
    const auto value_type = ValuePathType(ctx, *ent->origin_opt, target_name);
    if (value_type.ok && value_type.type) {
      return value_type.type;
    }
  }
  return std::nullopt;
}

class ClosureCaptureCollector {
 public:
  explicit ClosureCaptureCollector(const TypeEnv& env) : env_(env) {
    local_scopes_.emplace_back();
  }

  std::unordered_set<IdKey> CollectClosure(const ast::ClosureExpr& closure) {
    PushScope();
    for (const auto& param : closure.params) {
      DeclareName(param.name);
    }
    VisitExpr(closure.body);
    PopScope();
    return captures_;
  }

 private:
  void PushScope() { local_scopes_.emplace_back(); }

  void PopScope() {
    if (!local_scopes_.empty()) {
      local_scopes_.pop_back();
    }
  }

  bool IsLocal(std::string_view name) const {
    const IdKey key = IdKeyOf(name);
    for (auto it = local_scopes_.rbegin(); it != local_scopes_.rend(); ++it) {
      if (it->find(key) != it->end()) {
        return true;
      }
    }
    return false;
  }

  void DeclareName(std::string_view name) {
    if (local_scopes_.empty()) {
      local_scopes_.emplace_back();
    }
    local_scopes_.back().insert(IdKeyOf(name));
  }

  void DeclarePattern(const ast::PatternPtr& pattern) {
    if (!pattern) {
      return;
    }
    std::vector<IdKey> names;
    CollectPatNames(*pattern, names);
    for (const auto& name : names) {
      if (!name.empty()) {
        DeclareName(name);
      }
    }
  }

  void CaptureIfOuter(std::string_view name) {
    if (IsLocal(name)) {
      return;
    }
    if (BindOf(env_, name).has_value()) {
      captures_.insert(IdKeyOf(name));
    }
  }

  void VisitKeyPath(const ast::KeyPathExpr& path) {
    CaptureIfOuter(path.root);
    for (const auto& seg : path.segs) {
      if (const auto* idx = std::get_if<ast::KeySegIndex>(&seg)) {
        VisitExpr(idx->expr);
      }
    }
  }

  void VisitBlock(const ast::Block& block) {
    PushScope();
    for (const auto& stmt : block.stmts) {
      VisitStmt(stmt);
    }
    VisitExpr(block.tail_opt);
    PopScope();
  }

  void VisitStmt(const ast::Stmt& stmt) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<T, ast::LetStmt> ||
                        std::is_same_v<T, ast::VarStmt>) {
            VisitExpr(node.binding.init);
            DeclarePattern(node.binding.pat);
          } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
            // UsingLocalStmt is a compile-time alias; no runtime expression,
            // but the alias name still enters the surrounding scope.
            DeclareName(node.alias);
          } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
            VisitExpr(node.place);
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
            VisitExpr(node.place);
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
            if (node.opts_opt) {
              VisitExpr(node.opts_opt);
            }
            PushScope();
            if (node.alias_opt) {
              DeclareName(*node.alias_opt);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
            PopScope();
          } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
            if (node.target_opt) {
              CaptureIfOuter(*node.target_opt);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
            VisitExpr(node.value_opt);
          } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
            VisitExpr(node.value_opt);
          } else if constexpr (std::is_same_v<T, ast::ContinueStmt>) {
          } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
            for (const auto& path : node.paths) {
              VisitKeyPath(path);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::ErrorStmt>) {
          }
        },
        stmt);
  }

  void VisitExpr(const ast::ExprPtr& expr) {
    if (!expr) {
      return;
    }
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
            CaptureIfOuter(node.name);
          } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
            if (const auto* paren = std::get_if<ast::ParenArgs>(&node.args)) {
              for (const auto& arg : paren->args) {
                VisitExpr(arg.value);
              }
            } else if (const auto* brace =
                           std::get_if<ast::BraceArgs>(&node.args)) {
              for (const auto& field : brace->fields) {
                VisitExpr(field.value);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
            VisitExpr(node.place);
          } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
            VisitExpr(node.place);
          } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
            for (const auto& elem : node.elements) {
              VisitExpr(elem);
            }
          } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
            ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
              VisitExpr(elem);
            });
          } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
            VisitExpr(node.value);
            VisitExpr(node.count);
          } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
            for (const auto& field : node.fields) {
              VisitExpr(field.value);
            }
          } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
            if (node.payload_opt.has_value()) {
              if (const auto* tuple =
                      std::get_if<ast::EnumPayloadParen>(&*node.payload_opt)) {
                for (const auto& elem : tuple->elements) {
                  VisitExpr(elem);
                }
              } else if (const auto* rec =
                             std::get_if<ast::EnumPayloadBrace>(&*node.payload_opt)) {
                for (const auto& field : rec->fields) {
                  VisitExpr(field.value);
                }
              }
            }
          } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
            VisitExpr(node.base);
          } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
            VisitExpr(node.base);
          } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
            VisitExpr(node.base);
            VisitExpr(node.index);
          } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
            VisitExpr(node.cond);
            VisitExpr(node.then_expr);
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
            VisitExpr(node.scrutinee);
            for (const auto& case_clause : node.cases) {
              PushScope();
              DeclarePattern(case_clause.pattern);
              VisitExpr(case_clause.body);
              PopScope();
            }
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
            VisitExpr(node.scrutinee);
            PushScope();
            DeclarePattern(node.pattern);
            VisitExpr(node.then_expr);
            PopScope();
            VisitExpr(node.else_expr);
          } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
            VisitExpr(node.cond);
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
            VisitExpr(node.iter);
            PushScope();
            DeclarePattern(node.pattern);
            if (node.invariant_opt.has_value()) {
              VisitExpr(node.invariant_opt->predicate);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
            PopScope();
          } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                               std::is_same_v<T, ast::UnsafeBlockExpr>) {
            if (node.block) {
              VisitBlock(*node.block);
            }
          } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
            VisitExpr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
            PushScope();
            for (const auto& param : node.params) {
              DeclareName(param.name);
            }
            VisitExpr(node.body);
            PopScope();
          } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
            VisitExpr(node.lhs);
            VisitExpr(node.rhs);
          } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            VisitExpr(node.callee);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
            VisitExpr(node.receiver);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value);
            }
          } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
            VisitExpr(node.expr);
          } else if constexpr (std::is_same_v<T, ast::YieldExpr> ||
                               std::is_same_v<T, ast::YieldFromExpr> ||
                               std::is_same_v<T, ast::SyncExpr>) {
            VisitExpr(node.value);
          } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
            for (const auto& arm : node.arms) {
              VisitExpr(arm.expr);
              if (arm.pattern) {
                PushScope();
                DeclarePattern(arm.pattern);
                VisitExpr(arm.handler.value);
                PopScope();
              } else {
                VisitExpr(arm.handler.value);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
            for (const auto& sub : node.exprs) {
              VisitExpr(sub);
            }
          } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
            VisitExpr(node.domain);
            for (const auto& opt : node.opts) {
              VisitExpr(opt.value);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
            for (const auto& opt : node.opts) {
              VisitExpr(opt.value);
            }
            if (node.body) {
              VisitBlock(*node.body);
            }
          } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
            VisitExpr(node.handle);
          } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
            VisitExpr(node.range);
            if (node.key_clause.has_value()) {
              VisitKeyPath(node.key_clause->key_path);
            }
            for (const auto& opt : node.opts) {
              VisitExpr(opt.chunk_expr);
              VisitExpr(opt.workgroup_expr);
            }
            PushScope();
            DeclarePattern(node.pattern);
            if (node.body) {
              VisitBlock(*node.body);
            }
            PopScope();
          }
        },
        expr->node);
  }

  const TypeEnv& env_;
  std::vector<std::unordered_set<IdKey>> local_scopes_;
  std::unordered_set<IdKey> captures_;
};

static bool ClosureTypeCanEscape(const TypeRef& type) {
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return false;
  }
  const auto* closure = std::get_if<TypeClosure>(&stripped->node);
  return closure && closure->deps_opt.has_value();
}

static bool IsEscapingClosureExpr(const ScopeContext& ctx,
                                  const ast::ExprPtr& expr) {
  const auto type = CachedExprTypeOf(ctx, expr);
  return type.has_value() && ClosureTypeCanEscape(*type);
}

static std::optional<TypeRef> ClosureParamType(const ScopeContext& ctx,
                                               const ast::ExprPtr& expr,
                                               const ast::ClosureExpr& closure,
                                               std::size_t index) {
  if (index >= closure.params.size()) {
    return std::nullopt;
  }
  if (closure.params[index].type_opt) {
    const auto lowered = LocalLowerType(ctx, closure.params[index].type_opt);
    if (lowered.ok && lowered.type) {
      return lowered.type;
    }
  }
  const auto cached = CachedExprTypeOf(ctx, expr);
  if (!cached.has_value()) {
    return std::nullopt;
  }
  const auto stripped = StripPerm(*cached);
  if (!stripped) {
    return std::nullopt;
  }
  if (const auto* func = std::get_if<TypeFunc>(&stripped->node)) {
    if (index < func->params.size()) {
      return func->params[index].type;
    }
  }
  if (const auto* closure_type = std::get_if<TypeClosure>(&stripped->node)) {
    if (index < closure_type->params.size()) {
      return closure_type->params[index].second;
    }
  }
  return std::nullopt;
}

template <typename Fn>
static void ForEachChildLtr(const ast::ExprPtr& expr, Fn&& fn) {
  if (!expr) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          if (const auto* paren = std::get_if<ast::ParenArgs>(&node.args)) {
            for (const auto& arg : paren->args) {
              fn(arg.value);
            }
          } else if (const auto* brace =
                         std::get_if<ast::BraceArgs>(&node.args)) {
            for (const auto& field : brace->fields) {
              fn(field.value);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          if (node.lhs) {
            fn(node.lhs);
          }
          if (node.rhs) {
            fn(node.rhs);
          }
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          fn(node.lhs);
          fn(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          fn(node.value);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          fn(node.value);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          fn(node.value);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          fn(node.place);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          fn(node.place);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          fn(node.value);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            fn(elem);
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            fn(elem);
          });
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          fn(node.value);
          fn(node.count);
        } else if constexpr (std::is_same_v<T, ast::SizeofExpr>) {
          // sizeof(type) has no runtime sub-expressions
        } else if constexpr (std::is_same_v<T, ast::AlignofExpr>) {
          // alignof(type) has no runtime sub-expressions
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            fn(field.value);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (node.payload_opt.has_value()) {
            if (const auto* tuple =
                    std::get_if<ast::EnumPayloadParen>(&*node.payload_opt)) {
              for (const auto& elem : tuple->elements) {
                fn(elem);
              }
            } else if (const auto* rec =
                           std::get_if<ast::EnumPayloadBrace>(&*node.payload_opt)) {
              for (const auto& field : rec->fields) {
                fn(field.value);
              }
            }
          }
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          fn(node.value);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          fn(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          fn(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          fn(node.base);
          fn(node.index);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          fn(node.callee);
          for (const auto& arg : node.args) {
            fn(arg.value);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          fn(node.receiver);
          for (const auto& arg : node.args) {
            fn(arg.value);
          }
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          fn(node.value);
        }
      },
      expr->node);
}

static std::optional<IdKey> ResolveRegionTarget(const ProvEnv& env,
                                                const ProvTag& tag) {
  if (tag.kind != ProvKind::Region) {
    return std::nullopt;
  }
  for (auto it = env.regions.rbegin(); it != env.regions.rend(); ++it) {
    if (it->tag == tag.region) {
      return it->target;
    }
  }
  return std::nullopt;
}

static void RecordExprProv(const ast::ExprPtr& expr,
                           const ProvExprResult& res,
                           const ProvEnv& env,
                           ExprProvTagMap* map) {
  if (!map || !expr) {
    return;
  }
  if (!res.ok) {
    return;
  }
  ExprProvInfo info;
  info.tag = res.prov;
  info.target = ResolveRegionTarget(env, res.prov);
  (*map)[expr.get()] = std::move(info);
}

static ProvExprResult ProvExpr(const ScopeContext& ctx,
                               const ast::ExprPtr& expr,
                               const ProvEnv& env,
                               const TypeEnv& gamma,
                               ExprProvTagMap* expr_map);

static ProvExprResult ProvPlace(const ScopeContext& ctx,
                                const ast::ExprPtr& place,
                                const ProvEnv& env,
                                const TypeEnv& gamma,
                                ExprProvTagMap* expr_map) {
  (void)ctx;
  (void)expr_map;
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
          auto base = ProvPlace(ctx, node.base, env, gamma, expr_map);
          if (!base.ok) {
            return base;
          }
          SPEC_RULE("P-Field");
          base.prov = base.prov;
          return base;
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto base = ProvPlace(ctx, node.base, env, gamma, expr_map);
          if (!base.ok) {
            return base;
          }
          SPEC_RULE("P-Tuple");
          base.prov = base.prov;
          return base;
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto base = ProvPlace(ctx, node.base, env, gamma, expr_map);
          if (!base.ok) {
            return base;
          }
          SPEC_RULE("P-Index");
          base.prov = base.prov;
          return base;
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          auto inner = ProvExpr(ctx, node.value, env, gamma, expr_map);
          if (!inner.ok) {
            return inner;
          }
          SPEC_RULE("P-Deref");
          return inner;
        } else {
          ProvExprResult out;
          out.ok = true;
          out.prov = BottomTag();
          return out;
        }
      },
      place->node);
}

static ProvTag LoopProv(const ProvEnv& env,
                        const std::vector<ProvTag>& brk,
                        bool brk_void) {
  if (brk.empty()) {
    return BottomTag();
  }
  if (brk_void) {
    return BottomTag();
  }
  return JoinAllProv(env, brk);
}

static ProvExprResult BlockProv(const ScopeContext& ctx,
                                const ast::Block& block,
                                const ProvEnv& env,
                                const TypeEnv& gamma,
                                ExprProvTagMap* expr_map);

static ProvStmtResult ProvStmt(const ScopeContext& ctx,
                               const ast::Stmt& stmt,
                               const ProvEnv& env,
                               const TypeEnv& gamma,
                               ExprProvTagMap* expr_map);

static ProvSeqResult ProvStmtSeq(const ScopeContext& ctx,
                                 const std::vector<ast::Stmt>& stmts,
                                 const ProvEnv& env,
                                 const TypeEnv& gamma,
                                 ExprProvTagMap* expr_map) {
  ProvSeqResult result;
  if (stmts.empty()) {
    SPEC_RULE("Prov-Seq-Empty");
    result.ok = true;
    result.env = env;
    result.gamma = gamma;
    return result;
  }

  ProvEnv current_env = env;
  TypeEnv current_gamma = gamma;
  ProvFlow merged_flow;
  for (const auto& stmt : stmts) {
    auto step = ProvStmt(ctx, stmt, current_env, current_gamma, expr_map);
    if (!step.ok) {
      result.ok = false;
      result.diag_id = step.diag_id;
      result.span = step.span;
      return result;
    }
    for (auto& prov : step.flow.results) {
      merged_flow.results.push_back(std::move(prov));
    }
    for (auto& prov : step.flow.breaks) {
      merged_flow.breaks.push_back(std::move(prov));
    }
    merged_flow.break_void = merged_flow.break_void || step.flow.break_void;
    current_env = std::move(step.env);
    current_gamma = std::move(step.gamma);
  }

  SPEC_RULE("Prov-Seq-Cons");
  result.ok = true;
  result.env = std::move(current_env);
  result.gamma = std::move(current_gamma);
  result.flow = std::move(merged_flow);
  return result;
}

static ProvExprResult BlockProv(const ScopeContext& ctx,
                                const ast::Block& block,
                                const ProvEnv& env,
                                const TypeEnv& gamma,
                                ExprProvTagMap* expr_map) {
  ProvExprResult result;
  ProvEnv inner_env = env;
  PushScope_pi_inplace(inner_env);
  TypeEnv inner_gamma = gamma;
  inner_gamma.scopes.emplace_back();
  auto seq = ProvStmtSeq(ctx, block.stmts, inner_env, inner_gamma, expr_map);
  if (!seq.ok) {
    result.ok = false;
    result.diag_id = seq.diag_id;
    result.span = seq.span;
    return result;
  }

  std::optional<ProvTag> tail_prov;
  if (block.tail_opt) {
    const auto tail = ProvExpr(ctx, block.tail_opt, seq.env, seq.gamma, expr_map);
    if (!tail.ok) {
      return tail;
    }
    tail_prov = tail.prov;
  }

  if (!seq.flow.results.empty()) {
    SPEC_RULE("BlockProv-Res");
    result.ok = true;
    result.prov = JoinAllProv(seq.env, seq.flow.results);
    return result;
  }

  if (block.tail_opt && tail_prov.has_value()) {
    SPEC_RULE("BlockProv-Tail");
    result.ok = true;
    result.prov = *tail_prov;
    return result;
  }

  SPEC_RULE("BlockProv-Unit");
  result.ok = true;
  result.prov = BottomTag();
  return result;
}

static BreakProvResult BreakProv(const ScopeContext& ctx,
                                 const ast::Block& body,
                                 const ProvEnv& env,
                                 const TypeEnv& gamma) {
  BreakProvResult result;
  ProvEnv inner_env = env;
  PushScope_pi_inplace(inner_env);
  TypeEnv inner_gamma = gamma;
  inner_gamma.scopes.emplace_back();
  auto seq = ProvStmtSeq(ctx, body.stmts, inner_env, inner_gamma, nullptr);
  if (!seq.ok) {
    result.ok = false;
    result.diag_id = seq.diag_id;
    result.span = seq.span;
    return result;
  }
  if (body.tail_opt) {
    const auto tail = ProvExpr(ctx, body.tail_opt, seq.env, seq.gamma, nullptr);
    if (!tail.ok) {
      result.ok = false;
      result.diag_id = tail.diag_id;
      result.span = tail.span;
      return result;
    }
  }
  result.ok = true;
  result.breaks = seq.flow.breaks;
  result.break_void = seq.flow.break_void;
  return result;
}

static ProvExprResult ProvExpr(const ScopeContext& ctx,
                               const ast::ExprPtr& expr,
                               const ProvEnv& env,
                               const TypeEnv& gamma,
                               ExprProvTagMap* expr_map) {
  auto finish = [&](ProvExprResult res) -> ProvExprResult {
    RecordExprProv(expr, res, env, expr_map);
    return res;
  };
  ProvExprResult result;
  if (!expr) {
    result.ok = true;
    result.prov = BottomTag();
    return finish(result);
  }

  if (const auto* literal = std::get_if<ast::LiteralExpr>(&expr->node)) {
    (void)literal;
    SPEC_RULE("P-Literal");
    result.ok = true;
    result.prov = BottomTag();
    return finish(result);
  }

  if (const auto* move_expr = std::get_if<ast::MoveExpr>(&expr->node)) {
    auto inner = ProvPlace(ctx, move_expr->place, env, gamma, expr_map);
    if (!inner.ok) {
      return finish(inner);
    }
    SPEC_RULE("P-Move");
    inner.prov = inner.prov;
    return finish(inner);
  }

  if (const auto* addr = std::get_if<ast::AddressOfExpr>(&expr->node)) {
    auto inner = ProvPlace(ctx, addr->place, env, gamma, expr_map);
    if (!inner.ok) {
      return finish(inner);
    }
    SPEC_RULE("P-AddrOf");
    inner.prov = inner.prov;
    return finish(inner);
  }

  if (const auto* alloc = std::get_if<ast::AllocExpr>(&expr->node)) {
    auto inner = ProvExpr(ctx, alloc->value, env, gamma, expr_map);
    if (!inner.ok) {
      return finish(inner);
    }
    const auto tag = AllocTag(env, alloc->region_opt);
    SPEC_RULE("P-Alloc");
    result.ok = true;
    if (tag.has_value()) {
      result.prov = RegionTag(*tag);
      return finish(result);
    }
    result.prov = BottomTag();
    return finish(result);
  }

  if (const auto* call = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    const auto recv_res = ProvExpr(ctx, call->receiver, env, gamma, expr_map);
    if (!recv_res.ok) {
      return finish(recv_res);
    }
    const auto type =
        ExprTypeForProvenance(ctx, gamma, call->receiver, expr_map == nullptr);
    if (type.has_value()) {
      const TypeRef stripped = StripPerm(*type);
      if (stripped) {
        if (const auto* modal =
                std::get_if<TypeModalState>(&stripped->node)) {
          const auto sig =
              LookupBuiltinModalMemberSig(modal->path, modal->state, call->name);
          if (sig.has_value() && sig->allocates_in_receiver) {
            for (const auto& arg : call->args) {
              const auto arg_res = ProvExpr(ctx, arg.value, env, gamma, expr_map);
              if (!arg_res.ok) {
                return finish(arg_res);
              }
            }
            SPEC_RULE("P-Region-Alloc-Method");
            result.ok = true;
            result.prov = recv_res.prov.kind == ProvKind::Region
                              ? recv_res.prov
                              : BottomTag();
            return finish(result);
          }
        }
      }
    }
  }

  if (const auto* if_expr = std::get_if<ast::IfExpr>(&expr->node)) {
    const auto cond = ProvExpr(ctx, if_expr->cond, env, gamma, expr_map);
    if (!cond.ok) {
      return finish(cond);
    }
    const auto then_res = ProvExpr(ctx, if_expr->then_expr, env, gamma, expr_map);
    if (!then_res.ok) {
      return finish(then_res);
    }
    if (if_expr->else_expr) {
      const auto else_res = ProvExpr(ctx, if_expr->else_expr, env, gamma, expr_map);
      if (!else_res.ok) {
        return finish(else_res);
      }
      SPEC_RULE("P-If");
      result.ok = true;
      result.prov = JoinProv(env, then_res.prov, else_res.prov);
      return finish(result);
    }
    SPEC_RULE("P-If-No-Else");
    result.ok = true;
    result.prov = BottomTag();
    return finish(result);
  }

  if (const auto* if_case = std::get_if<ast::IfCaseExpr>(&expr->node)) {
    const auto scrut = ProvExpr(ctx, if_case->scrutinee, env, gamma, expr_map);
    if (!scrut.ok) {
      return finish(scrut);
    }
    std::vector<ProvTag> arm_provs;
    arm_provs.reserve(if_case->cases.size() +
                      (if_case->else_expr ? 1u : 0u));
    for (const auto& case_clause : if_case->cases) {
      if (!case_clause.pattern) {
        continue;
      }
      const auto names = PatNames(*case_clause.pattern);
      std::vector<std::string> bind_names;
      bind_names.reserve(names.size());
      for (const auto& name : names) {
        bind_names.push_back(name);
      }
      ProvEnv arm_env = env;
      const auto bind_pi = BindProv(arm_env, BottomTag());
      IntroAll_pi_inplace(arm_env, bind_names, bind_pi);
      auto body = ProvExpr(ctx, case_clause.body, arm_env, gamma, expr_map);
      if (!body.ok) {
        return finish(body);
      }
      arm_provs.push_back(body.prov);
    }
    if (if_case->else_expr) {
      auto else_res = ProvExpr(ctx, if_case->else_expr, env, gamma, expr_map);
      if (!else_res.ok) {
        return finish(else_res);
      }
      arm_provs.push_back(else_res.prov);
    }
    SPEC_RULE("P-IfCase");
    result.ok = true;
    result.prov = JoinAllProv(env, arm_provs);
    return finish(result);
  }

  if (const auto* if_is = std::get_if<ast::IfIsExpr>(&expr->node)) {
    const auto scrut = ProvExpr(ctx, if_is->scrutinee, env, gamma, expr_map);
    if (!scrut.ok) {
      return finish(scrut);
    }
    std::vector<ProvTag> branch_provs;
    branch_provs.reserve(if_is->else_expr ? 2u : 1u);
    if (if_is->pattern && if_is->then_expr) {
      const auto names = PatNames(*if_is->pattern);
      std::vector<std::string> bind_names;
      bind_names.reserve(names.size());
      for (const auto& name : names) {
        bind_names.push_back(name);
      }
      ProvEnv then_env = env;
      const auto bind_pi = BindProv(then_env, BottomTag());
      IntroAll_pi_inplace(then_env, bind_names, bind_pi);
      auto then_res = ProvExpr(ctx, if_is->then_expr, then_env, gamma, expr_map);
      if (!then_res.ok) {
        return finish(then_res);
      }
      branch_provs.push_back(then_res.prov);
    }
    if (if_is->else_expr) {
      auto else_res = ProvExpr(ctx, if_is->else_expr, env, gamma, expr_map);
      if (!else_res.ok) {
        return finish(else_res);
      }
      branch_provs.push_back(else_res.prov);
      SPEC_RULE("P-If");
      result.ok = true;
      result.prov = JoinAllProv(env, branch_provs);
      return finish(result);
    }
    SPEC_RULE("P-If-No-Else");
    result.ok = true;
    result.prov = BottomTag();
    return finish(result);
  }

  if (const auto* block_expr = std::get_if<ast::BlockExpr>(&expr->node)) {
    if (!block_expr->block) {
      result.ok = true;
      result.prov = BottomTag();
      return finish(result);
    }
    SPEC_RULE("P-Block");
    return finish(BlockProv(ctx, *block_expr->block, env, gamma, expr_map));
  }

  if (const auto* unsafe_expr =
          std::get_if<ast::UnsafeBlockExpr>(&expr->node)) {
    if (!unsafe_expr->block) {
      result.ok = true;
      result.prov = BottomTag();
      return finish(result);
    }
    SPEC_RULE("P-Block");
    return finish(BlockProv(ctx, *unsafe_expr->block, env, gamma, expr_map));
  }

  if (const auto* loop_inf =
          std::get_if<ast::LoopInfiniteExpr>(&expr->node)) {
    if (!loop_inf->body) {
      result.ok = true;
      result.prov = BottomTag();
      return finish(result);
    }
    const auto prov = BreakProv(ctx, *loop_inf->body, env, gamma);
    if (!prov.ok) {
      result.ok = false;
      result.diag_id = prov.diag_id;
      result.span = prov.span;
      return finish(result);
    }
    SPEC_RULE("P-Loop-Infinite");
    result.ok = true;
    result.prov = LoopProv(env, prov.breaks, prov.break_void);
    return finish(result);
  }

  if (const auto* loop_cond =
          std::get_if<ast::LoopConditionalExpr>(&expr->node)) {
    const auto cond = ProvExpr(ctx, loop_cond->cond, env, gamma, expr_map);
    if (!cond.ok) {
      return finish(cond);
    }
    if (!loop_cond->body) {
      result.ok = true;
      result.prov = BottomTag();
      return finish(result);
    }
    const auto prov = BreakProv(ctx, *loop_cond->body, env, gamma);
    if (!prov.ok) {
      result.ok = false;
      result.diag_id = prov.diag_id;
      result.span = prov.span;
      return finish(result);
    }
    SPEC_RULE("P-Loop-Conditional");
    result.ok = true;
    result.prov = LoopProv(env, prov.breaks, prov.break_void);
    return finish(result);
  }

  if (const auto* loop_iter =
          std::get_if<ast::LoopIterExpr>(&expr->node)) {
    const auto iter = ProvExpr(ctx, loop_iter->iter, env, gamma, expr_map);
    if (!iter.ok) {
      return finish(iter);
    }
    ProvEnv arm_env = env;
    std::vector<std::string> bind_names;
    if (loop_iter->pattern) {
      const auto names = PatNames(*loop_iter->pattern);
      bind_names.reserve(names.size());
      for (const auto& name : names) {
        bind_names.push_back(name);
      }
    }
    IntroAll_pi_inplace(arm_env, bind_names, iter.prov);
    if (!loop_iter->body) {
      result.ok = true;
      result.prov = BottomTag();
      return finish(result);
    }
    const auto prov = BreakProv(ctx, *loop_iter->body, arm_env, gamma);
    if (!prov.ok) {
      result.ok = false;
      result.diag_id = prov.diag_id;
      result.span = prov.span;
      return finish(result);
    }
    SPEC_RULE("P-Loop-Iter");
    result.ok = true;
    result.prov = LoopProv(env, prov.breaks, prov.break_void);
    return finish(result);
  }

  if (const auto* pipeline = std::get_if<ast::PipelineExpr>(&expr->node)) {
    const auto lhs = ProvExpr(ctx, pipeline->lhs, env, gamma, expr_map);
    if (!lhs.ok) {
      return finish(lhs);
    }
    const auto rhs = ProvExpr(ctx, pipeline->rhs, env, gamma, expr_map);
    if (!rhs.ok) {
      return finish(rhs);
    }
    SPEC_RULE("P-Pipeline");
    result.ok = true;
    result.prov = JoinProv(env, lhs.prov, rhs.prov);
    return finish(result);
  }

  if (const auto* closure = std::get_if<ast::ClosureExpr>(&expr->node)) {
    ProvEnv body_env = env;
    PushScope_pi_inplace(body_env);
    TypeEnv body_gamma = gamma;
    body_gamma.scopes.emplace_back();
    const auto param_prov = BindProv(body_env, BottomTag());
    for (std::size_t i = 0; i < closure->params.size(); ++i) {
      Intro_pi_inplace(body_env, closure->params[i].name, param_prov);
      if (const auto param_type = ClosureParamType(ctx, expr, *closure, i)) {
        body_gamma.scopes.back()[IdKeyOf(closure->params[i].name)] =
            TypeBinding{ast::Mutability::Let, *param_type};
      }
    }
    const auto body = ProvExpr(ctx, closure->body, body_env, body_gamma, expr_map);
    if (!body.ok) {
      return finish(body);
    }

    const auto captures = ClosureCaptureCollector(gamma).CollectClosure(*closure);
    if (captures.empty()) {
      SPEC_RULE("P-Closure-NonCapturing");
      result.ok = true;
      result.prov = GlobalTag();
      return finish(result);
    }

    const bool escaping = IsEscapingClosureExpr(ctx, expr);
    const ProvTag target = escaping ? FrameProv(gamma, env) : StackProv(env);
    std::vector<ProvTag> capture_provs;
    capture_provs.reserve(captures.size());
    for (const auto& capture : captures) {
      const auto prov = Lookup_pi(env, capture);
      if (!prov.has_value()) {
        continue;
      }
      if (escaping) {
        if (IsCapturedLocalSharedBinding(gamma, capture)) {
          SPEC_RULE("P-Closure-Escape-Err");
          ProvExprResult err;
          err.ok = false;
          err.diag_id = "E-CON-0086";
          err.span = expr->span;
          return finish(err);
        }
      }
      if (ProvLess(env, *prov, target)) {
        SPEC_RULE("P-Closure-Escape-Err");
        ProvExprResult err;
        err.ok = false;
        err.diag_id = "E-CON-0086";
        err.span = expr->span;
        return finish(err);
      }
      capture_provs.push_back(*prov);
    }

    SPEC_RULE("P-Closure-Capturing");
    result.ok = true;
    result.prov = JoinAllProv(env, capture_provs);
    return finish(result);
  }

  if (AsyncCreateExpr(ctx, gamma, expr, expr_map == nullptr)) {
    const auto args = AsyncCaptureArgs(expr);
    const auto frame_prov = FrameProv(gamma, env);
    for (const auto& arg : args) {
      const auto arg_res = ProvExpr(ctx, arg, env, gamma, expr_map);
      if (!arg_res.ok) {
        return finish(arg_res);
      }
      if (ProvLess(env, arg_res.prov, frame_prov)) {
        SPEC_RULE("Async-Capture-Err");
        ProvExprResult err;
        err.ok = false;
        err.diag_id = "Async-Capture-Err";
        err.span = expr->span;
        return finish(err);
      }
    }
    if (WarnAsyncCapture(args)) {
      SPEC_RULE("Warn-Async-LargeCapture");
    } else {
      SPEC_RULE("Warn-Async-LargeCapture-Ok");
    }
    SPEC_RULE("P-Async-Create");
    result.ok = true;
    result.prov = frame_prov;
    return finish(result);
  }

  if (const auto* call = std::get_if<ast::CallExpr>(&expr->node)) {
    if (CallExprIntroducesHeapProvenance(ctx, gamma, *call,
                                         expr_map == nullptr)) {
      const auto callee = ProvExpr(ctx, call->callee, env, gamma, expr_map);
      if (!callee.ok) {
        return finish(callee);
      }
      for (const auto& arg : call->args) {
        const auto arg_res = ProvExpr(ctx, arg.value, env, gamma, expr_map);
        if (!arg_res.ok) {
          return finish(arg_res);
        }
      }
      SPEC_RULE("P-Expr-Heap");
      result.ok = true;
      result.prov = HeapTag();
      return finish(result);
    }
  }

  if (const auto* call = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    if (MethodCallIntroducesHeapProvenance(ctx, gamma, *call,
                                           expr_map == nullptr)) {
      const auto recv = ProvExpr(ctx, call->receiver, env, gamma, expr_map);
      if (!recv.ok) {
        return finish(recv);
      }
      for (const auto& arg : call->args) {
        const auto arg_res = ProvExpr(ctx, arg.value, env, gamma, expr_map);
        if (!arg_res.ok) {
          return finish(arg_res);
        }
      }
      SPEC_RULE("P-Expr-Heap");
      result.ok = true;
      result.prov = HeapTag();
      return finish(result);
    }
  }

  if (IsPlaceExpr(expr)) {
    const auto place = ProvPlace(ctx, expr, env, gamma, expr_map);
    if (!place.ok) {
      return finish(place);
    }
    SPEC_RULE("P-Place-Expr");
    return finish(place);
  }

  if (std::holds_alternative<ast::BinaryExpr>(expr->node)) {
    struct BinaryFrame {
      ast::ExprPtr expr;
      bool children_ready = false;
    };

    std::vector<BinaryFrame> stack;
    stack.push_back(BinaryFrame{expr, false});
    std::unordered_map<const ast::Expr*, ProvExprResult> results;

    auto bottom_result = []() {
      ProvExprResult out;
      out.ok = true;
      out.prov = BottomTag();
      return out;
    };

    while (!stack.empty()) {
      BinaryFrame frame = std::move(stack.back());
      stack.pop_back();

      if (!frame.expr) {
        continue;
      }

      const auto* binary =
          std::get_if<ast::BinaryExpr>(&frame.expr->node);
      if (!binary) {
        auto child_res = ProvExpr(ctx, frame.expr, env, gamma, expr_map);
        if (!child_res.ok) {
          return finish(child_res);
        }
        results[frame.expr.get()] = std::move(child_res);
        continue;
      }

      if (!frame.children_ready) {
        stack.push_back(BinaryFrame{frame.expr, true});
        if (binary->rhs) {
          stack.push_back(BinaryFrame{binary->rhs, false});
        }
        if (binary->lhs) {
          stack.push_back(BinaryFrame{binary->lhs, false});
        }
        continue;
      }

      ProvExprResult left =
          binary->lhs && results.count(binary->lhs.get())
              ? results[binary->lhs.get()]
              : bottom_result();
      ProvExprResult right =
          binary->rhs && results.count(binary->rhs.get())
              ? results[binary->rhs.get()]
              : bottom_result();
      if (!left.ok) {
        return finish(left);
      }
      if (!right.ok) {
        return finish(right);
      }

      ProvExprResult joined;
      joined.ok = true;
      joined.prov = JoinProv(env, left.prov, right.prov);
      RecordExprProv(frame.expr, joined, env, expr_map);
      results[frame.expr.get()] = std::move(joined);
    }

    auto root_it = results.find(expr.get());
    if (root_it != results.end()) {
      SPEC_RULE("P-Expr-Sub");
      return finish(root_it->second);
    }
  }

  std::optional<ProvTag> joined_children;
  std::optional<ProvExprResult> first_error;
  ForEachChildLtr(expr, [&](const ast::ExprPtr& child) {
    if (first_error.has_value()) {
      return;
    }
    auto child_res = ProvExpr(ctx, child, env, gamma, expr_map);
    if (!child_res.ok) {
      first_error = std::move(child_res);
      return;
    }
    if (!joined_children.has_value()) {
      joined_children = child_res.prov;
      return;
    }
    joined_children = JoinProv(env, *joined_children, child_res.prov);
  });
  if (first_error.has_value()) {
    return finish(std::move(*first_error));
  }
  SPEC_RULE("P-Expr-Sub");
  result.ok = true;
  result.prov = joined_children.value_or(BottomTag());
  return finish(result);
}

static ProvStmtResult ProvStmt(const ScopeContext& ctx,
                               const ast::Stmt& stmt,
                               const ProvEnv& env,
                               const TypeEnv& gamma,
                               ExprProvTagMap* expr_map) {
  return std::visit(
      [&](const auto& node) -> ProvStmtResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          const auto init = ProvExpr(ctx, node.binding.init, env, gamma, expr_map);
          if (!init.ok) {
            return {false, init.diag_id, init.span, env,
                    gamma, {}};
          }
          const auto names = PatNames(node.binding.pat);
          auto bind_pi = BindProv(env, init.prov);
          ProvEnv out_env = env;

          const auto bind_type =
              BindingType(ctx, node.binding, gamma, expr_map == nullptr);
          TypeEnv out_gamma = gamma;
          bool fresh_region_expr = false;
          if (bind_type.has_value()) {
            const auto binds = PatternBindings(ctx, node.binding.pat, *bind_type);
            fresh_region_expr = IsFreshRegionExpr(node.binding.init);
            if (fresh_region_expr && bind_pi.kind != ProvKind::Region &&
                binds.size() == 1 && RegionActiveType(binds.front().second)) {
              bind_pi = RegionTag(IdKeyOf(binds.front().first));
            }
            IntroAll_pi_inplace(out_env, names, bind_pi);
            AddBindingsToTypeEnv(out_gamma, binds, ast::Mutability::Let, false);
            AddRegionBindingsToProvEnv(out_env, binds, bind_pi, fresh_region_expr);
          } else {
            IntroAll_pi_inplace(out_env, names, bind_pi);
          }
          SPEC_RULE("Prov-LetVar");
          return {true, std::nullopt, std::nullopt, std::move(out_env),
                  std::move(out_gamma), {}};
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          const auto init = ProvExpr(ctx, node.binding.init, env, gamma, expr_map);
          if (!init.ok) {
            return {false, init.diag_id, init.span, env,
                    gamma, {}};
          }
          const auto names = PatNames(node.binding.pat);
          auto bind_pi = BindProv(env, init.prov);
          ProvEnv out_env = env;

          const auto bind_type =
              BindingType(ctx, node.binding, gamma, expr_map == nullptr);
          TypeEnv out_gamma = gamma;
          bool fresh_region_expr = false;
          if (bind_type.has_value()) {
            const auto binds = PatternBindings(ctx, node.binding.pat, *bind_type);
            fresh_region_expr = IsFreshRegionExpr(node.binding.init);
            if (fresh_region_expr && bind_pi.kind != ProvKind::Region &&
                binds.size() == 1 && RegionActiveType(binds.front().second)) {
              bind_pi = RegionTag(IdKeyOf(binds.front().first));
            }
            IntroAll_pi_inplace(out_env, names, bind_pi);
            AddBindingsToTypeEnv(out_gamma, binds, ast::Mutability::Var, false);
            AddRegionBindingsToProvEnv(out_env, binds, bind_pi, fresh_region_expr);
          } else {
            IntroAll_pi_inplace(out_env, names, bind_pi);
          }
          SPEC_RULE("Prov-LetVar");
          return {true, std::nullopt, std::nullopt, std::move(out_env),
                  std::move(out_gamma), {}};
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expression and
          // no new storage. Provenance/type environments are unchanged.
          (void)node;
          return {true, std::nullopt, std::nullopt, env, gamma, {}};
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          const auto place = ProvPlace(ctx, node.place, env, gamma, expr_map);
          if (!place.ok) {
            return {false, place.diag_id, place.span, env,
                    gamma, {}};
          }
          const auto value = ProvExpr(ctx, node.value, env, gamma, expr_map);
          if (!value.ok) {
            return {false, value.diag_id, value.span, env,
                    gamma, {}};
          }
          if (ProvLess(env, value.prov, place.prov)) {
            if (AsyncSigForExpr(ctx, gamma, node.value, expr_map == nullptr)
                    .has_value()) {
              SPEC_RULE("Prov-Async-Escape-Err");
              return {false, "E-MEM-3020", node.span,
                      env, gamma, {}};
            }
            SPEC_RULE("Prov-Escape-Err");
            return {false, "E-MEM-3020", node.span, env,
                    gamma, {}};
          }
          SPEC_RULE("Prov-Assign");
          return {true, std::nullopt, std::nullopt, env,
                  gamma, {}};
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          const auto place = ProvPlace(ctx, node.place, env, gamma, expr_map);
          if (!place.ok) {
            return {false, place.diag_id, place.span, env,
                    gamma, {}};
          }
          const auto value = ProvExpr(ctx, node.value, env, gamma, expr_map);
          if (!value.ok) {
            return {false, value.diag_id, value.span, env,
                    gamma, {}};
          }
          if (ProvLess(env, value.prov, place.prov)) {
            if (AsyncSigForExpr(ctx, gamma, node.value, expr_map == nullptr)
                    .has_value()) {
              SPEC_RULE("Prov-Async-Escape-Err");
              return {false, "E-MEM-3020", node.span,
                      env, gamma, {}};
            }
            SPEC_RULE("Prov-Escape-Err");
            return {false, "E-MEM-3020", node.span, env,
                    gamma, {}};
          }
          SPEC_RULE("Prov-CompoundAssign");
          return {true, std::nullopt, std::nullopt, env,
                  gamma, {}};
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          const auto expr = ProvExpr(ctx, node.value, env, gamma, expr_map);
          if (!expr.ok) {
            return {false, expr.diag_id, expr.span, env,
                    gamma, {}};
          }
          SPEC_RULE("Prov-ExprStmt");
          return {true, std::nullopt, std::nullopt, env,
                  gamma, {}};
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          if (!node.body) {
            return {true, std::nullopt, std::nullopt, env,
                    gamma, {}};
          }
          const auto body = BlockProv(ctx, *node.body, env, gamma, expr_map);
          if (!body.ok) {
            return {false, body.diag_id, body.span, env,
                    gamma, {}};
          }
          SPEC_RULE("Prov-DeferStmt");
          return {true, std::nullopt, std::nullopt, env,
                  gamma, {}};
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          ast::ExprPtr opts_expr = node.opts_opt;
          if (!opts_expr) {
            opts_expr = MakeDefaultRegionOptionsExpr();
          }
          const auto opts = ProvExpr(ctx, opts_expr, env, gamma, expr_map);
          if (!opts.ok) {
            return {false, opts.diag_id, opts.span, env,
                    gamma, {}};
          }
          std::string name =
              node.alias_opt.has_value() ? *node.alias_opt
                                         : FreshRegionName(gamma);

          ProvEnv inner_env = env;
          PushScope_pi_inplace(inner_env);
          const auto bind_pi = BindProv(inner_env, BottomTag());
          Intro_pi_inplace(inner_env, name, bind_pi);
          inner_env.regions.push_back(
              RegionEntry{IdKeyOf(name), IdKeyOf(name), true});

          TypeEnv inner_gamma = gamma;
          inner_gamma.scopes.emplace_back();
          inner_gamma.scopes.back()[IdKeyOf(name)] =
              TypeBinding{ast::Mutability::Let, RegionActiveTypeRef()};

          if (node.body) {
            const auto body =
                BlockProv(ctx, *node.body, inner_env, inner_gamma, expr_map);
            if (!body.ok) {
              return {false, body.diag_id, body.span, env,
                      gamma, {}};
            }
          }
          SPEC_RULE("Prov-RegionStmt");
          return {true, std::nullopt, std::nullopt, env,
                  gamma, {}};
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          std::optional<std::string> target = node.target_opt;
          if (!target.has_value()) {
            target = InnermostActiveRegionName(gamma);
          } else {
            const auto type = LookupTypeForRegion(ctx, gamma, *target);
            if (!type.has_value() || !RegionActiveType(*type)) {
              target = std::nullopt;
            }
          }
          if (!target.has_value()) {
            SPEC_RULE("Prov-FrameStmt");
            return {true, std::nullopt, std::nullopt, env,
                    gamma, {}};
          }

          const std::string fresh = FreshRegionName(gamma);
          ProvEnv inner_env = env;
          PushScope_pi_inplace(inner_env);
          const auto bind_pi = BindProv(inner_env, BottomTag());
          Intro_pi_inplace(inner_env, fresh, bind_pi);
          inner_env.regions.push_back(
              RegionEntry{IdKeyOf(fresh), IdKeyOf(*target), true});

          TypeEnv inner_gamma = gamma;
          inner_gamma.scopes.emplace_back();
          inner_gamma.scopes.back()[IdKeyOf(fresh)] =
              TypeBinding{ast::Mutability::Let, RegionActiveTypeRef()};

          if (node.body) {
            const auto body =
                BlockProv(ctx, *node.body, inner_env, inner_gamma, expr_map);
            if (!body.ok) {
              return {false, body.diag_id, body.span, env,
                      gamma, {}};
            }
          }
          SPEC_RULE("Prov-FrameStmt");
          return {true, std::nullopt, std::nullopt, env,
                  gamma, {}};
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          if (!node.value_opt) {
            SPEC_RULE("Prov-Return-Unit");
            return {true, std::nullopt, std::nullopt, env,
                    gamma, {}};
          }
          const auto value = ProvExpr(ctx, node.value_opt, env, gamma, expr_map);
          if (!value.ok) {
            return {false, value.diag_id, value.span, env,
                    gamma, {}};
          }
          SPEC_RULE("Prov-Return");
          ProvFlow flow;
          flow.results.push_back(value.prov);
          return {true, std::nullopt, std::nullopt, env,
                  gamma, std::move(flow)};
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          if (!node.value_opt) {
            SPEC_RULE("Prov-Break-Unit");
            ProvFlow flow;
            flow.break_void = true;
            return {true, std::nullopt, std::nullopt, env,
                    gamma, std::move(flow)};
          }
          const auto value = ProvExpr(ctx, node.value_opt, env, gamma, expr_map);
          if (!value.ok) {
            return {false, value.diag_id, value.span, env,
                    gamma, {}};
          }
          SPEC_RULE("Prov-Break");
          ProvFlow flow;
          flow.breaks.push_back(value.prov);
          return {true, std::nullopt, std::nullopt, env,
                  gamma, std::move(flow)};
        } else if constexpr (std::is_same_v<T, ast::ContinueStmt>) {
          SPEC_RULE("Prov-Continue");
          return {true, std::nullopt, std::nullopt, env,
                  gamma, {}};
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          if (!node.body) {
            return {true, std::nullopt, std::nullopt, env,
                    gamma, {}};
          }
          const auto body = BlockProv(ctx, *node.body, env, gamma, expr_map);
          if (!body.ok) {
            return {false, body.diag_id, body.span, env,
                    gamma, {}};
          }
          SPEC_RULE("Prov-KeyBlockStmt");
          return {true, std::nullopt, std::nullopt, env,
                  gamma, {}};
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          if (!node.body) {
            return {true, std::nullopt, std::nullopt, env,
                    gamma, {}};
          }
          const auto body = BlockProv(ctx, *node.body, env, gamma, expr_map);
          if (!body.ok) {
            return {false, body.diag_id, body.span, env,
                    gamma, {}};
          }
          SPEC_RULE("Prov-UnsafeStmt");
          return {true, std::nullopt, std::nullopt, env,
                  gamma, {}};
        } else if constexpr (std::is_same_v<T, ast::ErrorStmt>) {
          SPEC_RULE("Prov-ErrorStmt");
          return {true, std::nullopt, std::nullopt, env,
                  gamma, {}};
        } else {
          return {true, std::nullopt, std::nullopt, env,
                  gamma, {}};
        }
      },
      stmt);
}

}  // namespace

TypeRef RegionOptionsTypeRef() {
  return MakeTypePath({"RegionOptions"});
}

ast::ExprPtr MakeDefaultRegionOptionsExpr() {
  auto ident = std::make_shared<ast::Expr>();
  ident->node = ast::IdentifierExpr{"RegionOptions"};
  ast::CallExpr call;
  call.callee = ident;
  call.args = {};
  auto expr = std::make_shared<ast::Expr>();
  expr->node = std::move(call);
  return expr;
}

TypeRef RegionActiveTypeRef() {
  return MakeTypePerm(Permission::Unique,
                      MakeTypeModalState({"Region"}, "Active"));
}

bool RegionActiveType(const TypeRef& type) {
  SpecDefsRegions();
  const auto stripped = StripPermOnce(type);
  if (!stripped) {
    return false;
  }
  const auto* modal = std::get_if<TypeModalState>(&stripped->node);
  if (!modal) {
    return false;
  }
  if (modal->path.size() != 1 || modal->path[0] != "Region") {
    return false;
  }
  return modal->state == "Active";
}

std::optional<std::string> InnermostActiveRegion(const TypeEnv& env) {
  SpecDefsRegions();
  for (auto it = env.scopes.rbegin(); it != env.scopes.rend(); ++it) {
    std::optional<std::string> best;
    for (const auto& [key, binding] : *it) {
      if (!RegionActiveType(binding.type)) {
        continue;
      }
      if (!best.has_value() || key < *best) {
        best = key;
      }
    }
    if (best.has_value()) {
      return best;
    }
  }
  return std::nullopt;
}

std::string FreshRegionName(const TypeEnv& env) {
  SpecDefsRegions();
  for (std::size_t i = 0;; ++i) {
    std::string name = "region$" + std::to_string(i);
    const auto key = IdKeyOf(name);
    bool used = false;
    for (const auto& scope : env.scopes) {
      if (scope.find(key) != scope.end()) {
        used = true;
        break;
      }
    }
    if (!used) {
      return name;
    }
  }
}

struct ParamProvInit {
  std::vector<std::string> names;
  std::vector<ProvTag> tags;
  std::vector<RegionEntry> regions;
};

static bool AstRegionActiveType(const std::shared_ptr<ast::Type>& type) {
  const ast::Type* cur = type.get();
  while (cur) {
    if (const auto* perm = std::get_if<ast::TypePermType>(&cur->node)) {
      cur = perm->base.get();
      continue;
    }
    if (const auto* refine = std::get_if<ast::TypeRefine>(&cur->node)) {
      cur = refine->base.get();
      continue;
    }
    if (const auto* modal = std::get_if<ast::TypeModalState>(&cur->node)) {
      if (modal->path.empty()) {
        return false;
      }
      return IdEq(modal->path.back(), "Region") && IdEq(modal->state, "Active");
    }
    return false;
  }
  return false;
}

static ParamProvInit BuildParamProvInit(
    const ScopeContext& ctx,
    const std::vector<ast::Param>& params,
    const std::optional<BindSelfParam>& self_param) {
  ParamProvInit init;
  if (self_param.has_value()) {
    init.names.push_back("self");
    init.tags.push_back(ParamTag(init.tags.size()));
  }
  for (const auto& param : params) {
    init.names.push_back(param.name);
    const auto lowered = LocalLowerType(ctx, param.type);
    const bool is_region_param =
        (lowered.ok && lowered.type && RegionActiveType(lowered.type)) ||
        AstRegionActiveType(param.type);
    if (is_region_param) {
      const auto region_key = IdKeyOf(param.name);
      init.tags.push_back(RegionTag(region_key));
      // Region-valued parameters participate in region ordering for ProvLess,
      // but do not make the callee frame itself region-scoped.
      init.regions.push_back(RegionEntry{region_key, region_key, false});
      continue;
    }
    init.tags.push_back(ParamTag(init.tags.size()));
  }
  return init;
}

class ScopedProvContextOverride {
 public:
  ScopedProvContextOverride(const ScopeContext& ctx,
                            const ast::ModulePath& module_path)
      : ctx_(const_cast<ScopeContext&>(ctx)),
        saved_current_module_(std::move(ctx_.current_module)),
        saved_scopes_(std::move(ctx_.scopes)) {
    // Provenance checks are module-level judgments. Keep sigma/project but
    // clear ambient lexical scopes so call resolution uses module declarations
    // deterministically (matching codegen provenance evaluation).
    ctx_.current_module = module_path;
    ctx_.scopes.clear();
  }

  ScopedProvContextOverride(const ScopedProvContextOverride&) = delete;
  ScopedProvContextOverride& operator=(const ScopedProvContextOverride&) =
      delete;

  ~ScopedProvContextOverride() {
    ctx_.current_module = std::move(saved_current_module_);
    ctx_.scopes = std::move(saved_scopes_);
  }

  const ScopeContext& view() const { return ctx_; }

 private:
  ScopeContext& ctx_;
  ast::ModulePath saved_current_module_;
  ScopeList saved_scopes_;
};

void LogProvenancePerfSummary() {
  if (!RegionsPerfEnabled()) {
    return;
  }
  const auto& stats = RegionsPerfStats();
  if (stats.body_calls == 0) {
    return;
  }
  std::fprintf(stderr,
               "[cursive] sema perf=provenance body_calls=%llu "
               "block_prov_us=%llu static_bindings_us=%llu "
               "find_module_calls=%llu find_module_scanned=%llu "
               "static_binding_items_scanned=%llu\n",
               static_cast<unsigned long long>(stats.body_calls),
               static_cast<unsigned long long>(stats.block_prov_us),
               static_cast<unsigned long long>(stats.static_bindings_us),
               static_cast<unsigned long long>(stats.find_module_calls),
               static_cast<unsigned long long>(stats.find_module_scanned),
               static_cast<unsigned long long>(stats.static_binding_items_scanned));
  std::fflush(stderr);
}

ProvCheckResult ProvBindCheck(const ScopeContext& ctx,
                              const ast::ModulePath& module_path,
                              const std::vector<ast::Param>& params,
                              const std::shared_ptr<ast::Block>& body,
                              const std::optional<BindSelfParam>& self_param) {
  SpecDefsRegions();
  auto& perf = RegionsPerfStats();
  const bool perf_on = RegionsPerfActive();
  if (perf_on) {
    ++perf.body_calls;
  }
  ProvCheckResult result;
  if (!body) {
    result.ok = true;
    return result;
  }

  ScopedProvContextOverride scoped_ctx(ctx, module_path);
  const ScopeContext& prov_ctx = scoped_ctx.view();
  const auto param_init = BuildParamProvInit(prov_ctx, params, self_param);
  ProvEnv prov_env = InitProvEnv(param_init.names, param_init.tags,
                                 param_init.regions);

  const auto static_bindings = StaticBindings(prov_ctx, module_path);
  if (!static_bindings.empty()) {
    ProvScope static_scope;
    static_scope.id = prov_env.next_scope_id++;
    for (const auto& binding : static_bindings) {
      static_scope.map.emplace(IdKeyOf(binding.name), GlobalTag());
    }
    prov_env.scopes.insert(prov_env.scopes.begin(), std::move(static_scope));
  }

  TypeEnv gamma;
  gamma.scopes.emplace_back();
  if (!static_bindings.empty()) {
    for (const auto& binding : static_bindings) {
      gamma.scopes.front()[IdKeyOf(binding.name)] =
          TypeBinding{binding.mut, binding.type};
    }
  }
  gamma.scopes.emplace_back();
  ParamTypeMap(prov_ctx, params, self_param, gamma);

  const auto block = [&]() {
    ScopedProvTimer timer(perf_on ? &perf.block_prov_us : nullptr);
    return BlockProv(prov_ctx, *body, prov_env, gamma, nullptr);
  }();
  if (!block.ok) {
    result.ok = false;
    result.diag_id = block.diag_id;
    result.span = block.span;
    return result;
  }

  result.ok = true;
  return result;
}

static ProvenanceKind ToPublicKind(const ProvTag& tag) {
  switch (tag.kind) {
    case ProvKind::Global:
      return ProvenanceKind::Global;
    case ProvKind::Stack:
      return ProvenanceKind::Stack;
    case ProvKind::Heap:
      return ProvenanceKind::Heap;
    case ProvKind::Region:
      return ProvenanceKind::Region;
    case ProvKind::Bottom:
      return ProvenanceKind::Bottom;
    case ProvKind::Param:
      return ProvenanceKind::Param;
  }
  return ProvenanceKind::Bottom;
}

ExprProvMapResult ComputeExprProvenanceMap(
    const ScopeContext& ctx,
    const ast::ModulePath& module_path,
    const std::vector<ast::Param>& params,
    const std::shared_ptr<ast::Block>& body,
    const std::optional<BindSelfParam>& self_param) {
  SpecDefsRegions();
  ExprProvMapResult result;
  if (!body) {
    result.ok = true;
    return result;
  }

  ScopedProvContextOverride scoped_ctx(ctx, module_path);
  const ScopeContext& prov_ctx = scoped_ctx.view();
  const auto param_init = BuildParamProvInit(prov_ctx, params, self_param);
  ProvEnv prov_env = InitProvEnv(param_init.names, param_init.tags,
                                 param_init.regions);

  const auto static_bindings = StaticBindings(prov_ctx, module_path);
  if (!static_bindings.empty()) {
    ProvScope static_scope;
    static_scope.id = prov_env.next_scope_id++;
    for (const auto& binding : static_bindings) {
      static_scope.map.emplace(IdKeyOf(binding.name), GlobalTag());
    }
    prov_env.scopes.insert(prov_env.scopes.begin(), std::move(static_scope));
  }

  TypeEnv gamma;
  gamma.scopes.emplace_back();
  if (!static_bindings.empty()) {
    for (const auto& binding : static_bindings) {
      gamma.scopes.front()[IdKeyOf(binding.name)] =
          TypeBinding{binding.mut, binding.type};
    }
  }
  gamma.scopes.emplace_back();
  ParamTypeMap(prov_ctx, params, self_param, gamma);

  ExprProvTagMap expr_map;
  const auto block = BlockProv(prov_ctx, *body, prov_env, gamma, &expr_map);
  if (!block.ok) {
    result.ok = false;
    result.diag_id = block.diag_id;
    result.span = block.span;
    return result;
  }

  result.ok = true;
  result.expr_prov.reserve(expr_map.size());
  for (const auto& [expr_ptr, info] : expr_map) {
    result.expr_prov.emplace(expr_ptr, ToPublicKind(info.tag));
    if (info.tag.kind == ProvKind::Region) {
      result.expr_region_tags.emplace(expr_ptr, info.tag.region);
    }
    if (info.tag.kind == ProvKind::Region && info.target.has_value()) {
      result.expr_region_targets.emplace(expr_ptr, *info.target);
    }
  }
  return result;
}

}  // namespace cursive::analysis
