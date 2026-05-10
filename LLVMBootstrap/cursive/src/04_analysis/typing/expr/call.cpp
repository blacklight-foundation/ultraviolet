// =================================================================
// File: 04_analysis/typing/expr/call.cpp
// Construct: Call Expression Type Checking
// Spec Section: 5.2.12, 13.1.2
// Spec Rules: T-Call, T-Generic-Call
// =================================================================

#include "04_analysis/typing/expr/call.h"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/process_config.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/composite/records.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/keys/key_paths.h"
#include "04_analysis/memory/calls.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_decls.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/deprecation_warnings.h"
#include "04_analysis/typing/typecheck.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsCall() {
  SPEC_DEF("T-Call", "5.2.12");
  SPEC_DEF("T-Generic-Call", "13.1.2");
  SPEC_DEF("T-Record-Default", "5.2.12");
  SPEC_DEF("FFI-Arg-RegionLocalRawPtr-Err", "23.5.4");
  SPEC_DEF("Barrier-Outside-Err", "20.2.4");
  SPEC_DEF("GpuIntrinsic-Outside-Err", "20.2.4");
}

struct CallLookupPerfStats {
  std::uint64_t find_module_calls = 0;
  std::uint64_t find_module_scanned = 0;
  std::uint64_t find_proc_in_module_calls = 0;
  std::uint64_t find_proc_in_module_scanned_items = 0;
  std::uint64_t find_extern_in_module_calls = 0;
  std::uint64_t find_extern_in_module_scanned_items = 0;
  std::uint64_t lookup_proc_callee_calls = 0;
  std::uint64_t lookup_proc_callee_us = 0;
  std::uint64_t is_extern_calls = 0;
  std::uint64_t is_extern_us = 0;
  std::uint64_t lookup_extern_callee_calls = 0;
  std::uint64_t lookup_extern_callee_us = 0;
};

static CallLookupPerfStats& CallPerfStats() {
  static CallLookupPerfStats stats;
  return stats;
}

static bool CallPerfEnabled() {
  return core::IsDebugEnabled("sema") || core::IsDebugEnabled("pipeline") ||
         core::IsDebugEnabled("typeperf");
}

static bool CallPerfActive() {
  static const bool enabled = CallPerfEnabled();
  return enabled;
}

class ScopedCallTimer {
 public:
  explicit ScopedCallTimer(std::uint64_t* slot)
      : slot_(slot), start_(std::chrono::steady_clock::now()) {}

  ~ScopedCallTimer() {
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

struct ExternLookupEntry {
  const ast::ExternBlock* block = nullptr;
  const ast::ExternProcDecl* proc = nullptr;
};

struct ProcLikeLookupEntry {
  const ast::ProcedureDecl* proc = nullptr;
  const ast::ComptimeProcedureDecl* comptime_proc = nullptr;
};

struct CallLookupIndex {
  const void* sigma_key = nullptr;
  std::size_t module_count = 0;
  std::map<PathKey, const ast::ASTModule*> modules_by_path;
  std::unordered_map<const ast::ASTModule*,
                     std::unordered_map<IdKey, ProcLikeLookupEntry>>
      procedures_by_module;
  std::unordered_map<const ast::ASTModule*,
                     std::unordered_map<IdKey, ExternLookupEntry>>
      externs_by_module;
};

static const CallLookupIndex& GetCallLookupIndex(const ScopeContext& ctx) {
  thread_local CallLookupIndex index;
  const analysis::Sigma* sigma_ptr =
      ctx.sigma_source ? ctx.sigma_source : &ctx.sigma;
  const analysis::Sigma& sigma = *sigma_ptr;
  const void* sigma_key = static_cast<const void*>(sigma_ptr);
  if (index.sigma_key == sigma_key &&
      index.module_count == sigma.mods.size()) {
    return index;
  }

  index = {};
  index.sigma_key = sigma_key;
  index.module_count = sigma.mods.size();
  index.procedures_by_module.reserve(sigma.mods.size());
  index.externs_by_module.reserve(sigma.mods.size());

  for (const auto& mod : sigma.mods) {
    const auto* mod_ptr = &mod;
    index.modules_by_path.emplace(PathKeyOf(mod.path), mod_ptr);

    auto& proc_map = index.procedures_by_module[mod_ptr];
    auto& ext_map = index.externs_by_module[mod_ptr];
    for (const auto& item : mod.items) {
      if (const auto* proc = std::get_if<ast::ProcedureDecl>(&item)) {
        proc_map.emplace(IdKeyOf(proc->name), ProcLikeLookupEntry{proc, nullptr});
        continue;
      }
      if (const auto* proc = std::get_if<ast::ComptimeProcedureDecl>(&item)) {
        proc_map.emplace(IdKeyOf(proc->name), ProcLikeLookupEntry{nullptr, proc});
        continue;
      }
      if (const auto* block = std::get_if<ast::ExternBlock>(&item)) {
        for (const auto& ext_item : block->items) {
          if (const auto* ext_proc = std::get_if<ast::ExternProcDecl>(&ext_item)) {
            ext_map.emplace(IdKeyOf(ext_proc->name),
                            ExternLookupEntry{block, ext_proc});
          }
        }
      }
    }
  }

  return index;
}

static const ast::ASTModule* FindModuleByPathForGeneric(
    const ScopeContext& ctx,
    const ast::ModulePath& path) {
  auto& perf = CallPerfStats();
  const bool perf_on = CallPerfActive();
  if (perf_on) {
    ++perf.find_module_calls;
    ++perf.find_module_scanned;
  }
  const auto& index = GetCallLookupIndex(ctx);
  const auto it = index.modules_by_path.find(PathKeyOf(path));
  if (it != index.modules_by_path.end()) {
    return it->second;
  }
  return nullptr;
}

static std::optional<ProcLikeLookupEntry> FindProcedureInModule(
    const ScopeContext& ctx,
    const ast::ASTModule& module,
    std::string_view name) {
  auto& perf = CallPerfStats();
  const bool perf_on = CallPerfActive();
  if (perf_on) {
    ++perf.find_proc_in_module_calls;
    ++perf.find_proc_in_module_scanned_items;
  }
  const auto& index = GetCallLookupIndex(ctx);
  const auto mod_it = index.procedures_by_module.find(&module);
  if (mod_it == index.procedures_by_module.end()) {
    return std::nullopt;
  }
  const auto proc_it = mod_it->second.find(IdKeyOf(name));
  if (proc_it != mod_it->second.end()) {
    return proc_it->second;
  }
  return std::nullopt;
}

struct CalleeProcedureLookupResult {
  const ast::ProcedureDecl* proc = nullptr;
  std::optional<ast::ProcedureDecl> proc_view;
  bool is_comptime_proc = false;
  ast::ModulePath origin;
  std::string name;
};

static ast::ProcedureDecl AsProcedureDecl(const ast::ComptimeProcedureDecl& decl) {
  ast::ProcedureDecl proc;
  proc.attrs = decl.attrs;
  proc.vis = decl.vis;
  proc.name = decl.name;
  proc.generic_params = decl.generic_params;
  proc.params = decl.params;
  proc.return_type_opt = decl.return_type_opt;
  proc.predicate_clause_opt = std::nullopt;
  proc.contract = decl.contract;
  proc.body = decl.body;
  proc.span = decl.span;
  proc.doc = decl.doc;
  return proc;
}

static bool IsComptimeTypingEnv(const TypeEnv& env) {
  return BindOf(env, "diagnostics").has_value() ||
         BindOf(env, "introspect").has_value() ||
         BindOf(env, "emitter").has_value() ||
         BindOf(env, "files").has_value() ||
         BindOf(env, "target").has_value();
}

static std::optional<CalleeProcedureLookupResult> LookupProcedureForCallee(
    const ScopeContext& ctx,
    const ast::ExprPtr& callee) {
  auto& perf = CallPerfStats();
  const bool perf_on = CallPerfActive();
  if (perf_on) {
    ++perf.lookup_proc_callee_calls;
  }
  ScopedCallTimer timer(perf_on ? &perf.lookup_proc_callee_us : nullptr);
  if (!callee) {
    return std::nullopt;
  }

  std::string name;
  std::optional<ast::ModulePath> origin;

  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&callee->node)) {
    const auto ent = ResolveValueName(ctx, ident->name);
    if (ent && ent->origin_opt.has_value()) {
      origin = *ent->origin_opt;
      name = ent->target_opt.value_or(std::string(ident->name));
    } else {
      // Fall back to current-module lookup for local procedures.
      origin = ctx.current_module;
      name = ident->name;
    }
  } else if (const auto* qualified =
                 std::get_if<ast::QualifiedNameExpr>(&callee->node)) {
    origin = qualified->path;
    name = qualified->name;
  } else if (const auto* path_expr = std::get_if<ast::PathExpr>(&callee->node)) {
    origin = path_expr->path.empty() ? ctx.current_module : path_expr->path;
    name = path_expr->name;
  } else {
    return std::nullopt;
  }

  if (!origin.has_value()) {
    return std::nullopt;
  }

  const auto* module = FindModuleByPathForGeneric(ctx, *origin);
  if (!module) {
    return std::nullopt;
  }

  const auto proc = FindProcedureInModule(ctx, *module, name);
  if (!proc.has_value()) {
    return std::nullopt;
  }

  CalleeProcedureLookupResult result;
  if (proc->proc) {
    result.proc = proc->proc;
  } else if (proc->comptime_proc) {
    result.proc_view = AsProcedureDecl(*proc->comptime_proc);
    result.proc = &*result.proc_view;
    result.is_comptime_proc = true;
  }
  result.origin = *origin;
  result.name = std::move(name);
  return result;
}

struct FormalSharedPathRef {
  std::size_t param_index = 0;
  std::vector<KeyPathSeg> prefix;
};

struct FormalKeyAccess {
  std::size_t param_index = 0;
  std::vector<KeyPathSeg> suffix;
  ast::KeyMode mode = ast::KeyMode::Read;
};

struct ProcedureKeyAccessSummary {
  std::vector<FormalKeyAccess> accesses;
  bool unknown = false;
};

static bool TypeIsSharedParamSurface(const ast::TypePtr& type) {
  if (!type) {
    return false;
  }

  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return node.perm == ast::TypePerm::Shared;
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return TypeIsSharedParamSurface(node.base);
        }
        return false;
      },
      type->node);
}

static std::unordered_map<IdKey, std::size_t> SharedParamIndexMap(
    const ast::ProcedureDecl& proc) {
  std::unordered_map<IdKey, std::size_t> indices;
  for (std::size_t i = 0; i < proc.params.size(); ++i) {
    if (TypeIsSharedParamSurface(proc.params[i].type)) {
      indices.emplace(IdKeyOf(proc.params[i].name), i);
    }
  }
  return indices;
}

static bool KeyModeSufficient(ast::KeyMode held, ast::KeyMode required) {
  if (held == ast::KeyMode::Write) {
    return true;
  }
  return held == required;
}

static std::optional<ast::KeyMode> CoveringKeyMode(
    const StmtTypeContext& type_ctx,
    const KeyPath& path) {
  std::optional<ast::KeyMode> best;
  for (const auto& held : type_ctx.held_key_paths) {
    if (!KeyPathContains(held.path, path)) {
      continue;
    }
    if (!best.has_value() || held.mode == ast::KeyMode::Write) {
      best = held.mode;
    }
  }
  return best;
}

