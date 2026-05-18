// =============================================================================
// File: 04_analysis/typing/expr/loop_conditional.cpp
// Conditional Loop Expression Typing
// Spec Section: 5.2.11
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 5.2.11: Loop Expressions
//   - T-Loop-Conditional (lines 9705-9708): Conditional loop typing
//   - loop_expression grammar (line 23432)
//   - Loop invariant (line 23427)
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
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

static inline void SpecDefsLoopConditional() {
  SPEC_DEF("T-Loop-Conditional", "5.2.11");
  SPEC_DEF("LoopTypeFin", "5.2.11");
  SPEC_DEF("Loop-Cond-NotBool", "5.2.11");
  SPEC_DEF("LoopInvariant", "5.2.11");
}

// Check if type is bool
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
  if (const auto* prim = std::get_if<TypePrim>(&cur->node)) {
    return prim->name == "bool";
  }
  return false;
}

// Compute loop result type from break types (LoopTypeFin).
static std::optional<TypeRef> LoopTypeFin(const std::vector<TypeRef>& breaks,
                                          bool break_void) {
  if (breaks.empty()) {
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

  const auto inv_type = TypeExpr(
      ctx, WithSharedAccessMode(type_ctx, ast::KeyMode::Read),
      invariant.predicate, env);
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

static ast::ExprPtr MakeLoopProofExpr(const core::Span& span,
                                      ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

static bool ExprMentionsAnyName(const ast::ExprPtr& expr,
                                const std::unordered_set<IdKey>& names) {
  if (!expr || names.empty()) {
    return false;
  }
  std::unordered_set<IdKey> expr_names;
  CollectInvariantNames(expr, expr_names);
  for (const IdKey& name : names) {
    if (expr_names.find(name) != expr_names.end()) {
      return true;
    }
  }
  return false;
}

static std::optional<IdKey> AssignmentRootName(const ast::ExprPtr& place) {
  if (!place) {
    return std::nullopt;
  }
  return std::visit(
      [&](const auto& node) -> std::optional<IdKey> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return IdKeyOf(node.name);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return AssignmentRootName(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return AssignmentRootName(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return AssignmentRootName(node.base);
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return AssignmentRootName(node.expr);
        } else {
          return std::nullopt;
        }
      },
      place->node);
}

static bool IsPlainIdentifierPlace(const ast::ExprPtr& place,
                                   const IdKey& name) {
  if (!place) {
    return false;
  }
  const auto* ident = std::get_if<ast::IdentifierExpr>(&place->node);
  return ident && IdKeyOf(ident->name) == name;
}

static ast::ExprPtr SubstituteIdentifierInProofExpr(
    const ast::ExprPtr& expr,
    const IdKey& name,
    const ast::ExprPtr& replacement,
    bool& blocked) {
  if (!expr || blocked) {
    return expr;
  }
  return std::visit(
      [&](const auto& node) -> ast::ExprPtr {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return IdKeyOf(node.name) == name ? replacement : expr;
        } else if constexpr (std::is_same_v<T, ast::LiteralExpr> ||
                             std::is_same_v<T, ast::PtrNullExpr>) {
          return expr;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          auto out = node;
          out.lhs = SubstituteIdentifierInProofExpr(
              node.lhs, name, replacement, blocked);
          out.rhs = SubstituteIdentifierInProofExpr(
              node.rhs, name, replacement, blocked);
          return MakeLoopProofExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          auto out = node;
          out.value = SubstituteIdentifierInProofExpr(
              node.value, name, replacement, blocked);
          return MakeLoopProofExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          auto out = node;
          out.base = SubstituteIdentifierInProofExpr(
              node.base, name, replacement, blocked);
          return MakeLoopProofExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          auto out = node;
          out.base = SubstituteIdentifierInProofExpr(
              node.base, name, replacement, blocked);
          return MakeLoopProofExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          auto out = node;
          out.base = SubstituteIdentifierInProofExpr(
              node.base, name, replacement, blocked);
          out.index = SubstituteIdentifierInProofExpr(
              node.index, name, replacement, blocked);
          return MakeLoopProofExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
          auto out = node;
          out.value = SubstituteIdentifierInProofExpr(
              node.value, name, replacement, blocked);
          return MakeLoopProofExpr(expr->span, std::move(out));
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          auto out = node;
          out.expr = SubstituteIdentifierInProofExpr(
              node.expr, name, replacement, blocked);
          return MakeLoopProofExpr(expr->span, std::move(out));
        } else {
          std::unordered_set<IdKey> names;
          names.insert(name);
          if (ExprMentionsAnyName(expr, names)) {
            blocked = true;
          }
          return expr;
        }
      },
      expr->node);
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

static bool LoopInvariantMaintainedByBody(
    const StmtTypeContext& type_ctx,
    const ast::LoopInvariant& invariant,
    const ast::ExprPtr& condition,
    const std::shared_ptr<ast::Block>& body) {
  std::unordered_set<IdKey> invariant_names;
  CollectInvariantNames(invariant.predicate, invariant_names);
  if (!BlockMutatesInvariantName(body, invariant_names)) {
    return true;
  }

  ast::ExprPtr proof_obligation = invariant.predicate;
  if (!body) {
    return true;
  }

  for (auto stmt_it = body->stmts.rbegin(); stmt_it != body->stmts.rend();
       ++stmt_it) {
    bool unsupported = false;
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::AssignStmt>) {
            const auto root = AssignmentRootName(node.place);
            if (!root.has_value() ||
                invariant_names.find(*root) == invariant_names.end()) {
              return;
            }
            if (!IsPlainIdentifierPlace(node.place, *root)) {
              unsupported = true;
              return;
            }
            bool blocked = false;
            proof_obligation = SubstituteIdentifierInProofExpr(
                proof_obligation, *root, node.value, blocked);
            unsupported = blocked;
          } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
            if (PlaceMutatesInvariantName(node.place, invariant_names)) {
              unsupported = true;
            }
          } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                               std::is_same_v<T, ast::RegionStmt> ||
                               std::is_same_v<T, ast::FrameStmt> ||
                               std::is_same_v<T, ast::UnsafeBlockStmt> ||
                               std::is_same_v<T, ast::KeyBlockStmt>) {
            if (BlockMutatesInvariantName(node.body, invariant_names)) {
              unsupported = true;
            }
          }
        },
        *stmt_it);
    if (unsupported) {
      return false;
    }
  }

  StaticProofContext proof_ctx;
  if (type_ctx.proof_ctx) {
    proof_ctx = *type_ctx.proof_ctx;
  }
  AddPredicateFactsAt(proof_ctx, invariant.predicate, invariant.span);
  AddPredicateFactsAt(proof_ctx, condition,
                      condition ? condition->span : invariant.span);
  const auto proof =
      StaticProofAt(proof_ctx, invariant.span, proof_obligation);
  return proof.provable;
}

}  // namespace

