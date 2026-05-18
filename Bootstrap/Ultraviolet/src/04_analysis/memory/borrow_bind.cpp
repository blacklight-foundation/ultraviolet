// =============================================================================
// MIGRATION MAPPING: borrow_bind.cpp
// =============================================================================
//
// SPEC REFERENCE:
//   - Docs/SPECIFICATION.md, Section 10.1 "Binding Operators" (lines 22110-22200)
//   - Docs/SPECIFICATION.md, Section 10.2 "Binding States" (lines 22210-22300)
//   - Docs/SPECIFICATION.md, Section 10.3 "Move Semantics" (lines 22310-22400)
//   - Docs/SPECIFICATION.md, Section 10.4 "Permission System" (lines 22410-22500)
//   - Docs/SPECIFICATION.md, Section 8.9 "E-OWN Errors" (lines 21800-21900)
//
// SOURCE FILE:
//   - ultraviolet-bootstrap/src/03_analysis/memory/borrow_bind.cpp
//
// FUNCTIONS TO MIGRATE:
//   - TrackBindingState(Binding* bind) -> BindingState
//       Track Alive/Moved/PartiallyMoved/Poisoned states
//   - CheckMove(Expr* expr) -> bool
//       Validate move expression is legal
//   - AnalyzeOwnership(Stmt* stmt) -> OwnershipInfo
//       Analyze ownership flow through statement
//   - ValidateUniqueAccess(Expr* expr) -> bool
//       Ensure unique permission has no aliases
//   - CheckPermissionCompatibility(Perm required, Perm actual) -> bool
//       Validate permission satisfies requirement
//   - TrackPartialMove(Expr* field_access) -> void
//       Track partial moves of record fields
//   - ValidateConsumingParam(Param* param) -> bool
//       Validate move parameter in procedure signature
//   - AnalyzeBindingLifetime(Binding* bind) -> Lifetime
//       Compute lifetime of a binding
//   - CheckUseAfterMove(Expr* expr) -> bool
//       Detect use of moved binding
//   - ValidateDropOrder(Block* block) -> Vec<Binding*>
//       Determine deterministic drop order (reverse declaration)
//
// DEPENDENCIES:
//   - Binding, BindingState enum
//   - Permission enum (const, unique, shared)
//   - Move semantics tracking
//   - Drop trait detection
//
// REFACTORING NOTES:
//   1. ULTRAVIOLET IS NOT RUST - no borrow checker, but ownership tracking
//   2. Binding operators: let/let:=/var/var:= (movable vs immovable)
//   3. States: Alive, Moved, PartiallyMoved, Poisoned
//   4. move keyword transfers responsibility (not permission)
//   5. Permissions: const (read), unique (exclusive write), shared (synchronized)
//   6. Drop::drop called in reverse declaration order
//
// DIAGNOSTIC CODES:
//   - E-OWN-0001: Use after move
//   - E-OWN-0002: Move of immovable binding
//   - E-OWN-0003: Unique permission violated (alias exists)
//   - E-OWN-0004: Permission insufficient
//   - E-OWN-0005: Partial move leaves binding unusable
//   - E-OWN-0006: Drop of moved binding
//
// =============================================================================

#include "04_analysis/memory/borrow_bind.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/composite/function_types.h"
#include "04_analysis/modal/modal_transitions.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/if_case_check.h"
#include "04_analysis/typing/type_pattern.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/memory/regions.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/memory/calls.h"
#include "04_analysis/memory/return_responsibility.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsBorrowBind() {
  SPEC_DEF("BindingState", "5.2.15");
  SPEC_DEF("Movability", "5.2.15");
  SPEC_DEF("Responsibility", "5.2.15");
  SPEC_DEF("BindInfo", "5.2.15");
  SPEC_DEF("BindScope", "5.2.15");
  SPEC_DEF("PermOf", "5.2.15");
  SPEC_DEF("ActiveState", "5.2.15");
  SPEC_DEF("PermKey", "5.2.15");
  SPEC_DEF("PermScope", "5.2.15");
  SPEC_DEF("FieldPath", "5.2.15");
  SPEC_DEF("FieldPathOf", "5.2.15");
  SPEC_DEF("FieldHead", "5.2.15");
  SPEC_DEF("AccessStateOk", "5.2.15");
  SPEC_DEF("AccessOk", "5.2.15");
  SPEC_DEF("BindInfoMap", "5.2.15");
  SPEC_DEF("MovEff", "5.2.15");
  SPEC_DEF("ParamBindMap", "5.2.15");
  SPEC_DEF("ParamTypeMap", "5.2.15");
  SPEC_DEF("ParamMov", "5.2.15");
  SPEC_DEF("ParamResp", "5.2.15");
  SPEC_DEF("JoinState", "5.2.15");
  SPEC_DEF("JoinBindInfo", "5.2.15");
  SPEC_DEF("JoinScope_B", "5.2.15");
  SPEC_DEF("Join_B", "5.2.15");
  SPEC_DEF("JoinPerm", "5.2.15");
  SPEC_DEF("JoinPermState", "5.2.15");
  SPEC_DEF("JoinAll_B", "5.2.15");
  SPEC_DEF("JoinAllPerm", "5.2.15");
  SPEC_DEF("LoopFix", "5.2.15");
  SPEC_DEF("B-Transition", "5.2.15");
  SPEC_DEF("B-Pipeline", "5.2.15");
}

static inline void SpecRuleTransitionAnchor() {
  SPEC_RULE("B-Transition");
}

struct BorrowBindPerfStats {
  std::uint64_t body_calls = 0;
  std::uint64_t find_module_calls = 0;
  std::uint64_t find_module_scanned = 0;
  std::uint64_t static_bind_items_scanned = 0;
  std::uint64_t static_bind_map_us = 0;
  std::uint64_t bind_block_us = 0;
};

static BorrowBindPerfStats& BorrowPerfStats() {
  static BorrowBindPerfStats stats;
  return stats;
}

static bool BorrowPerfEnabled() {
  return core::IsDebugEnabled("sema") || core::IsDebugEnabled("pipeline") ||
         core::IsDebugEnabled("typeperf");
}

static bool BorrowPerfActive() {
  static const bool enabled = BorrowPerfEnabled();
  return enabled;
}

class ScopedBorrowTimer {
 public:
  explicit ScopedBorrowTimer(std::uint64_t* slot)
      : slot_(slot), start_(std::chrono::steady_clock::now()) {}

  ~ScopedBorrowTimer() {
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

enum class BindStateKind {
  Valid,
  Moved,
  PartiallyMoved,
};

struct BindState {
  BindStateKind kind = BindStateKind::Valid;
  std::set<IdKey> fields;
};

enum class Movability {
  Mov,
  Immov,
};

enum class Responsibility {
  Resp,
  Alias,
};

struct BindInfo {
  BindState state;
  Movability mov = Movability::Mov;
  ast::Mutability mut = ast::Mutability::Let;
  Responsibility resp = Responsibility::Alias;
};

using BindScope = std::map<IdKey, BindInfo>;
using BindEnv = std::vector<BindScope>;

enum class ActiveState {
  Active,
  Inactive,
};

struct PermKey {
  IdKey root;
  std::vector<IdKey> path;
};

struct PermKeyLess {
  bool operator()(const PermKey& lhs, const PermKey& rhs) const {
    if (lhs.root != rhs.root) {
      return lhs.root < rhs.root;
    }
    const auto& a = lhs.path;
    const auto& b = rhs.path;
    const std::size_t min_len = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < min_len; ++i) {
      if (a[i] != b[i]) {
        return a[i] < b[i];
      }
    }
    return a.size() < b.size();
  }
};

using PermScope = std::map<PermKey, ActiveState, PermKeyLess>;
using PermEnv = std::vector<PermScope>;

struct BindStateBundle {
  BindEnv binds;
  PermEnv perms;
  TypeEnv env;
  bool keys_held = false;
  std::optional<ast::KeyMode> key_mode;
};

struct BindResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  BindStateBundle state;
  bool falls_through = true;
};

struct ArgPassResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  BindStateBundle state;
  std::set<PermKey, PermKeyLess> roots;
  bool falls_through = true;
};

struct ParamInfo {
  std::optional<ParamMode> mode;
};

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
  return std::visit(
      [&](const auto& node) -> LocalTypeLowerResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePrim>) {
          return {true, std::nullopt, MakeTypePrim(node.name)};
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          const auto base = LocalLowerType(ctx, node.base);
          if (!base.ok) {
            return base;
          }
          return {true, std::nullopt,
                  MakeTypePerm(LowerPermission(node.perm), base.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          std::vector<TypeRef> members;
          members.reserve(node.types.size());
          for (const auto& elem : node.types) {
            const auto lowered = LocalLowerType(ctx, elem);
            if (!lowered.ok) {
              return lowered;
            }
            members.push_back(lowered.type);
          }
          return {true, std::nullopt, MakeTypeUnion(std::move(members))};
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          std::vector<TypeFuncParam> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            const auto lowered = LocalLowerType(ctx, param.type);
            if (!lowered.ok) {
              return lowered;
            }
            params.push_back(TypeFuncParam{LowerParamMode(param.mode),
                                           lowered.type});
          }
          const auto ret = LocalLowerType(ctx, node.ret);
          if (!ret.ok) {
            return ret;
          }
          return {true, std::nullopt, MakeTypeFunc(std::move(params), ret.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          std::vector<std::pair<bool, TypeRef>> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            const auto lowered = LocalLowerType(ctx, param.type);
            if (!lowered.ok) {
              return lowered;
            }
            const bool is_move =
                param.mode.has_value() && *param.mode == ast::ParamMode::Move;
            params.emplace_back(is_move, lowered.type);
          }
          const auto ret = LocalLowerType(ctx, node.ret);
          if (!ret.ok) {
            return ret;
          }
          std::optional<std::vector<SharedDep>> deps_opt;
          if (node.deps_opt.has_value()) {
            std::vector<SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              const auto dep_type = LocalLowerType(ctx, dep.type);
              if (!dep_type.ok) {
                return dep_type;
              }
              SharedDep lowered_dep;
              lowered_dep.name = dep.name;
              lowered_dep.type = dep_type.type;
              deps.push_back(std::move(lowered_dep));
            }
            deps_opt = std::move(deps);
          }
          return {true, std::nullopt,
                  MakeTypeClosure(std::move(params), ret.type,
                                  std::move(deps_opt))};
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          std::vector<TypeRef> elements;
          elements.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            const auto lowered = LocalLowerType(ctx, elem);
            if (!lowered.ok) {
              return lowered;
            }
            elements.push_back(lowered.type);
          }
          return {true, std::nullopt, MakeTypeTuple(std::move(elements))};
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          const auto elem = LocalLowerType(ctx, node.element);
          if (!elem.ok) {
            return elem;
          }
          const auto len = ConstLen(ctx, node.length);
          if (!len.ok || !len.value.has_value()) {
            return {false, len.diag_id, {}};
          }
          return {true, std::nullopt, MakeTypeArray(elem.type, *len.value)};
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          const auto elem = LocalLowerType(ctx, node.element);
          if (!elem.ok) {
            return elem;
          }
          return {true, std::nullopt, MakeTypeSlice(elem.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          const auto elem = LocalLowerType(ctx, node.element);
          if (!elem.ok) {
            return elem;
          }
          return {true, std::nullopt,
                  MakeTypePtr(elem.type, LowerPtrState(node.state))};
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          const auto elem = LocalLowerType(ctx, node.element);
          if (!elem.ok) {
            return elem;
          }
          return {true, std::nullopt,
                  MakeTypeRawPtr(LowerRawPtrQual(node.qual), elem.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeString>) {
          return {true, std::nullopt,
                  MakeTypeString(LowerStringState(node.state))};
        } else if constexpr (std::is_same_v<T, ast::TypeBytes>) {
          return {true, std::nullopt,
                  MakeTypeBytes(LowerBytesState(node.state))};
        } else if constexpr (std::is_same_v<T, ast::TypeDynamic>) {
          return {true, std::nullopt, MakeTypeDynamic(node.path)};
        } else if constexpr (std::is_same_v<T, ast::TypeOpaque>) {
          return {true, std::nullopt,
                  MakeTypeOpaque(node.path, type.get(), type->span)};
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          const auto base = LocalLowerType(ctx, node.base);
          if (!base.ok) {
            return base;
          }
          return {true, std::nullopt,
                  MakeTypeRefine(base.type, node.predicate)};
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          std::vector<TypeRef> args;
          args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            const auto lowered = LocalLowerType(ctx, arg);
            if (!lowered.ok) {
              return lowered;
            }
            args.push_back(lowered.type);
          }
          return {true, std::nullopt,
                  MakeTypeModalState(node.path, node.state, std::move(args))};
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          // Section 5.2.9, Section 13.1: Generic type instantiation lowering
          // Per WF-Apply (Section 5.2.3), type arguments MUST be preserved
          if (!node.generic_args.empty()) {
            std::vector<TypeRef> lowered_args;
            lowered_args.reserve(node.generic_args.size());
            for (const auto& arg : node.generic_args) {
              const auto lower_result = LocalLowerType(ctx, arg);
              if (!lower_result.ok) {
                return lower_result;
              }
              lowered_args.push_back(lower_result.type);
            }
            return {true, std::nullopt,
                    MakeTypePath(node.path, std::move(lowered_args))};
          }
          return {true, std::nullopt, MakeTypePath(node.path)};
        } else {
          return {false, std::nullopt, {}};
        }
      },
      type->node);
}