static bool IsUniqueParamSurface(const ast::TypePtr& type) {
  if (!type) {
    return false;
  }
  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return node.perm == ast::TypePerm::Unique;
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return IsUniqueParamSurface(node.base);
        }
        return false;
      },
      type->node);
}

static std::optional<std::string_view> CheckSharedArgWriteRequirement(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const TypeEnv& env,
    const ast::ProcedureDecl& proc,
    const std::vector<ast::Arg>& args,
    const ExprTypeFn& type_expr) {
  for (std::size_t i = 0; i < args.size() && i < proc.params.size(); ++i) {
    if (!IsUniqueParamSurface(proc.params[i].type)) {
      continue;
    }

    const auto arg_expr = args[i].moved ? MovedArgExpr(args[i]) : args[i].value;
    const auto arg_typed = type_expr(arg_expr);
    if (!arg_typed.ok || !arg_typed.type ||
        PermOfType(arg_typed.type) != Permission::Shared) {
      continue;
    }

    const auto built = BuildKeyPath(arg_expr);
    if (!built.success) {
      continue;
    }

    const auto covering_mode = CoveringKeyMode(type_ctx, built.path);
    if (!covering_mode.has_value()) {
      continue;
    }
    if (*covering_mode != ast::KeyMode::Write) {
      return "E-CON-0005";
    }
  }

  (void)ctx;
  (void)env;
  return std::nullopt;
}

static bool HeldPrefixExistsForPath(
    const std::vector<HeldKeyTypingInfo>& held_key_paths,
    const KeyPath& path,
    std::optional<ast::KeyMode> required_mode = std::nullopt) {
  for (const auto& held : held_key_paths) {
    if (!KeyPathContains(held.path, path)) {
      continue;
    }
    if (!required_mode.has_value() ||
        KeyModeSufficient(held.mode, *required_mode)) {
      return true;
    }
  }
  return false;
}

class ProcedureKeyAccessSummaryBuilder {
 public:
  explicit ProcedureKeyAccessSummaryBuilder(const ScopeContext& ctx)
      : ctx_(ctx) {}

  ProcedureKeyAccessSummary Summarize(const ast::ProcedureDecl& proc) {
    if (const auto it = cache_.find(&proc); it != cache_.end()) {
      return it->second;
    }
    if (active_.find(&proc) != active_.end()) {
      ProcedureKeyAccessSummary recursive;
      recursive.unknown = true;
      return recursive;
    }

    active_.insert(&proc);
    ProcedureKeyAccessSummary summary;
    if (!proc.body) {
      summary.unknown = true;
    } else {
      const auto shared_params = SharedParamIndexMap(proc);
      AliasEnv aliases;
      VisitBlock(*proc.body, shared_params, aliases, summary);
    }
    active_.erase(&proc);
    cache_[&proc] = summary;
    return summary;
  }

 private:
  using SharedParamMap = std::unordered_map<IdKey, std::size_t>;
  using AliasEnv = std::unordered_map<IdKey, FormalSharedPathRef>;

  const ScopeContext& ctx_;
  std::unordered_map<const ast::ProcedureDecl*, ProcedureKeyAccessSummary> cache_;
  std::unordered_set<const ast::ProcedureDecl*> active_;

  static void AppendSegments(std::vector<KeyPathSeg>& out,
                             const std::vector<KeyPathSeg>& extra) {
    out.insert(out.end(), extra.begin(), extra.end());
  }

  static std::optional<IdKey> SimpleAssignedRoot(const ast::ExprPtr& place) {
    if (!place) {
      return std::nullopt;
    }
    if (const auto* ident = std::get_if<ast::IdentifierExpr>(&place->node)) {
      return IdKeyOf(ident->name);
    }
    return std::nullopt;
  }

  static std::optional<IdKey> BindingNameOf(const ast::PatternPtr& pat) {
    if (!pat) {
      return std::nullopt;
    }
    if (const auto* ident = std::get_if<ast::IdentifierPattern>(&pat->node)) {
      return IdKeyOf(ident->name);
    }
    if (const auto* typed = std::get_if<ast::TypedPattern>(&pat->node)) {
      if (typed->name == "_") {
        return std::nullopt;
      }
      return IdKeyOf(typed->name);
    }
    return std::nullopt;
  }

  static std::optional<FormalSharedPathRef> ResolveFormalPathFromKeyPath(
      const KeyPath& path,
      const SharedParamMap& shared_params,
      const AliasEnv& aliases) {
    if (const auto it = shared_params.find(IdKey(path.root));
        it != shared_params.end()) {
      FormalSharedPathRef ref;
      ref.param_index = it->second;
      ref.prefix = path.segs;
      return ref;
    }

    const auto alias_it = aliases.find(IdKey(path.root));
    if (alias_it == aliases.end()) {
      return std::nullopt;
    }

    FormalSharedPathRef ref = alias_it->second;
    AppendSegments(ref.prefix, path.segs);
    return ref;
  }

  static std::optional<FormalSharedPathRef> ResolveFormalPath(
      const ast::ExprPtr& expr,
      const SharedParamMap& shared_params,
      const AliasEnv& aliases) {
    const auto built = BuildKeyPath(expr);
    if (!built.success) {
      return std::nullopt;
    }
    return ResolveFormalPathFromKeyPath(built.path, shared_params, aliases);
  }

  static void AddAccess(ProcedureKeyAccessSummary& summary,
                        const FormalSharedPathRef& ref,
                        ast::KeyMode mode) {
    for (const auto& existing : summary.accesses) {
      if (existing.param_index != ref.param_index || existing.mode != mode ||
          existing.suffix.size() != ref.prefix.size()) {
        continue;
      }
      bool same = true;
      for (std::size_t i = 0; i < existing.suffix.size(); ++i) {
        if (!SegmentsEqual(existing.suffix[i], ref.prefix[i])) {
          same = false;
          break;
        }
      }
      if (same) {
        return;
      }
    }

    FormalKeyAccess access;
    access.param_index = ref.param_index;
    access.suffix = ref.prefix;
    access.mode = mode;
    summary.accesses.push_back(std::move(access));
  }

  void MarkUnknownIfSharedActual(const ast::ProcedureDecl& callee,
                                 const std::vector<ast::Arg>& args,
                                 const SharedParamMap& shared_params,
                                 const AliasEnv& aliases,
                                 ProcedureKeyAccessSummary& summary) {
    for (std::size_t i = 0; i < args.size() && i < callee.params.size(); ++i) {
      if (!TypeIsSharedParamSurface(callee.params[i].type)) {
        continue;
      }
      if (ResolveFormalPath(args[i].value, shared_params, aliases).has_value()) {
        summary.unknown = true;
        return;
      }
    }
  }

  void MergeNestedCallSummary(const ast::ProcedureDecl& callee,
                              const std::vector<ast::Arg>& args,
                              const SharedParamMap& shared_params,
                              const AliasEnv& aliases,
                              ProcedureKeyAccessSummary& summary) {
    const auto nested = Summarize(callee);
    if (nested.unknown) {
      MarkUnknownIfSharedActual(callee, args, shared_params, aliases, summary);
    }

    for (const auto& access : nested.accesses) {
      if (access.param_index >= args.size()) {
        continue;
      }
      const auto actual_ref =
          ResolveFormalPath(args[access.param_index].value, shared_params, aliases);
      if (!actual_ref.has_value()) {
        continue;
      }

      FormalSharedPathRef lifted = *actual_ref;
      AppendSegments(lifted.prefix, access.suffix);
      AddAccess(summary, lifted, access.mode);
    }
  }

  void HandleCallExpr(const ast::CallExpr& call,
                      const SharedParamMap& shared_params,
                      AliasEnv& aliases,
                      ProcedureKeyAccessSummary& summary) {
    if (!call.callee) {
      return;
    }

    const auto lookup = LookupProcedureForCallee(ctx_, call.callee);
    if (!lookup.has_value() || !lookup->proc || lookup->proc_view.has_value()) {
      // Extern / unresolved / compiler-synthesized callees remain unknown.
      summary.unknown = true;
      return;
    }

    MergeNestedCallSummary(*lookup->proc, call.args, shared_params, aliases,
                           summary);
  }