ExprTypeResult TypeLoopConditionalExpr(const ScopeContext& ctx,
                                       const StmtTypeContext& type_ctx,
                                       const ast::LoopConditionalExpr& expr,
                                       const TypeEnv& env,
                                       const ExprTypeFn& type_expr,
                                       const IdentTypeFn& type_ident,
                                       const PlaceTypeFn& type_place) {
  SpecDefsLoopConditional();
  ExprTypeResult result;

  if (!expr.cond || !expr.body) {
    return result;
  }

  // 1. Type the condition expression
  const auto cond_type = TypeExpr(
      ctx, WithSharedAccessMode(type_ctx, ast::KeyMode::Read), expr.cond, env);
  if (!cond_type.ok) {
    result.diag_id = cond_type.diag_id;
    result.diag_detail = cond_type.diag_detail;
    result.diag_span = cond_type.diag_span.has_value()
                           ? cond_type.diag_span
                           : std::optional<core::Span>(expr.cond->span);
    return result;
  }

  // 2. Verify condition is bool
  if (!IsBoolType(cond_type.type)) {
    SPEC_RULE("Loop-Cond-NotBool");
    result.diag_id = "Loop-Cond-NotBool";
    return result;
  }

  // 3. Create loop context for body typing
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

  // 4. Type the body block
  const auto body_info = TypeBlockInfo(ctx, loop_ctx, *expr.body, env,
                                       loop_type_expr, loop_type_ident, loop_type_place);
  if (!body_info.ok) {
    result.diag_id = body_info.diag_id;
    result.diag_detail = body_info.diag_detail;
    result.diag_span = body_info.diag_span;
    return result;
  }

  // 5. Check loop invariant if present
  if (expr.invariant_opt.has_value()) {
    SPEC_RULE("LoopInvariant");
    if (const auto inv_diag = ValidateLoopInvariantExpr(
            ctx, loop_ctx, env, *expr.invariant_opt);
        inv_diag.has_value()) {
      result.diag_id = *inv_diag;
      return result;
    }
    if (!loop_ctx.contract_dynamic &&
        !LoopInvariantMaintainedByBody(
            loop_ctx, *expr.invariant_opt, expr.cond, expr.body)) {
      result.diag_id = "E-SEM-2831";
      return result;
    }
  }

  // 6. Compute result type via LoopTypeFin
  SPEC_RULE("T-Loop-Conditional");
  const auto loop_type = LoopTypeFin(body_info.breaks, body_info.break_void);
  if (!loop_type.has_value()) {
    result.diag_id = "T-Loop-Conditional";
    return result;
  }
  result.ok = true;
  result.type = *loop_type;
  return result;
}

}  // namespace ultraviolet::analysis
