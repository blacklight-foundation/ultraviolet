// =============================================================================
// Expression Statement Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Lines 16650-16653 (Lower-Stmt-Expr)
//   - LowerExpr(expr) produces IR_e
//   - Result value discarded (side effects only)
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 436-456: LowerExprStmt function
//
// =============================================================================

#include "05_codegen/lower/stmt/expr_stmt.h"

#include <iterator>
#include <variant>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/modal/modal_transitions.h"
#include "04_analysis/typing/type_predicates.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

namespace {

struct ModalTransitionUpdate {
  std::string binding_name;
  analysis::TypeRef target_type;
  const ast::Expr* receiver_place = nullptr;
};

const ast::Expr* UnwrapExprForMethodCall(const ast::Expr* expr) {
  const ast::Expr* current = expr;
  while (current) {
    if (const auto* attr = std::get_if<ast::AttributedExpr>(&current->node)) {
      current = attr->expr.get();
      continue;
    }
    break;
  }
  return current;
}

const ast::MethodCallExpr* AsMethodCallExpr(const ast::Expr& expr) {
  const ast::Expr* unwrapped = UnwrapExprForMethodCall(&expr);
  if (!unwrapped) {
    return nullptr;
  }
  return std::get_if<ast::MethodCallExpr>(&unwrapped->node);
}

const ast::Expr* UnwrapReceiverExpr(const ast::ExprPtr& expr) {
  const ast::Expr* current = expr.get();
  while (current) {
    if (const auto* attr = std::get_if<ast::AttributedExpr>(&current->node)) {
      current = attr->expr.get();
      continue;
    }
    if (const auto* move_expr = std::get_if<ast::MoveExpr>(&current->node)) {
      current = move_expr->place.get();
      continue;
    }
    break;
  }
  return current;
}

std::optional<std::string> ReceiverRootName(const ast::ExprPtr& receiver) {
  const ast::Expr* unwrapped = UnwrapReceiverExpr(receiver);
  if (!unwrapped) {
    return std::nullopt;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&unwrapped->node)) {
    return ident->name;
  }
  return std::nullopt;
}

analysis::TypeRef ReceiverTypeForTransition(const ast::MethodCallExpr& call,
                                            const LowerCtx& ctx) {
  if (const auto root = ReceiverRootName(call.receiver)) {
    if (const BindingState* state = ctx.GetBindingState(*root)) {
      if (state->type) {
        return state->type;
      }
    }
  }
  if (ctx.expr_type) {
    return ctx.expr_type(*call.receiver);
  }
  return nullptr;
}

analysis::TypeRef ReapplyTransitionQualifiers(const analysis::TypeRef& original,
                                             const analysis::TypeRef& transitioned) {
  if (!original) {
    return transitioned;
  }
  if (const auto* perm = std::get_if<analysis::TypePerm>(&original->node)) {
    return analysis::MakeTypePerm(
        perm->perm,
        ReapplyTransitionQualifiers(perm->base, transitioned));
  }
  if (const auto* refine = std::get_if<analysis::TypeRefine>(&original->node)) {
    return analysis::MakeTypeRefine(
        ReapplyTransitionQualifiers(refine->base, transitioned),
        refine->predicate);
  }
  return transitioned;
}

std::optional<ModalTransitionUpdate> DetectModalTransitionUpdate(
    const ast::Expr& expr,
    const LowerCtx& ctx) {
  if (!ctx.sigma) {
    return std::nullopt;
  }
  const auto* call = AsMethodCallExpr(expr);
  if (!call) {
    return std::nullopt;
  }
  const auto root = ReceiverRootName(call->receiver);
  if (!root.has_value()) {
    return std::nullopt;
  }

  const analysis::TypeRef recv_type = ReceiverTypeForTransition(*call, ctx);
  const analysis::TypeRef stripped = analysis::StripPerm(recv_type);
  if (!stripped) {
    return std::nullopt;
  }

  const auto* modal = std::get_if<analysis::TypeModalState>(&stripped->node);
  if (!modal) {
    return std::nullopt;
  }

  const analysis::ScopeContext& scope = ScopeForLowering(ctx);

  const ast::ModalDecl* modal_decl = analysis::LookupModalDecl(scope, modal->path);
  if (!modal_decl) {
    return std::nullopt;
  }

  const ast::TransitionDecl* transition =
      analysis::LookupTransitionDecl(*modal_decl, modal->state, call->name);
  if (!transition) {
    return std::nullopt;
  }

  ModalTransitionUpdate update;
  update.binding_name = *root;
  update.target_type = ReapplyTransitionQualifiers(
      recv_type,
      analysis::MakeTypeModalState(
          modal->path, transition->target_state, modal->generic_args));
  const ast::Expr* receiver_place = UnwrapReceiverExpr(call->receiver);
  if (receiver_place && IsPlaceExpr(*receiver_place)) {
    update.receiver_place = receiver_place;
  }
  return update;
}

bool IsRegionModalState(const analysis::TypeRef& type,
                        std::string_view state_name) {
  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (!stripped) {
    return false;
  }
  const auto* modal = std::get_if<analysis::TypeModalState>(&stripped->node);
  if (!modal || modal->state != state_name) {
    return false;
  }
  return modal->path.size() == 1 && modal->path.front() == "Region";
}

