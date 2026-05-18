// =============================================================================
// File: 04_analysis/typing/expr/loop_iter.cpp
// Iterator Loop Expression Typing
// Spec Section: 5.2.11
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   Section 5.2.11: Loop Expressions
//   - T-Loop-Iter (lines 9710-9713): Iterator loop typing
//   - T-Loop-Iter-Async (lines 9715-9718): Async iterator loop
//   - Loop-Async-Err (lines 9720-9723): Async loop error
//   - Iterator class (line 23148)
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
#include <variant>

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "04_analysis/contracts/contract_check.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/generics/where_bounds.h"
#include "04_analysis/memory/calls.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsLoopIter() {
  SPEC_DEF("T-Loop-Iter", "5.2.11");
  SPEC_DEF("T-Loop-Iter-Async", "5.2.11");
  SPEC_DEF("Loop-Async-Err", "5.2.11");
  SPEC_DEF("LoopTypeFin", "5.2.11");
  SPEC_DEF("PatNames", "5.2.11");
  SPEC_DEF("Distinct", "5.2.11");
  SPEC_DEF("IntroAll", "5.2.11");
  SPEC_DEF("LoopInvariant", "5.2.11");
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

// Extract element type from iterable types (arrays, slices, ranges)
static std::optional<TypeRef> GetIterableElementType(const TypeRef& type) {
  const auto stripped = StripPermLocal(type);
  if (!stripped) {
    return std::nullopt;
  }

  // Array type
  if (const auto* arr = std::get_if<TypeArray>(&stripped->node)) {
    return arr->element;
  }

  // Slice type
  if (const auto* slice = std::get_if<TypeSlice>(&stripped->node)) {
    return slice->element;
  }

  if (const auto* range = std::get_if<TypeRange>(&stripped->node)) {
    return range->base;
  }
  if (const auto* range = std::get_if<TypeRangeInclusive>(&stripped->node)) {
    return range->base;
  }
  if (const auto* range = std::get_if<TypeRangeFrom>(&stripped->node)) {
    return range->base;
  }

  return std::nullopt;
}

static bool IsLoopRangeType(const TypeRef& type) {
  const auto stripped = StripPermLocal(type);
  if (!stripped) {
    return false;
  }

  return std::holds_alternative<TypeRange>(stripped->node) ||
         std::holds_alternative<TypeRangeInclusive>(stripped->node) ||
         std::holds_alternative<TypeRangeFrom>(stripped->node);
}

static bool RequiresLoopStepBound(const TypeRef& type) {
  return IsLoopRangeType(type);
}

static bool RequiresLoopEqBound(const TypeRef& type) {
  const auto stripped = StripPermLocal(type);
  if (!stripped) {
    return false;
  }

  return std::holds_alternative<TypeRange>(stripped->node) ||
         std::holds_alternative<TypeRangeInclusive>(stripped->node);
}

static bool CtIterableType(const TypeRef& type) {
  const auto stripped = StripPermLocal(type);
  if (!stripped) {
    return false;
  }
  return std::holds_alternative<TypeArray>(stripped->node) ||
         std::holds_alternative<TypeSlice>(stripped->node);
}

static std::optional<TypeRef> CtElemType(const TypeRef& type) {
  const auto stripped = StripPermLocal(type);
  if (!stripped) {
    return std::nullopt;
  }
  if (const auto* arr = std::get_if<TypeArray>(&stripped->node)) {
    return arr->element;
  }
  if (const auto* slice = std::get_if<TypeSlice>(&stripped->node)) {
    return slice->element;
  }
  return std::nullopt;
}

static bool IsUnitType(const TypeRef& type) {
  const auto stripped = StripPermLocal(type);
  if (!stripped) {
    return false;
  }
  const auto* prim = std::get_if<TypePrim>(&stripped->node);
  return prim && prim->name == "()";
}

static bool IsBoolType(const TypeRef& type) {
  const auto stripped = StripPermLocal(type);
  if (!stripped) {
    return false;
  }
  const auto* prim = std::get_if<TypePrim>(&stripped->node);
  return prim && prim->name == "bool";
}

// Compute loop result type from break types (LoopTypeFin)
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

// Introduce pattern bindings into environment
static TypeEnv IntroPatternBindings(const TypeEnv& env,
                                     const PatternTypeResult& pat_result) {
  if (!pat_result.ok || pat_result.bindings.empty()) {
    return env;
  }

  TypeEnv new_env = PushScope(env);
  for (const auto& [name, type] : pat_result.bindings) {
    TypeBinding binding;
    binding.mut = ast::Mutability::Let;
    binding.type = type;
    binding.storage_type = type;
    new_env.scopes.front()[name] = binding;
  }
  return new_env;
}

}  // namespace

