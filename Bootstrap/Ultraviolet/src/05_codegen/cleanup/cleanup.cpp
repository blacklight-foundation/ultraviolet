// =============================================================================
// MIGRATION MAPPING: cleanup.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.4 Expression Lowering - Drop and Cleanup (lines 15831-15992)
//   - ExecIR-Drop rule (lines 15837-15840)
//   - ExecIR-DropFields rule (lines 15842-15845)
//   - ExecIR-DropGlue rule (lines 15847-15850)
//   - Cleanup-* rules for control flow (lines 15880-15992)
//   - CleanupPlan computation (lines 15880-15900)
//   - EmitCleanup judgment (lines 15901-15920)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/cleanup.cpp
//   - Lines 1-200: CleanupPlan struct and computation
//   - Lines 201-400: EmitCleanup implementation
//   - Lines 401-600: EmitDrop for various types
//   - Lines 601-800: EmitDropFields for records
//   - Lines 801-1000: DropGlueSym generation
//   - Lines 1001-1200: DropOnAssign handling
//   - Lines 1201-1400: Binding validity tracking
//   - Lines 1401-1599: Control flow cleanup integration
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/cleanup/cleanup.h
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRDrop, IRDropFields)
//   - ultraviolet/include/04_analysis/layout/layout.h (SizeOf, AlignOf)
//   - ultraviolet/include/04_analysis/types/types.h (TypeRef, DropType predicate)
//   - ultraviolet/include/05_codegen/symbols/mangle.h (DropGlueSym)
//
// REFACTORING NOTES:
//   1. CleanupPlan tracks which bindings need cleanup at scope exit
//   2. Cleanup is deterministic and follows reverse declaration order
//   3. Drop::drop method called for types with Drop
//   4. Bitcopy types have no cleanup required
//   5. Records drop fields in reverse declaration order
//   6. Enums/unions drop active variant only
//   7. Modal types drop current state's fields
//   8. Arrays drop elements in reverse index order
//   9. Control flow (return, break, continue) triggers cleanup
//   10. Panic paths also execute cleanup (unwind)
//
// CLEANUP COMPUTATION:
//   CleanupPlan = {
//     bindings: Vec<BindingCleanup>,  // In reverse declaration order
//     scope_depth: u32
//   }
//
//   BindingCleanup = {
//     name: string,
//     type: TypeRef,
//     state: BindingState,  // Alive, Moved, PartiallyMoved, Poisoned
//     needs_drop: bool
//   }
//
// DROP EXECUTION ORDER:
//   1. Check binding state (skip if Moved)
//   2. If PartiallyMoved, drop only non-moved fields
//   3. Call Drop::drop if type has Drop
//   4. Recursively drop fields/elements
//   5. Deallocate storage (if heap)
//
// EMITCLEANUP JUDGMENT:
//   EmitCleanup(plan, label) |- IRSeq
//   - Generates IR sequence for cleanup at label
//   - Handles both normal and panic paths
// =============================================================================

#include "05_codegen/cleanup/cleanup.h"

#include "05_codegen/checks/checks.h"
#include "05_codegen/intrinsics/intrinsics_interface.h"
#include "05_codegen/cleanup/drop_hooks.h"
#include "05_codegen/cleanup/unwind.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/common/runtime_trace_utils.h"
#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/intrinsics/intrinsics_interface.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/symbols/mangle.h"
#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "00_core/symbols.h"
#include "01_project/language_profile.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_expr.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string_view>
#include <unordered_set>

namespace ultraviolet::codegen {

// BoolImmediate is defined in lower/expr/binary.cpp (canonical location)
IRValue BoolImmediate(bool value);

// ============================================================================
// Helper functions
// ============================================================================

// Helper to check if a TypeRef holds a specific type
template <typename T>
static bool HoldsType(const analysis::TypeRef& type) {
  return type && std::holds_alternative<T>(type->node);
}

template <typename T>
static const T& GetType(const analysis::TypeRef& type) {
  return std::get<T>(type->node);
}

struct StaticBindFlags {
  bool has_responsibility = false;
  bool immovable = false;
};

static bool IsMoveExprLite(const ast::ExprPtr& expr) {
  return expr && std::holds_alternative<ast::MoveExpr>(expr->node);
}

static bool IsPlaceExprLite(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return IsPlaceExprLite(node.value);
        }
        return false;
      },
      expr->node);
}

struct CleanupTraceIds {
  const char* ok;
  const char* panic;
  const char* abort;
};

static std::optional<CleanupTraceIds> CleanupTraceIdsFor(const CleanupAction& action) {
  switch (action.kind) {
    case CleanupAction::Kind::RunDefer:
      return CleanupTraceIds{
          "Cleanup-Step-Defer-Ok",
          "Cleanup-Step-Defer-Panic",
          "Cleanup-Step-Defer-Abort",
      };
    case CleanupAction::Kind::DropVar:
    case CleanupAction::Kind::DropStatic:
    case CleanupAction::Kind::DropTemp:
    case CleanupAction::Kind::DropField:
    case CleanupAction::Kind::ReleaseRegion:
    case CleanupAction::Kind::ReleaseKeyScope:
    case CleanupAction::Kind::ReacquireReleasedKey:
    case CleanupAction::Kind::ParallelJoin:
    case CleanupAction::Kind::RuntimeScopeExit:
      return CleanupTraceIds{
          "Cleanup-Step-Drop-Ok",
          "Cleanup-Step-Drop-Panic",
          "Cleanup-Step-Drop-Abort",
      };
  }
  return std::nullopt;
}

static const char* CleanupActionEnterTraceIdFor(const CleanupAction& action) {
  switch (action.kind) {
    case CleanupAction::Kind::RunDefer:
      return "Cleanup-Action-Defer-Enter";
    case CleanupAction::Kind::DropVar:
      return "Cleanup-Action-DropVar-Enter";
    case CleanupAction::Kind::DropStatic:
      return "Cleanup-Action-DropStatic-Enter";
    case CleanupAction::Kind::DropTemp:
      return "Cleanup-Action-DropTemp-Enter";
    case CleanupAction::Kind::DropField:
      return "Cleanup-Action-DropField-Enter";
    case CleanupAction::Kind::ReleaseRegion:
      return "Cleanup-Action-ReleaseRegion-Enter";
    case CleanupAction::Kind::ReleaseKeyScope:
      return "Cleanup-Action-ReleaseKeyScope-Enter";
    case CleanupAction::Kind::ReacquireReleasedKey:
      return "Cleanup-Action-ReacquireReleasedKey-Enter";
    case CleanupAction::Kind::ParallelJoin:
      return "Cleanup-Action-ParallelJoin-Enter";
    case CleanupAction::Kind::RuntimeScopeExit:
      return "Cleanup-Action-ScopeExit-Enter";
  }
  return "Cleanup-Action-Unknown-Enter";
}

static bool CleanupActionCanPanic(const CleanupAction& action) {
  switch (action.kind) {
    case CleanupAction::Kind::DropVar:
    case CleanupAction::Kind::DropStatic:
    case CleanupAction::Kind::DropTemp:
    case CleanupAction::Kind::DropField:
    case CleanupAction::Kind::RunDefer:
      return true;
    case CleanupAction::Kind::ReleaseRegion:
    case CleanupAction::Kind::ReleaseKeyScope:
    case CleanupAction::Kind::ReacquireReleasedKey:
      return false;
    case CleanupAction::Kind::ParallelJoin:
      return true;
    case CleanupAction::Kind::RuntimeScopeExit:
      // These lower to runtime no-panic helpers and do not carry a hidden
      // panic out-parameter. Treating them as panic-capable bloats cleanup CFG
      // and forces unnecessary panic-state save/restore around guaranteed-safe
      // runtime operations.
      return false;
  }
  return true;
}

