// =============================================================================
// File: 04_analysis/typing/expr/loop_infinite.cpp
// Infinite Loop Expression Typing
// Spec Section: 5.2.11
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 5.2.11: Loop Expressions
//   - T-Loop-Infinite (lines 9700-9703): Infinite loop typing
//   - loop_expression grammar (line 23432)
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <variant>

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "04_analysis/contracts/contract_check.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/memory/calls.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsLoopInfinite() {
  SPEC_DEF("T-Loop-Infinite", "5.2.11");
  SPEC_DEF("LoopTypeInf", "5.2.11");
  SPEC_DEF("NeverType", "5.2.11");
  SPEC_DEF("LoopInvariant", "5.2.11");
}

static bool IsBoolType(const TypeRef& type) {
  if (!type) {
    return false;
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
  if (!cur) {
    return false;
  }
  const auto* prim = std::get_if<TypePrim>(&cur->node);
  return prim && prim->name == "bool";
}

// Compute loop result type from break types (LoopTypeInf).
static std::optional<TypeRef> LoopTypeInf(const std::vector<TypeRef>& breaks,
                                          bool break_void) {
  if (breaks.empty() && !break_void) {
    return MakeTypePrim("!");
  }

  if (breaks.empty() && break_void) {
    return MakeTypePrim("()");
  }

  if (break_void) {
    return std::nullopt;
  }

  TypeRef base = breaks.front();
  if (!base) {
    return std::nullopt;
  }
  for (std::size_t i = 1; i < breaks.size(); ++i) {
    if (!breaks[i]) {
      return std::nullopt;
    }
    const auto eq = TypeEquiv(base, breaks[i]);
    if (!eq.ok || !eq.equiv) {
      return std::nullopt;
    }
  }

  return base;
}

static std::optional<std::string_view> ValidateLoopInvariantExpr(
    const ScopeContext& ctx,
    const StmtTypeContext& type_ctx,
    const TypeEnv& env,
    const ast::LoopInvariant& invariant) {
  ContractContext contract_ctx;
  contract_ctx.scope_ctx = &ctx;
  const auto invariant_check = CheckLoopInvariant(contract_ctx, invariant);
  if (!invariant_check.ok) {
    return invariant_check.diag_id;
  }

  const auto inv_type = TypeExpr(ctx, type_ctx, invariant.predicate, env);
  if (!inv_type.ok) {
    return inv_type.diag_id;
  }
  if (!IsBoolType(inv_type.type)) {
    return std::optional<std::string_view>("E-SEM-2851");
  }

  if (!type_ctx.contract_dynamic) {
    StaticProofContext proof_ctx;
    if (type_ctx.proof_ctx) {
      proof_ctx = *type_ctx.proof_ctx;
    }
    const auto proof = StaticProof(proof_ctx, invariant.predicate);
    if (!proof.provable) {
      return std::optional<std::string_view>("E-SEM-2830");
    }
  }

  return std::nullopt;
}

static void CollectInvariantNames(const ast::ExprPtr& expr,
                                  std::unordered_set<IdKey>& out) {
  if (!expr) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          out.insert(IdKeyOf(node.name));
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          if (node.path.empty()) {
            out.insert(IdKeyOf(node.name));
          }
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          CollectInvariantNames(node.lhs, out);
          CollectInvariantNames(node.rhs, out);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          CollectInvariantNames(node.value, out);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          CollectInvariantNames(node.base, out);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          CollectInvariantNames(node.base, out);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          CollectInvariantNames(node.base, out);
          CollectInvariantNames(node.index, out);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          CollectInvariantNames(node.callee, out);
          for (const auto& arg : node.args) {
            CollectInvariantNames(arg.value, out);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          CollectInvariantNames(node.receiver, out);
          for (const auto& arg : node.args) {
            CollectInvariantNames(arg.value, out);
          }
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          CollectInvariantNames(node.value, out);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          CollectInvariantNames(node.lhs, out);
          CollectInvariantNames(node.rhs, out);
        } else if constexpr (std::is_same_v<T, ast::EntryExpr>) {
          CollectInvariantNames(node.expr, out);
        }
      },
      expr->node);
}