  void VisitApplyArgs(const ast::ApplyArgs& args,
                      const SharedParamMap& shared_params,
                      AliasEnv& aliases,
                      ProcedureKeyAccessSummary& summary) {
    std::visit(
        [&](const auto& apply_args) {
          using T = std::decay_t<decltype(apply_args)>;
          if constexpr (std::is_same_v<T, ast::ParenArgs>) {
            for (const auto& arg : apply_args.args) {
              VisitExpr(arg.value, shared_params, aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::BraceArgs>) {
            for (const auto& field : apply_args.fields) {
              VisitExpr(field.value, shared_params, aliases, summary);
            }
          }
        },
        args);
  }

  void VisitExpr(const ast::ExprPtr& expr,
                 const SharedParamMap& shared_params,
                 AliasEnv& aliases,
                 ProcedureKeyAccessSummary& summary) {
    if (!expr) {
      return;
    }

    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<T, ast::CallExpr>) {
            HandleCallExpr(node, shared_params, aliases, summary);
            VisitExpr(node.callee, shared_params, aliases, summary);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value, shared_params, aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
            VisitApplyArgs(node.args, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
            if (ResolveFormalPath(node.receiver, shared_params, aliases).has_value()) {
              summary.unknown = true;
            }
            VisitExpr(node.receiver, shared_params, aliases, summary);
            for (const auto& arg : node.args) {
              VisitExpr(arg.value, shared_params, aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
            if (node.block) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.block, shared_params, inner_aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
            VisitExpr(node.cond, shared_params, aliases, summary);
            VisitExpr(node.then_expr, shared_params, aliases, summary);
            VisitExpr(node.else_expr, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
            VisitExpr(node.scrutinee, shared_params, aliases, summary);
            for (const auto& arm : node.cases) {
              VisitExpr(arm.body, shared_params, aliases, summary);
            }
            VisitExpr(node.else_expr, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
            VisitExpr(node.scrutinee, shared_params, aliases, summary);
            VisitExpr(node.then_expr, shared_params, aliases, summary);
            VisitExpr(node.else_expr, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
            if (node.invariant_opt) {
              VisitExpr(node.invariant_opt->predicate, shared_params, aliases,
                        summary);
            }
            if (node.body) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.body, shared_params, inner_aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
            VisitExpr(node.cond, shared_params, aliases, summary);
            if (node.invariant_opt) {
              VisitExpr(node.invariant_opt->predicate, shared_params, aliases,
                        summary);
            }
            if (node.body) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.body, shared_params, inner_aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
            VisitExpr(node.iter, shared_params, aliases, summary);
            if (node.invariant_opt) {
              VisitExpr(node.invariant_opt->predicate, shared_params, aliases,
                        summary);
            }
            if (node.body) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.body, shared_params, inner_aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            VisitExpr(node.lhs, shared_params, aliases, summary);
            VisitExpr(node.rhs, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
            VisitExpr(node.value, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
            VisitExpr(node.value, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
            VisitExpr(node.place, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
            VisitExpr(node.place, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
            VisitExpr(node.value, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
            VisitExpr(node.value, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
            for (const auto& elem : node.elements) {
              VisitExpr(elem, shared_params, aliases, summary);
            }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            VisitExpr(elem, shared_params, aliases, summary);
          });
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          VisitExpr(node.value, shared_params, aliases, summary);
          VisitExpr(node.count, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
            for (const auto& field : node.fields) {
              VisitExpr(field.value, shared_params, aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
            if (node.payload_opt) {
              std::visit(
                  [&](const auto& payload) {
                    using P = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                      for (const auto& elem : payload.elements) {
                        VisitExpr(elem, shared_params, aliases, summary);
                      }
                    } else if constexpr (std::is_same_v<P, ast::EnumPayloadBrace>) {
                      for (const auto& field : payload.fields) {
                        VisitExpr(field.value, shared_params, aliases, summary);
                      }
                    }
                  },
                  *node.payload_opt);
            }
          } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
            VisitExpr(node.base, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
            VisitExpr(node.base, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
            VisitExpr(node.base, shared_params, aliases, summary);
            VisitExpr(node.index, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
            VisitExpr(node.lhs, shared_params, aliases, summary);
            VisitExpr(node.rhs, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
            VisitExpr(node.expr, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
            if (node.body) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.body, shared_params, inner_aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
            if (node.body) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.body, shared_params, inner_aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
            VisitExpr(node.range, shared_params, aliases, summary);
            if (node.body) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.body, shared_params, inner_aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
            if (node.block) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.block, shared_params, inner_aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::ComptimeExpr>) {
            VisitExpr(node.body, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::CtIfExpr>) {
            VisitExpr(node.cond, shared_params, aliases, summary);
            if (node.then_block) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.then_block, shared_params, inner_aliases, summary);
            }
            if (node.else_block_opt) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.else_block_opt, shared_params, inner_aliases,
                         summary);
            }
          } else if constexpr (std::is_same_v<T, ast::CtLoopIterExpr>) {
            VisitExpr(node.iter, shared_params, aliases, summary);
            if (node.body) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.body, shared_params, inner_aliases, summary);
            }
          }
        },
        expr->node);
  }

  void VisitStmt(const ast::Stmt& stmt,
                 const SharedParamMap& shared_params,
                 AliasEnv& aliases,
                 ProcedureKeyAccessSummary& summary) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<T, ast::LetStmt> ||
                        std::is_same_v<T, ast::VarStmt>) {
            VisitExpr(node.binding.init, shared_params, aliases, summary);
            if (const auto bind_name = BindingNameOf(node.binding.pat)) {
              if (const auto formal_ref =
                      ResolveFormalPath(node.binding.init, shared_params, aliases);
                  formal_ref.has_value()) {
                aliases[*bind_name] = *formal_ref;
              } else {
                aliases.erase(*bind_name);
              }
            }
          } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
            // UsingLocalStmt is a compile-time alias; no runtime expression.
            // Any formal-path alias information attached to `source` carries
            // through to `alias` transparently.
            const auto alias_key = IdKeyOf(node.alias);
            const auto source_key = IdKeyOf(node.source);
            if (const auto it = aliases.find(source_key); it != aliases.end()) {
              aliases[alias_key] = it->second;
            } else {
              aliases.erase(alias_key);
            }
          } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            VisitExpr(node.value, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                               std::is_same_v<T, ast::CompoundAssignStmt>) {
            VisitExpr(node.place, shared_params, aliases, summary);
            VisitExpr(node.value, shared_params, aliases, summary);
            if (const auto root = SimpleAssignedRoot(node.place)) {
              aliases.erase(*root);
            }
          } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
            VisitExpr(node.value_opt, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
            VisitExpr(node.value_opt, shared_params, aliases, summary);
          } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                               std::is_same_v<T, ast::UnsafeBlockStmt>) {
            if (node.body) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.body, shared_params, inner_aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
            VisitExpr(node.opts_opt, shared_params, aliases, summary);
            if (node.body) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.body, shared_params, inner_aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::FrameStmt> ||
                               std::is_same_v<T, ast::CtStmt>) {
            if (node.body) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.body, shared_params, inner_aliases, summary);
            }
          } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
            const auto mode = node.mode.value_or(ast::KeyMode::Read);
            for (const auto& path_expr : node.paths) {
              const auto lowered = ParseKeyPathSpec(path_expr);
              if (const auto ref =
                      ResolveFormalPathFromKeyPath(lowered, shared_params, aliases);
                  ref.has_value()) {
                AddAccess(summary, *ref, mode);
              }
            }
            if (node.body) {
              AliasEnv inner_aliases = aliases;
              VisitBlock(*node.body, shared_params, inner_aliases, summary);
            }
          }
        },
        stmt);
  }

  void VisitBlock(const ast::Block& block,
                  const SharedParamMap& shared_params,
                  AliasEnv& aliases,
                  ProcedureKeyAccessSummary& summary) {
    for (const auto& stmt : block.stmts) {
      VisitStmt(stmt, shared_params, aliases, summary);
    }
    VisitExpr(block.tail_opt, shared_params, aliases, summary);
  }
};

static bool SharedArgUnderHeldPrefix(const ast::ExprPtr& arg,
                                     const std::vector<HeldKeyTypingInfo>& held) {
  const auto built = BuildKeyPath(arg);
  if (!built.success) {
    return false;
  }
  return HeldPrefixExistsForPath(held, built.path);
}

static void EmitUnknownCalleeAccessWarningIfNeeded(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const ast::CallExpr& node) {
  if (!type_ctx.keys_held || !type_ctx.diags || type_ctx.held_key_paths.empty()) {
    return;
  }

  const auto lookup = LookupProcedureForCallee(ctx, node.callee);
  if (!lookup.has_value() || !lookup->proc || lookup->proc_view.has_value()) {
    return;
  }

  ProcedureKeyAccessSummaryBuilder builder(ctx);
  const auto summary = builder.Summarize(*lookup->proc);
  if (!summary.unknown) {
    return;
  }

  for (std::size_t i = 0; i < node.args.size() && i < lookup->proc->params.size();
       ++i) {
    if (!TypeIsSharedParamSurface(lookup->proc->params[i].type)) {
      continue;
    }
    if (!SharedArgUnderHeldPrefix(node.args[i].value, type_ctx.held_key_paths)) {
      continue;
    }
    const core::Span diag_span =
        node.callee ? node.callee->span : core::Span{};
    if (auto diag = core::MakeDiagnosticById("W-CON-0005", diag_span)) {
      core::Emit(*type_ctx.diags, *diag);
    }
    return;
  }
}

static void EmitDeprecatedReferenceWarning(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const ast::ExprPtr& callee) {
  const auto lookup = LookupProcedureForCallee(ctx, callee);
  if (!lookup.has_value() || !lookup->proc) {
    return;
  }
  EmitDeprecatedReferenceWarningFromAttrs(
      lookup->proc->attrs, type_ctx,
      callee ? std::optional<core::Span>(callee->span) : std::nullopt);
}

static std::size_t RequiredTypeArgCount(const std::vector<ast::TypeParam>& params) {
  std::size_t required = 0;
  for (const auto& param : params) {
    if (!param.default_type) {
      ++required;
    }
  }
  return required;
}

static bool IsTypeParamName(const std::vector<ast::TypeParam>& params,
                            std::string_view name) {
  for (const auto& param : params) {
    if (IdEq(param.name, name)) {
      return true;
    }
  }
  return false;
}

static bool TypePathEqLocal(const TypePath& lhs, const TypePath& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!IdEq(lhs[i], rhs[i])) {
      return false;
    }
  }
  return true;
}