static std::optional<StaticBindFlags> StaticBindFlagsFor(
    const ast::ModulePath& module_path,
    const std::string& name,
    LowerCtx& ctx) {
  if (!ctx.sigma) {
    return std::nullopt;
  }

  const ast::ASTModule* module = nullptr;
  for (const auto& mod : ctx.sigma->mods) {
    if (analysis::PathEq(mod.path, module_path)) {
      module = &mod;
      break;
    }
  }
  if (!module) {
    return std::nullopt;
  }

  for (const auto& item : module->items) {
    const auto* decl = std::get_if<ast::StaticDecl>(&item);
    if (!decl) {
      continue;
    }

    // Check if this static contains our binding
    // StaticDecl has a Binding which has a pattern (pat field)
    // For simple bindings, check the pattern
    if (!decl->binding.pat) {
      continue;
    }
    if (const auto* ident_pat =
            std::get_if<ast::IdentifierPattern>(&decl->binding.pat->node)) {
      if (ident_pat->name != name) {
        continue;
      }
    } else {
      continue;
    }

    bool has_resp = true;
    if (decl->binding.init && IsPlaceExprLite(decl->binding.init) &&
        !IsMoveExprLite(decl->binding.init)) {
      has_resp = false;
    }
    // Check if binding operator is := (immovable) by checking token lexeme
    bool immovable = (decl->binding.op.lexeme == ":=") || !has_resp;
    return StaticBindFlags{has_resp, immovable};
  }

  return std::nullopt;
}

static std::optional<analysis::TypeRef> LowerTypeForDrop(
    const std::shared_ptr<ast::Type>& type,
    LowerCtx& ctx) {
  if (!type || !ctx.sigma) {
    return std::nullopt;
  }
  analysis::ScopeContext scope;
  scope.sigma = *ctx.sigma;
  scope.sigma_source = ctx.sigma;
  scope.current_module = ctx.module_path;
  return ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, type);
}

static ast::Path ToSyntaxPath(const analysis::TypePath& path) {
  ast::Path out;
  out.reserve(path.size());
  for (const auto& seg : path) {
    out.push_back(seg);
  }
  return out;
}

static const ast::StateBlock* FindModalState(
    const ast::ModalDecl& decl,
    std::string_view name) {
  for (const auto& state : decl.states) {
    if (analysis::IdEq(state.name, name)) {
      return &state;
    }
  }
  return nullptr;
}

static std::optional<std::vector<std::pair<std::string, analysis::TypeRef>>>
CollectModalFields(const ast::StateBlock& state, LowerCtx& ctx) {
  std::vector<std::pair<std::string, analysis::TypeRef>> fields;
  for (const auto& member : state.members) {
    const auto* field = std::get_if<ast::StateFieldDecl>(&member);
    if (!field) {
      continue;
    }
    const auto lowered = LowerTypeForDrop(field->type, ctx);
    if (!lowered.has_value()) {
      return std::nullopt;
    }
    fields.emplace_back(field->name, *lowered);
  }
  return fields;
}

// ============================================================================
// Section 6.8 CleanupPlan - Compute cleanup actions for a scope
// ============================================================================

static void AppendCleanupItemToPlan(const CleanupItem& item,
                                    LowerCtx& ctx,
                                    CleanupPlan& plan) {
  switch (item.kind) {
    case CleanupItem::Kind::DropBinding: {
      const BindingState* state = ctx.GetBindingState(item.name);
      if (!state) {
        return;
      }
      if (!state->has_responsibility) {
        return;
      }

      BindValidity validity = GetBindValidity(item.name, ctx);
      if (validity == BindValidity::Moved) {
        return;
      }

      CleanupAction action;
      action.kind = CleanupAction::Kind::DropVar;
      action.name = item.name;
      action.type = state->type;

      if (validity == BindValidity::PartiallyMoved) {
        action.skip_fields = GetMovedFields(item.name, ctx);
      }

      plan.push_back(std::move(action));
      return;
    }
    case CleanupItem::Kind::DeferBlock: {
      CleanupAction action;
      action.kind = CleanupAction::Kind::RunDefer;
      action.defer_ir = item.defer_ir;
      plan.push_back(std::move(action));
      return;
    }
    case CleanupItem::Kind::ReleaseRegion: {
      CleanupAction action;
      action.kind = CleanupAction::Kind::ReleaseRegion;
      action.name = item.name;
      plan.push_back(std::move(action));
      return;
    }
    case CleanupItem::Kind::ReleaseKeyScope: {
      CleanupAction action;
      action.kind = CleanupAction::Kind::ReleaseKeyScope;
      action.name = item.name;
      plan.push_back(std::move(action));
      return;
    }
    case CleanupItem::Kind::ReacquireReleasedKey: {
      CleanupAction action;
      action.kind = CleanupAction::Kind::ReacquireReleasedKey;
      action.name = item.name;
      plan.push_back(std::move(action));
      return;
    }
    case CleanupItem::Kind::ParallelJoin: {
      CleanupAction action;
      action.kind = CleanupAction::Kind::ParallelJoin;
      action.name = item.name;
      IRValue parallel_ctx;
      parallel_ctx.kind = IRValue::Kind::Opaque;
      parallel_ctx.name = item.name;
      action.value = parallel_ctx;
      plan.push_back(std::move(action));
      return;
    }
    case CleanupItem::Kind::RuntimeScopeExit: {
      CleanupAction action;
      action.kind = CleanupAction::Kind::RuntimeScopeExit;
      action.scope_runtime_id = item.scope_runtime_id;
      plan.push_back(std::move(action));
      return;
    }
  }
}

static void AppendScopeCleanupItems(const std::vector<CleanupItem>& items,
                                    LowerCtx& ctx,
                                    CleanupPlan& plan) {
  // Key scopes must be released before any binding drops in the same scope.
  for (auto it = items.rbegin(); it != items.rend(); ++it) {
    if (it->kind != CleanupItem::Kind::ReleaseKeyScope) {
      continue;
    }
    AppendCleanupItemToPlan(*it, ctx, plan);
  }
  for (auto it = items.rbegin(); it != items.rend(); ++it) {
    if (it->kind != CleanupItem::Kind::ReacquireReleasedKey) {
      continue;
    }
    AppendCleanupItemToPlan(*it, ctx, plan);
  }
  for (auto it = items.rbegin(); it != items.rend(); ++it) {
    if (it->kind == CleanupItem::Kind::ReleaseKeyScope ||
        it->kind == CleanupItem::Kind::ReacquireReleasedKey) {
      continue;
    }
    AppendCleanupItemToPlan(*it, ctx, plan);
  }
}

static CleanupPlan ComputeCleanupPlanForScopes(LowerCtx& ctx, bool stop_at_loop) {
  CleanupPlan plan;
  LowerCtx tmp = ctx;

  while (!tmp.scope_stack.empty()) {
    const bool is_loop = tmp.scope_stack.back().is_loop;
    const auto items = tmp.scope_stack.back().cleanup_items;
    AppendScopeCleanupItems(items, tmp, plan);
    tmp.PopScope();
    if (stop_at_loop && is_loop) {
      break;
    }
  }

  return plan;
}

CleanupPlan ComputeCleanupPlan(const std::vector<CleanupItem>& cleanup_items,
                               LowerCtx& ctx) {
  SPEC_RULE("CleanupPlan");

  CleanupPlan plan;
  for (const auto& item : cleanup_items) {
    AppendCleanupItemToPlan(item, ctx, plan);
  }

  return plan;
}

CleanupPlan ComputeCleanupPlanForCurrentScope(LowerCtx& ctx) {
  if (ctx.scope_stack.empty()) {
    return {};
  }
  CleanupPlan plan;
  AppendScopeCleanupItems(ctx.scope_stack.back().cleanup_items, ctx, plan);
  return plan;
}

CleanupPlan ComputeCleanupPlanToLoopScope(LowerCtx& ctx) {
  return ComputeCleanupPlanForScopes(ctx, true);
}