static bool PlaceMutatesInvariantName(const ast::ExprPtr& place,
                                      const std::unordered_set<IdKey>& names) {
  if (!place || names.empty()) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return names.find(IdKeyOf(node.name)) != names.end();
        } else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          return node.path.empty() &&
                 names.find(IdKeyOf(node.name)) != names.end();
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return PlaceMutatesInvariantName(node.base, names);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return PlaceMutatesInvariantName(node.base, names);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return PlaceMutatesInvariantName(node.base, names);
        } else if constexpr (std::is_same_v<T, ast::MoveExpr>) {
          return PlaceMutatesInvariantName(node.place, names);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return PlaceMutatesInvariantName(node.value, names);
        } else {
          return false;
        }
      },
      place->node);
}

static bool BlockMutatesInvariantName(const std::shared_ptr<ast::Block>& block,
                                      const std::unordered_set<IdKey>& names) {
  if (!block || names.empty()) {
    return false;
  }
  for (const auto& stmt : block->stmts) {
    const bool mutated = std::visit(
        [&](const auto& node) -> bool {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::AssignStmt>) {
            return PlaceMutatesInvariantName(node.place, names);
          } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
            return PlaceMutatesInvariantName(node.place, names);
          } else if constexpr (std::is_same_v<T, ast::DeferStmt>) {
            return BlockMutatesInvariantName(node.body, names);
          } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
            return BlockMutatesInvariantName(node.body, names);
          } else if constexpr (std::is_same_v<T, ast::FrameStmt>) {
            return BlockMutatesInvariantName(node.body, names);
          } else if constexpr (std::is_same_v<T, ast::UnsafeBlockStmt>) {
            return BlockMutatesInvariantName(node.body, names);
          } else if constexpr (std::is_same_v<T, ast::KeyBlockStmt>) {
            return BlockMutatesInvariantName(node.body, names);
          } else {
            return false;
          }
        },
        stmt);
    if (mutated) {
      return true;
    }
  }
  return false;
}

static bool ViolatesLoopInvariantMaintenance(
    const ast::LoopInvariant& invariant,
    const std::shared_ptr<ast::Block>& body) {
  std::unordered_set<IdKey> names;
  CollectInvariantNames(invariant.predicate, names);
  return BlockMutatesInvariantName(body, names);
}

}  // namespace

ExprTypeResult TypeLoopInfiniteExpr(const ScopeContext& ctx,
                                    const StmtTypeContext& type_ctx,
                                    const ast::LoopInfiniteExpr& expr,
                                    const TypeEnv& env,
                                    const ExprTypeFn& type_expr,
                                    const IdentTypeFn& type_ident,
                                    const PlaceTypeFn& type_place) {
  SpecDefsLoopInfinite();
  ExprTypeResult result;

  if (!expr.body) {
    return result;
  }

  // 1. Create loop context for body typing
  StmtTypeContext loop_ctx = type_ctx;
  loop_ctx.loop_flag = LoopFlag::Loop;

  // Ensure nested expressions in the loop body type-check with loop context.
  ExprTypeFn loop_type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, loop_ctx, inner, env);
  };
  PlaceTypeFn loop_type_place = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, loop_ctx, inner, env);
  };
  IdentTypeFn loop_type_ident = type_ident;

  // 2. Type the body block
  const auto body_info = TypeBlockInfo(ctx, loop_ctx, *expr.body, env,
                                       loop_type_expr, loop_type_ident, loop_type_place);
  if (!body_info.ok) {
    result.diag_id = body_info.diag_id;
    result.diag_detail = body_info.diag_detail;
    result.diag_span = body_info.diag_span;
    return result;
  }

  // 3. Check loop invariant if present
  if (expr.invariant_opt.has_value()) {
    SPEC_RULE("LoopInvariant");
    if (const auto inv_diag = ValidateLoopInvariantExpr(
            ctx, loop_ctx, env, *expr.invariant_opt);
        inv_diag.has_value()) {
      result.diag_id = *inv_diag;
      return result;
    }
    if (!loop_ctx.contract_dynamic &&
        ViolatesLoopInvariantMaintenance(*expr.invariant_opt, expr.body)) {
      result.diag_id = "E-SEM-2831";
      return result;
    }
  }

  // 4. Compute result type via LoopTypeInf
  SPEC_RULE("T-Loop-Infinite");
  const auto loop_type = LoopTypeInf(body_info.breaks, body_info.break_void);
  if (!loop_type.has_value()) {
    result.diag_id = "T-Loop-Infinite";
    return result;
  }
  result.ok = true;
  result.type = *loop_type;
  return result;
}

}  // namespace ultraviolet::analysis