static StmtTypeContext MakeTypeCtx() {
  StmtTypeContext ctx;
  ctx.return_type = MakeTypePrim("()");
  ctx.loop_flag = LoopFlag::None;
  ctx.in_unsafe = false;
  ctx.diags = nullptr;
  ctx.env_ref = nullptr;
  return ctx;
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

static std::optional<TypeRef> ExprTypeOf(const ScopeContext& ctx,
                                        const TypeEnv& env,
                                        const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  if (const auto cached = CachedExprTypeOf(ctx, expr)) {
    return cached;
  }
  auto type_ctx = MakeTypeCtx();
  if (IsPlaceExpr(expr)) {
    const auto place = TypePlace(ctx, type_ctx, expr, env);
    if (place.ok) {
      return place.type;
    }
  }
  const auto typed = TypeExpr(ctx, type_ctx, expr, env);
  if (!typed.ok) {
    return std::nullopt;
  }
  return typed.type;
}

static std::optional<TypeRef> PlaceTypeOf(const ScopeContext& ctx,
                                         const TypeEnv& env,
                                         const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  if (const auto cached = CachedExprTypeOf(ctx, expr)) {
    return cached;
  }
  auto type_ctx = MakeTypeCtx();
  const auto place = TypePlace(ctx, type_ctx, expr, env);
  if (!place.ok) {
    return std::nullopt;
  }
  return place.type;
}

static bool BindStateEqual(const BindState& lhs, const BindState& rhs) {
  if (lhs.kind != rhs.kind) {
    return false;
  }
  if (lhs.kind != BindStateKind::PartiallyMoved) {
    return true;
  }
  return lhs.fields == rhs.fields;
}

static bool BindInfoEqual(const BindInfo& lhs, const BindInfo& rhs) {
  return lhs.mov == rhs.mov && lhs.mut == rhs.mut && lhs.resp == rhs.resp &&
         BindStateEqual(lhs.state, rhs.state);
}

static bool BindScopeEqual(const BindScope& lhs, const BindScope& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  auto it_l = lhs.begin();
  auto it_r = rhs.begin();
  for (; it_l != lhs.end(); ++it_l, ++it_r) {
    if (it_l->first != it_r->first) {
      return false;
    }
    if (!BindInfoEqual(it_l->second, it_r->second)) {
      return false;
    }
  }
  return true;
}

static bool BindEnvEqual(const BindEnv& lhs, const BindEnv& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!BindScopeEqual(lhs[i], rhs[i])) {
      return false;
    }
  }
  return true;
}

static bool PermScopeEqual(const PermScope& lhs, const PermScope& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  auto it_l = lhs.begin();
  auto it_r = rhs.begin();
  for (; it_l != lhs.end(); ++it_l, ++it_r) {
    if (!PermKeyLess{}(it_l->first, it_r->first) &&
        !PermKeyLess{}(it_r->first, it_l->first)) {
      if (it_l->second != it_r->second) {
        return false;
      }
      continue;
    }
    return false;
  }
  return true;
}

static bool PermEnvEqual(const PermEnv& lhs, const PermEnv& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!PermScopeEqual(lhs[i], rhs[i])) {
      return false;
    }
  }
  return true;
}

static std::optional<BindInfo> Lookup_B(const BindEnv& env, std::string_view name) {
  const auto key = IdKeyOf(name);
  for (auto it = env.rbegin(); it != env.rend(); ++it) {
    const auto found = it->find(key);
    if (found != it->end()) {
      return found->second;
    }
  }
  return std::nullopt;
}

static bool Update_B_inplace(BindEnv& env,
                             std::string_view name,
                             const BindInfo& info) {
  const auto key = IdKeyOf(name);
  for (auto it = env.rbegin(); it != env.rend(); ++it) {
    const auto found = it->find(key);
    if (found != it->end()) {
      found->second = info;
      return true;
    }
  }
  return false;
}

static std::optional<BindEnv> Update_B(const BindEnv& env,
                                      std::string_view name,
                                      const BindInfo& info) {
  BindEnv out = env;
  if (Update_B_inplace(out, name, info)) {
    return out;
  }
  return std::nullopt;
}

static BindEnv Intro_B(const BindEnv& env,
                       std::string_view name,
                       const BindInfo& info) {
  BindEnv out = env;
  if (out.empty()) {
    out.emplace_back();
  }
  out.back()[IdKeyOf(name)] = info;
  return out;
}

static BindEnv IntroAll_B(const BindEnv& env, const BindScope& scope) {
  BindEnv out = env;
  if (out.empty()) {
    out.emplace_back();
  }
  auto& merged = out.back();
  for (const auto& [name, info] : scope) {
    merged[name] = info;
  }
  return out;
}

static bool ShadowAll_B_inplace(BindEnv& env, const BindScope& scope) {
  for (const auto& [name, info] : scope) {
    const auto key = IdKeyOf(name);
    bool updated = false;
    for (auto it = env.rbegin(); it != env.rend(); ++it) {
      const auto found = it->find(key);
      if (found != it->end()) {
        found->second = info;
        updated = true;
        break;
      }
    }
    if (!updated) {
      return false;
    }
  }
  return true;
}

static std::optional<BindEnv> ShadowAll_B(const BindEnv& env,
                                          const BindScope& scope) {
  BindEnv current = env;
  if (!ShadowAll_B_inplace(current, scope)) {
    return std::nullopt;
  }
  return current;
}

static BindState JoinState(const BindState& lhs, const BindState& rhs) {
  if (lhs.kind == BindStateKind::Moved || rhs.kind == BindStateKind::Moved) {
    return BindState{BindStateKind::Moved, {}};
  }
  if (lhs.kind == BindStateKind::PartiallyMoved &&
      rhs.kind == BindStateKind::PartiallyMoved) {
    std::set<IdKey> merged = lhs.fields;
    merged.insert(rhs.fields.begin(), rhs.fields.end());
    return BindState{BindStateKind::PartiallyMoved, std::move(merged)};
  }
  if (lhs.kind == BindStateKind::PartiallyMoved &&
      rhs.kind == BindStateKind::Valid) {
    return lhs;
  }
  if (lhs.kind == BindStateKind::Valid &&
      rhs.kind == BindStateKind::PartiallyMoved) {
    return rhs;
  }
  return BindState{BindStateKind::Valid, {}};
}

static std::optional<BindInfo> JoinBindInfo(const BindInfo& lhs,
                                            const BindInfo& rhs) {
  if (lhs.mov != rhs.mov || lhs.mut != rhs.mut || lhs.resp != rhs.resp) {
    return std::nullopt;
  }
  BindInfo out = lhs;
  out.state = JoinState(lhs.state, rhs.state);
  return out;
}

static std::optional<BindScope> JoinScope_B(const BindScope& lhs,
                                            const BindScope& rhs) {
  if (lhs.size() != rhs.size()) {
    return std::nullopt;
  }
  BindScope out;
  auto it_l = lhs.begin();
  auto it_r = rhs.begin();
  for (; it_l != lhs.end(); ++it_l, ++it_r) {
    if (it_l->first != it_r->first) {
      return std::nullopt;
    }
    const auto joined = JoinBindInfo(it_l->second, it_r->second);
    if (!joined.has_value()) {
      return std::nullopt;
    }
    out.emplace(it_l->first, *joined);
  }
  return out;
}

static std::optional<BindEnv> Join_B(const BindEnv& lhs, const BindEnv& rhs) {
  if (lhs.size() != rhs.size()) {
    return std::nullopt;
  }
  BindEnv out;
  out.reserve(lhs.size());
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const auto scope = JoinScope_B(lhs[i], rhs[i]);
    if (!scope.has_value()) {
      return std::nullopt;
    }
    out.push_back(*scope);
  }
  return out;
}

static ActiveState JoinPermState(ActiveState lhs, ActiveState rhs) {
  if (lhs == ActiveState::Active && rhs == ActiveState::Active) {
    return ActiveState::Active;
  }
  return ActiveState::Inactive;
}

static ActiveState PermAt(const PermScope& scope, const PermKey& key) {
  const auto it = scope.find(key);
  if (it == scope.end()) {
    return ActiveState::Active;
  }
  return it->second;
}

static PermScope JoinScope_Pi(const PermScope& lhs, const PermScope& rhs) {
  PermScope out;
  std::set<PermKey, PermKeyLess> keys;
  for (const auto& [key, _] : lhs) {
    keys.insert(key);
  }
  for (const auto& [key, _] : rhs) {
    keys.insert(key);
  }
  for (const auto& key : keys) {
    out.emplace(key, JoinPermState(PermAt(lhs, key), PermAt(rhs, key)));
  }
  return out;
}

static std::optional<PermEnv> JoinPerm(const PermEnv& lhs, const PermEnv& rhs) {
  if (lhs.size() != rhs.size()) {
    return std::nullopt;
  }
  PermEnv out;
  out.reserve(lhs.size());
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    out.push_back(JoinScope_Pi(lhs[i], rhs[i]));
  }
  return out;
}

static std::optional<BindEnv> JoinAll_B(const std::vector<BindEnv>& envs) {
  if (envs.empty()) {
    return std::nullopt;
  }
  BindEnv current = envs.front();
  for (std::size_t i = 1; i < envs.size(); ++i) {
    const auto joined = Join_B(current, envs[i]);
    if (!joined.has_value()) {
      return std::nullopt;
    }
    current = std::move(*joined);
  }
  return current;
}

static std::optional<PermEnv> JoinAllPerm(const std::vector<PermEnv>& envs) {
  if (envs.empty()) {
    return std::nullopt;
  }
  PermEnv current = envs.front();
  for (std::size_t i = 1; i < envs.size(); ++i) {
    const auto joined = JoinPerm(current, envs[i]);
    if (!joined.has_value()) {
      return std::nullopt;
    }
    current = std::move(*joined);
  }
  return current;
}

static ActiveState Lookup_Pi(const PermEnv& env, const PermKey& key) {
  for (auto it = env.rbegin(); it != env.rend(); ++it) {
    const auto found = it->find(key);
    if (found != it->end() && found->second == ActiveState::Inactive) {
      return ActiveState::Inactive;
    }
  }
  return ActiveState::Active;
}

static const PermScope& TopPerm(const PermEnv& env) {
  return env.back();
}

static void InactivateScope_inplace(PermScope& scope,
                                    const std::set<PermKey, PermKeyLess>& keys) {
  for (const auto& key : keys) {
    scope[key] = ActiveState::Inactive;
  }
}

static std::set<PermKey, PermKeyLess> Roots(const PermEnv& after,
                                            const PermEnv& before) {
  std::set<PermKey, PermKeyLess> roots;
  if (after.empty()) {
    return roots;
  }
  const auto& top = TopPerm(after);
  for (const auto& [key, state] : top) {
    if (state == ActiveState::Inactive &&
        Lookup_Pi(before, key) == ActiveState::Active) {
      roots.insert(key);
    }
  }
  return roots;
}

static void RemoveKeys_inplace(PermScope& scope,
                               const std::set<PermKey, PermKeyLess>& keys) {
  for (const auto& key : keys) {
    scope.erase(key);
  }
}

static void Reactivate_inplace(PermEnv& env,
                               const std::set<PermKey, PermKeyLess>& keys) {
  if (env.empty()) {
    return;
  }
  RemoveKeys_inplace(env.back(), keys);
}

static PermEnv Reactivate(const PermEnv& env,
                          const std::set<PermKey, PermKeyLess>& keys) {
  PermEnv out = env;
  Reactivate_inplace(out, keys);
  return out;
}

static std::optional<IdKey> PlaceRoot(const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    return IdKeyOf(ident->name);
  }
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    return PlaceRoot(field->base);
  }
  if (const auto* tuple = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    return PlaceRoot(tuple->base);
  }
  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    return PlaceRoot(index->base);
  }
  if (const auto* deref = std::get_if<ast::DerefExpr>(&expr->node)) {
    return PlaceRoot(deref->value);
  }
  return std::nullopt;
}

static bool IsRootIdentifierPlace(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&expr->node)) {
    return IsRootIdentifierPlace(attributed->expr);
  }
  return std::holds_alternative<ast::IdentifierExpr>(expr->node);
}

static bool PlaceWritesThroughDeref(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  if (std::holds_alternative<ast::DerefExpr>(expr->node)) {
    return true;
  }
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    return PlaceWritesThroughDeref(field->base);
  }
  if (const auto* tuple = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    return PlaceWritesThroughDeref(tuple->base);
  }
  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    return PlaceWritesThroughDeref(index->base);
  }
  return false;
}

static std::optional<IdKey> FieldHead(const ast::ExprPtr& expr) {
  if (!expr) {
    return std::nullopt;
  }
  if (std::holds_alternative<ast::IdentifierExpr>(expr->node)) {
    return std::nullopt;
  }
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    const auto inner = FieldHead(field->base);
    if (!inner.has_value()) {
      return IdKeyOf(field->name);
    }
    return inner;
  }
  if (const auto* tuple = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    return FieldHead(tuple->base);
  }
  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    return FieldHead(index->base);
  }
  if (std::holds_alternative<ast::DerefExpr>(expr->node)) {
    return std::nullopt;
  }
  return std::nullopt;
}

static std::vector<IdKey> FieldPathOf(const ast::ExprPtr& expr) {
  if (!expr) {
    return {};
  }
  if (std::holds_alternative<ast::IdentifierExpr>(expr->node)) {
    return {};
  }
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    auto path = FieldPathOf(field->base);
    path.push_back(IdKeyOf(field->name));
    return path;
  }
  if (const auto* tuple = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    return FieldPathOf(tuple->base);
  }
  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    return FieldPathOf(index->base);
  }
  if (std::holds_alternative<ast::DerefExpr>(expr->node)) {
    return {};
  }
  return {};
}

struct ScopedNames {
  std::vector<std::unordered_set<IdKey>> scopes;