CleanupPlan ComputeCleanupPlanToFunctionRoot(LowerCtx& ctx) {
  return ComputeCleanupPlanForScopes(ctx, false);
}

CleanupPlan ComputeCleanupPlanRemainder(CleanupTarget target, LowerCtx& ctx) {
  LowerCtx tmp = ctx;
  switch (target) {
    case CleanupTarget::CurrentScope: {
      if (!tmp.scope_stack.empty()) {
        tmp.PopScope();
      }
      return ComputeCleanupPlanToFunctionRoot(tmp);
    }
    case CleanupTarget::ToLoopScope: {
      while (!tmp.scope_stack.empty()) {
        const bool is_loop = tmp.scope_stack.back().is_loop;
        tmp.PopScope();
        if (is_loop) {
          break;
        }
      }
      return ComputeCleanupPlanToFunctionRoot(tmp);
    }
    case CleanupTarget::ToFunctionRoot:
    default:
      return {};
  }
}

// ============================================================================
// A6.8 EmitCleanup - Emit IR for a cleanup plan
// ============================================================================

static IRPtr EmitCleanupAction(const CleanupAction& action, LowerCtx& ctx) {
  switch (action.kind) {
    case CleanupAction::Kind::DropVar: {
      IRValue var_value;
      var_value.kind = IRValue::Kind::Local;
      var_value.name = action.name;

      if (action.skip_fields.empty()) {
        return EmitDrop(action.type, var_value, ctx);
      }
      return EmitDropFields(action.type, var_value, action.skip_fields, ctx);
    }
    case CleanupAction::Kind::DropStatic: {
      if (!TypeNeedsDrop(action.type, ctx)) {
        return EmptyIR();
      }

      IRReadPath read;
      read.path = action.static_module_path;
      read.name = action.name;

      IRValue loaded_value;
      loaded_value.kind = IRValue::Kind::Symbol;
      loaded_value.name = action.name;
      ctx.RegisterValueType(loaded_value, action.type);

      IRPtr drop_ir = EmitDropInline(action.type, loaded_value, ctx);
      return SeqIR({MakeIR(std::move(read)), drop_ir});
    }
    case CleanupAction::Kind::DropTemp: {
      if (action.value) {
        return EmitDrop(action.type, *action.value, ctx);
      }
      return EmptyIR();
    }
    case CleanupAction::Kind::DropField: {
      if (action.value) {
        return EmitDrop(action.type, *action.value, ctx);
      }
      return EmptyIR();
    }
    case CleanupAction::Kind::ReleaseRegion: {
      IRValue region_value;
      if (action.value) {
        region_value = *action.value;
      } else {
        region_value.kind = IRValue::Kind::Local;
        region_value.name = action.name;
      }

      IRCall call;
      call.callee.kind = IRValue::Kind::Symbol;
      call.callee.name = BuiltinModalSymRegionFreeUnchecked();
      call.args.push_back(region_value);
      return MakeIR(std::move(call));
    }
    case CleanupAction::Kind::ReleaseKeyScope: {
      IRValue scope_value;
      if (action.value) {
        scope_value = *action.value;
      } else {
        scope_value.kind = IRValue::Kind::Local;
        scope_value.name = action.name;
      }

      IRCall call;
      call.callee.kind = IRValue::Kind::Symbol;
      call.callee.name = ConcurrencySymKeyScopeExit();
      call.args.push_back(scope_value);
      call.result = ctx.FreshTempValue("key_scope_exit");
      ctx.RegisterValueType(call.result, analysis::MakeTypePrim("()"));
      return MakeIR(std::move(call));
    }
    case CleanupAction::Kind::ReacquireReleasedKey: {
      IRValue released_handle;
      if (action.value) {
        released_handle = *action.value;
      } else {
        released_handle.kind = IRValue::Kind::Local;
        released_handle.name = action.name;
      }

      IRCall call;
      call.callee.kind = IRValue::Kind::Symbol;
      call.callee.name = ConcurrencySymKeyReacquireOne();
      call.args.push_back(released_handle);
      call.result = ctx.FreshTempValue("key_reacquire_one");
      ctx.RegisterValueType(call.result, analysis::MakeTypePrim("()"));
      return MakeIR(std::move(call));
    }
    case CleanupAction::Kind::ParallelJoin: {
      IRValue parallel_ctx;
      if (action.value) {
        parallel_ctx = *action.value;
      } else {
        parallel_ctx.kind = IRValue::Kind::Local;
        parallel_ctx.name = action.name;
      }

      IRCall call;
      call.callee.kind = IRValue::Kind::Symbol;
      call.callee.name = ConcurrencySymParallelJoin();
      call.args.push_back(parallel_ctx);
      call.result = ctx.FreshTempValue("parallel_join_status");
      ctx.RegisterValueType(call.result, analysis::MakeTypePrim("i32"));

      IRBinaryOp panic_cmp;
      panic_cmp.op = "!=";
      panic_cmp.lhs = call.result;
      panic_cmp.rhs = U32Immediate(0);
      panic_cmp.result = ctx.FreshTempValue("parallel_join_panicked");
      ctx.RegisterValueType(panic_cmp.result, analysis::MakeTypePrim("bool"));

      IRPtr write_panic = WritePanicRecord(
          BuildPanicAccess(ctx),
          BoolImmediate(true),
          call.result);

      IRIf if_panic;
      if_panic.cond = panic_cmp.result;
      if_panic.then_ir = write_panic;
      if_panic.then_value = UnitValue();
      if_panic.else_ir = EmptyIR();
      if_panic.else_value = UnitValue();
      if_panic.result = ctx.FreshTempValue("parallel_join_panic_if");

      return SeqIR({MakeIR(std::move(call)),
                    MakeIR(std::move(panic_cmp)),
                    MakeIR(std::move(if_panic))});
    }
    case CleanupAction::Kind::RunDefer: {
      if (action.defer_ir) {
        return *action.defer_ir;
      }
      return EmptyIR();
    }
    case CleanupAction::Kind::RuntimeScopeExit: {
      IRCall call;
      call.callee.kind = IRValue::Kind::Symbol;
      call.callee.name = BuiltinModalSymRegionScopeExit();
      call.args.push_back(USizeConstValue(action.scope_runtime_id));
      IRValue result = ctx.FreshTempValue("scope_exit");
      call.result = result;
      ctx.RegisterValueType(result, analysis::MakeTypePrim("()"));
      return MakeIR(std::move(call));
    }
  }
  return EmptyIR();
}

