// =============================================================================
// Expression Lowering: MethodCallExpr
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16153-16156: (Lower-Expr-MethodCall)
//     Gamma |- LowerMethodCall(...) => <IR, v_call>
//   - Section 6.10: Dynamic Dispatch (Lines 17161-17202)
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_expr_calls.cpp
//   - Lines 527-736: LowerMethodCall function
//
// =============================================================================

#include "05_codegen/lower/expr/method_call.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/cleanup/unwind.h"
#include "05_codegen/dyn_dispatch/dyn_dispatch.h"
#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/intrinsics/intrinsics_interface.h"
#include "05_codegen/lower/expr/call.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/symbols/mangle.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/caps/cap_filesystem.h"
#include "04_analysis/caps/cap_heap.h"
#include "04_analysis/caps/cap_network.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/composite/record_methods.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/modal/builtin_modal_intrinsics.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/modal/modal_transitions.h"
#include "04_analysis/memory/string_bytes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/memory/calls.h"
#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "00_core/symbols.h"

#include <algorithm>
#include <iostream>
#include <variant>

namespace cursive::codegen {

namespace {

ast::KeyMode KeyModeForReceiverPerm(analysis::Permission perm) {
    return perm == analysis::Permission::Const ? ast::KeyMode::Read
                                               : ast::KeyMode::Write;
}

// Extract parameter modes from function parameters
ParamModeList ParamModesFromFuncParams(const std::vector<analysis::TypeFuncParam>& params) {
    ParamModeList modes;
    modes.reserve(params.size());
    for (const auto& param : params) {
        modes.push_back(param.mode);
    }
    return modes;
}

ParamTypeList ParamTypesFromFuncParams(const std::vector<analysis::TypeFuncParam>& params) {
    ParamTypeList types;
    types.reserve(params.size());
    for (const auto& param : params) {
        types.push_back(param.type);
    }
    return types;
}

// Extract parameter modes from syntax parameters
ParamModeList ParamModesFromParams(const std::vector<ast::Param>& params) {
    ParamModeList modes;
    modes.reserve(params.size());
    for (const auto& param : params) {
        if (param.mode.has_value()) {
            modes.push_back(analysis::ParamMode::Move);
        } else {
            modes.push_back(std::nullopt);
        }
    }
    return modes;
}

ParamTypeList ParamTypesFromParams(const analysis::ScopeContext& scope,
                                   const std::vector<ast::Param>& params) {
    ParamTypeList types;
    types.reserve(params.size());
    for (const auto& param : params) {
        analysis::TypeRef param_type = nullptr;
        if (param.type) {
            const auto lowered = analysis::LowerType(scope, param.type);
            if (lowered.ok) {
                param_type = lowered.type;
            }
        }
        types.push_back(param_type);
    }
    return types;
}

analysis::TypeRef InstantiateActiveGenericType(const analysis::TypeRef& type,
                                               const LowerCtx& ctx) {
    if (!type || !ctx.active_generic_type_subst.has_value() ||
        ctx.active_generic_type_subst->empty()) {
        return type;
    }
    return analysis::InstantiateType(type, *ctx.active_generic_type_subst);
}

analysis::TypeRef LowerMethodSourceType(
    const analysis::ScopeContext& scope,
    const std::shared_ptr<ast::Type>& type) {
    if (!type) {
        return analysis::MakeTypePrim("()");
    }
    if (const auto lowered =
            ::cursive::analysis::layout::LowerTypeForLayout(scope, type)) {
        return *lowered;
    }
    const auto fallback = analysis::LowerType(scope, type);
    if (fallback.ok && fallback.type) {
        return fallback.type;
    }
    return analysis::MakeTypePrim("()");
}

analysis::TypeRef LowerMethodReturnType(
    const analysis::ScopeContext& scope,
    const std::shared_ptr<ast::Type>& return_type,
    const LowerCtx& ctx) {
    return InstantiateActiveGenericType(
        LowerMethodSourceType(scope, return_type),
        ctx);
}

// Extract modal state info from a type
std::optional<std::pair<analysis::TypePath, std::string>> ModalStateInfo(
    const analysis::TypeRef& type) {
    const auto stripped = analysis::StripPerm(type);
    if (!stripped) {
        return std::nullopt;
    }
    if (const auto* modal = std::get_if<analysis::TypeModalState>(&stripped->node)) {
        return std::make_pair(modal->path, modal->state);
    }
    return std::nullopt;
}

std::optional<analysis::TypePath> ResolveDispatchTypePath(
    const analysis::ScopeContext& scope,
    const analysis::TypePath& path) {
    if (path.size() != 1) {
        return path;
    }

    const auto ent = analysis::ResolveTypeName(scope, path.front());
    if (!ent.has_value() ||
        ent->kind != analysis::EntityKind::Type ||
        !ent->origin_opt.has_value()) {
        return path;
    }

    analysis::TypePath resolved = *ent->origin_opt;
    resolved.push_back(ent->target_opt.value_or(path.front()));
    return resolved;
}

const ast::ModalDecl* ResolveModalDeclForDispatch(
    const analysis::ScopeContext& scope,
    analysis::TypePath& path) {
    if (const ast::ModalDecl* direct = analysis::LookupModalDecl(scope, path)) {
        return direct;
    }

    const auto resolved = ResolveDispatchTypePath(scope, path);
    if (!resolved.has_value() || *resolved == path) {
        return nullptr;
    }

    if (const ast::ModalDecl* decl = analysis::LookupModalDecl(scope, *resolved)) {
        path = *resolved;
        return decl;
    }
    return nullptr;
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

analysis::TypeRef ReceiverBindingType(const ast::ExprPtr& receiver,
                                      const LowerCtx& ctx) {
    const auto root = ReceiverRootName(receiver);
    if (!root.has_value()) {
        return nullptr;
    }
    const BindingState* state = ctx.GetBindingState(*root);
    if (!state) {
        return nullptr;
    }
    return state->type;
}

analysis::TypeRef ReceiverSemanticType(const ast::ExprPtr& receiver,
                                       const LowerCtx& ctx) {
    if (!receiver || !ctx.expr_type) {
        return nullptr;
    }
    return ctx.expr_type(*receiver);
}

analysis::TypeRef ReceiverDispatchType(const ast::ExprPtr& receiver,
                                       const LowerCtx& ctx) {
    analysis::TypeRef binding_type = ReceiverBindingType(receiver, ctx);
    analysis::TypeRef semantic_type = ReceiverSemanticType(receiver, ctx);

    if (ModalStateInfo(semantic_type)) {
        return semantic_type;
    }
    if (binding_type) {
        return binding_type;
    }
    return semantic_type;
}

bool ShouldTraceMethodCall(std::string_view name) {
    return core::IsDebugEnabled("call") &&
           (name == "currentDiagnostic" || name == "remainingDiagnostics");
}

// Materialize a temporary for non-`move` receiver expressions that do not
// already carry source provenance and must be passed by reference.
LowerResult LowerRefReceiverWithTemp(const ast::ExprPtr& expr,
                                     const analysis::TypeRef& expected_type,
                                     LowerCtx& ctx) {
    if (!expr) {
        return LowerResult{EmptyIR(), IRValue{}};
    }

    if (analysis::HasSourceProvenance(expr)) {
        return LowerAddrOf(*expr, ctx);
    }

    auto value_result = LowerExpr(*expr, ctx);
    std::string temp_name = ctx.FreshTempValue("method_recv_tmp").name;

    analysis::TypeRef temp_type = expected_type;
    if (!temp_type) {
        temp_type = ctx.LookupValueType(value_result.value);
        if (!temp_type && ctx.expr_type) {
            temp_type = ctx.expr_type(*expr);
        }
    }
    if (!temp_type) {
        temp_type = analysis::MakeTypePrim("()");
    }

    IRBindVar bind;
    bind.name = temp_name;
    bind.value = value_result.value;
    bind.type = temp_type;
    bind.prov = analysis::ProvenanceKind::Stack;

    ctx.RegisterVar(temp_name, temp_type, false, false,
                    analysis::ProvenanceKind::Stack, std::nullopt);
    bind.stable_name = ctx.StableBindingName(temp_name);

    ast::Expr temp_ident;
    temp_ident.span = expr->span;
    temp_ident.node = ast::IdentifierExpr{temp_name};
    auto addr_result = LowerAddrOf(temp_ident, ctx);

    return LowerResult{SeqIR({value_result.ir, MakeIR(std::move(bind)), addr_result.ir}),
                       addr_result.value};
}

// Lower receiver argument expression - either by reference or by move
LowerResult LowerRecvArgExpr(const ast::ExprPtr& base,
                             LowerCtx& ctx,
                             const analysis::TypeRef& expected_type = nullptr) {
    if (!base) {
        return LowerResult{EmptyIR(), IRValue{}};
    }
    if (std::holds_alternative<ast::MoveExpr>(base->node)) {
        SPEC_RULE("Lower-RecvArg-Move");
        return LowerExpr(*base, ctx);
    }
    SPEC_RULE("Lower-RecvArg-Ref");
    return LowerRefReceiverWithTemp(base, expected_type, ctx);
}

std::optional<ParamModeList> BuiltinCapabilityParamModes(
    const analysis::TypePath& cap_path,
    std::string_view method_name) {
    ast::ClassPath class_path;
    class_path.reserve(cap_path.size());
    for (const auto& seg : cap_path) {
        class_path.push_back(seg);
    }

    if (analysis::IsFileSystemClassPath(class_path)) {
        if (const auto sig = analysis::LookupFileSystemMethodSig(method_name)) {
            return ParamModesFromParams(sig->params);
        }
        return std::nullopt;
    }

    if (analysis::IsHeapAllocatorClassPath(class_path)) {
        if (const auto sig = analysis::LookupHeapAllocatorMethodSig(method_name)) {
            return ParamModesFromParams(sig->params);
        }
        return std::nullopt;
    }

    if (analysis::IsNetworkClassPath(class_path)) {
        if (const auto sig = analysis::LookupNetworkMethodSig(method_name)) {
            return ParamModesFromParams(sig->params);
        }
        return std::nullopt;
    }

  if (analysis::IsExecutionDomainTypePath(cap_path)) {
    if (const auto sig = analysis::LookupExecutionDomainMethodSig(method_name)) {
      return ParamModesFromParams(sig->params);
    }
    return std::nullopt;
  }

  if (analysis::IsSystemTypePath(cap_path)) {
    if (const auto sig = analysis::LookupSystemMethodSig(method_name)) {
      return ParamModesFromParams(sig->params);
    }
    return std::nullopt;
  }

  return std::nullopt;
}

std::optional<ParamTypeList> BuiltinCapabilityParamTypes(
    const analysis::ScopeContext& scope,
    const analysis::TypePath& cap_path,
    std::string_view method_name) {
    ast::ClassPath class_path;
    class_path.reserve(cap_path.size());
    for (const auto& seg : cap_path) {
        class_path.push_back(seg);
    }

    if (analysis::IsFileSystemClassPath(class_path)) {
        if (const auto sig = analysis::LookupFileSystemMethodSig(method_name)) {
            return ParamTypesFromParams(scope, sig->params);
        }
        return std::nullopt;
    }

    if (analysis::IsHeapAllocatorClassPath(class_path)) {
        if (const auto sig = analysis::LookupHeapAllocatorMethodSig(method_name)) {
            return ParamTypesFromParams(scope, sig->params);
        }
        return std::nullopt;
    }

    if (analysis::IsNetworkClassPath(class_path)) {
        if (const auto sig = analysis::LookupNetworkMethodSig(method_name)) {
            return ParamTypesFromParams(scope, sig->params);
        }
        return std::nullopt;
    }

    if (analysis::IsExecutionDomainTypePath(cap_path)) {
        if (const auto sig = analysis::LookupExecutionDomainMethodSig(method_name)) {
            return ParamTypesFromParams(scope, sig->params);
        }
        return std::nullopt;
    }

    if (analysis::IsSystemTypePath(cap_path)) {
        if (const auto sig = analysis::LookupSystemMethodSig(method_name)) {
            return ParamTypesFromParams(scope, sig->params);
        }
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<std::string> AsyncCombinatorRuntimeSymbol(std::string_view name) {
    const analysis::TypePath async_modal_path = {"Async"};
    if (!analysis::IsBuiltinModalGeneralMember(async_modal_path, name)) {
        return std::nullopt;
    }
    return analysis::LookupBuiltinModalRuntimeSymbol(
        async_modal_path,
        std::nullopt,
        name);
}

analysis::TypeRef InferAsyncCombinatorResultType(
    std::string_view name,
    const analysis::ScopeContext& scope,
    const analysis::TypeRef& receiver_type,
    const std::vector<ast::Arg>& args,
    const LowerCtx& ctx) {
    const auto source_sig = analysis::AsyncSigOf(scope, receiver_type);
    if (!source_sig.has_value()) {
        return nullptr;
    }

    if (analysis::IdEq(name, "filter") || analysis::IdEq(name, "take")) {
        return receiver_type;
    }

    if (analysis::IdEq(name, "fold")) {
        if (args.empty() || !args[0].value) {
            return nullptr;
        }
        analysis::TypeRef acc_type;
        if (ctx.expr_type) {
            acc_type = ctx.expr_type(*args[0].value);
        }
        if (!acc_type) {
            return nullptr;
        }
        return analysis::MakeTypePath(
            {"Async"},
            {analysis::MakeTypePrim("()"),
             analysis::MakeTypePrim("()"),
             acc_type,
             source_sig->err});
    }

    if ((analysis::IdEq(name, "map") || analysis::IdEq(name, "chain")) &&
        !args.empty() && args[0].value) {
        analysis::TypeRef fn_type;
        if (ctx.expr_type) {
            fn_type = ctx.expr_type(*args[0].value);
        }
        fn_type = analysis::StripPerm(fn_type);
        if (!fn_type) {
            return nullptr;
        }

        if (const auto* func = std::get_if<analysis::TypeFunc>(&fn_type->node)) {
            if (analysis::IdEq(name, "map")) {
                return analysis::MakeTypePath(
                    {"Async"},
                    {func->ret, source_sig->in, source_sig->result, source_sig->err});
            }
            return func->ret;
        }

        if (const auto* closure = std::get_if<analysis::TypeClosure>(&fn_type->node)) {
            if (analysis::IdEq(name, "map")) {
                return analysis::MakeTypePath(
                    {"Async"},
                    {closure->ret, source_sig->in, source_sig->result, source_sig->err});
            }
            return closure->ret;
        }
    }

    return nullptr;
}

const ast::Expr* UnwrapRegionReceiverExpr(const ast::ExprPtr& expr) {
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

std::optional<std::string> RegionReceiverRootName(const ast::ExprPtr& receiver) {
    const ast::Expr* unwrapped = UnwrapRegionReceiverExpr(receiver);
    if (!unwrapped) {
        return std::nullopt;
    }
    if (const auto* ident = std::get_if<ast::IdentifierExpr>(&unwrapped->node)) {
        return ident->name;
    }
    return std::nullopt;
}

bool IsRegionModalState(const analysis::TypeRef& type, std::string_view state_name) {
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

bool IsRegionModalStateAny(const analysis::TypeRef& type) {
    analysis::TypeRef stripped = analysis::StripPerm(type);
    if (!stripped) {
        return false;
    }
    const auto* modal = std::get_if<analysis::TypeModalState>(&stripped->node);
    return modal && modal->path.size() == 1 && modal->path.front() == "Region";
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

void SyncRegionAliasForMethodResult(const ast::MethodCallExpr& expr,
                                    const analysis::TypeRef& result_type,
                                    LowerCtx& ctx) {
    if (!IsRegionModalStateAny(result_type)) {
        return;
    }
    const auto root = RegionReceiverRootName(expr.receiver);
    if (!root.has_value()) {
        return;
    }
    if (IsRegionModalState(result_type, "Active")) {
        if (std::find(ctx.active_region_aliases.begin(),
                      ctx.active_region_aliases.end(),
                      *root) == ctx.active_region_aliases.end()) {
            ctx.active_region_aliases.push_back(*root);
        }
        return;
    }
    RemoveActiveRegionAlias(ctx, *root);
}

}  // namespace

// =============================================================================
// LowerMethodCall - Lower a method call expression to IR
// =============================================================================
// SPEC: (Lower-Expr-MethodCall)
//   Gamma |- LowerRecvArg(recv) => <IR_r, v_recv>
//   Gamma |- LowerArgs(args) => <IR_a, vs>
//   ResolveMethod(recv_type, name) = symbol
//   --------------------------------------------------------
//   Gamma |- LowerExpr(recv~>name(args)) => <SeqIR(IR_r, IR_a, Call), v_result>
//
// Method calls are lowered differently based on:
// 1. Static dispatch: receiver type is concrete, method symbol resolved at compile time
// 2. Dynamic dispatch: receiver is $ClassName, vtable lookup at runtime
// 3. Capability methods: built-in methods on FileSystem, HeapAllocator, etc.
// 4. Modal methods: methods on modal types in specific states
// =============================================================================

LowerResult LowerMethodCall(const ast::Expr& expr_wrapper,
                            const ast::MethodCallExpr& expr,
                            LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-MethodCall");

    // Get receiver type for dispatch determination
    analysis::TypeRef recv_type = ReceiverDispatchType(expr.receiver, ctx);
    analysis::TypeRef stripped = recv_type ? analysis::StripPerm(recv_type) : recv_type;
    const auto* dyn_type = stripped ? std::get_if<analysis::TypeDynamic>(&stripped->node) : nullptr;
    if (ShouldTraceMethodCall(expr.name)) {
        const auto root = ReceiverRootName(expr.receiver);
        const analysis::TypeRef binding_type = ReceiverBindingType(expr.receiver, ctx);
        const analysis::TypeRef semantic_type = ReceiverSemanticType(expr.receiver, ctx);
        std::cerr << "[cursive] method-call-lower"
                  << " name=" << expr.name
                  << " root=" << (root.has_value() ? *root : std::string("<none>"))
                  << " binding_type="
                  << (binding_type ? analysis::TypeToString(binding_type)
                                   : std::string("<null>"))
                  << " semantic_type="
                  << (semantic_type ? analysis::TypeToString(semantic_type)
                                    : std::string("<null>"))
                  << " dispatch_type="
                  << (recv_type ? analysis::TypeToString(recv_type)
                                : std::string("<null>"))
                  << "\n";
    }

    // Handle shared-value until(pred, action) specially.
    if (analysis::IdEq(expr.name, "until") &&
        expr.args.size() == 2 &&
        expr.args[0].value &&
        expr.args[1].value) {
        SPEC_RULE("Lower-MethodCall-Until");

        auto recv_result = LowerExpr(*expr.receiver, ctx);

        analysis::TypeRef recv_value_type = ctx.LookupValueType(recv_result.value);
        if (!recv_value_type && ctx.expr_type) {
            recv_value_type = ctx.expr_type(*expr.receiver);
        }
        if (!recv_value_type) {
            recv_value_type = analysis::MakeTypePrim("()");
        }

        const std::string recv_temp_name = ctx.FreshTempValue("until_recv").name;
        IRBindVar bind_recv;
        bind_recv.name = recv_temp_name;
        bind_recv.value = recv_result.value;
        bind_recv.type = recv_value_type;
        bind_recv.prov = analysis::ProvenanceKind::Stack;
        ctx.RegisterVar(recv_temp_name,
                        recv_value_type,
                        false,
                        false,
                        analysis::ProvenanceKind::Stack,
                        std::nullopt);
        bind_recv.stable_name = ctx.StableBindingName(recv_temp_name);

        ast::Expr recv_ident;
        recv_ident.span = expr.receiver ? expr.receiver->span : core::Span{};
        recv_ident.node = ast::IdentifierExpr{recv_temp_name};
        auto recv_ident_ptr = std::make_shared<ast::Expr>(recv_ident);

        ast::CallExpr pred_call_expr;
        pred_call_expr.callee = expr.args[0].value;
        ast::Arg pred_arg;
        pred_arg.value = recv_ident_ptr;
        pred_arg.moved = false;
        pred_call_expr.args.push_back(std::move(pred_arg));
        ast::Expr pred_call_wrapper;
        pred_call_wrapper.node = pred_call_expr;
        auto pred_result = LowerCallExpr(pred_call_wrapper, pred_call_expr, ctx);

        ast::CallExpr action_call_expr;
        action_call_expr.callee = expr.args[1].value;
        ast::Arg action_arg;
        action_arg.value = recv_ident_ptr;
        action_arg.moved = false;
        action_call_expr.args.push_back(std::move(action_arg));
        ast::Expr action_call_wrapper;
        action_call_wrapper.node = action_call_expr;
        auto action_result = LowerCallExpr(action_call_wrapper, action_call_expr, ctx);

        analysis::TypeRef action_result_type = ctx.LookupValueType(action_result.value);
        if (!action_result_type && ctx.expr_type) {
            action_result_type = ctx.expr_type(action_call_wrapper);
        }
        if (!action_result_type && ctx.expr_type) {
            analysis::TypeRef action_callee_type = ctx.expr_type(*expr.args[1].value);
            if (action_callee_type) {
                action_callee_type = analysis::StripPerm(action_callee_type);
                if (const auto* fn = std::get_if<analysis::TypeFunc>(&action_callee_type->node)) {
                    action_result_type = fn->ret;
                }
            }
        }
        if (!action_result_type) {
            action_result_type = analysis::MakeTypePrim("()");
        }

        const analysis::TypeRef until_async_type =
            analysis::MakeTypePath({"Async"},
                                   {analysis::MakeTypePrim("()"),
                                    analysis::MakeTypePrim("()"),
                                    action_result_type,
                                    analysis::MakeTypePrim("!")});

        IRAsyncComplete complete;
        complete.value = action_result.value;
        complete.result = ctx.FreshTempValue("until_async");
        complete.async_type = until_async_type;
        complete.result_type = action_result_type;
        ctx.RegisterValueType(complete.result, until_async_type);
        IRValue complete_value = complete.result;

        IRIf guard;
        guard.cond = pred_result.value;
        guard.then_ir = SeqIR({action_result.ir, MakeIR(std::move(complete))});
        guard.then_value = complete_value;
        guard.else_ir = LowerPanic(PanicReason::AsyncFailed, ctx);
        guard.else_value = complete_value;
        guard.result = ctx.FreshTempValue("until_result");
        ctx.RegisterValueType(guard.result, until_async_type);

        IRValue until_value = guard.result;
        return LowerResult{
            SeqIR({recv_result.ir,
                   MakeIR(std::move(bind_recv)),
                   pred_result.ir,
                   MakeIR(std::move(guard))}),
            until_value};
    }

    // Async combinators are lowered to pseudo-calls that are interpreted
    // directly by LLVM emission, preserving combinator semantics.
    const analysis::ScopeContext& scope = ScopeForLowering(ctx);
    const auto async_combinator_symbol = AsyncCombinatorRuntimeSymbol(expr.name);
    analysis::TypeRef method_expr_type;
    if (ctx.expr_type) {
        method_expr_type = ctx.expr_type(expr_wrapper);
    }
    if (((stripped && analysis::AsyncSigOf(scope, stripped).has_value()) ||
         (method_expr_type && analysis::AsyncSigOf(scope, method_expr_type).has_value())) &&
        async_combinator_symbol.has_value()) {
        SPEC_RULE("Lower-MethodCall-AsyncCombinator");
        auto recv_result = LowerExpr(*expr.receiver, ctx);
        analysis::TypeRef recv_result_type = recv_type;
        if (ctx.expr_type && expr.receiver) {
            if (analysis::TypeRef recv_expr_type = ctx.expr_type(*expr.receiver)) {
                recv_result_type = recv_expr_type;
            }
        }
        if (!recv_result_type) {
            recv_result_type = ctx.LookupValueType(recv_result.value);
        }
        if (recv_result_type) {
            ctx.RegisterValueType(recv_result.value, recv_result_type);
        }
        if (!method_expr_type && recv_result_type) {
            method_expr_type = InferAsyncCombinatorResultType(
                expr.name, scope, recv_result_type, expr.args, ctx);
        }
        ParamModeList param_modes;
        // Preserve callable/function arguments as direct values for combinator
        // emission; address-of lowering here turns procedure identifiers into
        // non-callable references at LLVM emission time.
        param_modes.reserve(expr.args.size());
        for (std::size_t i = 0; i < expr.args.size(); ++i) {
            param_modes.push_back(analysis::ParamMode::Move);
        }
        auto [args_ir, arg_values] = LowerArgs(param_modes, expr.args, ctx);
        IRCall comb_call;
        comb_call.callee = IRValue{IRValue::Kind::Symbol,
                                   *async_combinator_symbol,
                                   {}};
        comb_call.args.reserve(1 + arg_values.size());
        comb_call.args.push_back(recv_result.value);
        for (const auto& arg : arg_values) {
            comb_call.args.push_back(arg);
        }
        comb_call.result = ctx.FreshTempValue("async_comb");
        IRValue comb_result = comb_call.result;

        if (method_expr_type) {
            ctx.RegisterValueType(comb_call.result, method_expr_type);
        }

        return LowerResult{
            SeqIR({recv_result.ir, args_ir, MakeIR(std::move(comb_call))}),
            comb_result};
    }

    // Handle builtin modal calls that lower to receiver-backed allocation.
    if (stripped) {
        if (const auto* modal = std::get_if<analysis::TypeModalState>(&stripped->node)) {
            const auto sig = analysis::LookupBuiltinModalMemberSig(modal->path, modal->state, expr.name);
            if (sig.has_value() &&
                sig->lowering == analysis::BuiltinModalLoweringOp::AllocInReceiver) {
                SPEC_RULE("Lower-MethodCall-Region-Alloc");
                if (expr.args.size() == 1 && expr.args[0].value) {
                    auto recv_result = LowerExpr(*expr.receiver, ctx);
                    auto value_result = LowerExpr(*expr.args[0].value, ctx);
                    analysis::TypeRef value_type;
                    if (ctx.expr_type) {
                        value_type = ctx.expr_type(*expr.args[0].value);
                    }
                    if (!value_type) {
                        value_type = ctx.LookupValueType(value_result.value);
                    }
                    IRAlloc alloc;
                    alloc.region = recv_result.value;
                    alloc.value = value_result.value;
                    alloc.type = value_type;
                    IRValue ptr_value = ctx.FreshTempValue("alloc_ptr");
                    alloc.result = ptr_value;
                    IRValue alloc_val = ctx.FreshTempValue("alloc_val");
                    DerivedValueInfo info;
                    info.kind = DerivedValueInfo::Kind::LoadFromAddr;
                    info.base = ptr_value;
                    ctx.RegisterDerivedValue(alloc_val, info);
                    if (value_type) {
                        ctx.RegisterValueType(alloc_val, value_type);
                    }
                    return LowerResult{SeqIR({recv_result.ir, value_result.ir, MakeIR(std::move(alloc))}),
                                       alloc_val};
                }
            }
        }
    }

    ParamModeList param_modes;
    ParamTypeList param_types;
    bool move_receiver = false;
    ast::KeyMode receiver_key_mode = ast::KeyMode::Read;
    auto lower_type = [&](const std::shared_ptr<ast::Type>& type)
        -> analysis::LowerTypeResult {
        return analysis::LowerType(scope, type);
    };

    // Handle Context builtin methods (cpu, gpu, inline)
    if (const auto* path = stripped ? std::get_if<analysis::TypePathType>(&stripped->node) : nullptr) {
    if (analysis::IsContextTypePath(path->path) &&
        (expr.name == "cpu" || expr.name == "gpu" || expr.name == "inline")) {
            SPEC_RULE("Lower-MethodCall-ContextBuiltin");
            auto recv_result = LowerRecvArgExpr(expr.receiver, ctx, recv_type);
            auto [args_ir, arg_values] =
                LowerArgs(param_modes,
                          expr.args,
                          ctx,
                          param_types.empty() ? nullptr : &param_types);

            std::vector<IRValue> all_args;
            all_args.push_back(recv_result.value);
            all_args.insert(all_args.end(), arg_values.begin(), arg_values.end());

            const std::string qualified = "Context::" + expr.name;
            std::string callee_sym = BuiltinSym(qualified);
            IRValue result_value = ctx.FreshTempValue("method_call");

            IRCall call;
            call.callee = IRValue{IRValue::Kind::Symbol, callee_sym, {}};
            call.args = std::move(all_args);
            call.result = result_value;

            return LowerResult{SeqIR({recv_result.ir, args_ir, MakeIR(std::move(call))}),
                               result_value};
        }

        if (analysis::IsSystemTypePath(path->path)) {
            const auto sig = analysis::LookupSystemMethodSig(expr.name);
            if (sig.has_value()) {
                SPEC_RULE("Lower-MethodCall-SystemBuiltin");
                analysis::TypeRef result_type = sig->ret;
                const std::string qualified = "System::" + expr.name;
                std::string callee_sym = BuiltinSym(qualified);
                if (const auto runtime_info = GetRuntimeFuncInfo(callee_sym)) {
                    param_modes.reserve(runtime_info->params.size());
                    param_types.reserve(runtime_info->params.size());
                    for (const auto& param : runtime_info->params) {
                        param_modes.push_back(param.mode);
                        param_types.push_back(param.type);
                    }
                    if (runtime_info->ret) {
                        result_type = runtime_info->ret;
                    }
                } else {
                    param_modes = ParamModesFromParams(sig->params);
                    param_types = ParamTypesFromParams(scope, sig->params);
                }
                auto recv_result = LowerRecvArgExpr(expr.receiver, ctx, recv_type);
                auto [args_ir, arg_values] =
                    LowerArgs(param_modes,
                              expr.args,
                              ctx,
                              param_types.empty() ? nullptr : &param_types);

                std::vector<IRValue> all_args;
                all_args.insert(all_args.end(), arg_values.begin(), arg_values.end());

                IRValue result_value = ctx.FreshTempValue("method_call");
                if (result_type) {
                    ctx.RegisterValueType(result_value, result_type);
                }

                IRCall call;
                call.callee = IRValue{IRValue::Kind::Symbol, callee_sym, {}};
                call.args = std::move(all_args);
                call.result = result_value;

                return LowerResult{SeqIR({recv_result.ir, args_ir, MakeIR(std::move(call))}),
                                   result_value};
            }
        }
    }

    if (stripped) {
        if (const auto foundational_sig =
                analysis::LookupFoundationalBuiltinMethodSig(stripped, expr.name)) {
            SPEC_RULE("Lower-MethodCall-FoundationalBuiltin");
            auto recv_result = LowerExpr(*expr.receiver, ctx);
            std::vector<IRPtr> arg_ir_parts;
            std::vector<IRValue> arg_values;
            arg_ir_parts.reserve(expr.args.size());
            arg_values.reserve(expr.args.size());
            for (const auto& arg : expr.args) {
                if (!arg.value) {
                    continue;
                }
                auto arg_result =
                    arg.moved ? LowerExpr(*analysis::MovedArgExpr(arg), ctx)
                              : LowerExpr(*arg.value, ctx);
                arg_ir_parts.push_back(arg_result.ir);
                arg_values.push_back(arg_result.value);
            }
            IRPtr args_ir = SeqIR(std::move(arg_ir_parts));

            std::vector<IRValue> all_args;
            all_args.push_back(recv_result.value);
            all_args.insert(all_args.end(), arg_values.begin(), arg_values.end());

            std::string callee_sym;
            if (analysis::IdEq(expr.name, "eq")) {
                callee_sym = BuiltinSymEqEq();
            } else if (analysis::IdEq(expr.name, "successor")) {
                callee_sym = BuiltinSymStepSuccessor();
            } else {
                callee_sym = BuiltinSymStepPredecessor();
            }

            IRValue result_value = ctx.FreshTempValue("method_call");
            ctx.RegisterValueType(result_value, foundational_sig->ret);

            IRCall call;
            call.callee = IRValue{IRValue::Kind::Symbol, callee_sym, {}};
            call.args = std::move(all_args);
            call.result = result_value;

            return LowerResult{
                SeqIR({recv_result.ir, args_ir, MakeIR(std::move(call))}),
                result_value};
        }
    }

    // Handle dynamic dispatch ($ClassName)
    if (dyn_type && ctx.sigma) {
        const bool is_builtin = ::cursive::codegen::IsBuiltinCapClass(dyn_type->path);
        const auto* class_method = analysis::LookupClassMethod(scope, dyn_type->path, expr.name);
        if (class_method) {
            param_modes = ParamModesFromParams(class_method->params);
            param_types = ParamTypesFromParams(scope, class_method->params);
        }

        // Capability methods (FileSystem, HeapAllocator, etc.)
        if (is_builtin) {
            SPEC_RULE("Lower-MethodCall-Capability");
            if (const auto builtin_param_modes =
                    BuiltinCapabilityParamModes(dyn_type->path, expr.name)) {
                param_modes = *builtin_param_modes;
            }
            if (const auto builtin_param_types =
                    BuiltinCapabilityParamTypes(scope, dyn_type->path, expr.name)) {
                param_types = *builtin_param_types;
            }
            auto recv_result = LowerRecvArgExpr(expr.receiver, ctx, recv_type);
            auto [args_ir, arg_values] =
                LowerArgs(param_modes,
                          expr.args,
                          ctx,
                          param_types.empty() ? nullptr : &param_types);

            std::vector<IRValue> all_args;
            all_args.push_back(recv_result.value);
            all_args.insert(all_args.end(), arg_values.begin(), arg_values.end());

            std::string callee_sym = expr.name;
            if (auto sym = BuiltinMethodSym(dyn_type->path, expr.name)) {
                callee_sym = *sym;
            }

            IRValue result_value = ctx.FreshTempValue("method_call");

            IRCall call;
            call.callee = IRValue{IRValue::Kind::Symbol, callee_sym, {}};
            call.args = std::move(all_args);
            call.result = result_value;

            return LowerResult{SeqIR({recv_result.ir, args_ir, MakeIR(std::move(call))}),
                               result_value};
        }

        // Dynamic dispatch via vtable
        const ast::ClassDecl* class_decl = nullptr;
        const auto class_key = analysis::PathKeyOf(dyn_type->path);
        auto it = ctx.sigma->classes.find(class_key);
        if (it != ctx.sigma->classes.end()) {
            class_decl = &it->second;
        }

        if (class_decl) {
            SPEC_RULE("Lower-MethodCall-Dynamic");
            auto recv_result = LowerExpr(*expr.receiver, ctx);
            auto [args_ir, arg_values] =
                LowerArgs(param_modes,
                          expr.args,
                          ctx,
                          param_types.empty() ? nullptr : &param_types);
            IRValue panic_out;
            panic_out.kind = IRValue::Kind::Local;
            panic_out.name = std::string(kPanicOutName);
            arg_values.push_back(panic_out);

            auto dyn_result = LowerDynCall(recv_result.value,
                                           "",
                                           *class_decl,
                                           expr.name,
                                           arg_values,
                                           ctx);

            return LowerResult{SeqIR({recv_result.ir, args_ir, dyn_result.ir}),
                               dyn_result.value};
        }
    }

    // Static dispatch - look up method from type
    analysis::TypeRef resolved_method_result_type;
    if (!dyn_type && ctx.sigma) {
        if (auto modal_info = ModalStateInfo(recv_type)) {
            analysis::TypePath modal_path = modal_info->first;
            const auto* modal_decl = ResolveModalDeclForDispatch(scope, modal_path);
            if (modal_decl) {
                if (const auto* state_method =
                        analysis::LookupStateMethodDecl(*modal_decl, modal_info->second, expr.name)) {
                    param_modes = ParamModesFromParams(state_method->params);
                    param_types = ParamTypesFromParams(scope, state_method->params);
                    resolved_method_result_type =
                        LowerMethodReturnType(scope, state_method->return_type_opt, ctx);
                } else if (const auto* transition =
                               analysis::LookupTransitionDecl(*modal_decl, modal_info->second, expr.name)) {
                    param_modes = ParamModesFromParams(transition->params);
                    param_types = ParamTypesFromParams(scope, transition->params);
                    move_receiver = true;
                    receiver_key_mode = ast::KeyMode::Write;
                }
            }
        } else if (stripped) {
            if (const auto builtin_sig =
                    analysis::LookupStringBytesBuiltinMethodSig(stripped, expr.name)) {
                param_modes = ParamModesFromFuncParams(builtin_sig->params);
                param_types = ParamTypesFromFuncParams(builtin_sig->params);
                resolved_method_result_type = builtin_sig->ret;
            }
            const auto lookup = analysis::LookupMethodStatic(scope, stripped, expr.name);
            if (lookup.record_method) {
                param_modes = ParamModesFromParams(lookup.record_method->params);
                param_types = ParamTypesFromParams(scope, lookup.record_method->params);
                resolved_method_result_type =
                    LowerMethodReturnType(scope, lookup.record_method->return_type_opt, ctx);
                const auto recv_info = analysis::RecvTypeForReceiver(
                    scope, stripped, lookup.record_method->receiver, lower_type);
                if (recv_info.ok && recv_info.type) {
                    receiver_key_mode =
                        KeyModeForReceiverPerm(analysis::PermOfType(recv_info.type));
                }
            } else if (lookup.class_method) {
                param_modes = ParamModesFromParams(lookup.class_method->params);
                param_types = ParamTypesFromParams(scope, lookup.class_method->params);
                resolved_method_result_type =
                    LowerMethodReturnType(scope, lookup.class_method->return_type_opt, ctx);
                const auto recv_info = analysis::RecvTypeForReceiver(
                    scope, stripped, lookup.class_method->receiver, lower_type);
                if (recv_info.ok && recv_info.type) {
                    receiver_key_mode =
                        KeyModeForReceiverPerm(analysis::PermOfType(recv_info.type));
                }
            }
        }
    }

    // Lower receiver and arguments
    IRPtr recv_key_ir =
        expr.receiver ? LowerImplicitKeyAccess(*expr.receiver, receiver_key_mode, ctx)
                      : EmptyIR();
    LowerResult recv_result;
    if (move_receiver && expr.receiver) {
        // Modal transitions consume the source state receiver.
        if (std::holds_alternative<ast::MoveExpr>(expr.receiver->node)) {
            recv_result = LowerExpr(*expr.receiver, ctx);
        } else {
            recv_result = LowerMovePlace(*expr.receiver, ctx);
        }
    } else {
        recv_result = LowerRecvArgExpr(expr.receiver, ctx, recv_type);
    }
    auto [args_ir, arg_values] =
        LowerArgs(param_modes,
                  expr.args,
                  ctx,
                  param_types.empty() ? nullptr : &param_types);

    // Build argument list with receiver first
    std::vector<IRValue> all_args;
    all_args.push_back(recv_result.value);
    all_args.insert(all_args.end(), arg_values.begin(), arg_values.end());

    // Resolve method symbol
    std::string callee_sym = expr.name;
    if (ctx.sigma && ctx.expr_type) {
        const analysis::ScopeContext& sym_scope = ScopeForLowering(ctx);
        auto recv_type_for_sym = recv_type;
        if (!recv_type_for_sym) {
            recv_type_for_sym = ctx.expr_type(*expr.receiver);
        }
        if (recv_type_for_sym) {
            const auto stripped_for_sym = analysis::StripPerm(recv_type_for_sym);
            if (stripped_for_sym) {
                if (const auto builtin_sig =
                        analysis::LookupStringBytesBuiltinMethodSig(stripped_for_sym, expr.name)) {
                    (void)builtin_sig;
                    if (std::holds_alternative<analysis::TypeString>(stripped_for_sym->node)) {
                        if (const std::string builtin =
                                BuiltinSym(std::string("string::") + expr.name);
                            !builtin.empty()) {
                            callee_sym = builtin;
                        }
                    } else if (std::holds_alternative<analysis::TypeBytes>(stripped_for_sym->node)) {
                        if (const std::string builtin =
                                BuiltinSym(std::string("bytes::") + expr.name);
                            !builtin.empty()) {
                            callee_sym = builtin;
                        }
                    }
                }
            }
            if (auto sym = MethodSymbol(sym_scope, recv_type_for_sym, expr.name)) {
                callee_sym = *sym;
                if (ShouldTraceMethodCall(expr.name)) {
                    std::cerr << "[cursive] method-call-symbol"
                              << " name=" << expr.name
                              << " symbol=" << callee_sym
                              << " recv_type="
                              << analysis::TypeToString(recv_type_for_sym)
                              << "\n";
                }
            } else if (ShouldTraceMethodCall(expr.name)) {
                std::cerr << "[cursive] method-call-symbol-missing"
                          << " name=" << expr.name
                          << " recv_type="
                          << analysis::TypeToString(recv_type_for_sym)
                          << "\n";
            }
        }
    }

    IRValue result_value = ctx.FreshTempValue("method_call");
    analysis::TypeRef method_result_type = resolved_method_result_type;
    if (ctx.expr_type) {
        if (analysis::TypeRef result_type = ctx.expr_type(expr_wrapper)) {
            method_result_type = result_type;
        }
    }
    if (method_result_type) {
        ctx.RegisterValueType(result_value, method_result_type);
    }
    SyncRegionAliasForMethodResult(expr, method_result_type, ctx);

    if (callee_sym == BuiltinSymCancelTokenActiveIsCancelled()) {
        IRCancelCheck check;
        check.token = recv_result.value;
        check.result = result_value;
        return LowerResult{
            SeqIR({recv_key_ir, recv_result.ir, args_ir, MakeIR(std::move(check))}),
            result_value};
    }

    IRCall call;
    call.callee = IRValue{IRValue::Kind::Symbol, callee_sym, {}};
    call.args = std::move(all_args);
    call.result = result_value;

    bool needs_panic_out = ctx.NeedsPanicOutForSymbol(callee_sym);
    // Async::resume executes a generated resume function via the runtime.
    // The runtime forwards panic_out to that callback, so resume calls must
    // always receive a concrete panic record pointer.
    if (callee_sym == BuiltinSymAsyncResume()) {
        needs_panic_out = true;
    }

    if (needs_panic_out) {
        SPEC_RULE("Lower-MethodCall-Static-PanicOut");
        IRValue panic_out;
        panic_out.kind = IRValue::Kind::Local;
        panic_out.name = std::string(kPanicOutName);
        call.args.push_back(panic_out);
    } else {
        SPEC_RULE("Lower-MethodCall-Static-NoPanicOut");
    }

    if (needs_panic_out) {
        return LowerResult{
            SeqIR({recv_key_ir, recv_result.ir, args_ir, MakeIR(std::move(call)),
                   PanicFollowup(ctx)}),
            result_value};
    }

    return LowerResult{
        SeqIR({recv_key_ir, recv_result.ir, args_ir, MakeIR(std::move(call))}),
        result_value};
}

}  // namespace cursive::codegen