  void Push() { scopes.emplace_back(); }
  void Pop() {
    if (!scopes.empty()) {
      scopes.pop_back();
    }
  }
  bool IsLocal(const IdKey& name) const {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
      if (it->find(name) != it->end()) {
        return true;
      }
    }
    return false;
  }
  void Add(const IdKey& name) {
    if (!scopes.empty()) {
      scopes.back().insert(name);
    }
  }
  void AddAll(const std::vector<IdKey>& names) {
    for (const auto& name : names) {
      Add(name);
    }
  }
};

struct ClosureCaptureCollector {
  std::set<IdKey> captures;
  std::set<IdKey> move_captures;
  ScopedNames locals;

  void RecordCapture(std::string_view name) {
    const IdKey key = IdKeyOf(name);
    if (locals.IsLocal(key)) {
      return;
    }
    captures.insert(key);
  }

  void RecordMoveCapture(std::string_view name) {
    const IdKey key = IdKeyOf(name);
    if (locals.IsLocal(key)) {
      return;
    }
    captures.insert(key);
    move_captures.insert(key);
  }

  void VisitExpr(const ast::ExprPtr& expr);
  void VisitStmt(const ast::Stmt& stmt);
  void VisitBlock(const ast::Block& block);
};

void ClosureCaptureCollector::VisitExpr(const ast::ExprPtr& expr) {
  if (!expr) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          RecordCapture(node.name);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          VisitExpr(node.expr);
        } else if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          if (std::holds_alternative<ast::ParenArgs>(node.args)) {
            const auto& args = std::get<ast::ParenArgs>(node.args).args;
            for (const auto& arg : args) {
              VisitExpr(arg.value);
            }
          } else {
            const auto& fields = std::get<ast::BraceArgs>(node.args).fields;
            for (const auto& field : fields) {
              VisitExpr(field.value);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          // Path expressions don't capture
        } else if constexpr (std::is_same_v<T, ast::LiteralExpr>) {
          // Literals don't capture
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          VisitExpr(node.lhs);
          VisitExpr(node.rhs);
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
          if (node.payload_opt) {
            std::visit(
                [this](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::EnumPayloadParen>) {
                    for (const auto& elem : payload.elements) {
                      VisitExpr(elem);
                    }
                  } else if constexpr (std::is_same_v<P, ast::EnumPayloadBrace>) {
                    for (const auto& field : payload.fields) {
                      VisitExpr(field.value);
                    }
                  }
                },
                *node.payload_opt);
          }
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          VisitExpr(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          VisitExpr(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          VisitExpr(node.base);
          VisitExpr(node.index);
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
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          VisitExpr(node.cond);
          VisitExpr(node.then_expr);
          VisitExpr(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          VisitExpr(node.scrutinee);
          for (const auto& arm : node.cases) {
            locals.Push();
            std::vector<IdKey> names;
            if (arm.pattern) {
              CollectPatNames(*arm.pattern, names);
            }
            locals.AddAll(names);
            if (arm.body) {
              VisitExpr(arm.body);
            }
            locals.Pop();
          }
          VisitExpr(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          VisitExpr(node.scrutinee);
          locals.Push();
          std::vector<IdKey> names;
          if (node.pattern) {
            CollectPatNames(*node.pattern, names);
          }
          locals.AddAll(names);
          VisitExpr(node.then_expr);
          locals.Pop();
          VisitExpr(node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          VisitExpr(node.cond);
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          VisitExpr(node.iter);
          locals.Push();
          std::vector<IdKey> names;
          if (node.pattern) {
            CollectPatNames(*node.pattern, names);
          }
          locals.AddAll(names);
          if (node.body) {
            VisitBlock(*node.body);
          }
          locals.Pop();
        } else if constexpr (std::is_same_v<T, ast::BlockExpr>) {
          if (node.block) {
            VisitBlock(*node.block);
          }
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockExpr>) {
          if (node.block) {
            VisitBlock(*node.block);
          }
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          VisitExpr(node.place);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          const auto root = PlaceRoot(node.place);
          if (root.has_value()) {
            RecordMoveCapture(*root);
          }
          VisitExpr(node.place);
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          VisitExpr(node.lhs);
          VisitExpr(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          VisitExpr(node.domain);
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::WaitExpr>) {
          VisitExpr(node.handle);
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          VisitExpr(node.range);
          locals.Push();
          std::vector<IdKey> names;
          if (node.pattern) {
            CollectPatNames(*node.pattern, names);
          }
          locals.AddAll(names);
          if (node.body) {
            VisitBlock(*node.body);
          }
          locals.Pop();
        } else if constexpr (std::is_same_v<T, ast::YieldExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::YieldFromExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::SyncExpr>) {
          VisitExpr(node.value);
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            VisitExpr(arm.expr);
          }
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (const auto& elem : node.exprs) {
            VisitExpr(elem);
          }
        } else if constexpr (std::is_same_v<T, ast::PipelineExpr>) {
          VisitExpr(node.lhs);
          VisitExpr(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          // Nested closure captures are handled by their own capture analysis.
        }
      },
      expr->node);
}

void ClosureCaptureCollector::VisitStmt(const ast::Stmt& stmt) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt>) {
          VisitExpr(node.binding.init);
          std::vector<IdKey> names;
          if (node.binding.pat) {
            CollectPatNames(*node.binding.pat, names);
          }
          locals.AddAll(names);
        } else if constexpr (std::is_same_v<T, ast::VarStmt>) {
          VisitExpr(node.binding.init);
          std::vector<IdKey> names;
          if (node.binding.pat) {
            CollectPatNames(*node.binding.pat, names);
          }
          locals.AddAll(names);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; no runtime expression.
          // The alias name is still observable in the local scope.
          locals.Add(IdKeyOf(node.alias));
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
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          VisitExpr(node.value_opt);
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          VisitExpr(node.value_opt);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          if (node.body) {
            VisitBlock(*node.body);
          }
        }
      },
      stmt);
}

void ClosureCaptureCollector::VisitBlock(const ast::Block& block) {
  locals.Push();
  for (const auto& stmt : block.stmts) {
    VisitStmt(stmt);
  }
  VisitExpr(block.tail_opt);
  locals.Pop();
}

static std::vector<std::vector<IdKey>> Prefixes(
    const std::vector<IdKey>& path) {
  std::vector<std::vector<IdKey>> out;
  out.emplace_back();
  if (path.empty()) {
    return out;
  }
  for (std::size_t i = 0; i < path.size(); ++i) {
    std::vector<IdKey> prefix;
    prefix.reserve(i + 1);
    for (std::size_t j = 0; j <= i; ++j) {
      prefix.push_back(path[j]);
    }
    out.push_back(std::move(prefix));
  }
  return out;
}

static std::set<PermKey, PermKeyLess> AncPaths(const ast::ExprPtr& expr) {
  std::set<PermKey, PermKeyLess> keys;
  const auto root = PlaceRoot(expr);
  if (!root.has_value()) {
    return keys;
  }
  const auto path = FieldPathOf(expr);
  const auto prefixes = Prefixes(path);
  for (const auto& prefix : prefixes) {
    keys.insert(PermKey{*root, prefix});
  }
  return keys;
}

static bool AccessPathOk(const PermEnv& env, const ast::ExprPtr& expr) {
  const auto keys = AncPaths(expr);
  for (const auto& key : keys) {
    if (Lookup_Pi(env, key) != ActiveState::Active) {
      return false;
    }
  }
  return true;
}

static bool AccessStateOk(const BindState& state,
                          const ast::ExprPtr& expr) {
  if (state.kind == BindStateKind::Valid) {
    return true;
  }
  if (state.kind == BindStateKind::Moved) {
    return false;
  }
  const auto head = FieldHead(expr);
  if (!head.has_value()) {
    return false;
  }
  return state.fields.find(*head) == state.fields.end();
}

static bool AccessOk_B(const BindEnv& env, const ast::ExprPtr& expr) {
  const auto root = PlaceRoot(expr);
  if (!root.has_value()) {
    return false;
  }
  const auto info = Lookup_B(env, *root);
  if (!info.has_value()) {
    return false;
  }
  return AccessStateOk(info->state, expr);
}

static bool AccessOk_Pi(const ScopeContext& ctx,
                        const TypeEnv& env,
                        const PermEnv& perms,
                        const ast::ExprPtr& expr) {
  const auto place_type = PlaceTypeOf(ctx, env, expr);
  if (!place_type.has_value()) {
    return true;
  }
  if (PermOfType(*place_type) != Permission::Unique) {
    return true;
  }
  return AccessPathOk(perms, expr);
}

static bool AccessOk(const ScopeContext& ctx,
                     const TypeEnv& env,
                     const BindEnv& binds,
                     const PermEnv& perms,
                     const ast::ExprPtr& expr) {
  return AccessOk_B(binds, expr) && AccessOk_Pi(ctx, env, perms, expr);
}

static Movability MovOf(const ast::Token& op) {
  if (op.lexeme == ":=") {
    return Movability::Immov;
  }
  return Movability::Mov;
}

static bool IsMoveExpr(const ast::ExprPtr& expr) {
  return expr && std::holds_alternative<ast::MoveExpr>(expr->node);
}

static bool IsCopyExpr(const ast::ExprPtr& expr) {
  return expr && std::holds_alternative<ast::CopyExpr>(expr->node);
}

static ast::ExprPtr MoveInner(const ast::ExprPtr& expr) {
  if (!expr) {
    return nullptr;
  }
  if (const auto* move = std::get_if<ast::MoveExpr>(&expr->node)) {
    return move->place;
  }
  return nullptr;
}

static Responsibility RespOfInit(const ScopeContext& ctx,
                                 const ast::ExprPtr& init) {
  if (!IsPlaceExpr(init)) {
    if (init) {
      if (const auto* call = std::get_if<ast::CallExpr>(&init->node)) {
        if (const auto has_resp = CallResultHasResponsibility(ctx, *call)) {
          return *has_resp ? Responsibility::Resp : Responsibility::Alias;
        }
      }
      if (const auto* method = std::get_if<ast::MethodCallExpr>(&init->node)) {
        if (const auto has_resp =
                MethodCallResultHasResponsibility(ctx, *method)) {
          return *has_resp ? Responsibility::Resp : Responsibility::Alias;
        }
      }
    }
    return Responsibility::Resp;
  }
  if (IsMoveExpr(init)) {
    return Responsibility::Resp;
  }
  return Responsibility::Alias;
}

static Movability MovEff(Movability mv, Responsibility resp) {
  if (resp == Responsibility::Alias) {
    return Movability::Immov;
  }
  return mv;
}

static BindScope BindInfoMap(const std::map<IdKey, TypeRef>& types,
                             Responsibility resp,
                             Movability mv,
                             ast::Mutability mut) {
  BindScope out;
  for (const auto& [name, _] : types) {
    BindInfo info;
    info.state = BindState{BindStateKind::Valid, {}};
    info.mov = MovEff(mv, resp);
    info.mut = mut;
    info.resp = resp;
    out.emplace(name, info);
  }
  return out;
}

static BindState PM(const BindState& state, const IdKey& field) {
  if (state.kind == BindStateKind::Valid) {
    return BindState{BindStateKind::PartiallyMoved, {field}};
  }
  if (state.kind == BindStateKind::PartiallyMoved) {
    BindState out = state;
    out.fields.insert(field);
    return out;
  }
  return BindState{BindStateKind::Moved, {}};
}

static void ConsumeOnMove_inplace(BindEnv& env, const ast::ExprPtr& expr) {
  if (!IsMoveExpr(expr)) {
    return;
  }
  const auto inner = MoveInner(expr);
  const auto root = PlaceRoot(inner);
  if (!root.has_value()) {
    return;
  }
  const auto info = Lookup_B(env, *root);
  if (!info.has_value()) {
    return;
  }
  BindInfo updated = *info;
  updated.state = BindState{BindStateKind::Moved, {}};
  (void)Update_B_inplace(env, *root, updated);
}

static BindEnv ConsumeOnMove(const BindEnv& env, const ast::ExprPtr& expr) {
  BindEnv out = env;
  ConsumeOnMove_inplace(out, expr);
  return out;
}

static ast::ExprPtr ReturnDestExprForBind(const ScopeContext& ctx,
                                          const BindEnv& binds,
                                          const TypeEnv& env,
                                          const ast::ExprPtr& expr) {
  if (!expr || IsMoveExpr(expr) || IsCopyExpr(expr) || !IsPlaceExpr(expr)) {
    return expr;
  }
  const auto root = PlaceRoot(expr);
  if (root.has_value()) {
    const auto info = Lookup_B(binds, *root);
    if (info.has_value() && info->resp == Responsibility::Alias) {
      return expr;
    }
  }
  const auto type = PlaceTypeOf(ctx, env, expr);
  if (type.has_value() && BitcopyType(ctx, *type)) {
    return expr;
  }

  auto out = std::make_shared<ast::Expr>();
  out->span = expr->span;
  out->node = ast::MoveExpr{expr};
  return out;
}

static void DowngradeUniquePath_inplace(const ScopeContext& ctx,
                                        const TypeEnv& env,
                                        PermEnv& perms,
                                        const std::optional<ParamMode>& mode,
                                        const ast::ExprPtr& expr) {
  if (mode.has_value()) {
    return;
  }
  if (!IsPlaceExpr(expr)) {
    return;
  }
  const auto place_type = PlaceTypeOf(ctx, env, expr);
  if (!place_type.has_value()) {
    return;
  }
  if (PermOfType(*place_type) != Permission::Unique) {
    return;
  }
  if (perms.empty()) {
    perms.emplace_back();
  }
  const auto keys = AncPaths(expr);
  InactivateScope_inplace(perms.back(), keys);
}

static PermEnv DowngradeUniquePath(const ScopeContext& ctx,
                                   const TypeEnv& env,
                                   const PermEnv& perms,
                                   const std::optional<ParamMode>& mode,
                                   const ast::ExprPtr& expr) {
  PermEnv out = perms;
  DowngradeUniquePath_inplace(ctx, env, out, mode, expr);
  return out;
}