static IRPtr EmitCleanupImpl(const CleanupPlan& plan,
                             const CleanupPlan& remainder,
                             LowerCtx& ctx,
                             bool start_panicking,
                             bool emit_panic_check) {
  SPEC_RULE("EmitCleanup");

  if (plan.empty()) {
    return EmitRuntimeTrace("Cleanup-Empty", ctx);
  }

  const PanicAccess access = BuildPanicAccess(ctx);

  IRValue panicking = BoolImmediate(start_panicking);
  IRValue panic_code = U32Immediate(0);

  std::vector<IRPtr> parts;

  if (start_panicking) {
    auto snapshot = ReadPanicRecord(access, ctx);
    parts.push_back(snapshot.ir);
    panic_code = snapshot.code;
  }

  parts.push_back(EmitRuntimeTrace("Cleanup-Start", ctx));

  bool cleanup_action_can_panic = false;
  for (const auto& action : plan) {
    IRPtr action_ir = EmitCleanupAction(action, ctx);
    if (IsNoopIR(action_ir)) {
      continue;
    }
    const auto trace_ids = CleanupTraceIdsFor(action);
    const char* action_enter_trace_id = CleanupActionEnterTraceIdFor(action);
    const bool can_panic = CleanupActionCanPanic(action);

    parts.push_back(EmitRuntimeTrace(action_enter_trace_id, ctx));
    if ((action.kind == CleanupAction::Kind::DropVar ||
         action.kind == CleanupAction::Kind::DropStatic) &&
        !action.name.empty()) {
      parts.push_back(
          EmitRuntimeTrace("Cleanup-Action-DropVar-Name-" + action.name, ctx));
    }
    if (!can_panic) {
      parts.push_back(action_ir);
      if (trace_ids.has_value()) {
        parts.push_back(EmitRuntimeTrace(trace_ids->ok, ctx));
      }
      continue;
    }

    cleanup_action_can_panic = true;

    // Clear panic before running each cleanup action.
    parts.push_back(MakeIR(IRClearPanic{}));
    parts.push_back(action_ir);

    // Read panic flag/code after the action.
    auto snapshot = ReadPanicRecord(access, ctx);
    parts.push_back(snapshot.ir);
    IRValue flag = snapshot.flag;
    IRValue code = snapshot.code;
    IRPtr abort_trace = EmptyIR();
    IRPtr panic_trace = EmptyIR();
    IRPtr ok_trace = EmptyIR();
    if (trace_ids.has_value()) {
      abort_trace = EmitRuntimeTrace(trace_ids->abort, ctx);
      panic_trace = EmitRuntimeTrace(trace_ids->panic, ctx);
      ok_trace = EmitRuntimeTrace(trace_ids->ok, ctx);
    }

    // Double panic: abort.
    IRValue double_cond = ctx.FreshTempValue("double_panic");
    IRBinaryOp double_and;
    double_and.op = "&&";
    double_and.lhs = panicking;
    double_and.rhs = flag;
    double_and.result = double_cond;
    parts.push_back(MakeIR(std::move(double_and)));

    IRCall panic_call;
    panic_call.callee.kind = IRValue::Kind::Symbol;
    panic_call.callee.name = RuntimePanicSym();
    panic_call.args.push_back(code);
    panic_call.result = ctx.FreshTempValue("panic_abort");

    IRIf if_double;
    if_double.cond = double_cond;
    IRPtr panic_ir = MakeIR(std::move(panic_call));
    if (IsNoopIR(abort_trace)) {
      if_double.then_ir = panic_ir;
    } else {
      if_double.then_ir = SeqIR({abort_trace, panic_ir});
    }
    if_double.then_value = UnitValue();
    if_double.else_ir = EmptyIR();
    if_double.else_value = UnitValue();
    if_double.result = ctx.FreshTempValue("panic_double_if");
    parts.push_back(MakeIR(std::move(if_double)));

    if (!IsNoopIR(panic_trace)) {
      IRValue not_panicking = ctx.FreshTempValue("panic_not_panicking");
      IRUnaryOp not_panicking_op;
      not_panicking_op.op = "!";
      not_panicking_op.operand = panicking;
      not_panicking_op.result = not_panicking;
      parts.push_back(MakeIR(std::move(not_panicking_op)));

      IRValue panic_cond = ctx.FreshTempValue("cleanup_panic");
      IRBinaryOp panic_and;
      panic_and.op = "&&";
      panic_and.lhs = flag;
      panic_and.rhs = not_panicking;
      panic_and.result = panic_cond;
      parts.push_back(MakeIR(std::move(panic_and)));

      IRIf if_panic;
      if_panic.cond = panic_cond;
      if_panic.then_ir = panic_trace;
      if_panic.then_value = UnitValue();
      if_panic.else_ir = EmptyIR();
      if_panic.else_value = UnitValue();
      if_panic.result = ctx.FreshTempValue("cleanup_panic_if");
      parts.push_back(MakeIR(std::move(if_panic)));
    }

    // Restore panic record when continuing cleanup during a panic.
    IRValue not_flag = ctx.FreshTempValue("panic_clear");
    IRUnaryOp not_op;
    not_op.op = "!";
    not_op.operand = flag;
    not_op.result = not_flag;
    parts.push_back(MakeIR(std::move(not_op)));

    if (!IsNoopIR(ok_trace)) {
      IRIf if_ok;
      if_ok.cond = not_flag;
      if_ok.then_ir = ok_trace;
      if_ok.then_value = UnitValue();
      if_ok.else_ir = EmptyIR();
      if_ok.else_value = UnitValue();
      if_ok.result = ctx.FreshTempValue("cleanup_ok_if");
      parts.push_back(MakeIR(std::move(if_ok)));
    }

    IRValue restore_cond = ctx.FreshTempValue("panic_restore");
    IRBinaryOp restore_and;
    restore_and.op = "&&";
    restore_and.lhs = panicking;
    restore_and.rhs = not_flag;
    restore_and.result = restore_cond;
    parts.push_back(MakeIR(std::move(restore_and)));

    IRPtr restore_ir = WritePanicRecord(access, BoolImmediate(true), panic_code);
    IRIf if_restore;
    if_restore.cond = restore_cond;
    if_restore.then_ir = restore_ir;
    if_restore.then_value = UnitValue();
    if_restore.else_ir = EmptyIR();
    if_restore.else_value = UnitValue();
    if_restore.result = ctx.FreshTempValue("panic_restore_if");
    parts.push_back(MakeIR(std::move(if_restore)));

    // Update panicking state.
    IRValue new_panicking = ctx.FreshTempValue("panicking");
    IRBinaryOp or_op;
    or_op.op = "||";
    or_op.lhs = panicking;
    or_op.rhs = flag;
    or_op.result = new_panicking;
    parts.push_back(MakeIR(std::move(or_op)));
    panicking = new_panicking;

    // Update panic code when a new panic occurs.
    IRValue new_code = ctx.FreshTempValue("panic_code_sel");
    IRIf code_if;
    code_if.cond = flag;
    code_if.then_ir = EmptyIR();
    code_if.then_value = code;
    code_if.else_ir = EmptyIR();
    code_if.else_value = panic_code;
    code_if.result = new_code;
    parts.push_back(MakeIR(std::move(code_if)));
    panic_code = new_code;
  }

  parts.push_back(EmitRuntimeTrace("Cleanup-Done", ctx));

  if (emit_panic_check && cleanup_action_can_panic) {
    IRCleanupPanicCheck check;
    check.cleanup_ir =
        remainder.empty() ? EmptyIR() : EmitCleanupOnPanic(remainder, ctx);
    parts.push_back(MakeIR(std::move(check)));
  }

  return SeqIR(std::move(parts));
}

IRPtr EmitCleanup(const CleanupPlan& plan, LowerCtx& ctx) {
  return EmitCleanupWithRemainder(plan, {}, ctx);
}

IRPtr EmitCleanupOnPanic(const CleanupPlan& plan, LowerCtx& ctx) {
  return EmitCleanupImpl(plan, {}, ctx, true, false);
}

IRPtr EmitCleanupWithRemainder(const CleanupPlan& plan,
                               const CleanupPlan& remainder,
                               LowerCtx& ctx) {
  return EmitCleanupImpl(plan, remainder, ctx, false, true);
}

static IRPtr SeqWithPanicStop(std::vector<IRPtr> drops, LowerCtx& ctx) {
  drops.erase(std::remove_if(drops.begin(), drops.end(), IsNoopIR), drops.end());
  if (drops.empty()) {
    return EmptyIR();
  }
  if (drops.size() == 1) {
    return drops.front();
  }

  const PanicAccess access = BuildPanicAccess(ctx);
  IRPtr tail = drops.back();
  for (std::size_t i = drops.size() - 1; i-- > 0;) {
    IRValue flag = ctx.FreshTempValue("drop_panic_flag");
    ctx.RegisterValueType(flag, analysis::MakeTypePrim("bool"));
    IRReadPtr read_flag;
    read_flag.ptr = access.flag_ptr;
    read_flag.result = flag;

    IRValue no_panic = ctx.FreshTempValue("drop_no_panic");
    IRUnaryOp not_op;
    not_op.op = "!";
    not_op.operand = flag;
    not_op.result = no_panic;

    IRIf if_ir;
    if_ir.cond = no_panic;
    if_ir.then_ir = tail;
    if_ir.then_value = UnitValue();
    if_ir.else_ir = EmptyIR();
    if_ir.else_value = UnitValue();
    if_ir.result = ctx.FreshTempValue("drop_seq");

    tail = SeqIR({drops[i],
                  MakeIR(std::move(read_flag)),
                  MakeIR(std::move(not_op)),
                  MakeIR(std::move(if_ir))});
  }

  return tail;
}