void RemoveActiveRegionAlias(LowerCtx& ctx, const std::string& name) {
  for (auto it = ctx.active_region_aliases.rbegin();
       it != ctx.active_region_aliases.rend();
       ++it) {
    if (*it != name) {
      continue;
    }
    ctx.active_region_aliases.erase(std::next(it).base());
    return;
  }
}

void SyncActiveRegionAliasForTransition(const ModalTransitionUpdate& update,
                                        LowerCtx& ctx) {
  if (IsRegionModalState(update.target_type, "Active")) {
    for (const auto& active : ctx.active_region_aliases) {
      if (active == update.binding_name) {
        return;
      }
    }
    ctx.active_region_aliases.push_back(update.binding_name);
    return;
  }

  RemoveActiveRegionAlias(ctx, update.binding_name);
}

void ApplyModalTransitionUpdate(const ModalTransitionUpdate& update, LowerCtx& ctx) {
  auto it = ctx.binding_states.find(update.binding_name);
  if (it != ctx.binding_states.end() && !it->second.empty()) {
    BindingState& state = it->second.back();
    state.type = update.target_type;
    state.is_moved = false;
    state.moved_fields.clear();
  }

  IRValue local;
  local.kind = IRValue::Kind::Local;
  local.name = update.binding_name;
  ctx.RegisterValueType(local, update.target_type);
  SyncActiveRegionAliasForTransition(update, ctx);
}

// Check if a dispatch expression has a reduce option
bool DispatchHasReduceLocal(const ast::DispatchExpr& expr) {
  for (const auto& opt : expr.opts) {
    if (opt.kind == ast::DispatchOptionKind::Reduce) {
      return true;
    }
  }
  return false;
}

// Check if an expression is a collectable parallel expression (spawn or dispatch with reduce)
bool IsCollectableParallelExprLocal(const ast::Expr& expr, bool& needs_wait) {
  if (std::holds_alternative<ast::SpawnExpr>(expr.node)) {
    needs_wait = true;
    return true;
  }
  if (const auto* dispatch = std::get_if<ast::DispatchExpr>(&expr.node)) {
    if (DispatchHasReduceLocal(*dispatch)) {
      needs_wait = false;
      return true;
    }
  }
  return false;
}

analysis::TypeRef InferParallelCollectedType(const ast::Expr& expr,
                                             LowerCtx& ctx,
                                             bool needs_wait) {
  if (!ctx.expr_type) {
    return nullptr;
  }
  analysis::TypeRef expr_type = ctx.expr_type(expr);
  analysis::TypeRef stripped = analysis::StripPerm(expr_type);
  if (!stripped) {
    stripped = expr_type;
  }
  if (!stripped) {
    return nullptr;
  }
  if (!needs_wait) {
    return stripped;
  }
  if (const auto inner = analysis::ExtractSpawnedInner(stripped)) {
    return *inner;
  }
  return nullptr;
}

}  // namespace

// ============================================================================
// Lower-Stmt-Expr
// ============================================================================
//
// Per the spec (Lines 16650-16653):
//   LowerExpr(expr) => <IR_e, v>
//   Result: IR_e (value discarded)
//
// The implementation handles:
//   - Special parallel_collect handling for spawn/dispatch expressions
//   - Value is evaluated for side effects only
//
IRPtr LowerExprStmt(const ast::ExprStmt& stmt, LowerCtx& ctx) {
  SPEC_RULE("Lower-Stmt-Expr");

  if (!stmt.value) {
    return EmptyIR();
  }

  // Handle parallel collection for spawn and dispatch expressions
  if (ctx.parallel_collect && ctx.parallel_collect_depth == 1) {
    bool needs_wait = false;
    if (IsCollectableParallelExprLocal(*stmt.value, needs_wait)) {
      // Suppress temp registration for the collected expression
      auto prev_suppress = ctx.suppress_temp_at_depth;
      ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
      auto expr_result = LowerExpr(*stmt.value, ctx);
      ctx.suppress_temp_at_depth = prev_suppress;

      // Register this expression for parallel collection
      ParallelCollectItem item;
      item.value = expr_result.value;
      item.needs_wait = needs_wait;
      item.value_type = InferParallelCollectedType(*stmt.value, ctx, needs_wait);
      ctx.parallel_collect->push_back(std::move(item));
      return expr_result.ir;
    }
  }

  const auto modal_transition_update =
      DetectModalTransitionUpdate(*stmt.value, ctx);

  // Standard path: lower the expression, discard the value
  auto expr_result = LowerExpr(*stmt.value, ctx);

  if (modal_transition_update.has_value()) {
    IRPtr write_back = EmptyIR();
    if (modal_transition_update->receiver_place) {
      write_back =
          LowerWritePlace(*modal_transition_update->receiver_place,
                          expr_result.value,
                          ctx);
    }
    ApplyModalTransitionUpdate(*modal_transition_update, ctx);
    return SeqIR({expr_result.ir, write_back});
  }

  // The expression is evaluated for side effects
  // The result value is discarded
  return expr_result.ir;
}

}  // namespace ultraviolet::codegen