static void DowngradeUnique_inplace(const ScopeContext& ctx,
                                    const TypeEnv& env,
                                    PermEnv& perms,
                                    const std::optional<ParamMode>& mode,
                                    const ast::ExprPtr& expr) {
  if (!IsPlaceExpr(expr)) {
    return;
  }
  DowngradeUniquePath_inplace(ctx, env, perms, mode, expr);
}

static PermEnv DowngradeUnique(const ScopeContext& ctx,
                               const TypeEnv& env,
                               const PermEnv& perms,
                               const std::optional<ParamMode>& mode,
                               const ast::ExprPtr& expr) {
  PermEnv out = perms;
  DowngradeUnique_inplace(ctx, env, out, mode, expr);
  return out;
}

static void DowngradeUniqueBind_inplace(const ScopeContext& ctx,
                                        const TypeEnv& env,
                                        PermEnv& perms,
                                        const ast::ExprPtr& init,
                                        const TypeRef& bind_type) {
  if (!IsPlaceExpr(init)) {
    return;
  }
  const auto init_type = PlaceTypeOf(ctx, env, init);
  if (!init_type.has_value()) {
    return;
  }
  if (PermOfType(*init_type) != Permission::Unique) {
    return;
  }
  // Binding-state suspension is the call/binding use-site rule; it is not
  // general permission subtyping and it does not rewrite the source type.
  const auto bind_perm = PermOfType(bind_type);
  if (bind_perm != Permission::Const && bind_perm != Permission::Shared) {
    return;
  }
  if (BitcopyType(ctx, bind_type)) {
    return;
  }
  DowngradeUniquePath_inplace(ctx, env, perms, std::nullopt, init);
}

static PermEnv DowngradeUniqueBind(const ScopeContext& ctx,
                                   const TypeEnv& env,
                                   const PermEnv& perms,
                                   const ast::ExprPtr& init,
                                   const TypeRef& bind_type) {
  PermEnv out = perms;
  DowngradeUniqueBind_inplace(ctx, env, out, init, bind_type);
  return out;
}

static std::optional<TypeRef> InferBindType(const ScopeContext& ctx,
                                            const TypeEnv& env,
                                            const ast::ExprPtr& init,
                                            std::optional<std::string_view>& diag_id) {
  if (const auto cached = CachedExprTypeOf(ctx, init)) {
    return cached;
  }
  auto type_ctx = MakeTypeCtx();
  auto type_expr = [&](const ast::ExprPtr& expr) {
    return TypeExpr(ctx, type_ctx, expr, env);
  };
  auto type_ident = [&](std::string_view name) -> ExprTypeResult {
    return TypeIdentifierExpr(ctx, ast::IdentifierExpr{std::string(name)}, env);
  };
  auto type_place = [&](const ast::ExprPtr& expr) {
    return TypePlace(ctx, type_ctx, expr, env);
  };
  auto if_case_check = [&](const ast::IfCaseExpr& match,
                         const TypeRef& expected) -> CheckResult {
    return CheckIfCaseExpr(ctx, type_ctx, match, env, expected);
  };
  ConstraintSet constraints;
  const auto inferred = InferExpr(ctx, init, type_expr, type_place, type_ident,
                                  if_case_check, &constraints);
  if (!inferred.ok) {
    diag_id = inferred.diag_id;
    return std::nullopt;
  }
  const auto solved = Solve(ctx, constraints);
  if (!solved.ok) {
    diag_id = solved.diag_id;
    return std::nullopt;
  }
  return ApplySubstitution(inferred.type, solved.subst);
}

static std::optional<TypeRef> BindTypeForBinding(const ScopeContext& ctx,
                                                 const TypeEnv& env,
                                                 const ast::Binding& binding,
                                                 std::optional<std::string_view>& diag_id) {
  const auto ann_type = ast::BindingAnnotationTypeOpt(binding);
  if (ann_type) {
    const auto lowered = LocalLowerType(ctx, ann_type);
    if (!lowered.ok) {
      diag_id = lowered.diag_id;
      return std::nullopt;
    }
    return lowered.type;
  }
  return InferBindType(ctx, env, binding.init, diag_id);
}

static std::map<IdKey, TypeRef> BindTypeMapFromBindings(
    const std::vector<std::pair<std::string, TypeRef>>& bindings) {
  std::map<IdKey, TypeRef> out;
  for (const auto& [name, type] : bindings) {
    out.emplace(IdKeyOf(name), type);
  }
  return out;
}

static std::optional<IdKey> FieldHeadName(const ast::ExprPtr& expr) {
  return FieldHead(expr);
}

static bool BindingMovedErrCond(const BindInfo& info,
                                const ast::ExprPtr& expr) {
  if (info.state.kind == BindStateKind::Moved) {
    return true;
  }
  if (info.state.kind == BindStateKind::PartiallyMoved) {
    const auto head = FieldHeadName(expr);
    if (!head.has_value()) {
      return true;
    }
    return info.state.fields.find(*head) != info.state.fields.end();
  }
  return false;
}

static bool AllowMovedRootAssignLhs(const BindStateBundle& in,
                                    const ast::ExprPtr& place) {
  if (!IsPlaceExpr(place)) {
    return false;
  }
  const auto root = PlaceRoot(place);
  if (!root.has_value()) {
    return false;
  }
  const auto head = FieldHeadName(place);
  if (head.has_value()) {
    return false;
  }
  const auto info = Lookup_B(in.binds, *root);
  if (!info.has_value() || info->mut != ast::Mutability::Var) {
    return false;
  }
  if (info->state.kind != BindStateKind::Moved &&
      info->state.kind != BindStateKind::PartiallyMoved) {
    return false;
  }
  return true;
}

static BindResult ErrorResult(std::string_view diag_id,
                              const std::optional<core::Span>& span) {
  BindResult result;
  result.ok = false;
  result.diag_id = diag_id;
  result.span = span;
  return result;
}

static BindResult ErrorResult(std::optional<std::string_view> diag_id,
                              const std::optional<core::Span>& span) {
  BindResult result;
  result.ok = false;
  result.diag_id = diag_id;
  result.span = span;
  return result;
}

static BindResult OkResult(const BindStateBundle& state,
                           bool falls_through = true) {
  BindResult result;
  result.ok = true;
  result.state = state;
  result.falls_through = falls_through;
  return result;
}

static ArgPassResult ArgError(std::optional<std::string_view> diag_id,
                              const std::optional<core::Span>& span) {
  ArgPassResult result;
  result.ok = false;
  result.diag_id = diag_id;
  result.span = span;
  return result;
}