static IRPtr EmitReleaseValue(const analysis::TypeRef& /*type*/,
                              const IRValue& /*value*/,
                              LowerCtx& /*ctx*/) {
  SPEC_RULE("ReleaseValue");
  return EmptyIR();
}

static IRPtr EmitDropMethodCall(const analysis::TypeRef& type,
                                const IRValue& value,
                                const std::optional<IRValue>& panic_out,
                                LowerCtx& ctx) {
  if (!ctx.sigma) {
    return EmptyIR();
  }

  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (!stripped) {
    return EmptyIR();
  }

  analysis::ScopeContext scope;
  scope.sigma = *ctx.sigma;
  scope.sigma_source = ctx.sigma;
  scope.current_module = ctx.module_path;

  ast::ClassPath drop_path;
  drop_path.push_back("Drop");
  if (!analysis::TypeImplementsClass(scope, stripped, drop_path)) {
    return EmptyIR();
  }

  auto sym = MethodSymbol(scope, stripped, "drop");
  if (!sym.has_value()) {
    return EmptyIR();
  }

  IRCall call;
  call.callee.kind = IRValue::Kind::Symbol;
  call.callee.name = *sym;
  call.args.push_back(value);
  if (panic_out.has_value() && ctx.NeedsPanicOutForSymbol(*sym)) {
    call.args.push_back(*panic_out);
  }
  return MakeIR(std::move(call));
}

// ============================================================================
// Section 6.8 EmitDrop - Emit IR to drop a value of a given type
// ============================================================================

