/*
 * =============================================================================
 * prov_expr.cpp - Expression Provenance Tracking
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - CursiveSpecification.md, Section 21.4.2 "Expression Provenance" (lines 25110-25180)
 *   - CursiveSpecification.md, Section 10.5 "Memory Provenance" (lines 22510-22600)
 *   - CursiveSpecification.md, Section 6.5 "Expression Evaluation" (lines 16200-16400)
 *
 * DIAGNOSTIC CODES:
 *   - E-PROV-0010: Provenance tracking lost
 *   - E-PROV-0011: Incompatible provenance merge
 *   - E-PROV-0012: Address of temporary
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
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
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

struct ProvExprResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  ProvTag prov;
};

static inline void SpecDefsExprProv() {
  SPEC_DEF("ProvExprJudg", "5.2.17");
  SPEC_DEF("JoinProv", "5.2.17");
  SPEC_DEF("JoinAllProv", "5.2.17");
  SPEC_DEF("AllocTag", "5.2.17");
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

static ProvTag HeapTag() {
  return ProvTag{ProvKind::Heap, 0, IdKey{}, 0};
}

static ProvTag RegionTag(const IdKey& name) {
  return ProvTag{ProvKind::Region, 0, name, 0};
}

static ProvTag ParamTag(std::size_t idx) {
  return ProvTag{ProvKind::Param, 0, IdKey{}, idx};
}

// =============================================================================
// Provenance Comparison
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

// =============================================================================
// Provenance Merge Operations
// =============================================================================

static ProvTag JoinProv(const ProvEnv& env, const ProvTag& lhs,
                        const ProvTag& rhs) {
  SpecDefsExprProv();
  if (ProvLeq(env, lhs, rhs)) {
    return lhs;
  }
  if (ProvLeq(env, rhs, lhs)) {
    return rhs;
  }
  SPEC_RULE("JoinProv-Bottom");
  return BottomTag();
}

static ProvTag JoinAllProv(const ProvEnv& env, const std::vector<ProvTag>& tags) {
  SpecDefsExprProv();
  if (tags.empty()) {
    return BottomTag();
  }
  ProvTag current = tags.front();
  for (std::size_t i = 1; i < tags.size(); ++i) {
    current = JoinProv(env, current, tags[i]);
  }
  SPEC_RULE("JoinAllProv");
  return current;
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

static ProvTag StackProv(const ProvEnv& env) {
  SpecDefsExprProv();
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
      return HeapTag();
    case BindingProvenanceSeedKind::Region:
      if (binding.provenance_region.has_value()) {
        return RegionTag(*binding.provenance_region);
      }
      return BottomTag();
    case BindingProvenanceSeedKind::Bottom:
      return BottomTag();
    case BindingProvenanceSeedKind::Param:
      return ParamTag(0);
  }
  return BottomTag();
}

static void SeedMinimalProvEnv(const TypeEnv& gamma, ProvEnv& env) {
  env.next_scope_id = 0;

  ProvScope scope;
  scope.id = env.next_scope_id++;
  for (const auto& type_scope : gamma.scopes) {
    for (const auto& [key, binding] : type_scope) {
      if (RegionActiveType(binding.type)) {
        const auto tag = binding.provenance_region.value_or(key);
        env.regions.push_back(RegionEntry{tag, key});
        scope.map[key] = RegionTag(tag);
        continue;
      }
      scope.map[key] = BindingSeedTag(env, binding);
    }
  }
  env.scopes.push_back(std::move(scope));
}

static std::optional<IdKey> AllocTag(const ProvEnv& env,
                                     const std::optional<std::string>& target) {
  SpecDefsExprProv();
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

static std::optional<TypeRef> ExprTypeForProvenance(const ScopeContext& ctx,
                                                    const TypeEnv& env,
                                                    const ast::ExprPtr& expr) {
  if (!expr) {
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

// =============================================================================
// Helper: Check if Type is Region@Active
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

static TypeRef StripPermDeep(const TypeRef& type) {
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

struct LocalTypeLowerResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
};

static LocalTypeLowerResult LocalLowerType(const ScopeContext& ctx,
                                           const ast::TypePtr& type) {
  LocalTypeLowerResult result;
  if (!type) {
    return result;
  }
  const auto lowered = LowerType(ctx, type);
  result.ok = lowered.ok;
  result.diag_id = lowered.diag_id;
  result.type = lowered.type;
  return result;
}

static bool IsPointerCarrierType(const TypeRef& type) {
  const auto stripped = StripPermDeep(type);
  if (!stripped) {
    return false;
  }
  return std::holds_alternative<TypePtr>(stripped->node) ||
         std::holds_alternative<TypeRawPtr>(stripped->node);
}

static bool IsRegionActiveType(const TypeRef& type) {
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

// =============================================================================
// Helper: Extract Child Expressions
// =============================================================================

static std::vector<ast::ExprPtr> ChildrenLtr(const ast::ExprPtr& expr) {
  std::vector<ast::ExprPtr> out;
  if (!expr) {
    return out;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          if (const auto* paren = std::get_if<ast::ParenArgs>(&node.args)) {
            for (const auto& arg : paren->args) {
              out.push_back(arg.value);
            }
          } else if (const auto* brace = std::get_if<ast::BraceArgs>(&node.args)) {
            for (const auto& field : brace->fields) {
              out.push_back(field.value);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          if (node.lhs) out.push_back(node.lhs);
          if (node.rhs) out.push_back(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          out.push_back(node.lhs);
          out.push_back(node.rhs);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          out.push_back(node.value);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          out.push_back(node.value);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          out.push_back(node.value);
        } else if constexpr (std::is_same_v<T, ast::AddressOfExpr>) {
          out.push_back(node.place);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          out.push_back(node.place);
        } else if constexpr (std::is_same_v<T, ast::AllocExpr>) {
          out.push_back(node.value);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            out.push_back(elem);
          }
    } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
      ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
        out.push_back(elem);
      });
    } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
      out.push_back(node.value);
      out.push_back(node.count);
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            out.push_back(field.value);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          if (node.payload_opt.has_value()) {
            if (const auto* tuple = std::get_if<ast::EnumPayloadParen>(&*node.payload_opt)) {
              for (const auto& elem : tuple->elements) {
                out.push_back(elem);
              }
            } else if (const auto* rec = std::get_if<ast::EnumPayloadBrace>(&*node.payload_opt)) {
              for (const auto& field : rec->fields) {
                out.push_back(field.value);
              }
            }
          }
        } else if constexpr (std::is_same_v<T, ast::TransmuteExpr>) {
          out.push_back(node.value);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          out.push_back(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          out.push_back(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          out.push_back(node.base);
          out.push_back(node.index);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          out.push_back(node.callee);
          for (const auto& arg : node.args) {
            out.push_back(arg.value);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          out.push_back(node.receiver);
          for (const auto& arg : node.args) {
            out.push_back(arg.value);
          }
        } else if constexpr (std::is_same_v<T, ast::PropagateExpr>) {
          out.push_back(node.value);
        }
      },
      expr->node);
  return out;
}

// =============================================================================
// Forward Declarations for Mutual Recursion
// =============================================================================

static ProvExprResult ProvPlace(const ScopeContext& ctx,
                                const ast::ExprPtr& place,
                                const ProvEnv& env,
                                const TypeEnv& gamma);

static ProvExprResult ProvExpr(const ScopeContext& ctx,
                               const ast::ExprPtr& expr,
                               const ProvEnv& env,
                               const TypeEnv& gamma);

// =============================================================================
// Place Provenance (for expressions that are places)
// =============================================================================

static ProvExprResult ProvPlace(const ScopeContext& ctx,
                                const ast::ExprPtr& place,
                                const ProvEnv& env,
                                const TypeEnv& gamma) {
  (void)ctx;
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
          SPEC_RULE("P-Ident");
          return result;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto base = ProvPlace(ctx, node.base, env, gamma);
          if (!base.ok) return base;
          SPEC_RULE("P-Field");
          return base;
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto base = ProvPlace(ctx, node.base, env, gamma);
          if (!base.ok) return base;
          SPEC_RULE("P-Tuple");
          return base;
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto base = ProvPlace(ctx, node.base, env, gamma);
          if (!base.ok) return base;
          SPEC_RULE("P-Index");
          return base;
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          auto inner = ProvExpr(ctx, node.value, env, gamma);
          if (!inner.ok) return inner;
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

// =============================================================================
// Expression Provenance Tracking
// =============================================================================

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

  // Literal expressions have no provenance (they're values, not pointers)
  if (const auto* literal = std::get_if<ast::LiteralExpr>(&expr->node)) {
    (void)literal;
    SPEC_RULE("P-Literal");
    result.ok = true;
    result.prov = BottomTag();
    return result;
  }

  // Move expression inherits provenance from the place
  if (const auto* move_expr = std::get_if<ast::MoveExpr>(&expr->node)) {
    auto inner = ProvPlace(ctx, move_expr->place, env, gamma);
    if (!inner.ok) return inner;
    SPEC_RULE("P-Move");
    return inner;
  }

  // Address-of expression - creates pointer with place's provenance
  if (const auto* addr = std::get_if<ast::AddressOfExpr>(&expr->node)) {
    auto inner = ProvPlace(ctx, addr->place, env, gamma);
    if (!inner.ok) return inner;
    SPEC_RULE("P-AddrOf");
    return inner;
  }

  // Allocation expression - creates pointer with region provenance
  if (const auto* alloc = std::get_if<ast::AllocExpr>(&expr->node)) {
    auto inner = ProvExpr(ctx, alloc->value, env, gamma);
    if (!inner.ok) return inner;
    const auto tag = AllocTag(env, alloc->region_opt);
    SPEC_RULE("P-Alloc");
    result.ok = true;
    if (tag.has_value()) {
      result.prov = RegionTag(*tag);
      return result;
    }
    result.prov = BottomTag();
    return result;
  }

  // Region alloc method call: receiver~>alloc(...)
  if (const auto* call = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    if (IdEq(call->name, "alloc")) {
      const auto recv_res = ProvExpr(ctx, call->receiver, env, gamma);
      if (!recv_res.ok) {
        return recv_res;
      }
      const auto type = ExprTypeForProvenance(ctx, gamma, call->receiver);
      if (type.has_value() && IsRegionActiveType(*type)) {
        for (const auto& arg : call->args) {
          const auto arg_res = ProvExpr(ctx, arg.value, env, gamma);
          if (!arg_res.ok) return arg_res;
        }
        SPEC_RULE("P-Region-Alloc-Method");
        result.ok = true;
        result.prov = recv_res.prov.kind == ProvKind::Region
                          ? recv_res.prov
                          : BottomTag();
        return result;
      }
    }
  }

  if (const auto* transmute = std::get_if<ast::TransmuteExpr>(&expr->node)) {
    const auto from = LocalLowerType(ctx, transmute->from);
    if (!from.ok) {
      result.ok = false;
      result.diag_id = from.diag_id;
      return result;
    }
    const auto to = LocalLowerType(ctx, transmute->to);
    if (!to.ok) {
      result.ok = false;
      result.diag_id = to.diag_id;
      return result;
    }
    if (!IsPointerCarrierType(from.type) || !IsPointerCarrierType(to.type)) {
      SPEC_RULE("P-Expr-Sub");
      result.ok = true;
      result.prov = BottomTag();
      return result;
    }
    auto inner = ProvExpr(ctx, transmute->value, env, gamma);
    if (!inner.ok) {
      return inner;
    }
    SPEC_RULE("P-Expr-Sub");
    return inner;
  }

  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&expr->node)) {
    auto inner = ProvExpr(ctx, attributed->expr, env, gamma);
    if (!inner.ok) {
      return inner;
    }
    SPEC_RULE("P-Expr-Sub");
    return inner;
  }

  if (const auto* unsafe_block = std::get_if<ast::UnsafeBlockExpr>(&expr->node)) {
    if (unsafe_block->block && unsafe_block->block->tail_opt &&
        unsafe_block->block->stmts.empty()) {
      auto inner = ProvExpr(ctx, unsafe_block->block->tail_opt, env, gamma);
      if (!inner.ok) {
        return inner;
      }
      SPEC_RULE("P-Expr-Sub");
      return inner;
    }
    SPEC_RULE("P-Expr-Sub");
    result.ok = true;
    result.prov = BottomTag();
    return result;
  }

  if (const auto* block_expr = std::get_if<ast::BlockExpr>(&expr->node)) {
    if (block_expr->block && block_expr->block->tail_opt &&
        block_expr->block->stmts.empty()) {
      auto inner = ProvExpr(ctx, block_expr->block->tail_opt, env, gamma);
      if (!inner.ok) {
        return inner;
      }
      SPEC_RULE("P-Expr-Sub");
      return inner;
    }
    SPEC_RULE("P-Expr-Sub");
    result.ok = true;
    result.prov = BottomTag();
    return result;
  }

  // If expression - merge provenance from branches
  if (const auto* if_expr = std::get_if<ast::IfExpr>(&expr->node)) {
    const auto cond = ProvExpr(ctx, if_expr->cond, env, gamma);
    if (!cond.ok) return cond;
    const auto then_res = ProvExpr(ctx, if_expr->then_expr, env, gamma);
    if (!then_res.ok) return then_res;
    if (if_expr->else_expr) {
      const auto else_res = ProvExpr(ctx, if_expr->else_expr, env, gamma);
      if (!else_res.ok) return else_res;
      SPEC_RULE("P-If");
      result.ok = true;
      result.prov = JoinProv(env, then_res.prov, else_res.prov);
      return result;
    }
    SPEC_RULE("P-If-No-Else");
    result.ok = true;
    result.prov = BottomTag();
    return result;
  }

  // Check if expression is a place expression
  if (IsPlaceExpr(expr)) {
    const auto place = ProvPlace(ctx, expr, env, gamma);
    if (!place.ok) return place;
    SPEC_RULE("P-Place-Expr");
    return place;
  }

  // Default: merge provenance from all children
  const auto children = ChildrenLtr(expr);
  std::vector<ProvTag> child_provs;
  child_provs.reserve(children.size());
  for (const auto& child : children) {
    const auto child_res = ProvExpr(ctx, child, env, gamma);
    if (!child_res.ok) return child_res;
    child_provs.push_back(child_res.prov);
  }
  SPEC_RULE("P-Expr-Sub");
  result.ok = true;
  result.prov = JoinAllProv(env, child_provs);
  return result;
}

}  // namespace

// =============================================================================
// Public API: Expression Provenance Tracking
// =============================================================================

ProvExprTrackResult TrackExprProvenance(const ScopeContext& ctx,
                                         const ast::ExprPtr& expr,
                                         const TypeEnv& gamma) {
  SpecDefsExprProv();

  ProvEnv env;
  SeedMinimalProvEnv(gamma, env);

  const auto result = ProvExpr(ctx, expr, env, gamma);

  ProvExprTrackResult out;
  out.ok = result.ok;
  out.diag_id = result.diag_id;
  out.span = result.span;

  // Convert internal kind to public enum
  switch (result.prov.kind) {
    case ProvKind::Global: out.kind = ProvenanceKind::Global; break;
    case ProvKind::Stack: out.kind = ProvenanceKind::Stack; break;
    case ProvKind::Heap: out.kind = ProvenanceKind::Heap; break;
    case ProvKind::Region: out.kind = ProvenanceKind::Region; break;
    case ProvKind::Bottom: out.kind = ProvenanceKind::Bottom; break;
    case ProvKind::Param: out.kind = ProvenanceKind::Param; break;
  }
  if (result.prov.kind == ProvKind::Region) {
    out.region = result.prov.region;
    if (const auto target = ResolveRegionTarget(env, result.prov)) {
      out.region_target = *target;
    }
  }

  return out;
}

ProvenanceKind MergeProvenance(ProvenanceKind a, ProvenanceKind b) {
  SpecDefsExprProv();

  // Same provenance - keep it
  if (a == b) {
    SPEC_RULE("MergeProv-Same");
    return a;
  }

  // Bottom absorbs everything
  if (a == ProvenanceKind::Bottom) {
    SPEC_RULE("MergeProv-BottomL");
    return b;
  }
  if (b == ProvenanceKind::Bottom) {
    SPEC_RULE("MergeProv-BottomR");
    return a;
  }

  // Param provenance is incompatible with others
  if (a == ProvenanceKind::Param || b == ProvenanceKind::Param) {
    SPEC_RULE("MergeProv-Param-Incompatible");
    return ProvenanceKind::Bottom;
  }

  // Create ordering: Region < Stack < Heap < Global
  auto rank = [](ProvenanceKind k) -> int {
    switch (k) {
      case ProvenanceKind::Region: return 0;
      case ProvenanceKind::Stack: return 1;
      case ProvenanceKind::Heap: return 2;
      case ProvenanceKind::Global: return 3;
      default: return -1;
    }
  };

  // Return the "lower" provenance (shorter lifetime)
  SPEC_RULE("MergeProv-Order");
  return rank(a) < rank(b) ? a : b;
}

ProvenanceKind AddressOfProvenance(const ScopeContext& ctx,
                                    const ast::ExprPtr& place,
                                    const TypeEnv& gamma) {
  SpecDefsExprProv();
  SPEC_RULE("P-AddrOf");

  // The provenance of &place is the provenance of the place
  return ComputePlaceProvenance(ctx, place, gamma);
}

ProvenanceKind DerefProvenance(const ScopeContext& ctx,
                                const ast::ExprPtr& ptr,
                                const TypeEnv& gamma) {
  SpecDefsExprProv();
  SPEC_RULE("P-Deref");

  // The provenance of *ptr is the provenance of ptr
  const auto result = TrackExprProvenance(ctx, ptr, gamma);
  if (!result.ok) {
    return ProvenanceKind::Bottom;
  }
  return result.kind;
}

ProvenanceKind AllocProvenance(const std::optional<std::string>& region_name,
                                const TypeEnv& gamma) {
  SpecDefsExprProv();

  // If region specified, check if it's active
  if (region_name.has_value()) {
    const auto binding = BindOf(gamma, *region_name);
    if (binding.has_value() && RegionActiveType(binding->type)) {
      SPEC_RULE("P-Alloc-Named-Region");
      return ProvenanceKind::Region;
    }
    // Named region not found or not active
    SPEC_RULE("P-Alloc-Invalid-Region");
    return ProvenanceKind::Bottom;
  }

  // No region specified - check for innermost active region
  const auto innermost = InnermostActiveRegion(gamma);
  if (innermost.has_value()) {
    SPEC_RULE("P-Alloc-Innermost-Region");
    return ProvenanceKind::Region;
  }

  // No active region - allocation fails
  SPEC_RULE("P-Alloc-No-Region");
  return ProvenanceKind::Bottom;
}

}  // namespace cursive::analysis