static ArgPassResult ArgOk(const BindStateBundle& state,
                           std::set<PermKey, PermKeyLess> roots = {},
                           bool falls_through = true) {
  ArgPassResult result;
  result.ok = true;
  result.state = state;
  result.roots = std::move(roots);
  result.falls_through = falls_through;
  return result;
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
          } else if (const auto* brace = std::get_if<ast::BraceArgs>(&node.args)) {
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
            if (const auto* tuple = std::get_if<ast::EnumPayloadParen>(&*node.payload_opt)) {
              for (const auto& elem : tuple->elements) {
                fn(elem);
              }
            } else if (const auto* rec = std::get_if<ast::EnumPayloadBrace>(&*node.payload_opt)) {
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

static BindResult BindExpr(const ScopeContext& ctx,
                           const ast::ExprPtr& expr,
                           const BindStateBundle& in);

static BindResult BindStmt(const ScopeContext& ctx,
                           const ast::Stmt& stmt,
                           const BindStateBundle& in);

static void PopScopedBlock_inplace(BindStateBundle& state) {
  if (!state.binds.empty()) {
    state.binds.pop_back();
  }
  if (!state.perms.empty()) {
    state.perms.pop_back();
  }
  if (!state.env.scopes.empty()) {
    state.env.scopes.pop_back();
  }
}

static BindResult BindStmtSeq(const ScopeContext& ctx,
                              const std::vector<ast::Stmt>& stmts,
                              const BindStateBundle& in) {
  if (stmts.empty()) {
    SPEC_RULE("B-Seq-Empty");
    return OkResult(in);
  }
  BindStateBundle current = in;
  for (const auto& stmt : stmts) {
    auto res = BindStmt(ctx, stmt, current);
    if (!res.ok) {
      return res;
    }
    if (!res.falls_through) {
      return res;
    }
    current = std::move(res.state);
  }
  SPEC_RULE("B-Seq-Cons");
  return OkResult(current);
}

static BindResult BindBlock(const ScopeContext& ctx,
                            const ast::Block& block,
                            const BindStateBundle& in) {
  BindStateBundle scoped = in;
  scoped.binds.emplace_back();
  scoped.perms.emplace_back();
  scoped.env.scopes.emplace_back();

  auto res = BindStmtSeq(ctx, block.stmts, scoped);
  if (!res.ok) {
    return res;
  }

  BindStateBundle current = std::move(res.state);
  if (!res.falls_through) {
    PopScopedBlock_inplace(current);
    SPEC_RULE("B-Block");
    return OkResult(current, false);
  }
  if (block.tail_opt) {
    auto tail = BindExpr(ctx, block.tail_opt, current);
    if (!tail.ok) {
      return tail;
    }
    current = std::move(tail.state);
    if (!tail.falls_through) {
      PopScopedBlock_inplace(current);
      SPEC_RULE("B-Block");
      return OkResult(current, false);
    }
  }

  PopScopedBlock_inplace(current);
  SPEC_RULE("B-Block");
  return OkResult(current);
}

static bool IsMoveMissing(const ast::ExprPtr& expr) {
  if (!expr) {
    return true;
  }
  return !std::holds_alternative<ast::MoveExpr>(expr->node);
}

static ast::ExprPtr WrapMoveExpr(const ast::Arg& arg) {
  if (!arg.value) {
    return arg.value;
  }
  if (std::holds_alternative<ast::MoveExpr>(arg.value->node)) {
    return arg.value;
  }
  auto expr = std::make_shared<ast::Expr>();
  expr->node = ast::MoveExpr{arg.value};
  expr->span = arg.span.file.empty() ? arg.value->span : arg.span;
  return expr;
}

static ArgPassResult ArgPass(const ScopeContext& ctx,
                             const std::vector<ParamInfo>& params,
                             const std::vector<ast::Arg>& args,
                             const BindStateBundle& in,
                             std::size_t idx = 0) {
  if (idx >= params.size() || idx >= args.size()) {
    SPEC_RULE("B-ArgPass-Empty");
    return ArgOk(in);
  }

  const auto& param = params[idx];
  const auto& arg = args[idx];
  if (param.mode.has_value() && ast::IsRefArg(arg) && IsMoveMissing(arg.value) &&
      HasSourceProvenance(arg.value)) {
    SPEC_RULE("B-ArgPass-Move-Missing");
    return ArgError("E-MOD-2411", arg.span);
  }

  ast::ExprPtr eval_expr = arg.value;
  if (param.mode.has_value() && ast::IsMoveArg(arg)) {
    eval_expr = WrapMoveExpr(arg);
  }

  auto eval = BindExpr(ctx, eval_expr, in);
  if (!eval.ok) {
    return ArgError(eval.diag_id, eval.span);
  }
  if (!eval.falls_through) {
    return ArgOk(eval.state, {}, false);
  }

  if (!param.mode.has_value()) {
    if (HasSourceProvenance(arg.value) && !IsPlaceExprForCall(arg.value)) {
      SPEC_RULE("Call-Arg-NotPlace");
      return ArgError("E-TYP-1603", arg.span);
    }
    if (!HasSourceProvenance(arg.value)) {
      return ArgPass(ctx, params, args, eval.state, idx + 1);
    }
  }

  BindStateBundle next = std::move(eval.state);
  const auto perms_before = next.perms;
  DowngradeUnique_inplace(ctx, next.env, next.perms, param.mode, arg.value);

  const auto rest = ArgPass(ctx, params, args, next, idx + 1);
  if (!rest.ok) {
    return rest;
  }

  std::set<PermKey, PermKeyLess> roots = rest.roots;
  const auto new_roots = Roots(next.perms, perms_before);
  roots.insert(new_roots.begin(), new_roots.end());

  SPEC_RULE("B-ArgPass-Cons");
  return ArgOk(rest.state, std::move(roots), rest.falls_through);
}

static BindResult BindMoveExpr(const ScopeContext& ctx,
                               const ast::MoveExpr& move,
                               const BindStateBundle& in) {
  const auto place = move.place;
  if (!IsPlaceExpr(place)) {
    return OkResult(in);
  }

  const auto place_type = PlaceTypeOf(ctx, in.env, place);
  if (place_type.has_value() &&
      PermOfType(*place_type) == Permission::Unique &&
      !AccessPathOk(in.perms, place)) {
    SPEC_RULE("B-Move-Unique-Err");
    return ErrorResult(std::string_view("E-TYP-1602"), std::optional<core::Span>(place->span));
  }

  const auto root = PlaceRoot(place);
  if (!root.has_value()) {
    return OkResult(in);
  }
  const auto info = Lookup_B(in.binds, *root);
  if (!info.has_value()) {
    return OkResult(in);
  }

  const auto head = FieldHeadName(place);
  if (info->mov == Movability::Immov) {
    if (head.has_value()) {
      SPEC_RULE("B-Move-Field-Immovable-Err");
      return ErrorResult(std::string_view("E-MEM-3006"), std::optional<core::Span>(place->span));
    }
    SPEC_RULE("B-Move-Whole-Immovable-Err");
    return ErrorResult(std::string_view("E-MEM-3006"), std::optional<core::Span>(place->span));
  }

  if (!head.has_value()) {
    if (info->state.kind != BindStateKind::Valid) {
      SPEC_RULE("B-Move-Whole-Moved-Err");
      return ErrorResult(std::string_view("E-MEM-3001"), std::optional<core::Span>(place->span));
    }
    BindInfo updated = *info;
    updated.state = BindState{BindStateKind::Moved, {}};
    BindStateBundle out = in;
    (void)Update_B_inplace(out.binds, *root, updated);
    SPEC_RULE("B-Move-Whole");
    return OkResult(out);
  }

  const auto place_perm = place_type.has_value() ? PermOfType(*place_type)
                                                 : Permission::Const;
  if (place_perm != Permission::Unique) {
    SPEC_RULE("B-Move-Field-NonUnique-Err");
    return ErrorResult(std::string_view("E-MEM-3004"), std::optional<core::Span>(place->span));
  }
  if (info->state.kind == BindStateKind::Moved ||
      (info->state.kind == BindStateKind::PartiallyMoved &&
       info->state.fields.find(*head) != info->state.fields.end())) {
    SPEC_RULE("B-Move-Field-Moved-Err");
    return ErrorResult(std::string_view("E-MEM-3001"), std::optional<core::Span>(place->span));
  }

  BindInfo updated = *info;
  updated.state = PM(info->state, *head);
  BindStateBundle out = in;
  (void)Update_B_inplace(out.binds, *root, updated);
  SPEC_RULE("B-Move-Field");
  return OkResult(out);
}

static BindResult BindPlaceExpr(const ScopeContext& ctx,
                                const ast::ExprPtr& expr,
                                const BindStateBundle& in) {
  if (AccessOk(ctx, in.env, in.binds, in.perms, expr)) {
    SPEC_RULE("B-Place");
    return OkResult(in);
  }

  const auto place_type = PlaceTypeOf(ctx, in.env, expr);
  if (place_type.has_value() &&
      PermOfType(*place_type) == Permission::Unique &&
      !AccessPathOk(in.perms, expr)) {
    SPEC_RULE("B-Place-Unique-Err");
    return ErrorResult(std::string_view("E-TYP-1602"), std::optional<core::Span>(expr->span));
  }

  const auto root = PlaceRoot(expr);
  if (!root.has_value()) {
    return ErrorResult(std::nullopt, expr->span);
  }
  const auto info = Lookup_B(in.binds, *root);
  if (!info.has_value()) {
    return ErrorResult(std::nullopt, expr->span);
  }
  if (BindingMovedErrCond(*info, expr)) {
    SPEC_RULE("B-Place-Moved-Err");
    return ErrorResult(std::string_view("E-MEM-3001"), std::optional<core::Span>(expr->span));
  }

  return ErrorResult(std::nullopt, expr->span);
}

static std::vector<ParamInfo> ParamInfosFromFunc(const TypeFunc& func) {
  std::vector<ParamInfo> params;
  params.reserve(func.params.size());
  for (const auto& param : func.params) {
    params.push_back(ParamInfo{param.mode});
  }
  return params;
}

static const ast::ASTModule* FindModuleByPathForCallParams(
    const ScopeContext& ctx,
    const ast::ModulePath& path) {
  for (const auto& module : ctx.sigma.mods) {
    if (module.path == path) {
      return &module;
    }
  }
  return nullptr;
}

static const ast::ProcedureDecl* FindProcedureByNameForCallParams(
    const ast::ASTModule& module,
    std::string_view name) {
  for (const auto& item : module.items) {
    const auto* proc = std::get_if<ast::ProcedureDecl>(&item);
    if (!proc) {
      continue;
    }
    if (IdEq(proc->name, name)) {
      return proc;
    }
  }
  return nullptr;
}

static std::optional<std::vector<ParamInfo>> ParamsFromProcedureDecl(
    const ScopeContext& ctx,
    const ast::ModulePath& path,
    std::string_view name) {
  const auto* module = FindModuleByPathForCallParams(ctx, path);
  if (!module) {
    return std::nullopt;
  }
  const auto* proc = FindProcedureByNameForCallParams(*module, name);
  if (!proc) {
    return std::nullopt;
  }
  std::vector<ParamInfo> params;
  params.reserve(proc->params.size());
  for (const auto& param : proc->params) {
    params.push_back(ParamInfo{LowerParamMode(param.mode)});
  }
  return params;
}

static std::optional<std::vector<ParamInfo>> ParamsFromUniqueProcedureName(
    const ScopeContext& ctx,
    std::string_view name) {
  const ast::ProcedureDecl* matched = nullptr;
  for (const auto& module : ctx.sigma.mods) {
    const auto* proc = FindProcedureByNameForCallParams(module, name);
    if (!proc) {
      continue;
    }
    if (matched && matched != proc) {
      return std::nullopt;
    }
    matched = proc;
  }
  if (!matched) {
    return std::nullopt;
  }
  std::vector<ParamInfo> params;
  params.reserve(matched->params.size());
  for (const auto& param : matched->params) {
    params.push_back(ParamInfo{LowerParamMode(param.mode)});
  }
  return params;
}

static std::optional<std::vector<ParamInfo>> ParamsForCall(
    const ScopeContext& ctx,
    const TypeEnv& env,
    const ast::ExprPtr& callee) {
  const auto type = ExprTypeOf(ctx, env, callee);
  if (!type.has_value() || !*type) {
    // Fall through to declaration-lookup fallback.
  } else {
    TypeRef callee_type = *type;
    while (callee_type) {
      if (const auto* perm = std::get_if<TypePerm>(&callee_type->node)) {
        callee_type = perm->base;
        continue;
      }
      break;
    }
    if (const auto* func = std::get_if<TypeFunc>(&callee_type->node)) {
      return ParamInfosFromFunc(*func);
    }
  }

  auto lookup_value_params = [&](const ast::ModulePath& path,
                                 std::string_view name)
      -> std::optional<std::vector<ParamInfo>> {
    if (const auto params = ParamsFromProcedureDecl(ctx, path, name)) {
      return params;
    }
    if (path.empty()) {
      if (const auto params = ParamsFromUniqueProcedureName(ctx, name)) {
        return params;
      }
    }
    const auto value_type = ValuePathType(ctx, path, name);
    if (!value_type.ok || !value_type.type) {
      return std::nullopt;
    }
    if (const auto* func = std::get_if<TypeFunc>(&value_type.type->node)) {
      return ParamInfosFromFunc(*func);
    }
    return std::nullopt;
  };

  if (!callee) {
    return std::nullopt;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&callee->node)) {
    if (const auto params = lookup_value_params(ctx.current_module, ident->name)) {
      return params;
    }
  } else if (const auto* qname = std::get_if<ast::QualifiedNameExpr>(&callee->node)) {
    const auto& path = qname->path.empty() ? ctx.current_module : qname->path;
    if (const auto params = lookup_value_params(path, qname->name)) {
      return params;
    }
  } else if (const auto* path = std::get_if<ast::PathExpr>(&callee->node)) {
    const auto& resolved_path =
        path->path.empty() ? ctx.current_module : path->path;
    if (const auto params = lookup_value_params(resolved_path, path->name)) {
      return params;
    }
  }
  return std::nullopt;
}

static std::optional<std::vector<ParamInfo>> ParamsForMethod(
    const ScopeContext& ctx,
    const TypeEnv& env,
    const ast::ExprPtr& receiver,
    std::string_view name,
    std::optional<ParamMode>& recv_mode) {
  (void)env;
  const auto recv_type = ExprTypeOf(ctx, env, receiver);
  if (!recv_type.has_value() || !*recv_type) {
    return std::nullopt;
  }

  TypeRef base = StripPerm(*recv_type);
  if (!base) {
    return std::nullopt;
  }

  if (const auto* dyn = std::get_if<TypeDynamic>(&base->node)) {
    const auto* method = LookupClassMethod(ctx, dyn->path, name);
    if (!method) {
      return std::nullopt;
    }
    if (const auto* explicit_recv =
            std::get_if<ast::ReceiverExplicit>(&method->receiver)) {
      recv_mode = LowerParamMode(explicit_recv->mode_opt);
    }
    std::vector<ParamInfo> params;
    params.reserve(method->params.size());
    for (const auto& param : method->params) {
      params.push_back(ParamInfo{LowerParamMode(param.mode)});
    }
    return params;
  }

  const auto* path_type = std::get_if<TypePathType>(&base->node);
  if (!path_type) {
    return std::nullopt;
  }

  const ast::RecordDecl* record = nullptr;
  std::vector<ast::ClassPath> implements;
  const auto it = ctx.sigma.types.find(PathKeyOf(path_type->path));
  if (it != ctx.sigma.types.end()) {
    if (const auto* record_decl = std::get_if<ast::RecordDecl>(&it->second)) {
      record = record_decl;
      implements = record_decl->implements;
    } else if (const auto* enum_decl =
                   std::get_if<ast::EnumDecl>(&it->second)) {
      implements = enum_decl->implements;
    } else if (const auto* modal_decl =
                   std::get_if<ast::ModalDecl>(&it->second)) {
      implements = modal_decl->implements;
    }
  }

  if (record) {
    for (const auto& member : record->members) {
      const auto* method = std::get_if<ast::MethodDecl>(&member);
      if (!method) {
        continue;
      }
      if (!IdEq(method->name, name)) {
        continue;
      }
      if (const auto* explicit_recv =
              std::get_if<ast::ReceiverExplicit>(&method->receiver)) {
        recv_mode = LowerParamMode(explicit_recv->mode_opt);
      }
      std::vector<ParamInfo> params;
      params.reserve(method->params.size());
      for (const auto& param : method->params) {
        params.push_back(ParamInfo{LowerParamMode(param.mode)});
      }
      return params;
    }
  }

  const ast::ClassMethodDecl* default_method = nullptr;
  for (const auto& class_path : implements) {
    const auto table = ClassMethodTable(ctx, class_path);
    if (!table.ok) {
      continue;
    }
    for (const auto& entry : table.methods) {
      const auto* method = entry.method;
      if (!method || !method->body_opt) {
        continue;
      }
      if (!IdEq(method->name, name)) {
        continue;
      }
      if (default_method && default_method != method) {
        return std::nullopt;
      }
      default_method = method;
    }
  }

  if (default_method) {
    if (const auto* explicit_recv =
            std::get_if<ast::ReceiverExplicit>(&default_method->receiver)) {
      recv_mode = LowerParamMode(explicit_recv->mode_opt);
    }
    std::vector<ParamInfo> params;
    params.reserve(default_method->params.size());
    for (const auto& param : default_method->params) {
      params.push_back(ParamInfo{LowerParamMode(param.mode)});
    }
    return params;
  }

  return std::nullopt;
}

static bool IsModalTransitionCall(const ScopeContext& ctx,
                                  const TypeEnv& env,
                                  const ast::ExprPtr& receiver,
                                  std::string_view name) {
  const auto recv_type = ExprTypeOf(ctx, env, receiver);
  if (!recv_type.has_value() || !*recv_type) {
    return false;
  }
  const auto base = StripPerm(*recv_type);
  const auto* modal = base ? std::get_if<TypeModalState>(&base->node) : nullptr;
  if (!modal) {
    return false;
  }

  const auto it = ctx.sigma.types.find(PathKeyOf(modal->path));
  if (it == ctx.sigma.types.end()) {
    return false;
  }
  const auto* modal_decl = std::get_if<ast::ModalDecl>(&it->second);
  if (!modal_decl) {
    return false;
  }

  for (const auto& state : modal_decl->states) {
    if (!IdEq(state.name, modal->state)) {
      continue;
    }
    for (const auto& member : state.members) {
      const auto* transition = std::get_if<ast::TransitionDecl>(&member);
      if (!transition) {
        continue;
      }
      if (IdEq(transition->name, std::string(name))) {
        return true;
      }
    }
    break;
  }
  return false;
}

static std::optional<std::vector<ParamInfo>> ParamsForModalTransition(
    const ScopeContext& ctx,
    const TypeEnv& env,
    const ast::ExprPtr& receiver,
    std::string_view name,
    std::optional<ParamMode>& recv_mode) {
  const auto recv_type = ExprTypeOf(ctx, env, receiver);
  if (!recv_type.has_value() || !*recv_type) {
    return std::nullopt;
  }
  const auto base = StripPerm(*recv_type);
  const auto* modal = base ? std::get_if<TypeModalState>(&base->node) : nullptr;
  if (!modal) {
    return std::nullopt;
  }

  const auto it = ctx.sigma.types.find(PathKeyOf(modal->path));
  if (it == ctx.sigma.types.end()) {
    return std::nullopt;
  }
  const auto* modal_decl = std::get_if<ast::ModalDecl>(&it->second);
  if (!modal_decl) {
    return std::nullopt;
  }
  const auto* transition = LookupTransitionDecl(*modal_decl, modal->state, name);
  if (!transition) {
    return std::nullopt;
  }

  recv_mode = ParamMode::Move;
  std::vector<ParamInfo> params;
  params.reserve(transition->params.size());
  for (const auto& param : transition->params) {
    params.push_back(ParamInfo{LowerParamMode(param.mode)});
  }
  return params;
}

static BindResult BindCallExpr(const ScopeContext& ctx,
                               const ast::CallExpr& call,
                               const BindStateBundle& in) {
  // Direct named callees denote callable symbols, not mutable places.
  // Evaluating them via BindPlaceExpr incorrectly produces null-diag failures,
  // which can silently bypass later provenance diagnostics.
  auto is_direct_symbol_callee = [](const ast::ExprPtr& expr) -> bool {
    if (!expr) {
      return false;
    }
    return std::holds_alternative<ast::IdentifierExpr>(expr->node) ||
           std::holds_alternative<ast::PathExpr>(expr->node) ||
           std::holds_alternative<ast::QualifiedNameExpr>(expr->node);
  };

  BindStateBundle callee_state = in;
  if (!is_direct_symbol_callee(call.callee)) {
    auto callee_res = BindExpr(ctx, call.callee, in);
    if (!callee_res.ok) {
      return callee_res;
    }
    if (!callee_res.falls_through) {
      return callee_res;
    }
    callee_state = std::move(callee_res.state);
  }

  auto params = ParamsForCall(ctx, callee_state.env, call.callee);
  if (!params.has_value()) {
    BindStateBundle current = callee_state;
    for (const auto& arg : call.args) {
      auto eval = BindExpr(ctx, arg.value, current);
      if (!eval.ok) {
        return eval;
      }
      if (!eval.falls_through) {
        return eval;
      }
      current = std::move(eval.state);
    }
    SPEC_RULE("B-Expr-Sub");
    return OkResult(current);
  }

  const auto args_res =
      ArgPass(ctx, *params, call.args, callee_state);
  if (!args_res.ok) {
    return ErrorResult(args_res.diag_id, args_res.span);
  }
  if (!args_res.falls_through) {
    return OkResult(args_res.state, false);
  }

  BindStateBundle out = std::move(args_res.state);
  Reactivate_inplace(out.perms, args_res.roots);
  SPEC_RULE("B-Call");
  return OkResult(out);
}

static BindResult BindMethodCallExpr(const ScopeContext& ctx,
                                     const ast::MethodCallExpr& call,
                                     const BindStateBundle& in) {
  std::optional<ParamMode> recv_mode;
  auto params = ParamsForMethod(ctx, in.env, call.receiver, call.name, recv_mode);
  const bool is_transition =
      IsModalTransitionCall(ctx, in.env, call.receiver, call.name);
  if (!params.has_value() && is_transition) {
    params = ParamsForModalTransition(
        ctx, in.env, call.receiver, call.name, recv_mode);
  }
  if (!params.has_value()) {
    BindStateBundle current = in;
    auto base = BindExpr(ctx, call.receiver, current);
    if (!base.ok) {
      return base;
    }
    if (!base.falls_through) {
      return base;
    }
    current = std::move(base.state);
    for (const auto& arg : call.args) {
      auto eval = BindExpr(ctx, arg.value, current);
      if (!eval.ok) {
        return eval;
      }
      if (!eval.falls_through) {
        return eval;
      }
      current = std::move(eval.state);
    }
    if (is_transition) {
      SPEC_RULE("B-Transition");
    } else {
      SPEC_RULE("B-Expr-Sub");
    }
    return OkResult(current);
  }

  if (recv_mode.has_value() && !is_transition && IsMoveMissing(call.receiver) &&
      HasSourceProvenance(call.receiver)) {
    SPEC_RULE("B-ArgPass-Move-Missing");
    return ErrorResult(std::string_view("E-MOD-2411"), std::optional<core::Span>(call.receiver->span));
  }

  if (!recv_mode.has_value() && HasSourceProvenance(call.receiver) &&
      !IsPlaceExprForCall(call.receiver)) {
    SPEC_RULE("Call-Arg-NotPlace");
    return ErrorResult(std::string_view("E-TYP-1603"), std::optional<core::Span>(call.receiver->span));
  }

  auto recv_res = BindExpr(ctx, call.receiver, in);
  if (!recv_res.ok) {
    return recv_res;
  }
  if (!recv_res.falls_through) {
    return recv_res;
  }

  BindStateBundle recv_state = std::move(recv_res.state);
  const auto perms_before = recv_state.perms;
  DowngradeUnique_inplace(ctx, recv_state.env, recv_state.perms, recv_mode,
                          call.receiver);

  const auto recv_roots = Roots(recv_state.perms, perms_before);

  const auto args_res = ArgPass(ctx, *params, call.args, recv_state);
  if (!args_res.ok) {
    return ErrorResult(args_res.diag_id, args_res.span);
  }
  if (!args_res.falls_through) {
    return OkResult(args_res.state, false);
  }

  std::set<PermKey, PermKeyLess> all_roots = recv_roots;
  all_roots.insert(args_res.roots.begin(), args_res.roots.end());

  BindStateBundle out = std::move(args_res.state);
  Reactivate_inplace(out.perms, all_roots);
  if (is_transition) {
    SPEC_RULE("B-Transition");
  } else {
    SPEC_RULE("B-MethodCall");
  }
  return OkResult(out);
}

static BindResult BindIfExpr(const ScopeContext& ctx,
                             const ast::IfExpr& expr,
                             const BindStateBundle& in) {
  auto cond = BindExpr(ctx, expr.cond, in);
  if (!cond.ok) {
    return cond;
  }
  if (!cond.falls_through) {
    return cond;
  }

  auto then_res = BindExpr(ctx, expr.then_expr, cond.state);
  if (!then_res.ok) {
    return then_res;
  }
  auto else_res = BindExpr(ctx, expr.else_expr, cond.state);
  if (!else_res.ok) {
    return else_res;
  }

  if (!then_res.falls_through && !else_res.falls_through) {
    SPEC_RULE("B-If");
    return OkResult(cond.state, false);
  }
  if (then_res.falls_through && !else_res.falls_through) {
    SPEC_RULE("B-If");
    return OkResult(then_res.state);
  }
  if (!then_res.falls_through && else_res.falls_through) {
    SPEC_RULE("B-If");
    return OkResult(else_res.state);
  }

  const auto joined_b = Join_B(then_res.state.binds, else_res.state.binds);
  const auto joined_p = JoinPerm(then_res.state.perms, else_res.state.perms);
  if (!joined_b.has_value() || !joined_p.has_value()) {
    return ErrorResult(std::nullopt, expr.cond ? expr.cond->span : expr.else_expr->span);
  }

  BindStateBundle out = std::move(cond.state);
  out.binds = *joined_b;
  out.perms = *joined_p;
  SPEC_RULE("B-If");
  return OkResult(out);
}

static BindResult BindIfCaseExpr(const ScopeContext& ctx,
                                const ast::IfCaseExpr& expr,
                                const BindStateBundle& in) {
  auto scrutinee = BindExpr(ctx, expr.scrutinee, in);
  if (!scrutinee.ok) {
    return scrutinee;
  }
  if (!scrutinee.falls_through) {
    return scrutinee;
  }

  const auto scrut_type = ExprTypeOf(ctx, scrutinee.state.env, expr.scrutinee);
  if (!scrut_type.has_value()) {
    return ErrorResult(std::nullopt, expr.scrutinee ? std::optional<core::Span>(expr.scrutinee->span) : std::nullopt);
  }

  const bool moved = IsMoveExpr(expr.scrutinee);
  BindStateBundle base = std::move(scrutinee.state);
  ConsumeOnMove_inplace(base.binds, expr.scrutinee);

  std::vector<BindEnv> bind_envs;
  std::vector<PermEnv> perm_envs;
  for (const auto& arm : expr.cases) {
    if (!arm.pattern || !arm.body) {
      return ErrorResult(std::nullopt, arm.pattern ? std::optional<core::Span>(arm.pattern->span) : std::nullopt);
    }
    const auto pat = TypePatternAgainstType(ctx, arm.pattern, *scrut_type);
    if (!pat.ok) {
      return ErrorResult(pat.diag_id, arm.pattern->span);
    }
    const auto type_map = BindTypeMapFromBindings(pat.bindings);
    const Responsibility resp = moved ? Responsibility::Resp
                                      : Responsibility::Alias;

    BindStateBundle arm_state = base;
    arm_state.binds.emplace_back();
    arm_state.perms.emplace_back();
    arm_state.env.scopes.emplace_back();

    arm_state.binds = IntroAll_B(
        arm_state.binds, BindInfoMap(type_map, resp, Movability::Mov,
                                     ast::Mutability::Let));
    for (const auto& [name, type] : pat.bindings) {
      arm_state.env.scopes.back()[IdKeyOf(name)] =
          TypeBinding{ast::Mutability::Let, type};
    }

    auto body = BindExpr(ctx, arm.body, arm_state);
    if (!body.ok) {
      return body;
    }
    arm_state = std::move(body.state);
    const bool arm_falls_through = body.falls_through;

    SPEC_RULE("B-Arm");

    if (!arm_state.binds.empty()) {
      arm_state.binds.pop_back();
    }
    if (!arm_state.perms.empty()) {
      arm_state.perms.pop_back();
    }
    if (!arm_state.env.scopes.empty()) {
      arm_state.env.scopes.pop_back();
    }

    if (arm_falls_through) {
      bind_envs.push_back(std::move(arm_state.binds));
      perm_envs.push_back(std::move(arm_state.perms));
    }
  }

  if (bind_envs.empty()) {
    SPEC_RULE("B-IfCase");
    return OkResult(base, false);
  }

  const auto joined_b = JoinAll_B(bind_envs);
  const auto joined_p = JoinAllPerm(perm_envs);
  if (!joined_b.has_value() || !joined_p.has_value()) {
    return ErrorResult(std::nullopt, expr.scrutinee ? std::optional<core::Span>(expr.scrutinee->span) : std::nullopt);
  }

  BindStateBundle out = std::move(base);
  out.binds = *joined_b;
  out.perms = *joined_p;
  SPEC_RULE("B-IfCase");
  return OkResult(out);
}

static BindResult BindIfIsExpr(const ScopeContext& ctx,
                              const ast::IfIsExpr& expr,
                              const BindStateBundle& in) {
  if (!expr.pattern || !expr.then_expr) {
    return ErrorResult(std::nullopt,
                       expr.scrutinee ? std::optional<core::Span>(expr.scrutinee->span)
                                      : std::nullopt);
  }

  auto scrutinee = BindExpr(ctx, expr.scrutinee, in);
  if (!scrutinee.ok) {
    return scrutinee;
  }
  if (!scrutinee.falls_through) {
    return scrutinee;
  }

  const auto scrut_type = ExprTypeOf(ctx, scrutinee.state.env, expr.scrutinee);
  if (!scrut_type.has_value()) {
    return ErrorResult(std::nullopt,
                       expr.scrutinee ? std::optional<core::Span>(expr.scrutinee->span)
                                      : std::nullopt);
  }

  const bool moved = IsMoveExpr(expr.scrutinee);
  BindStateBundle base = std::move(scrutinee.state);
  ConsumeOnMove_inplace(base.binds, expr.scrutinee);

  const auto pat = TypePatternAgainstType(ctx, expr.pattern, *scrut_type);
  if (!pat.ok) {
    return ErrorResult(pat.diag_id, expr.pattern->span);
  }

  const auto type_map = BindTypeMapFromBindings(pat.bindings);
  const Responsibility resp = moved ? Responsibility::Resp
                                    : Responsibility::Alias;

  BindStateBundle then_state = base;
  then_state.binds.emplace_back();
  then_state.perms.emplace_back();
  then_state.env.scopes.emplace_back();
  then_state.binds = IntroAll_B(
      then_state.binds, BindInfoMap(type_map, resp, Movability::Mov,
                                    ast::Mutability::Let));
  for (const auto& [name, type] : pat.bindings) {
    then_state.env.scopes.back()[IdKeyOf(name)] =
        TypeBinding{ast::Mutability::Let, type};
  }

  auto then_res = BindExpr(ctx, expr.then_expr, then_state);
  if (!then_res.ok) {
    return then_res;
  }
  const bool then_falls_through = then_res.falls_through;
  then_state = std::move(then_res.state);
  if (!then_state.binds.empty()) {
    then_state.binds.pop_back();
  }
  if (!then_state.perms.empty()) {
    then_state.perms.pop_back();
  }
  if (!then_state.env.scopes.empty()) {
    then_state.env.scopes.pop_back();
  }

  auto else_res = BindExpr(ctx, expr.else_expr, base);
  if (!else_res.ok) {
    return else_res;
  }

  if (!then_falls_through && !else_res.falls_through) {
    SPEC_RULE("B-If");
    return OkResult(base, false);
  }
  if (then_falls_through && !else_res.falls_through) {
    SPEC_RULE("B-If");
    return OkResult(then_state);
  }
  if (!then_falls_through && else_res.falls_through) {
    SPEC_RULE("B-If");
    return OkResult(else_res.state);
  }

  const auto joined_b = Join_B(then_state.binds, else_res.state.binds);
  const auto joined_p = JoinPerm(then_state.perms, else_res.state.perms);
  if (!joined_b.has_value() || !joined_p.has_value()) {
    return ErrorResult(std::nullopt,
                       expr.scrutinee ? std::optional<core::Span>(expr.scrutinee->span)
                                      : std::nullopt);
  }

  BindStateBundle out = std::move(base);
  out.binds = *joined_b;
  out.perms = *joined_p;
  SPEC_RULE("B-If");
  return OkResult(out);
}

static std::optional<TypeRef> IterElementType(const TypeRef& iter_type) {
  const auto stripped = StripPerm(iter_type);
  if (!stripped) {
    return std::nullopt;
  }
  if (const auto* slice = std::get_if<TypeSlice>(&stripped->node)) {
    return slice->element;
  }
  if (const auto* array = std::get_if<TypeArray>(&stripped->node)) {
    return array->element;
  }
  return std::nullopt;
}

static BindResult LoopFix(const ScopeContext& ctx,
                          const BindStateBundle& init,
                          const std::function<BindResult(const BindStateBundle&)>& step) {
  BindStateBundle current = init;
  for (std::size_t iter = 0; iter < 128; ++iter) {
    auto body = step(current);
    if (!body.ok) {
      return body;
    }
    if (!body.falls_through) {
      return body;
    }
    const auto joined_b = Join_B(init.binds, body.state.binds);
    const auto joined_p = JoinPerm(init.perms, body.state.perms);
    if (!joined_b.has_value() || !joined_p.has_value()) {
      return ErrorResult(std::nullopt, std::nullopt);
    }
    BindStateBundle next = current;
    next.binds = *joined_b;
    next.perms = *joined_p;
    if (BindEnvEqual(next.binds, current.binds) &&
        PermEnvEqual(next.perms, current.perms)) {
      return OkResult(next);
    }
    current = std::move(next);
  }
  return ErrorResult(std::nullopt, std::nullopt);
}

static BindResult BindLoopInfinite(const ScopeContext& ctx,
                                   const ast::LoopInfiniteExpr& loop,
                                   const BindStateBundle& in) {
  auto step = [&](const BindStateBundle& state) {
    return BindBlock(ctx, *loop.body, state);
  };
  const auto fixed = LoopFix(ctx, in, step);
  if (!fixed.ok) {
    return fixed;
  }
  SPEC_RULE("B-Loop-Infinite");
  return fixed;
}

static BindResult BindLoopConditional(const ScopeContext& ctx,
                                      const ast::LoopConditionalExpr& loop,
                                      const BindStateBundle& in) {
  auto step = [&](const BindStateBundle& state) {
    auto cond = BindExpr(ctx, loop.cond, state);
    if (!cond.ok) {
      return cond;
    }
    return BindBlock(ctx, *loop.body, std::move(cond.state));
  };
  const auto fixed = LoopFix(ctx, in, step);
  if (!fixed.ok) {
    return fixed;
  }
  SPEC_RULE("B-Loop-Conditional");
  return fixed;
}

static BindResult BindLoopIter(const ScopeContext& ctx,
                              const ast::LoopIterExpr& loop,
                              const BindStateBundle& in) {
  auto iter_eval = BindExpr(ctx, loop.iter, in);
  if (!iter_eval.ok) {
    return iter_eval;
  }

  TypeRef pat_type;
  if (loop.type_opt) {
    const auto lowered = LocalLowerType(ctx, loop.type_opt);
    if (!lowered.ok) {
      return ErrorResult(lowered.diag_id, loop.type_opt->span);
    }
    pat_type = lowered.type;
  } else {
    const auto iter_type = ExprTypeOf(ctx, iter_eval.state.env, loop.iter);
    if (!iter_type.has_value()) {
      return ErrorResult(std::nullopt, loop.iter ? std::optional<core::Span>(loop.iter->span) : std::nullopt);
    }
    const auto elem = IterElementType(*iter_type);
    if (!elem.has_value()) {
      return ErrorResult(std::nullopt, loop.iter ? std::optional<core::Span>(loop.iter->span) : std::nullopt);
    }
    pat_type = *elem;
  }

  const auto pat = TypePattern(ctx, loop.pattern, pat_type);
  if (!pat.ok) {
    return ErrorResult(pat.diag_id, loop.pattern->span);
  }

  const auto type_map = BindTypeMapFromBindings(pat.bindings);

  auto step = [&](const BindStateBundle& state) {
    BindStateBundle scoped = state;
    scoped.binds.emplace_back();
    scoped.perms.emplace_back();
    scoped.env.scopes.emplace_back();

    scoped.binds = IntroAll_B(
        scoped.binds, BindInfoMap(type_map, Responsibility::Resp,
                                  Movability::Mov, ast::Mutability::Let));
    for (const auto& [name, type] : pat.bindings) {
      scoped.env.scopes.back()[IdKeyOf(name)] =
          TypeBinding{ast::Mutability::Let, type};
    }

    auto body = BindBlock(ctx, *loop.body, scoped);
    if (!body.ok) {
      return body;
    }
    BindStateBundle out = std::move(body.state);
    if (!out.binds.empty()) {
      out.binds.pop_back();
    }
    if (!out.perms.empty()) {
      out.perms.pop_back();
    }
    if (!out.env.scopes.empty()) {
      out.env.scopes.pop_back();
    }
    return OkResult(out);
  };

  const auto fixed = LoopFix(ctx, std::move(iter_eval.state), step);
  if (!fixed.ok) {
    return fixed;
  }

  SPEC_RULE("B-Loop-Iter");
  return fixed;
}

static BindResult BindClosureExpr(const ScopeContext& ctx,
                                  const ast::ExprPtr& expr,
                                  const ast::ClosureExpr& closure,
                                  const BindStateBundle& in) {
  ClosureCaptureCollector collector;
  collector.locals.Push();
  for (const auto& param : closure.params) {
    collector.locals.Add(IdKeyOf(param.name));
  }
  collector.VisitExpr(closure.body);
  collector.locals.Pop();

  if (collector.captures.empty()) {
    SPEC_RULE("B-Closure-NonCapturing");
    return OkResult(in);
  }

  std::set<IdKey> move_caps = collector.move_captures;
  for (const auto& param : closure.params) {
    if (!param.move_capture) {
      continue;
    }
    const auto key = IdKeyOf(param.name);
    if (collector.captures.find(key) != collector.captures.end()) {
      move_caps.insert(key);
    }
  }

  for (const auto& name : collector.captures) {
    const auto binding = BindOf(in.env, name);
    if (!binding.has_value() || !binding->type) {
      continue;
    }
    if (PermOfType(binding->type) == Permission::Unique &&
        move_caps.find(name) == move_caps.end()) {
      SPEC_RULE("Capture-Unique-NoMove-Err");
      return ErrorResult(std::string_view("E-CON-0120"),
                         expr ? std::optional<core::Span>(expr->span)
                              : std::optional<core::Span>{});
    }
  }

  for (const auto& name : move_caps) {
    const auto info = Lookup_B(in.binds, name);
    if (!info.has_value()) {
      continue;
    }
    if (info->mov == Movability::Immov) {
      SPEC_RULE("B-Closure-MoveCapture-Immovable-Err");
      return ErrorResult(std::string_view("E-MEM-3006"),
                         expr ? std::optional<core::Span>(expr->span)
                              : std::optional<core::Span>{});
    }
    if (info->state.kind != BindStateKind::Valid) {
      SPEC_RULE("B-Closure-MoveCapture-Moved-Err");
      return ErrorResult(std::string_view("E-CON-0121"),
                         expr ? std::optional<core::Span>(expr->span)
                              : std::optional<core::Span>{});
    }
  }

  for (const auto& name : collector.captures) {
    if (move_caps.find(name) != move_caps.end()) {
      continue;
    }
    const auto info = Lookup_B(in.binds, name);
    if (!info.has_value()) {
      continue;
    }
    if (info->state.kind != BindStateKind::Valid) {
      SPEC_RULE("B-Closure-RefCapture-Moved-Err");
      return ErrorResult(std::string_view("E-MEM-3001"),
                         expr ? std::optional<core::Span>(expr->span)
                              : std::optional<core::Span>{});
    }
  }

  BindStateBundle out = in;
  for (const auto& name : move_caps) {
    const auto info = Lookup_B(out.binds, name);
    if (!info.has_value() || info->mov == Movability::Immov) {
      continue;
    }
    BindInfo updated = *info;
    updated.state = BindState{BindStateKind::Moved, {}};
    (void)Update_B_inplace(out.binds, name, updated);
  }
  SPEC_RULE("B-Closure-Capturing");
  return OkResult(out);
}

static BindResult BindExpr(const ScopeContext& ctx,
                           const ast::ExprPtr& expr,
                           const BindStateBundle& in) {
  if (!expr) {
    return OkResult(in);
  }

  if (const auto* move = std::get_if<ast::MoveExpr>(&expr->node)) {
    return BindMoveExpr(ctx, *move, in);
  }

  if (const auto* attr = std::get_if<ast::AttributedExpr>(&expr->node)) {
    return BindExpr(ctx, attr->expr, in);
  }

  if (const auto* closure = std::get_if<ast::ClosureExpr>(&expr->node)) {
    return BindClosureExpr(ctx, expr, *closure, in);
  }

  if (IsPlaceExpr(expr)) {
    return BindPlaceExpr(ctx, expr, in);
  }

  if (const auto* call = std::get_if<ast::CallExpr>(&expr->node)) {
    return BindCallExpr(ctx, *call, in);
  }

  if (const auto* method = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    return BindMethodCallExpr(ctx, *method, in);
  }

  if (const auto* ifexpr = std::get_if<ast::IfExpr>(&expr->node)) {
    return BindIfExpr(ctx, *ifexpr, in);
  }

  if (const auto* if_is = std::get_if<ast::IfIsExpr>(&expr->node)) {
    return BindIfIsExpr(ctx, *if_is, in);
  }

  if (const auto* if_case = std::get_if<ast::IfCaseExpr>(&expr->node)) {
    return BindIfCaseExpr(ctx, *if_case, in);
  }

  if (const auto* block = std::get_if<ast::BlockExpr>(&expr->node)) {
    return BindBlock(ctx, *block->block, in);
  }

  if (const auto* unsafe_block =
          std::get_if<ast::UnsafeBlockExpr>(&expr->node)) {
    return BindBlock(ctx, *unsafe_block->block, in);
  }

  if (const auto* loop = std::get_if<ast::LoopInfiniteExpr>(&expr->node)) {
    return BindLoopInfinite(ctx, *loop, in);
  }

  if (const auto* loop =
          std::get_if<ast::LoopConditionalExpr>(&expr->node)) {
    return BindLoopConditional(ctx, *loop, in);
  }

  if (const auto* loop = std::get_if<ast::LoopIterExpr>(&expr->node)) {
    return BindLoopIter(ctx, *loop, in);
  }

  if (const auto* pipeline = std::get_if<ast::PipelineExpr>(&expr->node)) {
    auto lhs = BindExpr(ctx, pipeline->lhs, in);
    if (!lhs.ok) {
      return lhs;
    }
    if (!lhs.falls_through) {
      return lhs;
    }
    auto rhs = BindExpr(ctx, pipeline->rhs, lhs.state);
    if (!rhs.ok) {
      return rhs;
    }
    if (!rhs.falls_through) {
      return rhs;
    }
    SPEC_RULE("B-Pipeline");
    return OkResult(std::move(rhs.state));
  }

  BindStateBundle current = in;
  std::optional<BindResult> first_error;
  ForEachChildLtr(expr, [&](const ast::ExprPtr& child) {
    if (first_error.has_value()) {
      return;
    }
    auto res = BindExpr(ctx, child, current);
    if (!res.ok) {
      first_error = std::move(res);
      return;
    }
    if (!res.falls_through) {
      first_error = std::move(res);
      return;
    }
    current = std::move(res.state);
  });
  if (first_error.has_value()) {
    return std::move(*first_error);
  }
  SPEC_RULE("B-Expr-Sub");
  return OkResult(current);
}

static bool PlaceConstPerm(const ScopeContext& ctx,
                           const TypeEnv& env,
                           const ast::ExprPtr& place) {
  const auto type = PlaceTypeOf(ctx, env, place);
  if (!type.has_value()) {
    return false;
  }
  if (const auto* perm = std::get_if<TypePerm>(&(*type)->node)) {
    return perm->perm == Permission::Const;
  }
  return false;
}

static bool PlaceSharedWriteWithKey(const ScopeContext& ctx,
                                    const BindStateBundle& state,
                                    const ast::ExprPtr& place) {
  if (!state.keys_held ||
      !state.key_mode.has_value() ||
      *state.key_mode != ast::KeyMode::Write) {
    return false;
  }
  const auto type = PlaceTypeOf(ctx, state.env, place);
  if (!type.has_value()) {
    return false;
  }
  if (const auto* perm = std::get_if<TypePerm>(&(*type)->node)) {
    return perm->perm == Permission::Shared;
  }
  return false;
}

static BindResult BindStmt(const ScopeContext& ctx,
                           const ast::Stmt& stmt,
                           const BindStateBundle& in) {
  return std::visit(
      [&](const auto& node) -> BindResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          const auto& binding = node.binding;
          std::optional<std::string_view> diag_id;
          const auto bind_type = BindTypeForBinding(ctx, in.env, binding, diag_id);
          if (!bind_type.has_value()) {
            return ErrorResult(diag_id, binding.span);
          }

          if (PermOfType(*bind_type) == Permission::Unique &&
              IsPlaceExpr(binding.init) && !IsMoveExpr(binding.init)) {
            SPEC_RULE("B-LetVar-UniqueNonMove-Err");
            return ErrorResult(std::string_view("E-MEM-3007"), std::optional<core::Span>(binding.init->span));
          }

          auto init_res = BindExpr(ctx, binding.init, in);
          if (!init_res.ok) {
            return init_res;
          }

          BindStateBundle current = std::move(init_res.state);
          DowngradeUniqueBind_inplace(ctx, current.env, current.perms,
                                      binding.init, *bind_type);
          ConsumeOnMove_inplace(current.binds, binding.init);

          const auto pat = TypePattern(ctx, binding.pat, *bind_type);
          if (!pat.ok) {
            return ErrorResult(pat.diag_id, binding.pat->span);
          }
          const auto type_map = BindTypeMapFromBindings(pat.bindings);
          const Responsibility resp = RespOfInit(ctx, binding.init);
          const Movability mv = MovOf(binding.op);
          const auto mut = std::is_same_v<T, ast::LetStmt>
                               ? ast::Mutability::Let
                               : ast::Mutability::Var;

          current.binds = IntroAll_B(
              current.binds, BindInfoMap(type_map, resp, mv, mut));
          for (const auto& [name, type] : pat.bindings) {
            if (current.env.scopes.empty()) {
              current.env.scopes.emplace_back();
            }
            current.env.scopes.back()[IdKeyOf(name)] = TypeBinding{mut, type};
          }
          SPEC_RULE("B-LetVar");
          return OkResult(current);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          // UsingLocalStmt is a compile-time alias; it introduces no new
          // storage, no permissions, and no init expression. Borrow/bind
          // state is unchanged.
          (void)node;
          return OkResult(in);
        } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                             std::is_same_v<T, ast::CompoundAssignStmt>) {
          const auto place = node.place;
          if (IsPlaceExpr(place)) {
            if (PlaceConstPerm(ctx, in.env, place)) {
              return ErrorResult(std::string_view("E-TYP-1601"), std::optional<core::Span>(node.span));
            }
            const auto root = PlaceRoot(place);
            if (!PlaceWritesThroughDeref(place) && root.has_value()) {
              const auto info = Lookup_B(in.binds, *root);
              if (info.has_value() &&
                  info->mut == ast::Mutability::Let &&
                  !PlaceSharedWriteWithKey(ctx, in, place)) {
                SPEC_RULE("B-Assign-Immutable-Err");
                return ErrorResult(std::string_view("E-MOD-2401"), std::optional<core::Span>(node.span));
              }
            }
          }

          BindStateBundle current = in;
          if constexpr (std::is_same_v<T, ast::AssignStmt>) {
            // Spec order for assignment statement expression effects:
            // StmtExprs(AssignStmt(p, e)) = [e, p]
            auto val = BindExpr(ctx, node.value, current);
            if (!val.ok) {
              return val;
            }
            if (AllowMovedRootAssignLhs(val.state, node.place)) {
              current = std::move(val.state);
            } else {
              auto lhs = BindExpr(ctx, node.place, val.state);
              if (!lhs.ok) {
                return lhs;
              }
              current = std::move(lhs.state);
            }
          } else {
            auto lhs = BindExpr(ctx, node.place, current);
            if (!lhs.ok) {
              return lhs;
            }
            auto rhs = BindExpr(ctx, node.value, lhs.state);
            if (!rhs.ok) {
              return rhs;
            }
            current = std::move(rhs.state);
          }

          if (IsPlaceExpr(place)) {
            const auto root = PlaceRoot(place);
            if (root.has_value()) {
              const auto info = Lookup_B(current.binds, *root);
              if (info.has_value() && info->mut == ast::Mutability::Var) {
                BindInfo updated = *info;
                updated.state = BindState{BindStateKind::Valid, {}};
                (void)Update_B_inplace(current.binds, *root, updated);
              }
            }
          }
          SPEC_RULE("B-Assign");
          return OkResult(current);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          auto res = BindExpr(ctx, node.value, in);
          if (!res.ok) {
            return res;
          }
          SPEC_RULE("B-ExprStmt");
          return std::move(res);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
          if (!node.value_opt) {
            SPEC_RULE("B-Return-Unit");
            return OkResult(in, false);
          }
          auto res = BindExpr(
              ctx, ReturnDestExprForBind(ctx, in.binds, in.env, node.value_opt), in);
          if (!res.ok) {
            return res;
          }
          SPEC_RULE("B-Return");
          return OkResult(res.state, false);
        } else if constexpr (std::is_same_v<T, ast::BreakStmt>) {
          if (!node.value_opt) {
            SPEC_RULE("B-Break-Unit");
            return OkResult(in);
          }
          auto res = BindExpr(ctx, node.value_opt, in);
          if (!res.ok) {
            return res;
          }
          SPEC_RULE("B-Break");
          return std::move(res);
        } else if constexpr (std::is_same_v<T, ast::ContinueStmt>) {
          SPEC_RULE("B-Continue");
          return OkResult(in);
        } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
          auto res = BindBlock(ctx, *node.body, in);
          if (!res.ok) {
            return res;
          }
          SPEC_RULE("B-UnsafeStmt");
          return std::move(res);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
          auto res = BindBlock(ctx, *node.body, in);
          if (!res.ok) {
            return res;
          }
          if (!BindEnvEqual(res.state.binds, in.binds) ||
              !PermEnvEqual(res.state.perms, in.perms)) {
            return ErrorResult(std::nullopt, node.span);
          }
          SPEC_RULE("B-Defer");
          return OkResult(in);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          BindStateBundle current = in;
          if (node.opts_opt) {
            auto opts_res = BindExpr(ctx, node.opts_opt, current);
            if (!opts_res.ok) {
              return opts_res;
            }
            current = std::move(opts_res.state);
          }

          std::string name = node.alias_opt.has_value()
                                 ? *node.alias_opt
                                 : FreshRegionName(current.env);

          const auto region_type = MakeTypePerm(
              Permission::Unique,
              MakeTypeModalState({"Region"}, "Active"));
          std::map<IdKey, TypeRef> type_map;
          type_map.emplace(IdKeyOf(name), region_type);

          BindStateBundle scoped = current;
          scoped.binds.emplace_back();
          scoped.perms.emplace_back();
          scoped.env.scopes.emplace_back();

          scoped.binds = IntroAll_B(
              scoped.binds,
              BindInfoMap(type_map, Responsibility::Resp, Movability::Mov,
                          ast::Mutability::Let));
          scoped.env.scopes.back()[IdKeyOf(name)] =
              TypeBinding{ast::Mutability::Let, region_type};

          auto body = BindBlock(ctx, *node.body, scoped);
          if (!body.ok) {
            return body;
          }

          BindStateBundle out = std::move(body.state);
          if (!out.binds.empty()) {
            out.binds.pop_back();
          }
          if (!out.perms.empty()) {
            out.perms.pop_back();
          }
          if (!out.env.scopes.empty()) {
            out.env.scopes.pop_back();
          }
          SPEC_RULE("B-RegionStmt");
          return OkResult(out);
        } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
          BindStateBundle scoped = in;
          scoped.binds.emplace_back();
          scoped.perms.emplace_back();
          scoped.env.scopes.emplace_back();

          const std::string name = FreshRegionName(scoped.env);

          const auto region_type = MakeTypePerm(
              Permission::Unique,
              MakeTypeModalState({"Region"}, "Active"));
          std::map<IdKey, TypeRef> type_map;
          type_map.emplace(IdKeyOf(name), region_type);

          scoped.binds = IntroAll_B(
              scoped.binds,
              BindInfoMap(type_map, Responsibility::Resp, Movability::Mov,
                          ast::Mutability::Let));
          scoped.env.scopes.back()[IdKeyOf(name)] =
              TypeBinding{ast::Mutability::Let, region_type};

          auto body = BindBlock(ctx, *node.body, scoped);
          if (!body.ok) {
            return body;
          }

          BindStateBundle out = std::move(body.state);
          if (!out.binds.empty()) {
            out.binds.pop_back();
          }
          if (!out.perms.empty()) {
            out.perms.pop_back();
          }
          if (!out.env.scopes.empty()) {
            out.env.scopes.pop_back();
          }
          SPEC_RULE("B-FrameStmt");
          return OkResult(out);
        } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
          // UVX Extension: Key block statement
          // Key blocks introduce a new scope for permission tracking
          SPEC_RULE("B-KeyBlockStmt");

          BindStateBundle scoped = in;
          scoped.binds.emplace_back();
          scoped.perms.emplace_back();
          scoped.env.scopes.emplace_back();
          scoped.keys_held = true;
          scoped.key_mode = node.mode.value_or(ast::KeyMode::Read);

          // Process the body with the key scope
          auto body = BindBlock(ctx, *node.body, scoped);
          if (!body.ok) {
            return body;
          }

          BindStateBundle out = std::move(body.state);
          if (!out.binds.empty()) {
            out.binds.pop_back();
          }
          if (!out.perms.empty()) {
            out.perms.pop_back();
          }
          if (!out.env.scopes.empty()) {
            out.env.scopes.pop_back();
          }
          return OkResult(out);
        } else if constexpr (std::is_same_v<T, ast::ErrorStmt>) {
          return OkResult(in);
        } else {
          return OkResult(in);
        }
      },
      stmt);
}

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

  auto& perf = BorrowPerfStats();
  const bool perf_on = BorrowPerfActive();
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