static IRPtr EmitDropImpl(const analysis::TypeRef& type,
                          const IRValue& value,
                          LowerCtx& ctx,
                          bool allow_drop_glue,
                          const std::optional<IRValue>& panic_out) {
  if (!type) {
    return EmptyIR();
  }

  if (!TypeNeedsDrop(type, ctx)) {
    return EmitReleaseValue(type, value, ctx);
  }

  if (value.kind == IRValue::Kind::Opaque) {
    ctx.RegisterValueType(value, type);
  }

  if (HoldsType<analysis::TypePerm>(type)) {
    const auto& perm = GetType<analysis::TypePerm>(type);
    return EmitDropImpl(perm.base, value, ctx, allow_drop_glue, panic_out);
  }

  auto call_drop_glue = [&]() -> IRPtr {
    std::string drop_sym = DropGlueSym(type, ctx);
    IRCall call;
    call.callee.kind = IRValue::Kind::Symbol;
    call.callee.name = drop_sym;
    call.args.push_back(value);
    if (panic_out.has_value() && ctx.NeedsPanicOutForSymbol(drop_sym)) {
      call.args.push_back(*panic_out);
    }
    return MakeIR(std::move(call));
  };

  if (HoldsType<analysis::TypePrim>(type)) {
    return EmptyIR();
  }

  if (HoldsType<analysis::TypeRawPtr>(type)) {
    return EmptyIR();
  }

  if (HoldsType<analysis::TypePtr>(type)) {
    return EmptyIR();
  }

  if (analysis::IsRangeType(type)) {
    return EmptyIR();
  }

  if (HoldsType<analysis::TypeFunc>(type)) {
    return EmptyIR();
  }

  if (HoldsType<analysis::TypeSlice>(type)) {
    return EmptyIR();
  }

  if (HoldsType<analysis::TypeString>(type)) {
    const auto& str_type = GetType<analysis::TypeString>(type);
    if (str_type.state.has_value() &&
        *str_type.state == analysis::StringState::Managed) {
      const std::string drop_sym = BuiltinSymStringDropManaged();
      if (drop_sym.empty()) {
        SPEC_RULE("StringDropSym-Err");
        ctx.ReportCodegenFailure();
        return EmptyIR();
      }
      IRCall call;
      call.callee.kind = IRValue::Kind::Symbol;
      call.callee.name = drop_sym;
      call.args.push_back(value);
      return MakeIR(std::move(call));
    }
    return EmptyIR();
  }

  if (HoldsType<analysis::TypeBytes>(type)) {
    const auto& bytes_type = GetType<analysis::TypeBytes>(type);
    if (bytes_type.state.has_value() &&
        *bytes_type.state == analysis::BytesState::Managed) {
      const std::string drop_sym = BuiltinSymBytesDropManaged();
      if (drop_sym.empty()) {
        SPEC_RULE("BytesDropSym-Err");
        ctx.ReportCodegenFailure();
        return EmptyIR();
      }
      IRCall call;
      call.callee.kind = IRValue::Kind::Symbol;
      call.callee.name = drop_sym;
      call.args.push_back(value);
      return MakeIR(std::move(call));
    }
    return EmptyIR();
  }

  if (HoldsType<analysis::TypeArray>(type)) {
    const auto& arr_type = GetType<analysis::TypeArray>(type);
    std::vector<IRPtr> drops;

    for (std::size_t i = arr_type.length; i > 0; --i) {
      const std::size_t index = i - 1;
      IRValue elem;
      elem.kind = IRValue::Kind::Opaque;
      elem.name = value.name + "[" + std::to_string(index) + "]";
      ctx.RegisterValueType(elem, arr_type.element);
      {
        DerivedValueInfo info;
        info.kind = DerivedValueInfo::Kind::Index;
        info.base = value;
        info.index = USizeImmediate(index);
        ctx.RegisterDerivedValue(elem, info);
      }
      drops.push_back(EmitDropImpl(arr_type.element, elem, ctx, true, panic_out));
    }

    return SeqWithPanicStop(std::move(drops), ctx);
  }

  if (HoldsType<analysis::TypeTuple>(type)) {
    const auto& tuple_type = GetType<analysis::TypeTuple>(type);
    std::vector<IRPtr> drops;

    for (std::size_t i = tuple_type.elements.size(); i > 0; --i) {
      const std::size_t index = i - 1;
      IRValue elem;
      elem.kind = IRValue::Kind::Opaque;
      elem.name = value.name + "_" + std::to_string(index);
      ctx.RegisterValueType(elem, tuple_type.elements[index]);
      {
        DerivedValueInfo info;
        info.kind = DerivedValueInfo::Kind::Tuple;
        info.base = value;
        info.tuple_index = index;
        ctx.RegisterDerivedValue(elem, info);
      }
      drops.push_back(EmitDropImpl(tuple_type.elements[index],
                                   elem,
                                   ctx,
                                   true,
                                   panic_out));
    }

    return SeqWithPanicStop(std::move(drops), ctx);
  }

  if (HoldsType<analysis::TypeUnion>(type)) {
    const auto& uni_type = GetType<analysis::TypeUnion>(type);
    if (uni_type.members.empty()) {
      return EmptyIR();
    }

    std::vector<IRIfCaseClause> arms;
    arms.reserve(uni_type.members.size());
    for (std::size_t i = 0; i < uni_type.members.size(); ++i) {
      auto pattern = std::make_shared<IRPattern>();
      pattern->node =
          IRTypedPattern{"__case" + std::to_string(i), uni_type.members[i]};

      IRValue case_val;
      case_val.kind = IRValue::Kind::Opaque;
      case_val.name = value.name + "_case_" + std::to_string(i);
      ctx.RegisterValueType(case_val, uni_type.members[i]);
      {
        DerivedValueInfo info;
        info.kind = DerivedValueInfo::Kind::UnionPayload;
        info.base = value;
        info.union_index = i;
        ctx.RegisterDerivedValue(case_val, info);
      }

      IRPtr body = EmitDropImpl(uni_type.members[i],
                                case_val,
                                ctx,
                                true,
                                panic_out);

      IRValue unit;
      unit.kind = IRValue::Kind::Opaque;
      unit.name = "()";

      IRIfCaseClause arm;
      arm.pattern = std::move(pattern);
      arm.body = body;
      arm.value = unit;
      arms.push_back(std::move(arm));
    }

    IRIfCase if_case_ir;
    if_case_ir.scrutinee = value;
    if_case_ir.arms = std::move(arms);
    return MakeIR(std::move(if_case_ir));
  }

  if (HoldsType<analysis::TypeModalState>(type)) {
    if (allow_drop_glue) {
      return call_drop_glue();
    }
    if (!ctx.sigma) {
      return EmptyIR();
    }
    const auto& modal_state = GetType<analysis::TypeModalState>(type);
    const auto syntax_path = ToSyntaxPath(modal_state.path);
    const auto it = ctx.sigma->types.find(analysis::PathKeyOf(syntax_path));
    if (it == ctx.sigma->types.end()) {
      return EmptyIR();
    }
    const auto* decl = std::get_if<ast::ModalDecl>(&it->second);
    if (!decl) {
      return EmptyIR();
    }
    const auto* state = FindModalState(*decl, modal_state.state);
    if (!state) {
      return EmptyIR();
    }
    const auto fields = CollectModalFields(*state, ctx);
    if (!fields.has_value()) {
      return EmptyIR();
    }

    IRPtr drop_method = EmitDropMethodCall(type, value, panic_out, ctx);
    std::vector<IRPtr> drops;
    drops.reserve(fields->size() + 1);
    if (!IsNoopIR(drop_method)) {
      drops.push_back(drop_method);
    }
    for (auto rit = fields->rbegin(); rit != fields->rend(); ++rit) {
      IRValue field_val;
      field_val.kind = IRValue::Kind::Opaque;
      field_val.name = value.name + "_" + rit->first;
      ctx.RegisterValueType(field_val, rit->second);
      {
        DerivedValueInfo info;
        info.kind = DerivedValueInfo::Kind::ModalField;
        info.base = value;
        info.modal_state = modal_state.state;
        info.field = rit->first;
        ctx.RegisterDerivedValue(field_val, info);
      }
      drops.push_back(EmitDropImpl(rit->second, field_val, ctx, true, panic_out));
    }
    return SeqWithPanicStop(std::move(drops), ctx);
  }

  if (HoldsType<analysis::TypeDynamic>(type)) {
    // Spec: DropValue/DropChildren has no dynamic-specific behavior.
    // TypeDynamic does not implement Drop and has no children, so drop is a no-op.
    return EmptyIR();
  }

  if (HoldsType<analysis::TypePathType>(type)) {
    if (!ctx.sigma) {
      return EmptyIR();
    }

    const auto& type_path = GetType<analysis::TypePathType>(type);
    const auto syntax_path = ToSyntaxPath(type_path.path);
    const auto it = ctx.sigma->types.find(analysis::PathKeyOf(syntax_path));
    if (it == ctx.sigma->types.end()) {
      return EmptyIR();
    }

    if (const auto* alias = std::get_if<ast::TypeAliasDecl>(&it->second)) {
      const auto lowered = LowerTypeForDrop(alias->type, ctx);
      if (!lowered.has_value()) {
        return EmptyIR();
      }
      analysis::TypeRef instantiated = *lowered;
      if (alias->generic_params && !alias->generic_params->params.empty()) {
        if (type_path.generic_args.size() > alias->generic_params->params.size()) {
          return EmptyIR();
        }
        const auto subst = analysis::BuildSubstitution(
            alias->generic_params->params,
            type_path.generic_args);
        instantiated = analysis::InstantiateType(instantiated, subst);
      }
      return EmitDropImpl(instantiated, value, ctx, allow_drop_glue, panic_out);
    }

    if (allow_drop_glue) {
      return call_drop_glue();
    }

    if (std::holds_alternative<ast::RecordDecl>(it->second)) {
      IRPtr drop_method = EmitDropMethodCall(type, value, panic_out, ctx);
      IRPtr field_drops = EmitDropFields(type, value, {}, ctx);
      return SeqWithPanicStop({drop_method, field_drops}, ctx);
    }

    if (const auto* enum_decl = std::get_if<ast::EnumDecl>(&it->second)) {
      analysis::TypeSubst enum_subst;
      if (enum_decl->generic_params && !enum_decl->generic_params->params.empty()) {
        if (type_path.generic_args.size() > enum_decl->generic_params->params.size()) {
          return EmptyIR();
        }
        enum_subst = analysis::BuildSubstitution(
            enum_decl->generic_params->params,
            type_path.generic_args);
      }
      auto lower_payload_drop_type =
          [&](const std::shared_ptr<ast::Type>& payload_type)
              -> std::optional<analysis::TypeRef> {
        const auto lowered = LowerTypeForDrop(payload_type, ctx);
        if (!lowered.has_value()) {
          return std::nullopt;
        }
        if (enum_subst.empty()) {
          return *lowered;
        }
        return analysis::InstantiateType(*lowered, enum_subst);
      };

      std::vector<IRIfCaseClause> arms;
      arms.reserve(enum_decl->variants.size());
      for (const auto& variant : enum_decl->variants) {
        auto pattern = std::make_shared<IRPattern>();
        pattern->node = IREnumPattern{syntax_path, variant.name, std::nullopt};

        IRPtr body = EmptyIR();
        if (variant.payload_opt.has_value()) {
          std::vector<IRPtr> drops;
          if (const auto* tuple_payload =
                  std::get_if<ast::VariantPayloadTuple>(&*variant.payload_opt)) {
            for (std::size_t i = tuple_payload->elements.size(); i > 0; --i) {
              const auto lowered =
                  lower_payload_drop_type(tuple_payload->elements[i - 1]);
              if (!lowered.has_value()) {
                continue;
              }
              const std::size_t index = i - 1;
              IRValue elem;
              elem.kind = IRValue::Kind::Opaque;
              elem.name = value.name + "_payload_" + std::to_string(index);
              ctx.RegisterValueType(elem, *lowered);
              {
                DerivedValueInfo info;
                info.kind = DerivedValueInfo::Kind::EnumPayloadIndex;
                info.base = value;
                info.variant = variant.name;
                info.tuple_index = index;
                ctx.RegisterDerivedValue(elem, info);
              }
              drops.push_back(EmitDropImpl(*lowered, elem, ctx, true, panic_out));
            }
          } else if (const auto* record_payload =
                         std::get_if<ast::VariantPayloadRecord>(
                             &*variant.payload_opt)) {
            for (std::size_t i = record_payload->fields.size(); i > 0; --i) {
              const auto& field = record_payload->fields[i - 1];
              const auto lowered = lower_payload_drop_type(field.type);
              if (!lowered.has_value()) {
                continue;
              }
              IRValue field_val;
              field_val.kind = IRValue::Kind::Opaque;
              field_val.name = value.name + "_payload_" + field.name;
              ctx.RegisterValueType(field_val, *lowered);
              {
                DerivedValueInfo info;
                info.kind = DerivedValueInfo::Kind::EnumPayloadField;
                info.base = value;
                info.variant = variant.name;
                info.field = field.name;
                ctx.RegisterDerivedValue(field_val, info);
              }
              drops.push_back(EmitDropImpl(*lowered,
                                           field_val,
                                           ctx,
                                           true,
                                           panic_out));
            }
          }
          body = SeqWithPanicStop(std::move(drops), ctx);
        }

        IRValue unit;
        unit.kind = IRValue::Kind::Opaque;
        unit.name = "()";

        IRIfCaseClause arm;
        arm.pattern = std::move(pattern);
        arm.body = body;
        arm.value = unit;
        arms.push_back(std::move(arm));
      }

      IRIfCase if_case_ir;
      if_case_ir.scrutinee = value;
      if_case_ir.arms = std::move(arms);
      IRPtr payload_if_case = MakeIR(std::move(if_case_ir));
      IRPtr drop_method = EmitDropMethodCall(type, value, panic_out, ctx);
      return SeqWithPanicStop({drop_method, payload_if_case}, ctx);
    }

    if (const auto* modal_decl = std::get_if<ast::ModalDecl>(&it->second)) {
      std::vector<IRIfCaseClause> arms;
      arms.reserve(modal_decl->states.size());
      for (const auto& state : modal_decl->states) {
        auto pattern = std::make_shared<IRPattern>();
        pattern->node = IRModalPattern{state.name, std::nullopt};

        IRPtr body = EmptyIR();
        const auto fields = CollectModalFields(state, ctx);
        if (fields.has_value()) {
          const auto state_type =
              analysis::MakeTypeModalState(type_path.path, state.name);

          IRValue state_value = ctx.FreshTempValue("modal_state");
          ctx.RegisterValueType(state_value, state_type);

          DerivedValueInfo record_info;
          record_info.kind = DerivedValueInfo::Kind::RecordLit;
          record_info.fields.reserve(fields->size());
          for (const auto& field : *fields) {
            IRValue field_val = ctx.FreshTempValue("modal_field");
            ctx.RegisterValueType(field_val, field.second);
            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::ModalField;
            info.base = value;
            info.modal_state = state.name;
            info.field = field.first;
            ctx.RegisterDerivedValue(field_val, info);
            record_info.fields.emplace_back(field.first, field_val);
          }
          ctx.RegisterDerivedValue(state_value, record_info);

          body = EmitDropImpl(state_type,
                              state_value,
                              ctx,
                              false,
                              panic_out);
        }

        IRValue unit;
        unit.kind = IRValue::Kind::Opaque;
        unit.name = "()";

        IRIfCaseClause arm;
        arm.pattern = std::move(pattern);
        arm.body = body;
        arm.value = unit;
        arms.push_back(std::move(arm));
      }

      IRIfCase if_case_ir;
      if_case_ir.scrutinee = value;
      if_case_ir.arms = std::move(arms);
      IRPtr state_if_case = MakeIR(std::move(if_case_ir));
      IRPtr drop_method = EmitDropMethodCall(type, value, panic_out, ctx);
      return SeqWithPanicStop({drop_method, state_if_case}, ctx);
    }

    return EmptyIR();
  }

  return EmptyIR();
}