static bool ContainsTypeParamForCall(
    const std::vector<ast::TypeParam>& params,
    const TypeRef& type) {
  if (!type) {
    return false;
  }

  if (const auto* path = std::get_if<TypePathType>(&type->node)) {
    if (path->path.size() == 1 && IsTypeParamName(params, path->path[0])) {
      return true;
    }
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePerm>) {
          return ContainsTypeParamForCall(params, node.base);
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          for (const auto& elem : node.elements) {
            if (ContainsTypeParamForCall(params, elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return ContainsTypeParamForCall(params, node.element);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          return ContainsTypeParamForCall(params, node.element);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          return ContainsTypeParamForCall(params, node.element);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          return ContainsTypeParamForCall(params, node.element);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          for (const auto& member : node.members) {
            if (ContainsTypeParamForCall(params, member)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          for (const auto& param : node.params) {
            if (ContainsTypeParamForCall(params, param.type)) {
              return true;
            }
          }
          return ContainsTypeParamForCall(params, node.ret);
          } else if constexpr (std::is_same_v<T, TypePathType>) {
            for (const auto& arg : node.generic_args) {
              if (ContainsTypeParamForCall(params, arg)) {
                return true;
              }
            }
            return false;
          } else if constexpr (std::is_same_v<T, TypeApply>) {
            for (const auto& arg : node.args) {
              if (ContainsTypeParamForCall(params, arg)) {
                return true;
              }
            }
            return false;
          } else if constexpr (std::is_same_v<T, TypeModalState>) {
          for (const auto& arg : node.generic_args) {
            if (ContainsTypeParamForCall(params, arg)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return ContainsTypeParamForCall(params, node.base);
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          for (const auto& param : node.params) {
            if (ContainsTypeParamForCall(params, param.second)) {
              return true;
            }
          }
          return ContainsTypeParamForCall(params, node.ret);
        }
        return false;
      },
      type->node);
}

static bool BindTypeParamsForCall(
    const std::vector<ast::TypeParam>& params,
    const TypeRef& expected,
    const TypeRef& actual,
    std::map<std::string, TypeRef>& bindings) {
  if (!expected || !actual) {
    return false;
  }

  if (const auto* path = std::get_if<TypePathType>(&expected->node)) {
    if (path->path.size() == 1 && IsTypeParamName(params, path->path[0])) {
      const std::string name = path->path[0];
      const auto it = bindings.find(name);
      if (it == bindings.end()) {
        bindings.emplace(name, actual);
        return true;
      }
      const auto equiv = TypeEquiv(it->second, actual);
      return equiv.ok && equiv.equiv;
    }
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePerm>) {
          const auto* other = std::get_if<TypePerm>(&actual->node);
          return other && node.perm == other->perm &&
                 BindTypeParamsForCall(params, node.base, other->base, bindings);
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          const auto* other = std::get_if<TypeTuple>(&actual->node);
          if (!other || node.elements.size() != other->elements.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.elements.size(); ++i) {
            if (!BindTypeParamsForCall(params, node.elements[i],
                                       other->elements[i], bindings)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          const auto* other = std::get_if<TypeArray>(&actual->node);
          return other && node.length == other->length &&
                 BindTypeParamsForCall(params, node.element, other->element,
                                       bindings);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          const auto* other = std::get_if<TypeSlice>(&actual->node);
          return other && BindTypeParamsForCall(params, node.element, other->element,
                                                bindings);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          const auto* other = std::get_if<TypePtr>(&actual->node);
          return other && node.state == other->state &&
                 BindTypeParamsForCall(params, node.element, other->element,
                                       bindings);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          const auto* other = std::get_if<TypeRawPtr>(&actual->node);
          return other && node.qual == other->qual &&
                 BindTypeParamsForCall(params, node.element, other->element,
                                       bindings);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          const auto* other = std::get_if<TypeUnion>(&actual->node);
          if (!other || node.members.size() != other->members.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.members.size(); ++i) {
            if (!BindTypeParamsForCall(params, node.members[i],
                                       other->members[i], bindings)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          const auto* other = std::get_if<TypeFunc>(&actual->node);
          if (!other || node.params.size() != other->params.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.params.size(); ++i) {
            if (node.params[i].mode != other->params[i].mode) {
              return false;
            }
            if (!BindTypeParamsForCall(params, node.params[i].type,
                                       other->params[i].type, bindings)) {
              return false;
            }
          }
          return BindTypeParamsForCall(params, node.ret, other->ret, bindings);
          } else if constexpr (std::is_same_v<T, TypePathType>) {
            const auto* other_path = AppliedTypePath(*actual);
            const auto* other_args = AppliedTypeArgs(*actual);
            if (!other_path || !other_args ||
                !TypePathEqLocal(node.path, *other_path) ||
                node.generic_args.size() != other_args->size()) {
              return false;
            }
            for (std::size_t i = 0; i < node.generic_args.size(); ++i) {
              if (!BindTypeParamsForCall(params, node.generic_args[i],
                                         (*other_args)[i], bindings)) {
                return false;
              }
            }
            return true;
          } else if constexpr (std::is_same_v<T, TypeApply>) {
            const auto* other_path = AppliedTypePath(*actual);
            const auto* other_args = AppliedTypeArgs(*actual);
            if (!other_path || !other_args ||
                !TypePathEqLocal(node.path, *other_path) ||
                node.args.size() != other_args->size()) {
              return false;
            }
            for (std::size_t i = 0; i < node.args.size(); ++i) {
              if (!BindTypeParamsForCall(params, node.args[i], (*other_args)[i],
                                         bindings)) {
                return false;
              }
            }
            return true;
          } else if constexpr (std::is_same_v<T, TypeModalState>) {
          const auto* other = std::get_if<TypeModalState>(&actual->node);
          if (!other || !TypePathEqLocal(node.path, other->path) ||
              !IdEq(node.state, other->state) ||
              node.generic_args.size() != other->generic_args.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.generic_args.size(); ++i) {
            if (!BindTypeParamsForCall(params, node.generic_args[i],
                                       other->generic_args[i], bindings)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeDynamic>) {
          const auto* other = std::get_if<TypeDynamic>(&actual->node);
          return other && TypePathEqLocal(node.path, other->path);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          const auto* other = std::get_if<TypeRefine>(&actual->node);
          return other && BindTypeParamsForCall(params, node.base, other->base,
                                                bindings);
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          const auto* other = std::get_if<TypeClosure>(&actual->node);
          if (!other || node.params.size() != other->params.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.params.size(); ++i) {
            if (node.params[i].first != other->params[i].first) {
              return false;
            }
            if (!BindTypeParamsForCall(params, node.params[i].second,
                                       other->params[i].second, bindings)) {
              return false;
            }
          }
          return BindTypeParamsForCall(params, node.ret, other->ret, bindings);
        } else if constexpr (std::is_same_v<T, TypeOpaque>) {
          const auto* other = std::get_if<TypeOpaque>(&actual->node);
          return other && TypePathEqLocal(node.class_path, other->class_path);
        } else if constexpr (std::is_same_v<T, TypeString>) {
          const auto* other = std::get_if<TypeString>(&actual->node);
          return other && node.state == other->state;
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          const auto* other = std::get_if<TypeBytes>(&actual->node);
          return other && node.state == other->state;
        } else if constexpr (std::is_same_v<T, TypePrim>) {
          const auto* other = std::get_if<TypePrim>(&actual->node);
          return other && IdEq(node.name, other->name);
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          const auto* other = std::get_if<TypeRange>(&actual->node);
          return other && BindTypeParamsForCall(params, node.base, other->base,
                                                bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          const auto* other = std::get_if<TypeRangeInclusive>(&actual->node);
          return other && BindTypeParamsForCall(params, node.base, other->base,
                                                bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          const auto* other = std::get_if<TypeRangeFrom>(&actual->node);
          return other && BindTypeParamsForCall(params, node.base, other->base,
                                                bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          const auto* other = std::get_if<TypeRangeTo>(&actual->node);
          return other && BindTypeParamsForCall(params, node.base, other->base,
                                                bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          const auto* other = std::get_if<TypeRangeToInclusive>(&actual->node);
          return other && BindTypeParamsForCall(params, node.base, other->base,
                                                bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeFull>) {
          return std::holds_alternative<TypeRangeFull>(actual->node);
        } else {
          const auto equiv = TypeEquiv(expected, actual);
          return equiv.ok && equiv.equiv;
        }
      },
      expected->node);
}

static bool ExpandTypeArgsWithDefaults(
    const ScopeContext& ctx,
    const std::vector<ast::TypeParam>& params,
    const std::vector<TypeRef>& provided_args,
    std::vector<TypeRef>& out_args,
    std::optional<std::string_view>& diag_id) {
  out_args = provided_args;
  if (out_args.size() > params.size()) {
    diag_id = "E-SEM-2532";
    return false;
  }

  for (std::size_t i = out_args.size(); i < params.size(); ++i) {
    if (!params[i].default_type) {
      diag_id = "E-SEM-2533";
      return false;
    }
    const auto lowered_default = LowerType(ctx, params[i].default_type);
    if (!lowered_default.ok) {
      diag_id = lowered_default.diag_id;
      return false;
    }

    TypeRef value = lowered_default.type;
    if (i > 0) {
      std::vector<ast::TypeParam> prefix_params(params.begin(),
                                                params.begin() + static_cast<long>(i));
      std::vector<TypeRef> prefix_args(out_args.begin(),
                                       out_args.begin() + static_cast<long>(i));
      const auto prefix_subst = BuildSubstitution(prefix_params, prefix_args);
      value = InstantiateType(value, prefix_subst);
    }
    out_args.push_back(value);
  }

  return true;
}

static bool IsPredicateReqName(std::string_view name) {
  return IdEq(name, "Bitcopy") || IdEq(name, "Clone") ||
         IdEq(name, "Drop") || IdEq(name, "FfiSafe");
}

static bool PredicateReqSatisfied(const ScopeContext& ctx,
                                  std::string_view pred,
                                  const TypeRef& type) {
  if (IdEq(pred, "Bitcopy")) {
    return BitcopyType(ctx, type);
  }
  if (IdEq(pred, "Clone")) {
    return CloneType(ctx, type);
  }
  if (IdEq(pred, "Drop")) {
    return DropType(ctx, type);
  }
  if (IdEq(pred, "FfiSafe")) {
    return FfiSafeType(ctx, type);
  }
  return false;
}

static std::optional<std::string_view> ValidateProcedureTypeArgConstraints(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& proc,
    const TypeSubst& subst) {
  if (!proc.generic_params || proc.generic_params->params.empty()) {
    return std::nullopt;
  }

  for (const auto& param : proc.generic_params->params) {
    const auto arg_it = subst.find(param.name);
    if (arg_it == subst.end() || !arg_it->second) {
      return std::optional<std::string_view>{"E-TYP-2302"};
    }
    for (const auto& bound : param.bounds) {
      if (ctx.sigma.classes.find(PathKeyOf(bound.class_path)) ==
          ctx.sigma.classes.end()) {
        return std::optional<std::string_view>{"E-TYP-2305"};
      }
      if (!TypeImplementsClass(ctx, arg_it->second, bound.class_path)) {
        return std::optional<std::string_view>{"E-TYP-2302"};
      }
    }
  }

  if (!proc.predicate_clause_opt.has_value()) {
    return std::nullopt;
  }

  for (const auto& wp : *proc.predicate_clause_opt) {
    if (!IsPredicateReqName(wp.pred)) {
      return std::optional<std::string_view>{"E-TYP-2302"};
    }
    const auto lowered = LowerType(ctx, wp.type);
    if (!lowered.ok) {
      return lowered.diag_id;
    }
    const auto instantiated = InstantiateType(lowered.type, subst);
    if (!PredicateReqSatisfied(ctx, wp.pred, instantiated)) {
      return std::optional<std::string_view>{"E-TYP-2302"};
    }
  }

  return std::nullopt;
}

static std::optional<std::string_view> CollectCallArgTypesForInference(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& proc,
    const std::vector<ast::Arg>& args,
    const ExprTypeFn& type_expr,
    const PlaceTypeFn* type_place,
    std::vector<TypeRef>& out_actual_arg_types,
    std::vector<TypeRef>& out_expected_param_types) {
  (void)ctx;
  if (proc.params.size() != args.size()) {
    return "E-SEM-2532";
  }

  std::vector<TypeFuncParam> lowered_params;
  lowered_params.reserve(proc.params.size());
  out_expected_param_types.clear();
  out_expected_param_types.reserve(proc.params.size());
  for (const auto& param : proc.params) {
    const auto lowered = LowerType(ctx, param.type);
    if (!lowered.ok) {
      return lowered.diag_id;
    }
    lowered_params.push_back(TypeFuncParam{LowerParamMode(param.mode), lowered.type});
    out_expected_param_types.push_back(lowered.type);
  }

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (MissingRequiredMoveForConsuming(lowered_params[i].mode, args[i])) {
      return "E-SEM-2534";
    }
    if (!lowered_params[i].mode.has_value() && args[i].moved) {
      return "E-SEM-2535";
    }
  }

  out_actual_arg_types.clear();
  out_actual_arg_types.reserve(args.size());
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (!lowered_params[i].mode.has_value()) {
      const bool has_source_prov = HasSourceProvenance(args[i].value);
      if (has_source_prov && !IsPlaceExprForCall(args[i].value)) {
        return "E-TYP-1603";
      }
      if (has_source_prov && type_place) {
        const auto place = (*type_place)(args[i].value);
        if (!place.ok) {
          return place.diag_id;
        }
        out_actual_arg_types.push_back(place.type);
      } else {
        const auto arg_type = type_expr(args[i].value);
        if (!arg_type.ok) {
          return arg_type.diag_id;
        }
        out_actual_arg_types.push_back(arg_type.type);
      }
      continue;
    }

    const auto arg_expr = MovedArgExpr(args[i]);
    const auto arg_type = type_expr(arg_expr);
    if (!arg_type.ok) {
      return arg_type.diag_id;
    }
    out_actual_arg_types.push_back(arg_type.type);
  }

  return std::nullopt;
}

static GenericCallSubstResult InferGenericCallSubstForProc(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& proc,
    const std::vector<ast::Arg>& args,
    const std::optional<TypeRef>& expected_return,
    const ExprTypeFn& type_expr,
    const PlaceTypeFn* type_place) {
  GenericCallSubstResult result;
  if (!proc.generic_params || proc.generic_params->params.empty()) {
    return result;
  }

  std::vector<TypeRef> actual_arg_types;
  std::vector<TypeRef> expected_param_types;
  if (const auto diag = CollectCallArgTypesForInference(
          ctx, proc, args, type_expr, type_place, actual_arg_types,
          expected_param_types)) {
    result.diag_id = *diag;
    return result;
  }

  std::map<std::string, TypeRef> bindings;
  const auto& type_params = proc.generic_params->params;
  for (std::size_t i = 0; i < expected_param_types.size() &&
                          i < actual_arg_types.size(); ++i) {
    if (!BindTypeParamsForCall(type_params, expected_param_types[i],
                               actual_arg_types[i], bindings)) {
      result.diag_id = "E-SEM-2533";
      return result;
    }
  }

  if (expected_return.has_value() && *expected_return && proc.return_type_opt) {
    const auto lowered_return = LowerType(ctx, proc.return_type_opt);
    if (!lowered_return.ok) {
      result.diag_id = lowered_return.diag_id;
      return result;
    }
    const bool contains_type_param =
        ContainsTypeParamForCall(type_params, lowered_return.type);
    const bool matched = BindTypeParamsForCall(type_params, lowered_return.type,
                                               *expected_return, bindings);
    if (contains_type_param && !matched) {
      result.diag_id = "E-SEM-2533";
      return result;
    }
  }

  std::vector<TypeRef> inferred_args;
  inferred_args.reserve(type_params.size());
  for (std::size_t i = 0; i < type_params.size(); ++i) {
    const auto it = bindings.find(type_params[i].name);
    if (it != bindings.end()) {
      inferred_args.push_back(it->second);
      continue;
    }

    if (!type_params[i].default_type) {
      result.diag_id = "E-TYP-2301";
      return result;
    }

    const auto lowered_default = LowerType(ctx, type_params[i].default_type);
    if (!lowered_default.ok) {
      result.diag_id = lowered_default.diag_id;
      return result;
    }

    TypeRef value = lowered_default.type;
    if (i > 0) {
      std::vector<ast::TypeParam> prefix_params(type_params.begin(),
                                                type_params.begin() + static_cast<long>(i));
      std::vector<TypeRef> prefix_args(inferred_args.begin(),
                                       inferred_args.begin() + static_cast<long>(i));
      const auto prefix_subst = BuildSubstitution(prefix_params, prefix_args);
      value = InstantiateType(value, prefix_subst);
    }
    inferred_args.push_back(value);
  }

  result.subst = BuildSubstitution(type_params, inferred_args);
  if (const auto constraint_diag =
          ValidateProcedureTypeArgConstraints(ctx, proc, result.subst)) {
    result.diag_id = *constraint_diag;
    return result;
  }
  result.ok = true;
  return result;
}

struct ExternProcLookupResult {
  const ast::ASTModule* module = nullptr;
  const ast::ExternBlock* block = nullptr;
  const ast::ExternProcDecl* proc = nullptr;
};

static std::optional<ExternProcLookupResult> FindExternProcedureInModule(
    const ScopeContext& ctx,
    const ast::ASTModule& module,
    std::string_view name) {
  auto& perf = CallPerfStats();
  const bool perf_on = CallPerfActive();
  if (perf_on) {
    ++perf.find_extern_in_module_calls;
    ++perf.find_extern_in_module_scanned_items;
  }

  const auto& index = GetCallLookupIndex(ctx);
  const auto mod_it = index.externs_by_module.find(&module);
  if (mod_it == index.externs_by_module.end()) {
    return std::nullopt;
  }
  const auto ext_it = mod_it->second.find(IdKeyOf(name));
  if (ext_it == mod_it->second.end()) {
    return std::nullopt;
  }
  return ExternProcLookupResult{nullptr, ext_it->second.block, ext_it->second.proc};
}

static bool HasExternProcedureInModule(
    const ScopeContext& ctx,
    const ast::ASTModule& module,
    const std::vector<std::string>& candidate_names) {
  for (const auto& candidate : candidate_names) {
    if (FindExternProcedureInModule(ctx, module, candidate).has_value()) {
      return true;
    }
  }
  return false;
}

static bool HasProcedureInModule(
    const ScopeContext& ctx,
    const ast::ASTModule& module,
    const std::vector<std::string>& candidate_names) {
  for (const auto& candidate : candidate_names) {
    if (FindProcedureInModule(ctx, module, candidate).has_value()) {
      return true;
    }
  }
  return false;
}

static bool GenericArgCountMismatch(const ScopeContext& ctx,
                                     const ast::ExprPtr& callee,
                                     std::size_t arg_count) {
  if (!callee) {
    return false;
  }

  const auto lookup = LookupProcedureForCallee(ctx, callee);
  if (!lookup || !lookup->proc || !lookup->proc->generic_params) {
    return false;
  }

  const auto& params = lookup->proc->generic_params->params;
  const auto required = RequiredTypeArgCount(params);
  const auto total = params.size();
  return arg_count < required || arg_count > total;
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

static std::optional<ExternProcLookupResult> LookupExternProcedureForCallee(
    const ScopeContext& ctx,
    const ast::ExprPtr& callee);
static bool IsExternCallee(const ScopeContext& ctx, const ast::ExprPtr& callee);

static bool IsRawPointerTypeLocal(const TypeRef& type) {
  const auto stripped = StripPermDeepLocal(type);
  return stripped && std::holds_alternative<TypeRawPtr>(stripped->node);
}

static bool BindingProvenanceIsLocalFfi(
    const TypeBinding& binding) {
  return binding.provenance_kind == BindingProvenanceSeedKind::Region;
}

static std::optional<TypeBinding> BindingForFfiBoundaryExpr(
    const TypeEnv& env,
    const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  return std::visit(
      [&](const auto& node) -> std::optional<TypeBinding> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return BindOf(env, node.name);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return BindingForFfiBoundaryExpr(env, node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return BindingForFfiBoundaryExpr(env, node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return BindingForFfiBoundaryExpr(env, node.base);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return BindingForFfiBoundaryExpr(env, node.value);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return BindingForFfiBoundaryExpr(env, node.place);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return BindingForFfiBoundaryExpr(env, node.expr);
        } else {
          return std::nullopt;
        }
      },
      expr->node);
}

static std::optional<TypeRef> CachedExprTypeOfForFfiBoundary(
    const ScopeContext& ctx,
    const ast::ExprPtr& expr) {
  if (!expr || !ctx.expr_types) {
    return std::nullopt;
  }
  const auto it = ctx.expr_types->find(expr.get());
  if (it == ctx.expr_types->end() || !it->second) {
    return std::nullopt;
  }
  return it->second;
}

static std::optional<std::string_view>
CheckFfiBoundaryRegionLocalRawPointerArgs(const ScopeContext& ctx,
                                          const StmtTypeContext& type_ctx,
                                          const ast::CallExpr& call,
                                          const TypeEnv& env) {
  if (!IsExternCallee(ctx, call.callee)) {
    return std::nullopt;
  }

  for (const auto& arg : call.args) {
    if (!arg.value) {
      continue;
    }
    TypeRef arg_type;
    if (const auto cached = CachedExprTypeOfForFfiBoundary(ctx, arg.value)) {
      arg_type = *cached;
    } else {
      const auto typed = TypeExpr(ctx, type_ctx, arg.value, env);
      if (!typed.ok) {
        return typed.diag_id;
      }
      arg_type = typed.type;
    }

    if (!IsRawPointerTypeLocal(arg_type)) {
      continue;
    }

    if (const auto binding = BindingForFfiBoundaryExpr(env, arg.value);
        binding.has_value() && BindingProvenanceIsLocalFfi(*binding)) {
      SPEC_RULE("FFI-Arg-RegionLocalRawPtr-Err");
      return "E-SYS-3360";
    }

    const auto prov = TrackExprProvenance(ctx, arg.value, env);
    if (!prov.ok) {
      return prov.diag_id;
    }
    if (prov.kind != ProvenanceKind::Region) {
      continue;
    }

    SPEC_RULE("FFI-Arg-RegionLocalRawPtr-Err");
    return "E-SYS-3360";
  }

  return std::nullopt;
}

static std::optional<std::string_view> ExtractDirectCalleeName(
    const ast::ExprPtr& callee) {
  if (!callee) {
    return std::nullopt;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&callee->node)) {
    return std::string_view{ident->name};
  }
  if (const auto* path = std::get_if<ast::PathExpr>(&callee->node)) {
    return std::string_view{path->name};
  }
  if (const auto* qualified =
          std::get_if<ast::QualifiedNameExpr>(&callee->node)) {
    return std::string_view{qualified->name};
  }
  if (const auto* qualified_apply =
          std::get_if<ast::QualifiedApplyExpr>(&callee->node)) {
    return std::string_view{qualified_apply->name};
  }
  return std::nullopt;
}

static bool IsGpuBarrierName(std::string_view name) {
  return name == "gpu_barrier" || name == "gpu_memory_barrier" ||
         name == "gpu_workgroup_barrier";
}

static bool IsExternCallee(const ScopeContext& ctx, const ast::ExprPtr& callee) {
  auto& perf = CallPerfStats();
  const bool perf_on = CallPerfActive();
  if (perf_on) {
    ++perf.is_extern_calls;
  }
  ScopedCallTimer timer(perf_on ? &perf.is_extern_us : nullptr);
  if (!callee) {
    return false;
  }

  std::optional<ast::ModulePath> module_path;
  std::vector<std::string> candidate_names;

  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&callee->node)) {
    candidate_names.push_back(std::string(ident->name));
    const auto ent = ResolveValueName(ctx, ident->name);
    if (ent && ent->origin_opt.has_value()) {
      module_path = *ent->origin_opt;
      if (ent->target_opt.has_value() &&
          !IdEq(*ent->target_opt, ident->name)) {
        candidate_names.push_back(*ent->target_opt);
      }
    } else {
      // Value-path typing can resolve current-module extern declarations even when
      // resolve-time value lookup omits them; fall back to current module.
      module_path = ctx.current_module;
    }
  } else if (const auto* qualified =
                 std::get_if<ast::QualifiedNameExpr>(&callee->node)) {
    module_path = qualified->path;
    candidate_names.push_back(qualified->name);
  } else if (const auto* qualified_apply =
                 std::get_if<ast::QualifiedApplyExpr>(&callee->node)) {
    module_path = qualified_apply->path;
    candidate_names.push_back(qualified_apply->name);
  } else if (const auto* path_expr = std::get_if<ast::PathExpr>(&callee->node)) {
    module_path = path_expr->path.empty() ? ctx.current_module : path_expr->path;
    candidate_names.push_back(path_expr->name);
  } else {
    return false;
  }

  if (candidate_names.empty()) {
    return false;
  }

  if (!module_path.has_value()) {
    // Prefer current-module resolution for unqualified calls.
    const auto* current = FindModuleByPathForGeneric(ctx, ctx.current_module);
    if (current != nullptr) {
      if (HasExternProcedureInModule(ctx, *current, candidate_names)) {
        return true;
      }
      if (HasProcedureInModule(ctx, *current, candidate_names)) {
        return false;
      }
    }
    if (ctx.sigma.mods.size() == 1) {
      return HasExternProcedureInModule(ctx, ctx.sigma.mods.front(),
                                        candidate_names);
    }
    for (const auto& mod : ctx.sigma.mods) {
      if (HasExternProcedureInModule(ctx, mod, candidate_names)) {
        return true;
      }
    }
    return false;
  }

  const auto* module = FindModuleByPathForGeneric(ctx, *module_path);
  if (!module) {
    // Fallback for single-module/unqualified resolution paths where
    // current-module metadata is not yet canonicalized.
    for (const auto& mod : ctx.sigma.mods) {
      if (HasExternProcedureInModule(ctx, mod, candidate_names)) {
        return true;
      }
    }
    return false;
  }

  if (HasExternProcedureInModule(ctx, *module, candidate_names)) {
    return true;
  }
  // If the resolved module has a normal procedure for the same symbol,
  // do not reinterpret it as extern from another module.
  if (HasProcedureInModule(ctx, *module, candidate_names)) {
    return false;
  }
  if (ctx.sigma.mods.size() == 1) {
    return HasExternProcedureInModule(ctx, ctx.sigma.mods.front(),
                                      candidate_names);
  }
  for (const auto& mod : ctx.sigma.mods) {
    if (HasExternProcedureInModule(ctx, mod, candidate_names)) {
      return true;
    }
  }
  return false;
}

static ForeignVerificationMode ToForeignVerificationMode(
    VerificationModeAttribute mode) {
  switch (mode) {
    case VerificationModeAttribute::Static:
      return ForeignVerificationMode::Static;
    case VerificationModeAttribute::Dynamic:
      return ForeignVerificationMode::Dynamic;
  }
  return ForeignVerificationMode::Static;
}

static ForeignVerificationMode ResolveForeignVerificationMode(
    const ast::ExternProcDecl& proc) {
  const auto proc_attr_mode = ResolveVerificationModeAttribute(proc.attrs);
  return proc_attr_mode.has_value() ? ToForeignVerificationMode(*proc_attr_mode)
                                    : ForeignVerificationMode::Static;
}

static ast::ExprPtr MakeExprNode(const core::Span& span, ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

static const ast::ExprPtr* FindBindingReplacement(
    std::string_view ident,
    const std::vector<std::pair<std::string, ast::ExprPtr>>& bindings) {
  for (const auto& [name, value] : bindings) {
    if (IdEq(name, ident)) {
      return &value;
    }
  }
  return nullptr;
}

static ast::ExprPtr FindModuleConstLiteralReplacement(
    const ScopeContext& ctx,
    std::string_view ident) {
  for (const auto& mod : ctx.sigma.mods) {
    for (const auto& item : mod.items) {
      const auto* decl = std::get_if<ast::StaticDecl>(&item);
      if (!decl || decl->mut != ast::Mutability::Let) {
        continue;
      }
      const auto* ident_pat = decl->binding.pat
                                  ? std::get_if<ast::IdentifierPattern>(
                                        &decl->binding.pat->node)
                                  : nullptr;
      if (!ident_pat || !IdEq(ident_pat->name, ident)) {
        continue;
      }
      if (decl->binding.init &&
          std::holds_alternative<ast::LiteralExpr>(decl->binding.init->node)) {
        return decl->binding.init;
      }
    }
  }
  return nullptr;
}

static std::optional<long long> ParseSimpleIntLiteral(std::string_view text) {
  std::string digits;
  digits.reserve(text.size());
  std::size_t i = 0;
  if (i < text.size() && text[i] == '-') {
    digits.push_back('-');
    ++i;
  }
  for (; i < text.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    if (!std::isdigit(ch)) {
      break;
    }
    digits.push_back(static_cast<char>(ch));
  }
  if (digits.empty() || digits == "-") {
    return std::nullopt;
  }
  try {
    return std::stoll(digits);
  } catch (...) {
    return std::nullopt;
  }
}

static std::optional<long long> ConstIntValue(const ScopeContext& ctx,
                                              const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  if (const auto* lit = std::get_if<ast::LiteralExpr>(&expr->node)) {
    if (lit->literal.kind == lexer::TokenKind::IntLiteral) {
      return ParseSimpleIntLiteral(lit->literal.lexeme);
    }
  }
  auto lookup_static = [&](std::string_view name) -> std::optional<long long> {
    if (auto replacement = FindModuleConstLiteralReplacement(ctx, name)) {
      return ConstIntValue(ctx, replacement);
    }
    return std::nullopt;
  };
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    return lookup_static(ident->name);
  }
  if (const auto* path = std::get_if<ast::PathExpr>(&expr->node)) {
    return lookup_static(path->name);
  }
  if (const auto* qualified = std::get_if<ast::QualifiedNameExpr>(&expr->node)) {
    return lookup_static(qualified->name);
  }
  return std::nullopt;
}

static std::optional<bool> ProveSimplePredicate(const ScopeContext& ctx,
                                                const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  const auto* binary = std::get_if<ast::BinaryExpr>(&expr->node);
  if (!binary) {
    return std::nullopt;
  }
  if (binary->op == "&&") {
    const auto left = ProveSimplePredicate(ctx, binary->lhs);
    const auto right = ProveSimplePredicate(ctx, binary->rhs);
    if (left.has_value() && right.has_value()) {
      return *left && *right;
    }
    return std::nullopt;
  }
  const auto left = ConstIntValue(ctx, binary->lhs);
  const auto right = ConstIntValue(ctx, binary->rhs);
  if (!left.has_value() || !right.has_value()) {
    return std::nullopt;
  }
  if (binary->op == ">=") return *left >= *right;
  if (binary->op == ">") return *left > *right;
  if (binary->op == "<=") return *left <= *right;
  if (binary->op == "<") return *left < *right;
  if (binary->op == "==") return *left == *right;
  if (binary->op == "!=") return *left != *right;
  return std::nullopt;
}

static ast::ExprPtr SubstituteForeignPredicate(
    const ScopeContext& ctx,
    const ast::ExprPtr& expr,
    const std::vector<std::pair<std::string, ast::ExprPtr>>& bindings) {
  if (!expr) {
    return expr;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    if (const auto* replacement = FindBindingReplacement(ident->name, bindings)) {
      return *replacement;
    }
    if (auto static_replacement =
            FindModuleConstLiteralReplacement(ctx, ident->name)) {
      return static_replacement;
    }
    return expr;
  }
  if (const auto* path = std::get_if<ast::PathExpr>(&expr->node)) {
    if (const auto* replacement = FindBindingReplacement(path->name, bindings)) {
      return *replacement;
    }
    if (auto static_replacement =
            FindModuleConstLiteralReplacement(ctx, path->name)) {
      return static_replacement;
    }
    return expr;
  }
  if (const auto* qualified = std::get_if<ast::QualifiedNameExpr>(&expr->node)) {
    if (const auto* replacement = FindBindingReplacement(qualified->name, bindings)) {
      return *replacement;
    }
    if (auto static_replacement =
            FindModuleConstLiteralReplacement(ctx, qualified->name)) {
      return static_replacement;
    }
    return expr;
  }
  return std::visit(
      [&](const auto& node) -> ast::ExprPtr {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          auto out = node;
          out.lhs = SubstituteForeignPredicate(ctx, node.lhs, bindings);
          out.rhs = SubstituteForeignPredicate(ctx, node.rhs, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          auto out = node;
          out.value = SubstituteForeignPredicate(ctx, node.value, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto out = node;
          out.base = SubstituteForeignPredicate(ctx, node.base, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto out = node;
          out.base = SubstituteForeignPredicate(ctx, node.base, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto out = node;
          out.base = SubstituteForeignPredicate(ctx, node.base, bindings);
          out.index = SubstituteForeignPredicate(ctx, node.index, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          auto out = node;
          out.callee = SubstituteForeignPredicate(ctx, node.callee, bindings);
          for (auto& arg : out.args) {
            arg.value = SubstituteForeignPredicate(ctx, arg.value, bindings);
          }
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          auto out = node;
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            auto paren = std::get<ast::ParenArgs>(node.args);
            for (auto& arg : paren.args) {
              arg.value = SubstituteForeignPredicate(ctx, arg.value, bindings);
            }
            out.args = paren;
          } else {
            auto brace = std::get<ast::BraceArgs>(node.args);
            for (auto& field : brace.fields) {
              field.value = SubstituteForeignPredicate(ctx, field.value, bindings);
            }
            out.args = brace;
          }
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          auto out = node;
          out.receiver = SubstituteForeignPredicate(ctx, node.receiver, bindings);
          for (auto& arg : out.args) {
            arg.value = SubstituteForeignPredicate(ctx, arg.value, bindings);
          }
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          auto out = node;
          out.value = SubstituteForeignPredicate(ctx, node.value, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          auto out = node;
          out.lhs = SubstituteForeignPredicate(ctx, node.lhs, bindings);
          out.rhs = SubstituteForeignPredicate(ctx, node.rhs, bindings);
          return MakeExprNode(expr->span, out);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          auto out = node;
          out.expr = SubstituteForeignPredicate(ctx, node.expr, bindings);
          return MakeExprNode(expr->span, out);
        } else {
          return expr;
        }
      },
      expr->node);
}

static std::optional<ExternProcLookupResult> LookupExternProcedureForCallee(
    const ScopeContext& ctx,
    const ast::ExprPtr& callee) {
  auto& perf = CallPerfStats();
  const bool perf_on = CallPerfActive();
  if (perf_on) {
    ++perf.lookup_extern_callee_calls;
  }
  ScopedCallTimer timer(perf_on ? &perf.lookup_extern_callee_us : nullptr);
  if (!callee) {
    return std::nullopt;
  }

  std::optional<ast::ModulePath> module_path;
  std::vector<std::string> candidate_names;

  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&callee->node)) {
    candidate_names.push_back(std::string(ident->name));
    const auto ent = ResolveValueName(ctx, ident->name);
    if (ent && ent->origin_opt.has_value()) {
      module_path = *ent->origin_opt;
      if (ent->target_opt.has_value() &&
          !IdEq(*ent->target_opt, ident->name)) {
        candidate_names.push_back(*ent->target_opt);
      }
    } else {
      module_path = ctx.current_module;
    }
  } else if (const auto* qualified =
                 std::get_if<ast::QualifiedNameExpr>(&callee->node)) {
    module_path = qualified->path;
    candidate_names.push_back(qualified->name);
  } else if (const auto* qualified_apply =
                 std::get_if<ast::QualifiedApplyExpr>(&callee->node)) {
    module_path = qualified_apply->path;
    candidate_names.push_back(qualified_apply->name);
  } else if (const auto* path_expr =
                 std::get_if<ast::PathExpr>(&callee->node)) {
    module_path = path_expr->path.empty() ? ctx.current_module : path_expr->path;
    candidate_names.push_back(path_expr->name);
  } else {
    return std::nullopt;
  }

  if (candidate_names.empty()) {
    return std::nullopt;
  }

  auto find_in_module =
      [&](const ast::ASTModule& module) -> std::optional<ExternProcLookupResult> {
    for (const auto& candidate : candidate_names) {
      const auto found = FindExternProcedureInModule(ctx, module, candidate);
      if (found.has_value()) {
        ExternProcLookupResult with_module = *found;
        with_module.module = &module;
        return with_module;
      }
    }
    return std::nullopt;
  };

  if (!module_path.has_value()) {
    const auto* current = FindModuleByPathForGeneric(ctx, ctx.current_module);
    if (current != nullptr) {
      if (const auto found = find_in_module(*current); found.has_value()) {
        return found;
      }
      if (HasProcedureInModule(ctx, *current, candidate_names)) {
        return std::nullopt;
      }
    }
    if (ctx.sigma.mods.size() == 1) {
      return find_in_module(ctx.sigma.mods.front());
    }
    for (const auto& mod : ctx.sigma.mods) {
      if (const auto found = find_in_module(mod); found.has_value()) {
        return found;
      }
    }
    return std::nullopt;
  }

  const auto* module = FindModuleByPathForGeneric(ctx, *module_path);
  if (!module) {
    for (const auto& mod : ctx.sigma.mods) {
      if (const auto found = find_in_module(mod); found.has_value()) {
        return found;
      }
    }
    return std::nullopt;
  }

  if (const auto found = find_in_module(*module); found.has_value()) {
    return found;
  }
  if (HasProcedureInModule(ctx, *module, candidate_names)) {
    return std::nullopt;
  }
  if (ctx.sigma.mods.size() == 1) {
    return find_in_module(ctx.sigma.mods.front());
  }
  for (const auto& mod : ctx.sigma.mods) {
    if (const auto found = find_in_module(mod); found.has_value()) {
      return found;
    }
  }
  return std::nullopt;
}

static std::optional<std::string_view> CheckForeignStaticAssumes(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const ast::CallExpr& call) {
  const auto lookup = LookupExternProcedureForCallee(ctx, call.callee);
  if (!lookup.has_value() || !lookup->block || !lookup->proc) {
    return std::nullopt;
  }

  const auto mode = ResolveForeignVerificationMode(*lookup->proc);
  const bool requires_static_proof = [&]() {
    switch (mode) {
      case ForeignVerificationMode::Static:
        return true;
      case ForeignVerificationMode::Dynamic:
        return false;
    }
    return true;
  }();
  if (!requires_static_proof) {
    return std::nullopt;
  }
  if (!lookup->proc->foreign_contracts_opt.has_value()) {
    return std::nullopt;
  }
  if (lookup->proc->params.size() != call.args.size()) {
    return std::nullopt;
  }

  std::vector<std::pair<std::string, ast::ExprPtr>> bindings;
  bindings.reserve(call.args.size());
  for (std::size_t i = 0; i < call.args.size(); ++i) {
    bindings.emplace_back(lookup->proc->params[i].name, call.args[i].value);
  }

  // Feed literal module statics into substitution so simple foreign preconditions
  // can be proven in static mode (for example `x >= ContractResolutionFloor`).
  if (lookup->module != nullptr) {
    for (const auto& item : lookup->module->items) {
      const auto* decl = std::get_if<ast::StaticDecl>(&item);
      if (!decl || decl->mut != ast::Mutability::Let) {
        continue;
      }
      const auto* ident_pat = decl->binding.pat
                                  ? std::get_if<ast::IdentifierPattern>(
                                        &decl->binding.pat->node)
                                  : nullptr;
      const auto* lit = decl->binding.init
                            ? std::get_if<ast::LiteralExpr>(
                                  &decl->binding.init->node)
                            : nullptr;
      if (!ident_pat || !lit) {
        continue;
      }
      bindings.emplace_back(ident_pat->name, decl->binding.init);
    }
  }

  for (const auto& clause : *lookup->proc->foreign_contracts_opt) {
    if (clause.kind != ast::ForeignContractKind::Assumes) {
      continue;
    }
    for (const auto& pred : clause.predicates) {
      if (!pred) {
        continue;
      }
      const auto substituted = SubstituteForeignPredicate(ctx, pred, bindings);
      StaticProofContext proof_ctx;
      if (type_ctx.proof_ctx) {
        proof_ctx = *type_ctx.proof_ctx;
      }
      if (type_ctx.contract && type_ctx.contract->precondition) {
        AddPredicateFacts(proof_ctx, type_ctx.contract->precondition);
      }
      const auto proof_location =
          call.callee ? call.callee->span
                      : (substituted ? substituted->span : core::Span{});
      const auto proof = StaticProofAt(proof_ctx, proof_location, substituted);
      if (!proof.provable) {
        if (const auto simple = ProveSimplePredicate(ctx, substituted);
            simple.has_value() && *simple) {
          continue;
        }
        SPEC_RULE("Foreign-Assumes-Static-Proof-Err");
        return std::optional<std::string_view>{"E-SEM-2850"};
      }
    }
  }

  return std::nullopt;
}

static std::optional<std::string_view> CheckCallSitePrecondition(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const ast::CallExpr& call) {
  const auto lookup = LookupProcedureForCallee(ctx, call.callee);
  if (!lookup.has_value() || !lookup->proc) {
    return std::nullopt;
  }
  if (!lookup->proc->contract.has_value() ||
      !lookup->proc->contract->precondition) {
    return std::nullopt;
  }
  if (lookup->proc->params.size() != call.args.size()) {
    return std::nullopt;
  }

  std::vector<std::pair<std::string, ast::ExprPtr>> bindings;
  bindings.reserve(call.args.size());
  for (std::size_t i = 0; i < call.args.size(); ++i) {
    bindings.emplace_back(lookup->proc->params[i].name, call.args[i].value);
  }

  const auto pre_subst =
      SubstituteForeignPredicate(ctx, lookup->proc->contract->precondition, bindings);
  StaticProofContext proof_ctx;
  if (type_ctx.proof_ctx) {
    proof_ctx = *type_ctx.proof_ctx;
  }
  if (type_ctx.contract && type_ctx.contract->precondition) {
    AddPredicateFacts(proof_ctx, type_ctx.contract->precondition);
  }
  const auto proof_location =
      call.callee ? call.callee->span
                  : (pre_subst ? pre_subst->span : core::Span{});
  const auto proof = StaticProofAt(proof_ctx, proof_location, pre_subst);
  if (proof.provable) {
    return std::nullopt;
  }

  if (!type_ctx.contract_dynamic) {
    SPEC_RULE("Contract-Static-Fail");
    return std::optional<std::string_view>{"E-SEM-2801"};
  }
  return std::nullopt;
}

}  // namespace

void LogCallLookupPerfSummary() {
  if (!CallPerfEnabled()) {
    return;
  }
  const auto& stats = CallPerfStats();
  if (stats.lookup_proc_callee_calls == 0 && stats.is_extern_calls == 0 &&
      stats.lookup_extern_callee_calls == 0) {
    return;
  }
  std::fprintf(stderr,
               "[cursive] sema perf=call-lookup lookup_proc_calls=%llu "
               "lookup_proc_us=%llu is_extern_calls=%llu is_extern_us=%llu "
               "lookup_extern_calls=%llu lookup_extern_us=%llu "
               "find_module_calls=%llu find_module_scanned=%llu "
               "find_proc_calls=%llu find_proc_scanned_items=%llu "
               "find_extern_calls=%llu find_extern_scanned_items=%llu\n",
               static_cast<unsigned long long>(stats.lookup_proc_callee_calls),
               static_cast<unsigned long long>(stats.lookup_proc_callee_us),
               static_cast<unsigned long long>(stats.is_extern_calls),
               static_cast<unsigned long long>(stats.is_extern_us),
               static_cast<unsigned long long>(stats.lookup_extern_callee_calls),
               static_cast<unsigned long long>(stats.lookup_extern_callee_us),
               static_cast<unsigned long long>(stats.find_module_calls),
               static_cast<unsigned long long>(stats.find_module_scanned),
               static_cast<unsigned long long>(stats.find_proc_in_module_calls),
               static_cast<unsigned long long>(
                   stats.find_proc_in_module_scanned_items),
               static_cast<unsigned long long>(stats.find_extern_in_module_calls),
               static_cast<unsigned long long>(
                   stats.find_extern_in_module_scanned_items));
  std::fflush(stderr);
}

std::optional<TypeSubst> BuildGenericCallSubst(
    const ScopeContext& ctx,
    const ast::ExprPtr& callee,
    const std::vector<std::shared_ptr<ast::Type>>& generic_args) {
  struct BuildGenericCallSubstResult {
    bool ok = false;
    std::optional<std::string_view> diag_id;
    TypeSubst subst;
  };
  const auto build_checked =
      [&](const ScopeContext& scope,
          const ast::ExprPtr& call_callee,
          const std::vector<std::shared_ptr<ast::Type>>& call_generic_args)
      -> BuildGenericCallSubstResult {
    BuildGenericCallSubstResult result;
    const auto lookup = LookupProcedureForCallee(scope, call_callee);
    if (!lookup || !lookup->proc || !lookup->proc->generic_params) {
      return result;
    }

    const auto& params = lookup->proc->generic_params->params;
    const auto required = RequiredTypeArgCount(params);
    if (call_generic_args.size() < required ||
        call_generic_args.size() > params.size()) {
      result.diag_id = "E-SEM-2532";
      return result;
    }

    std::vector<TypeRef> provided_args;
    provided_args.reserve(call_generic_args.size());
    for (const auto& arg : call_generic_args) {
      const auto lowered = LowerType(scope, arg);
      if (!lowered.ok) {
        result.diag_id = lowered.diag_id;
        return result;
      }
      provided_args.push_back(lowered.type);
    }

    std::vector<TypeRef> expanded_args;
    std::optional<std::string_view> expand_diag;
    if (!ExpandTypeArgsWithDefaults(scope, params, provided_args, expanded_args,
                                    expand_diag)) {
      result.diag_id = expand_diag;
      return result;
    }

    result.subst = BuildSubstitution(params, expanded_args);
    if (const auto constraint_diag = ValidateProcedureTypeArgConstraints(
            scope, *lookup->proc, result.subst)) {
      result.diag_id = *constraint_diag;
      return result;
    }

    result.ok = true;
    return result;
  };

  const auto checked = build_checked(ctx, callee, generic_args);
  if (!checked.ok) {
    return std::nullopt;
  }
  return checked.subst;
}

GenericCallSubstResult InferGenericCallSubst(
    const ScopeContext& ctx,
    const ast::ExprPtr& callee,
    const std::vector<ast::Arg>& args,
    const std::optional<TypeRef>& expected_return,
    const ExprTypeFn& type_expr,
    const PlaceTypeFn* type_place) {
  const auto lookup = LookupProcedureForCallee(ctx, callee);
  if (!lookup || !lookup->proc || !lookup->proc->generic_params ||
      lookup->proc->generic_params->params.empty()) {
    return {};
  }
  return InferGenericCallSubstForProc(ctx, *lookup->proc, args, expected_return,
                                      type_expr, type_place);
}

void RecordGenericCallSubst(const ScopeContext& ctx,
                            const ast::CallExpr& call,
                            const TypeSubst& subst) {
  if (!ctx.generic_call_substs) {
    return;
  }
  (*ctx.generic_call_substs)[&call] = subst;
}

static std::optional<std::string_view> BuildGenericCallSubstChecked(
    const ScopeContext& ctx,
    const ast::ExprPtr& callee,
    const std::vector<std::shared_ptr<ast::Type>>& generic_args,
    TypeSubst& out_subst) {
  const auto lookup = LookupProcedureForCallee(ctx, callee);
  if (!lookup || !lookup->proc || !lookup->proc->generic_params) {
    return std::optional<std::string_view>{"E-SEM-2533"};
  }

  const auto& params = lookup->proc->generic_params->params;
  const auto required = RequiredTypeArgCount(params);
  if (generic_args.size() < required || generic_args.size() > params.size()) {
    return std::optional<std::string_view>{"E-SEM-2532"};
  }

  std::vector<TypeRef> provided_args;
  provided_args.reserve(generic_args.size());
  for (const auto& arg : generic_args) {
    const auto lowered = LowerType(ctx, arg);
    if (!lowered.ok) {
      return lowered.diag_id;
    }
    provided_args.push_back(lowered.type);
  }

  std::vector<TypeRef> expanded_args;
  std::optional<std::string_view> diag_id;
  if (!ExpandTypeArgsWithDefaults(ctx, params, provided_args, expanded_args,
                                  diag_id)) {
    return diag_id;
  }

  out_subst = BuildSubstitution(params, expanded_args);
  if (const auto constraint_diag =
          ValidateProcedureTypeArgConstraints(ctx, *lookup->proc, out_subst)) {
    return constraint_diag;
  }

  return std::nullopt;
}

static bool IsSystemCtorResultType(const TypeRef& type) {
  if (!type) {
    return false;
  }
  const auto* path = std::get_if<TypePathType>(&type->node);
  if (!path) {
    return false;
  }
  return IsSystemTypePath(path->path);
}

ExprTypeResult TypeCallExprImpl(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::CallExpr& node,
                                const TypeEnv& env) {
  SpecDefsCall();

  auto type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, type_ctx, inner, env);
  };

  // Built-in record constructors are resolved through the shared capability/type
  // registry so builtin-record paths stay aligned with the spec-defined set.
  if (node.callee && node.args.empty()) {
    const auto* ident =
        std::get_if<ast::IdentifierExpr>(&node.callee->node);
    const auto builtin_path =
        ident ? LookupBuiltinRecordCtorPath(ident->name) : std::nullopt;
    if (builtin_path.has_value()) {
      TypePath builtin_type_path{builtin_path->begin(), builtin_path->end()};
      if (IsSystemTypePath(builtin_type_path) &&
          !IsInUnsafeSpan(ctx, node.callee->span)) {
        ExprTypeResult r;
        r.diag_id = "E-CON-0020";
        return r;
      }
      ExprTypeResult r;
      r.ok = true;
      r.type = MakeTypePath(ast::TypePath{builtin_path->begin(),
                                          builtin_path->end()});
      SPEC_RULE("T-Record-Default");
      return r;
    }
  }

  // Try record default constructor call
  const auto record =
      TypeRecordDefaultCall(ctx, node.callee, node.args, type_expr);
  if (record.ok) {
    if (IsSystemCtorResultType(record.type) &&
        !IsInUnsafeSpan(ctx, node.callee ? node.callee->span : core::Span{})) {
      ExprTypeResult r;
      r.diag_id = "E-CON-0020";
      return r;
    }
    return record;
  }
  if (record.diag_id.has_value()) {
    return record;
  }

  if (IsExternCallee(ctx, node.callee) &&
      !IsInUnsafeSpan(ctx, node.callee ? node.callee->span : core::Span{})) {
    ExprTypeResult r;
    SPEC_RULE("Call-Extern-Unsafe-Err");
    r.diag_id = "Call-Extern-Unsafe-Err";
    return r;
  }

  if (const auto callee_name = ExtractDirectCalleeName(node.callee);
      callee_name && IsGpuIntrinsicName(*callee_name)) {
    const bool in_gpu_context = GpuContext(env);
    if (!in_gpu_context) {
      ExprTypeResult r;
      if (IsGpuBarrierName(*callee_name)) {
        SPEC_RULE("Barrier-Outside-Err");
        r.diag_id = "Barrier-Outside-Err";
      } else {
        SPEC_RULE("GpuIntrinsic-Outside-Err");
        r.diag_id = "E-CON-0154";
      }
      return r;
    }
  }

  const auto arg_ctx_for = [&](const TypeRef& expected) {
    if (!expected) {
      return type_ctx;
    }
    const auto perm = PermOfType(expected);
    if (perm == Permission::Unique) {
      return WithSharedAccessMode(type_ctx, ast::KeyMode::Write);
    }
    if (perm == Permission::Shared || perm == Permission::Const) {
      return WithSharedAccessMode(type_ctx, ast::KeyMode::Read);
    }
    return type_ctx;
  };
  PlaceTypeFn type_place = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, type_ctx, inner, env);
  };
  ArgCheckFn check_expr = [&](const ast::ExprPtr& inner,
                              const TypeRef& expected) -> ArgCheckResult {
    const auto checked =
        CheckExprAgainst(ctx, arg_ctx_for(expected), inner, expected, env);
    return ArgCheckResult{checked.ok, checked.diag_id};
  };

  if (const auto lookup = LookupProcedureForCallee(ctx, node.callee);
      lookup && lookup->is_comptime_proc && !IsComptimeTypingEnv(env)) {
    ExprTypeResult r;
    r.diag_id = "E-CTE-0034";
    return r;
  }
  const auto proc_lookup = LookupProcedureForCallee(ctx, node.callee);

  // Handle generic procedure calls (section 13.1.2 T-Generic-Call)
  if (!node.generic_args.empty()) {
    TypeSubst subst;
    const auto subst_diag =
        BuildGenericCallSubstChecked(ctx, node.callee, node.generic_args, subst);
    if (subst_diag.has_value()) {
      ExprTypeResult r;
      if (*subst_diag == "E-SEM-2532" ||
          GenericArgCountMismatch(ctx, node.callee, node.generic_args.size())) {
        SPEC_RULE("Generic-Call-ArgCount-Err");
        r.diag_id = "E-SEM-2532";
      } else {
        r.diag_id = *subst_diag;
      }
      return r;
    }
    const auto call = TypeCallWithSubst(ctx, node.callee, node.args,
                                         subst, type_expr, &type_place,
                                         &check_expr);
    ExprTypeResult r;
    if (!call.ok) {
      r.diag_id = call.diag_id;
      r.diag_detail = call.diag_detail;
      return r;
    }
    if (type_ctx.require_pure) {
      const auto callee_type = type_expr(node.callee);
      if (callee_type.ok) {
        const auto* func =
            std::get_if<TypeFunc>(&StripPerm(callee_type.type)->node);
        if (func && !ParamsPure(ctx, func->params)) {
          r.diag_id = "E-SEM-2802";
          return r;
        }
      }
    }
    if (const auto pre_diag = CheckCallSitePrecondition(ctx, type_ctx, node)) {
      r.diag_id = *pre_diag;
      return r;
    }
    if (proc_lookup && proc_lookup->proc) {
      if (const auto key_diag = CheckSharedArgWriteRequirement(
              ctx, type_ctx, env, *proc_lookup->proc, node.args, type_expr)) {
        r.diag_id = *key_diag;
        return r;
      }
    }
    if (const auto foreign_diag = CheckForeignStaticAssumes(ctx, type_ctx, node)) {
      r.diag_id = *foreign_diag;
      return r;
    }
    if (const auto ffi_diag =
            CheckFfiBoundaryRegionLocalRawPointerArgs(ctx, type_ctx, node, env)) {
      r.diag_id = *ffi_diag;
      return r;
    }
    EmitUnknownCalleeAccessWarningIfNeeded(ctx, type_ctx, node);
    EmitDeprecatedReferenceWarning(ctx, type_ctx, node.callee);
    r.ok = true;
    r.type = call.type;
    RecordGenericCallSubst(ctx, node, subst);
    SPEC_RULE("T-Generic-Call");
    return r;
  }

  if (const auto lookup = LookupProcedureForCallee(ctx, node.callee);
      lookup && lookup->proc && lookup->proc->generic_params &&
      !lookup->proc->generic_params->params.empty()) {
    const auto inferred =
        InferGenericCallSubstForProc(ctx, *lookup->proc, node.args,
                                     std::nullopt, type_expr, &type_place);
    ExprTypeResult r;
    if (!inferred.ok) {
      r.diag_id = inferred.diag_id.value_or("E-SEM-2533");
      return r;
    }
    const auto call = TypeCallWithSubst(ctx, node.callee, node.args, inferred.subst,
                                        type_expr, &type_place, &check_expr);
    if (!call.ok) {
      r.diag_id = call.diag_id;
      r.diag_detail = call.diag_detail;
      return r;
    }
    if (type_ctx.require_pure) {
      const auto callee_type = type_expr(node.callee);
      if (callee_type.ok) {
        const auto* func =
            std::get_if<TypeFunc>(&StripPerm(callee_type.type)->node);
        if (func && !ParamsPure(ctx, func->params)) {
          r.diag_id = "E-SEM-2802";
          return r;
        }
      }
    }
    if (const auto pre_diag = CheckCallSitePrecondition(ctx, type_ctx, node)) {
      r.diag_id = *pre_diag;
      return r;
    }
    if (lookup && lookup->proc) {
      if (const auto key_diag = CheckSharedArgWriteRequirement(
              ctx, type_ctx, env, *lookup->proc, node.args, type_expr)) {
        r.diag_id = *key_diag;
        return r;
      }
    }
    if (const auto foreign_diag = CheckForeignStaticAssumes(ctx, type_ctx, node)) {
      r.diag_id = *foreign_diag;
      return r;
    }
    if (const auto ffi_diag =
            CheckFfiBoundaryRegionLocalRawPointerArgs(ctx, type_ctx, node, env)) {
      r.diag_id = *ffi_diag;
      return r;
    }
    EmitUnknownCalleeAccessWarningIfNeeded(ctx, type_ctx, node);
    EmitDeprecatedReferenceWarning(ctx, type_ctx, node.callee);
    r.ok = true;
    r.type = call.type;
    RecordGenericCallSubst(ctx, node, inferred.subst);
    SPEC_RULE("T-Generic-Call");
    return r;
  }

  // Non-generic call path
  const auto call =
      TypeCall(ctx, node.callee, node.args, type_expr, &type_place, &check_expr);
  ExprTypeResult r;
  if (!call.ok) {
    r.diag_id = call.diag_id;
    r.diag_detail = call.diag_detail;
    return r;
  }
  if (type_ctx.require_pure) {
    const auto callee_type = type_expr(node.callee);
    if (callee_type.ok) {
      const auto* func =
          std::get_if<TypeFunc>(&StripPerm(callee_type.type)->node);
      if (func && !ParamsPure(ctx, func->params)) {
        r.diag_id = "E-SEM-2802";
        return r;
      }
    }
  }
  if (const auto pre_diag = CheckCallSitePrecondition(ctx, type_ctx, node)) {
    r.diag_id = *pre_diag;
    return r;
  }
  if (const auto lookup = LookupProcedureForCallee(ctx, node.callee);
      lookup && lookup->proc) {
    if (const auto key_diag = CheckSharedArgWriteRequirement(
            ctx, type_ctx, env, *lookup->proc, node.args, type_expr)) {
      r.diag_id = *key_diag;
      return r;
    }
  }
  if (const auto foreign_diag = CheckForeignStaticAssumes(ctx, type_ctx, node)) {
    r.diag_id = *foreign_diag;
    return r;
  }
  if (const auto ffi_diag =
          CheckFfiBoundaryRegionLocalRawPointerArgs(ctx, type_ctx, node, env)) {
    r.diag_id = *ffi_diag;
    return r;
  }
  EmitUnknownCalleeAccessWarningIfNeeded(ctx, type_ctx, node);
  EmitDeprecatedReferenceWarning(ctx, type_ctx, node.callee);
  r.ok = true;
  r.type = call.type;
  return r;
}

}  // namespace cursive::analysis::expr