static BindScope StaticBindInfo(const ScopeContext& ctx,
                                const ast::StaticDecl& decl,
                                TypeEnv& env) {
  BindScope out;
  const auto ann_type = ast::BindingAnnotationTypeOpt(decl.binding);
  if (!ann_type) {
    return out;
  }
  const auto ann = LocalLowerType(ctx, ann_type);
  if (!ann.ok) {
    return out;
  }
  const auto pat = TypePatternAgainstType(ctx, decl.binding.pat, ann.type);
  if (!pat.ok) {
    return out;
  }
  for (const auto& [name, type] : pat.bindings) {
    if (env.scopes.empty()) {
      env.scopes.emplace_back();
    }
    env.scopes.front()[IdKeyOf(name)] = TypeBinding{decl.mut, type};
  }
  const auto type_map = BindTypeMapFromBindings(pat.bindings);
  const auto resp = RespOfInit(ctx, decl.binding.init);
  const auto mv = MovOf(decl.binding.op);
  const auto info = BindInfoMap(type_map, resp, mv, decl.mut);
  out.insert(info.begin(), info.end());
  return out;
}

static BindScope StaticBindMap(const ScopeContext& ctx,
                               const ast::ModulePath& module_path,
                               TypeEnv& env) {
  auto& perf = BorrowPerfStats();
  const bool perf_on = BorrowPerfActive();
  ScopedBorrowTimer timer(perf_on ? &perf.static_bind_map_us : nullptr);
  BindScope out;
  const auto* module = FindModuleByPath(ctx, module_path);
  if (!module) {
    return out;
  }
  for (const auto& item : module->items) {
    if (perf_on) {
      ++perf.static_bind_items_scanned;
    }
    const auto* decl = std::get_if<ast::StaticDecl>(&item);
    if (!decl) {
      continue;
    }
    const auto info = StaticBindInfo(ctx, *decl, env);
    out.insert(info.begin(), info.end());
  }
  return out;
}