IRPtr EmitDrop(const analysis::TypeRef& type, const IRValue& value, LowerCtx& ctx) {
  SPEC_RULE("EmitDrop");
  IRValue panic_out;
  panic_out.kind = IRValue::Kind::Local;
  panic_out.name = std::string(kPanicOutName);
  return EmitDropImpl(type, value, ctx, true, panic_out);
}

IRPtr EmitDropInline(const analysis::TypeRef& type,
                     const IRValue& value,
                     LowerCtx& ctx) {
  SPEC_RULE("EmitDrop");
  IRValue panic_out;
  panic_out.kind = IRValue::Kind::Local;
  panic_out.name = std::string(kPanicOutName);
  return EmitDropImpl(type, value, ctx, false, panic_out);
}

// ============================================================================
// Section 6.8 EmitDropFields - Emit IR to drop specific fields of a record
// ============================================================================

IRPtr EmitDropFields(const analysis::TypeRef& type,
                     const IRValue& value,
                     const std::vector<std::string>& skip_fields,
                     LowerCtx& ctx) {
  SPEC_RULE("FieldDropSeq");

  if (!type) {
    return EmptyIR();
  }

  // Only applicable to TypePathType (records)
  if (!HoldsType<analysis::TypePathType>(type)) {
    return EmptyIR();
  }

  // Get type path
  const auto& type_path = GetType<analysis::TypePathType>(type);

  // Look up RecordDecl from sigma
  if (!ctx.sigma) {
    return EmptyIR();
  }

  // Convert type path to syntax path key
  ast::Path syntax_path;
  syntax_path.reserve(type_path.path.size());
  for (const auto& seg : type_path.path) {
    syntax_path.push_back(seg);
  }

  const auto path_key = analysis::PathKeyOf(syntax_path);
  const auto it = ctx.sigma->types.find(path_key);
  if (it == ctx.sigma->types.end()) {
    return EmptyIR();
  }

  // Check if it's a RecordDecl
  const auto* record = std::get_if<ast::RecordDecl>(&it->second);
  if (!record) {
    return EmptyIR();
  }

  // Build set of fields to skip for O(1) lookup
  std::unordered_set<std::string> skip_set(skip_fields.begin(), skip_fields.end());

  // Get Fields(R) - all FieldDecl members from record
  // Per spec: Fields(R) = [ f | f in R.members && f = FieldDecl(...) ]
  std::vector<const ast::FieldDecl*> fields;
  for (const auto& member : record->members) {
    if (const auto* field = std::get_if<ast::FieldDecl>(&member)) {
      fields.push_back(field);
    }
  }

  // FieldsRev(R) = rev(Fields(R)) - process in reverse order for LIFO drop
  std::vector<IRPtr> drops;
  drops.reserve(fields.size());

  for (auto rit = fields.rbegin(); rit != fields.rend(); ++rit) {
    const ast::FieldDecl* field = *rit;

    // Skip fields in skip_fields set
    if (skip_set.count(field->name) > 0) {
      continue;
    }

    // FieldDropIR(slot, p, f, T) = EmitDrop(T, Load(FieldAddr(TypePath(p), slot, f), T))
    //
    // Create field value representation using opaque IR value pattern
    // (consistent with EmitDrop for tuples and arrays)
    IRValue field_val;
    field_val.kind = IRValue::Kind::Opaque;
    field_val.name = value.name + "." + field->name;

    // Lower field type from ast::Type to analysis::TypeRef
    analysis::TypeRef field_type;
    if (field->type) {
      auto lowered = LowerTypeForDrop(field->type, ctx);
      if (lowered.has_value()) {
        field_type = *lowered;
      }
    }
    if (field_type) {
      ctx.RegisterValueType(field_val, field_type);
    }
    {
      DerivedValueInfo info;
      info.kind = DerivedValueInfo::Kind::Field;
      info.base = value;
      info.field = field->name;
      ctx.RegisterDerivedValue(field_val, info);
    }

    // Emit drop for this field
    drops.push_back(EmitDrop(field_type, field_val, ctx));
  }

  return SeqWithPanicStop(std::move(drops), ctx);
}

// ============================================================================
// Section 6.12.13 Drop Glue
// ============================================================================

std::string DropGlueSym(const analysis::TypeRef& type, LowerCtx& ctx) {
  SPEC_RULE("DropGlueSym");

  std::vector<std::string> path = {
      std::string(project::ActiveLanguageProfile().runtime_root),
      "runtime",
      "drop"};

  analysis::TypeRef drop_type = type;
  if (HoldsType<analysis::TypePerm>(drop_type)) {
    drop_type = GetType<analysis::TypePerm>(drop_type).base;
  }

  const auto type_path = PathOfType(drop_type);
  if (type_path.empty()) {
    path.push_back("unknown");
  } else {
    path.insert(path.end(), type_path.begin(), type_path.end());
  }

  const std::string sym = core::Mangle(core::StringOfPath(path));
  ctx.RegisterDropGlueType(sym, drop_type);
  return sym;
}