ExprTypeResult TypeLoopIterExpr(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::LoopIterExpr& expr,
                                const TypeEnv& env,
                                const ExprTypeFn& type_expr,
                                const IdentTypeFn& type_ident,
                                const PlaceTypeFn& type_place) {
  SpecDefsLoopIter();
  ExprTypeResult result;

  if (!expr.iter || !expr.body || !expr.pattern) {
    return result;
  }

  // 1. Type the iterator expression
  const auto iter_type = TypeExpr(
      ctx, WithSharedAccessMode(type_ctx, ast::KeyMode::Read), expr.iter, env);
  if (!iter_type.ok) {
    result.diag_id = iter_type.diag_id;
    return result;
  }

  // 2. Check if iterator is an async type
  const auto iter_async_sig = AsyncSigOf(ctx, iter_type.type);
  if (iter_async_sig.has_value()) {
    // Appendix async loop surface is `loop pattern in expression block_expr`.
    // Reject iterator type annotations and loop invariants in async iteration.
    if (expr.type_opt || expr.invariant_opt.has_value()) {
      SPEC_RULE("Loop-Async-Err");
      result.diag_id = "E-CON-0240";
      return result;
    }

    // Async iteration is only valid in async procedures and only when
    // the iterable async input channel is unit.
    const auto proc_async_sig = AsyncSigOf(ctx, type_ctx.return_type);
    if (!proc_async_sig.has_value() || !IsUnitType(iter_async_sig->in)) {
      SPEC_RULE("Loop-Async-Err");
      result.diag_id = "E-CON-0240";  // Async loop outside async context
      return result;
    }

    const auto err_sub = Subtyping(ctx, iter_async_sig->err, proc_async_sig->err);
    if (!err_sub.ok) {
      result.diag_id = err_sub.diag_id;
      return result;
    }
    if (!err_sub.subtype) {
      result.diag_id = err_sub.diag_id.value_or("Loop-Async-Err");
      return result;
    }

    SPEC_RULE("T-Loop-Iter-Async");
  }

  // 3. Determine element type from iterator
  TypeRef element_type;
  if (iter_async_sig.has_value()) {
    element_type = iter_async_sig->out;
  } else {
    const auto elem = GetIterableElementType(iter_type.type);
    if (!elem.has_value()) {
      result.diag_id = "E-SEM-3133";
      return result;
    }
    element_type = *elem;

    if (RequiresLoopStepBound(iter_type.type) &&
        !CheckClassBound(ctx, element_type, TypePath{"Step"})) {
      result.diag_id = "E-SEM-3133";
      return result;
    }
    if (RequiresLoopEqBound(iter_type.type) &&
        !CheckClassBound(ctx, element_type, TypePath{"Eq"})) {
      result.diag_id = "E-SEM-3133";
      return result;
    }
  }

  // 4. If type annotation present, check compatibility
  if (expr.type_opt) {
    const auto annotation_type = LowerType(ctx, expr.type_opt);
    if (!annotation_type.ok) {
      result.diag_id = annotation_type.diag_id;
      return result;
    }
    const auto sub = Subtyping(ctx, element_type, annotation_type.type);
    if (!sub.ok) {
      result.diag_id = sub.diag_id;
      return result;
    }
    if (!sub.subtype) {
      result.diag_id = sub.diag_id.value_or("T-LetStmt-Ann-Mismatch");
      return result;
    }
    element_type = annotation_type.type;
  }

  // 5. Check pattern against element type
  const auto pat_result = TypePattern(ctx, expr.pattern, element_type);
  if (!pat_result.ok) {
    result.diag_id = pat_result.diag_id;
    return result;
  }

  // 6. Verify pattern names are distinct
  SPEC_RULE("Distinct");
  // Pattern names checked in TypePattern

  // 7. Extend environment with pattern bindings
  SPEC_RULE("IntroAll");
  const auto extended_env = IntroPatternBindings(env, pat_result);

  // 8. Create loop context for body typing
  StmtTypeContext loop_ctx = type_ctx;
  loop_ctx.loop_flag = LoopFlag::Loop;
  std::unordered_map<IdKey, ast::ExprPtr> loop_iteration_ranges;
  if (type_ctx.loop_iteration_ranges) {
    loop_iteration_ranges = *type_ctx.loop_iteration_ranges;
  }
  if (IsLoopRangeType(iter_type.type)) {
    for (const auto& [name, _] : pat_result.bindings) {
      loop_iteration_ranges[IdKeyOf(name)] = expr.iter;
    }
  }
  loop_ctx.loop_iteration_ranges = &loop_iteration_ranges;

  // Ensure nested expressions in the loop body type-check with loop context.
  ExprTypeFn loop_type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, loop_ctx, inner, extended_env);
  };
  PlaceTypeFn loop_type_place = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, loop_ctx, inner, extended_env);
  };
  IdentTypeFn loop_type_ident = type_ident;

  // 9. Type the body block
  const auto body_info = TypeBlockInfo(ctx, loop_ctx, *expr.body, extended_env,
                                       loop_type_expr, loop_type_ident, loop_type_place);
  if (!body_info.ok) {
    result.diag_id = body_info.diag_id;
    result.diag_detail = body_info.diag_detail;
    result.diag_span = body_info.diag_span;
    return result;
  }

  // 10. Check loop invariant if present
  if (expr.invariant_opt.has_value()) {
    SPEC_RULE("LoopInvariant");
    if (const auto inv_diag = ValidateLoopInvariantExpr(
            ctx, loop_ctx, extended_env, *expr.invariant_opt);
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

  // 11. Compute result type via LoopTypeFin
  SPEC_RULE("T-Loop-Iter");
  const auto loop_type = LoopTypeFin(body_info.breaks, body_info.break_void);
  if (!loop_type.has_value()) {
    result.diag_id = "T-Loop-Iter";
    return result;
  }
  result.ok = true;
  result.type = *loop_type;
  return result;
}

ExprTypeResult TypeCtLoopIterExpr(const ScopeContext& ctx,
                                  const StmtTypeContext& type_ctx,
                                  const ast::CtLoopIterExpr& expr,
                                  const TypeEnv& env,
                                  const ExprTypeFn& type_expr,
                                  const IdentTypeFn& type_ident,
                                  const PlaceTypeFn& type_place) {
  ExprTypeResult result;

  if (!expr.iter || !expr.body || !expr.pattern) {
    return result;
  }

  const auto iter_type = type_expr(expr.iter);
  if (!iter_type.ok) {
    result.diag_id = iter_type.diag_id;
    return result;
  }

  if (!CtIterableType(iter_type.type)) {
    result.diag_id = "E-SEM-3133";
    return result;
  }

  auto element_type = CtElemType(iter_type.type);
  if (!element_type.has_value()) {
    result.diag_id = "E-SEM-3133";
    return result;
  }

  if (expr.type_opt) {
    const auto annotation_type = LowerType(ctx, expr.type_opt);
    if (!annotation_type.ok) {
      result.diag_id = annotation_type.diag_id;
      return result;
    }
    const auto sub = Subtyping(ctx, *element_type, annotation_type.type);
    if (!sub.ok) {
      result.diag_id = sub.diag_id;
      return result;
    }
    if (!sub.subtype) {
      result.diag_id = sub.diag_id.value_or("T-LetStmt-Ann-Mismatch");
      return result;
    }
    element_type = annotation_type.type;
  }

  const auto pat_result = TypePattern(ctx, expr.pattern, *element_type);
  if (!pat_result.ok) {
    result.diag_id = pat_result.diag_id;
    return result;
  }

  const auto extended_env = IntroPatternBindings(env, pat_result);
  StmtTypeContext loop_ctx = type_ctx;
  loop_ctx.loop_flag = LoopFlag::Loop;

  ExprTypeFn loop_type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, loop_ctx, inner, extended_env);
  };
  PlaceTypeFn loop_type_place = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, loop_ctx, inner, extended_env);
  };
  IdentTypeFn loop_type_ident = type_ident;

  const auto body_result = TypeBlock(ctx, loop_ctx, *expr.body, extended_env,
                                     loop_type_expr, loop_type_ident,
                                     loop_type_place);
  if (!body_result.ok) {
    result.diag_id = body_result.diag_id;
    return result;
  }

  const auto unit_type = MakeTypePrim("()");
  const auto body_unit = TypeEquiv(body_result.type, unit_type);
  if (!body_unit.ok || !body_unit.equiv) {
    result.diag_id = body_unit.diag_id.value_or("T-CtLoopIter");
    return result;
  }

  SPEC_RULE("T-CtLoopIter");
  result.ok = true;
  result.type = unit_type;
  return result;
}

}  // namespace ultraviolet::analysis