static BindScope ParamBindMap(const std::vector<ast::Param>& params,
                              const std::optional<BindSelfParam>& self_param) {
  BindScope out;
  if (self_param.has_value()) {
    BindInfo info;
    info.state = BindState{BindStateKind::Valid, {}};
    info.mov = self_param->mode.has_value() ? Movability::Mov : Movability::Immov;
    // §4.2: ~! (unique) receiver grants mutable access to self
    if (self_param->recv_perm.has_value() &&
        *self_param->recv_perm == Permission::Unique) {
      info.mut = ast::Mutability::Var;
    } else {
      info.mut = ast::Mutability::Let;
    }
    info.resp = self_param->mode.has_value() ? Responsibility::Resp
                                             : Responsibility::Alias;
    out.emplace(IdKeyOf("self"), info);
  }
  for (const auto& param : params) {
    BindInfo info;
    info.state = BindState{BindStateKind::Valid, {}};
    info.mov = param.mode.has_value() ? Movability::Mov : Movability::Immov;
    info.mut = ast::Mutability::Let;
    info.resp = param.mode.has_value() ? Responsibility::Resp
                                       : Responsibility::Alias;
    out.emplace(IdKeyOf(param.name), info);
  }
  return out;
}

static void ParamTypeMap(const ScopeContext& ctx,
                         const std::vector<ast::Param>& params,
                         const std::optional<BindSelfParam>& self_param,
                         TypeEnv& env) {
  TypeRef self_base;
  if (self_param.has_value()) {
    self_base = StripPerm(self_param->type);
    if (env.scopes.empty()) {
      env.scopes.emplace_back();
    }
    ast::Mutability self_mut = ast::Mutability::Let;
    if (self_param->recv_perm.has_value() &&
        *self_param->recv_perm == Permission::Unique) {
      self_mut = ast::Mutability::Var;
    }
    env.scopes.back()[IdKeyOf("self")] =
        TypeBinding{self_mut, self_param->type};
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

}  // namespace

void LogBorrowBindPerfSummary() {
  if (!BorrowPerfEnabled()) {
    return;
  }
  const auto& stats = BorrowPerfStats();
  if (stats.body_calls == 0) {
    return;
  }
  std::fprintf(stderr,
               "[uv] sema perf=borrow-bind body_calls=%llu "
               "bind_block_us=%llu static_bind_map_us=%llu "
               "find_module_calls=%llu find_module_scanned=%llu "
               "static_bind_items_scanned=%llu\n",
               static_cast<unsigned long long>(stats.body_calls),
               static_cast<unsigned long long>(stats.bind_block_us),
               static_cast<unsigned long long>(stats.static_bind_map_us),
               static_cast<unsigned long long>(stats.find_module_calls),
               static_cast<unsigned long long>(stats.find_module_scanned),
               static_cast<unsigned long long>(stats.static_bind_items_scanned));
  std::fflush(stderr);
}

BindCheckResult BindCheckBody(const ScopeContext& ctx,
                              const ast::ModulePath& module_path,
                              const std::vector<ast::Param>& params,
                              const std::shared_ptr<ast::Block>& body,
                              const std::optional<BindSelfParam>& self_param) {
  SpecDefsBorrowBind();
  SpecRuleTransitionAnchor();
  auto& perf = BorrowPerfStats();
  const bool perf_on = BorrowPerfActive();
  if (perf_on) {
    ++perf.body_calls;
  }
  BindCheckResult result;
  if (!body) {
    result.ok = true;
    return result;
  }

  BindEnv binds;
  PermEnv perms;
  TypeEnv env;

  env.scopes.emplace_back();
  const auto static_info = StaticBindMap(ctx, module_path, env);
  binds.emplace_back();
  binds = IntroAll_B(binds, static_info);

  perms.emplace_back();
  perms.emplace_back();

  env.scopes.emplace_back();
  const auto param_info = ParamBindMap(params, self_param);
  binds.emplace_back();
  binds = IntroAll_B(binds, param_info);
  ParamTypeMap(ctx, params, self_param, env);

  BindStateBundle state{binds, perms, env};
  const auto checked = [&]() {
    ScopedBorrowTimer timer(perf_on ? &perf.bind_block_us : nullptr);
    return BindBlock(ctx, *body, state);
  }();
  if (!checked.ok) {
    result.ok = false;
    result.diag_id = checked.diag_id;
    result.span = checked.span;
    return result;
  }

  result.ok = true;
  return result;
}

}  // namespace ultraviolet::analysis