IRPtr DropGlueIR(const analysis::TypeRef& type, LowerCtx& ctx) {
  SPEC_RULE("DropGlueIR");

  // The drop glue reads the value from the data pointer and drops it
  IRValue data_ptr;
  data_ptr.kind = IRValue::Kind::Local;
  data_ptr.name = "data";

  // Read the value from the pointer
  IRReadPtr read;
  read.ptr = data_ptr;

  IRValue loaded_value;
  loaded_value = ctx.FreshTempValue("loaded");
  read.result = loaded_value;
  ctx.RegisterValueType(loaded_value, type);

  // Emit drop for the loaded value
  IRValue panic_out;
  panic_out.kind = IRValue::Kind::Local;
  panic_out.name = std::string(kPanicOutName);
  IRPtr drop_ir = EmitDropImpl(type, loaded_value, ctx, false, panic_out);

  return SeqIR({MakeIR(std::move(read)), drop_ir});
}

ProcIR EmitDropGlue(const analysis::TypeRef& type, LowerCtx& ctx) {
  SPEC_RULE("EmitDropGlue-Decl");

  ProcIR proc;
  proc.symbol = DropGlueSym(type, ctx);

  IRParam data_param;
  data_param.mode = analysis::ParamMode::Move;
  data_param.name = "data";
  data_param.type = analysis::MakeTypeRawPtr(
      analysis::RawPtrQual::Imm, analysis::MakeTypePrim("()"));
  proc.params.push_back(std::move(data_param));
  proc.params.push_back(PanicOutParam());
  proc.ret = analysis::MakeTypePrim("()");
  proc.body = DropGlueIR(type, ctx);

  return proc;
}

// ============================================================================
// Section 6.8 DropOnAssign - Drop value before assignment
// ============================================================================

IRPtr DropOnAssign(const std::string& name,
                   const IRPlace& /*slot*/,
                   LowerCtx& ctx) {
  SPEC_RULE("DropOnAssign");
  SPEC_RULE("DropOnAssign-NotApplicable");

  if (!DropOnAssignApplicable(name, ctx)) {
    SPEC_RULE("DropOnAssign-NotApplicable");
    return EmptyIR();
  }

  const BindingState* state = ctx.GetBindingState(name);
  if (!state || !state->type) {
    SPEC_RULE("DropOnAssign-Err");
    ctx.ReportCodegenFailure();
    return EmptyIR();
  }

  BindValidity validity = GetBindValidity(name, ctx);

  if (validity == BindValidity::Moved) {
    if (HoldsType<analysis::TypePathType>(state->type)) {
      SPEC_RULE("DropOnAssign-Record-Moved");
    } else if (HoldsType<analysis::TypeArray>(state->type) ||
               HoldsType<analysis::TypeTuple>(state->type) ||
               HoldsType<analysis::TypeUnion>(state->type) ||
               HoldsType<analysis::TypeModalState>(state->type)) {
      SPEC_RULE("DropOnAssign-Aggregate-Moved");
    }
    return EmptyIR();
  }

  IRValue current_value;
  current_value.kind = IRValue::Kind::Local;
  current_value.name = name;

  if (HoldsType<analysis::TypePathType>(state->type)) {
    if (validity == BindValidity::PartiallyMoved) {
      SPEC_RULE("DropOnAssign-Record-Partial");
      std::vector<std::string> moved = GetMovedFields(name, ctx);
      return EmitDropFields(state->type, current_value, moved, ctx);
    }
    SPEC_RULE("DropOnAssign-Record-Valid");
    return EmitDrop(state->type, current_value, ctx);
  }

  if (HoldsType<analysis::TypeArray>(state->type) ||
      HoldsType<analysis::TypeTuple>(state->type) ||
      HoldsType<analysis::TypeUnion>(state->type) ||
      HoldsType<analysis::TypeModalState>(state->type)) {
    SPEC_RULE("DropOnAssign-Aggregate-Ok");
    return EmitDrop(state->type, current_value, ctx);
  }

  return EmitDrop(state->type, current_value, ctx);
}

bool DropOnAssignApplicable(const std::string& name, LowerCtx& ctx) {
  SPEC_RULE("DropOnAssignApplicable");

  const BindingState* state = ctx.GetBindingState(name);
  if (!state) {
    return false;
  }
  return state->has_responsibility && state->is_immovable;
}

bool DropOnAssignRoot(const ast::Expr& place, LowerCtx& ctx) {
  SPEC_RULE("DropOnAssignRoot");

  std::function<std::optional<std::string>(const ast::Expr&)> root_name =
      [&](const ast::Expr& expr) -> std::optional<std::string> {
        return std::visit(
            [&](const auto& node) -> std::optional<std::string> {
              using T = std::decay_t<decltype(node)>;
              if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
                return node.name;
              } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
                return root_name(*node.base);
              } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
                return root_name(*node.base);
              } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
                return root_name(*node.base);
              } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
                return root_name(*node.value);
              } else {
                return std::nullopt;
              }
            },
            expr.node);
      };

  auto root = root_name(place);
  if (!root.has_value()) {
    return false;
  }

  if (ctx.GetBindingState(*root)) {
    return DropOnAssignApplicable(*root, ctx);
  }

  if (ctx.resolve_name) {
    auto resolved = ctx.resolve_name(*root);
    if (resolved.has_value() && !resolved->empty()) {
      std::vector<std::string> full = *resolved;
      const std::string resolved_name = full.back();
      full.pop_back();
      if (auto flags = StaticBindFlagsFor(full, resolved_name, ctx)) {
        return flags->has_responsibility && flags->immovable;
      }
      return false;
    }
  }

  return DropOnAssignApplicable(*root, ctx);
}

// ============================================================================
// Section 6.8 Binding Validity State
// ============================================================================

BindValidity GetBindValidity(const std::string& name, LowerCtx& ctx) {
  SPEC_RULE("BindValid");
  SPEC_RULE("BindValid-Sigma");

  // Look up binding state from context
  const BindingState* state = ctx.GetBindingState(name);
  if (!state) {
    const bool debug_obj = core::IsDebugEnabled("obj");
    if (!ctx.binding_states.empty()) {
      SPEC_RULE("BindValid-Err");
      if (debug_obj) {
        std::cerr << "[uv] missing binding state for `" << name << "`\n";
      }
      ctx.ReportCodegenFailure();
    }
    return BindValidity::Valid;
  }

  if (state->is_moved) {
    return BindValidity::Moved;
  }

  if (!state->moved_fields.empty()) {
    return BindValidity::PartiallyMoved;
  }

  return BindValidity::Valid;
}

std::vector<std::string> GetMovedFields(const std::string& name,
                                        LowerCtx& ctx) {
  SPEC_RULE("PartiallyMoved");

  // Look up binding state from context
  const BindingState* state = ctx.GetBindingState(name);
  if (!state) {
    return {};
  }

  return state->moved_fields;
}

// ============================================================================
// Anchor function for SPEC_RULE markers
// ============================================================================

void AnchorCleanupRules() {
  SPEC_RULE("CleanupPlan");
  SPEC_RULE("EmitCleanup");
  SPEC_RULE("EmitDrop");
  SPEC_RULE("FieldDropSeq");
  SPEC_RULE("DropGlueSym");
  SPEC_RULE("DropGlueIR");
  SPEC_RULE("EmitDropGlue-Decl");
  SPEC_RULE("DropOnAssign");
  SPEC_RULE("DropOnAssign-NotApplicable");
  SPEC_RULE("DropOnAssign-Record-Valid");
  SPEC_RULE("DropOnAssign-Record-Partial");
  SPEC_RULE("DropOnAssign-Record-Moved");
  SPEC_RULE("DropOnAssign-Aggregate-Ok");
  SPEC_RULE("DropOnAssign-Aggregate-Moved");
  SPEC_RULE("DropOnAssignApplicable");
  SPEC_RULE("DropOnAssignRoot");
  SPEC_RULE("BindValid");
  SPEC_RULE("PartiallyMoved");
}

}  // namespace ultraviolet::codegen


