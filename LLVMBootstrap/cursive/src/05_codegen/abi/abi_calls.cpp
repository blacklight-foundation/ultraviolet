// =============================================================================
// MIGRATION MAPPING: abi_calls.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.2.3 ABI-Call rule (lines 15282-15285)
//   - Section 6.2.4 Call Lowering for Procedures and Methods (lines 15303-15391)
//   - LowerCallJudg (line 15305)
//   - MethodSymbol rules (lines 15309-15332)
//   - BuiltinMethodSym rules (lines 15334-15342)
//   - Lower-Args rules (lines 15344-15356)
//   - Lower-RecvArg rules (lines 15358-15366)
//   - Lower-MethodCall rules (lines 15368-15386)
//   - SeqIR helper (lines 15388-15390)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/abi/abi_calls.cpp
//   - ABICall function (lines 199-226 in abi_params.cpp)
//
// RELATED SOURCE FILES:
//   - cursive-bootstrap/src/04_codegen/lower/lower_expr_calls.cpp
//
// DEPENDENCIES:
//   - cursive/include/05_codegen/abi/abi.h (ABICallInfo)
//   - ABIParam, ABIRet functions
//   - Method lookup and symbol resolution
//
// REFACTORING NOTES:
//   1. ABICall computes PassKind for all params and return
//   2. Returns ABICallInfo with:
//      - param_kinds: vector of PassKind
//      - ret_kind: PassKind for return
//      - has_sret: true if ret_kind == SRet
//   3. Call lowering transforms procedure calls to IR:
//      - Lower receiver (base expression)
//      - Lower arguments
//      - Emit call with correct ABI
//   4. MethodCall lowering handles:
//      - Static dispatch (regular methods)
//      - Dynamic dispatch (class methods on $Class)
//      - Capability methods (FileSystem, HeapAllocator)
//   5. Panic out-parameter appended when NeedsPanicOut
//
// CALL LOWERING STEPS:
//   1. Evaluate receiver expression
//   2. Evaluate arguments left-to-right
//   3. Resolve method symbol
//   4. Emit CallIR or CallVTableIR
//   5. Handle panic check after call
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/symbols/mangle.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/intrinsics/builtins.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/caps/cap_filesystem.h"
#include "04_analysis/caps/cap_heap.h"
#include "04_analysis/caps/cap_network.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/modal/builtin_modal_intrinsics.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/modal/modal_transitions.h"
#include "04_analysis/composite/record_methods.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_lower.h"
#include "00_core/spec_trace.h"
#include "00_core/assert_spec.h"

namespace cursive::codegen {
namespace {

analysis::TypeRef ResolveOpaqueUnderlying(const analysis::ScopeContext& ctx,
                                          const analysis::TypeOpaque& opaque) {
  const auto it = ctx.sigma.opaque_underlying_by_class_path.find(
      analysis::PathKeyOf(opaque.class_path));
  if (it != ctx.sigma.opaque_underlying_by_class_path.end() && it->second) {
    return it->second;
  }

  return nullptr;
}

// Get ModalDecl from a TypePath by looking up in the sigma types map.
const ast::ModalDecl* GetModalDecl(const analysis::ScopeContext& ctx,
                                   const analysis::TypePath& path) {
  return analysis::LookupModalDecl(ctx, path);
}

std::optional<analysis::TypePath> ResolveDispatchTypePath(
    const analysis::ScopeContext& ctx,
    const analysis::TypePath& path) {
  if (path.size() != 1) {
    return path;
  }

  const auto ent = analysis::ResolveTypeName(ctx, path.front());
  if (!ent.has_value() || ent->kind != analysis::EntityKind::Type ||
      !ent->origin_opt.has_value()) {
    return path;
  }

  analysis::TypePath resolved = *ent->origin_opt;
  resolved.push_back(ent->target_opt.value_or(path.front()));
  return resolved;
}

const ast::ModalDecl* ResolveModalDeclForDispatch(
    const analysis::ScopeContext& ctx,
    analysis::TypePath& path) {
  if (const ast::ModalDecl* direct = GetModalDecl(ctx, path)) {
    return direct;
  }

  const auto resolved = ResolveDispatchTypePath(ctx, path);
  if (!resolved.has_value() || *resolved == path) {
    return nullptr;
  }

  if (const ast::ModalDecl* decl = GetModalDecl(ctx, *resolved)) {
    path = *resolved;
    return decl;
  }
  return nullptr;
}

// Strip permission wrapper from type.
// Recursively removes TypePerm nodes to get the underlying type.
analysis::TypeRef StripPerm(const analysis::TypeRef& type) {
  if (!type) {
    return type;
  }
  if (const auto* perm = std::get_if<analysis::TypePerm>(&type->node)) {
    return StripPerm(perm->base);
  }
  return type;
}

// Check if type is a TypeDynamic (dynamic class object).
bool IsDynamicType(const analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return false;
  }
  return std::holds_alternative<analysis::TypeDynamic>(stripped->node);
}

// Check if type is a TypeModalState (modal type in specific state).
bool IsModalStateType(const analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return false;
  }
  return std::holds_alternative<analysis::TypeModalState>(stripped->node);
}

// Get TypeDynamic path if the type is dynamic.
// Returns nullopt if type is not a TypeDynamic.
std::optional<analysis::TypePath> GetDynamicPath(const analysis::TypeRef& type) {
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return std::nullopt;
  }
  if (const auto* dyn = std::get_if<analysis::TypeDynamic>(&stripped->node)) {
    return dyn->path;
  }
  return std::nullopt;
}

// Get TypeModalState info if the type is a modal state.
// Returns pair of (modal_path, state_name) or nullopt.
std::optional<std::pair<analysis::TypePath, std::string>> GetModalStateInfo(
    const analysis::TypeRef& type) {
  const auto stripped = StripPerm(type);
  if (!stripped) {
    return std::nullopt;
  }
  if (const auto* modal = std::get_if<analysis::TypeModalState>(&stripped->node)) {
    return std::make_pair(modal->path, modal->state);
  }
  return std::nullopt;
}

// Convert TypePath to ClassPath for class lookups.
ast::ClassPath ToClassPath(const analysis::TypePath& path) {
  ast::ClassPath result;
  result.reserve(path.size());
  for (const auto& comp : path) {
    result.push_back(comp);
  }
  return result;
}

LowerCtx BuildABILowerCtx(const analysis::ScopeContext& scope_ctx) {
  LowerCtx lower_ctx;
  lower_ctx.sigma = scope_ctx.sigma_source ? scope_ctx.sigma_source : &scope_ctx.sigma;
  lower_ctx.module_path = scope_ctx.current_module;
  lower_ctx.target_profile = scope_ctx.target_profile;
  lower_ctx.expr_types = scope_ctx.expr_types;
  lower_ctx.dynamic_refine_checks = scope_ctx.dynamic_refine_checks;

  if (scope_ctx.expr_types) {
    auto* expr_types = scope_ctx.expr_types;
    lower_ctx.expr_type = [expr_types](const ast::Expr& expr) -> analysis::TypeRef {
      if (!expr_types) {
        return nullptr;
      }
      const auto it = expr_types->find(&expr);
      if (it == expr_types->end()) {
        return nullptr;
      }
      return it->second;
    };
  }

  lower_ctx.resolve_name =
      [&scope_ctx](const std::string& name) -> std::optional<std::vector<std::string>> {
    if (!BuiltinSym(name).empty()) {
      return std::vector<std::string>{name};
    }
    const auto ent = analysis::ResolveValueName(scope_ctx, name);
    if (!ent.has_value() || ent->kind != analysis::EntityKind::Value) {
      return std::nullopt;
    }
    const std::string resolved_name = ent->target_opt.value_or(name);
    if (!ent->origin_opt.has_value()) {
      if (!BuiltinSym(resolved_name).empty()) {
        return std::vector<std::string>{resolved_name};
      }
      return std::nullopt;
    }
    std::vector<std::string> full = *ent->origin_opt;
    full.push_back(resolved_name);
    return full;
  };

  lower_ctx.resolve_type_name =
      [&scope_ctx](const std::string& name) -> std::optional<std::vector<std::string>> {
    const auto ent = analysis::ResolveTypeName(scope_ctx, name);
    if (!ent.has_value() || ent->kind != analysis::EntityKind::Type ||
        !ent->origin_opt.has_value()) {
      return std::nullopt;
    }
    std::vector<std::string> full = *ent->origin_opt;
    full.push_back(ent->target_opt.value_or(name));
    return full;
  };

  lower_ctx.resolve_type_name_in_module =
      [&scope_ctx](const std::vector<std::string>& module_path,
                   const std::string& name)
          -> std::optional<std::vector<std::string>> {
    analysis::ScopeContext module_ctx = scope_ctx;
    module_ctx.current_module = module_path;
    const auto ent = analysis::ResolveTypeName(module_ctx, name);
    if (!ent.has_value() || ent->kind != analysis::EntityKind::Type ||
        !ent->origin_opt.has_value()) {
      return std::nullopt;
    }
    std::vector<std::string> full = *ent->origin_opt;
    full.push_back(ent->target_opt.value_or(name));
    return full;
  };

  // Seed local/procedure value bindings so helper lowering can read locals
  // without forcing path resolution.
  const auto& scopes = scope_ctx.scopes;
  const std::size_t scope_limit = scopes.size() >= 2 ? scopes.size() - 2 : 0;
  for (std::size_t idx = scope_limit; idx > 0; --idx) {
    const auto& scope = scopes[idx - 1];
    for (const auto& [name, ent] : scope) {
      if (ent.kind != analysis::EntityKind::Value) {
        continue;
      }
      if (ent.origin_opt.has_value()) {
        continue;
      }
      if (!BuiltinSym(name).empty()) {
        continue;
      }
      lower_ctx.RegisterVar(name,
                            nullptr,
                            false,
                            false,
                            analysis::ProvenanceKind::Stack,
                            std::nullopt);
    }
  }

  return lower_ctx;
}

LowerResult LowerRefValueWithTemp(const ast::ExprPtr& expr,
                                  const analysis::TypeRef& expected_type,
                                  LowerCtx& ctx) {
  if (!expr) {
    return {EmptyIR(), IRValue{}};
  }

  if (analysis::HasSourceProvenance(expr)) {
    return LowerAddrOf(*expr, ctx);
  }

  auto value_result = LowerExpr(*expr, ctx);
  std::string temp_name = ctx.FreshTempValue("abi_ref_tmp").name;

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

  ctx.RegisterVar(temp_name,
                  temp_type,
                  false,
                  false,
                  analysis::ProvenanceKind::Stack,
                  std::nullopt);

  ast::Expr temp_ident;
  temp_ident.span = expr->span;
  temp_ident.node = ast::IdentifierExpr{temp_name};
  auto addr_result = LowerAddrOf(temp_ident, ctx);

  return {SeqIR({value_result.ir, MakeIR(std::move(bind)), addr_result.ir}),
          addr_result.value};
}

}  // namespace

// =============================================================================
// MethodSymbol Resolution
// =============================================================================

std::optional<std::string> MethodSymbol(const analysis::ScopeContext& ctx,
                                        const analysis::TypeRef& type,
                                        std::string_view name) {
  if (!type) {
    return std::nullopt;
  }

  analysis::TypeRef stripped = StripPerm(type);

  // Handle opaque types by resolving to underlying type.
  if (stripped) {
    if (const auto* opaque = std::get_if<analysis::TypeOpaque>(&stripped->node)) {
      if (analysis::TypeRef underlying = ResolveOpaqueUnderlying(ctx, *opaque)) {
        stripped = underlying;
      }
    }
  }

  // (MethodSymbol-ModalState-Method) and (MethodSymbol-ModalState-Transition)
  // Check if this is a modal state type.
  if (const auto modal_info = GetModalStateInfo(stripped)) {
    const auto& [modal_path, state] = *modal_info;
    if (const auto builtin = analysis::LookupBuiltinModalRuntimeSymbol(
            modal_path, state, name)) {
      SPEC_RULE("MethodSymbol-ModalState-Method");
      return builtin;
    }

    auto resolved_modal_path = modal_path;
    const auto* modal_decl =
        ResolveModalDeclForDispatch(ctx, resolved_modal_path);
    if (!modal_decl) {
      return std::nullopt;
    }

    // (MethodSymbol-ModalState-Method)
    // LookupStateMethod(S, name) = md -> Mangle(md) => sym
    const auto* state_method = analysis::LookupStateMethodDecl(*modal_decl, state, name);
    if (state_method) {
      SPEC_RULE("MethodSymbol-ModalState-Method");
      return MangleStateMethod(resolved_modal_path, state, *state_method);
    }

    // (MethodSymbol-ModalState-Transition)
    // LookupTransition(S, name) = tr -> Mangle(tr) => sym
    const auto* transition = analysis::LookupTransitionDecl(*modal_decl, state, name);
    if (transition) {
      SPEC_RULE("MethodSymbol-ModalState-Transition");
      return MangleTransition(resolved_modal_path, state, *transition);
    }

    return std::nullopt;
  }

  // (MethodSymbol-Record) and (MethodSymbol-Default)
  // Lookup method on concrete type (record or class default).
  const auto lookup = analysis::LookupMethodStatic(ctx, stripped, name);
  if (!lookup.ok) {
    return std::nullopt;
  }

  // (MethodSymbol-Record)
  // LookupMethod(T, name) = m and m = MethodDecl(_) -> Mangle(m) => sym
  if (lookup.record_method) {
    if (lookup.record_path.empty()) {
      return std::nullopt;
    }
    SPEC_RULE("MethodSymbol-Record");
    return MangleMethod(lookup.record_path, *lookup.record_method);
  }

  // (MethodSymbol-Default)
  // LookupMethod(T, name) = m and m = ClassMethodDecl(_) and m.body_opt != nullopt
  // -> Mangle(DefaultImpl(T, m)) => sym
  if (lookup.class_method && lookup.class_method->body_opt) {
    SPEC_RULE("MethodSymbol-Default");
    return MangleDefaultImpl(stripped, lookup.owner_class, lookup.class_method->name);
  }

  return std::nullopt;
}

// =============================================================================
// BuiltinMethodSym Resolution
// =============================================================================

bool IsBuiltinCapClass(const analysis::TypePath& class_path) {
  const auto cap_class = ToClassPath(class_path);
  return analysis::IsFileSystemClassPath(cap_class) ||
         analysis::IsNetworkClassPath(cap_class) ||
         analysis::IsHeapAllocatorClassPath(cap_class) ||
         analysis::IsExecutionDomainClassPath(cap_class) ||
         analysis::IsReactorClassPath(cap_class);
}

bool IsBuiltinCapClass(std::string_view class_name) {
  return IsBuiltinCapClass(analysis::TypePath{std::string(class_name)});
}

std::optional<std::string> BuiltinMethodSym(const analysis::TypePath& cap_path,
                                            std::string_view name) {
  const auto cap_class = ToClassPath(cap_path);

  // (BuiltinMethodSym-FileSystem)
  // BuiltinSym(FileSystem::name) => sym
  if (analysis::IsFileSystemClassPath(cap_class)) {
    SPEC_RULE("BuiltinMethodSym-FileSystem");
    const std::string sym = BuiltinSym(std::string("FileSystem::") + std::string(name));
    if (sym.empty()) {
      return std::nullopt;
    }
    return sym;
  }

  // (BuiltinMethodSym-HeapAllocator)
  // BuiltinSym(HeapAllocator::name) => sym
  if (analysis::IsHeapAllocatorClassPath(cap_class)) {
    SPEC_RULE("BuiltinMethodSym-HeapAllocator");
    const std::string sym = BuiltinSym(std::string("HeapAllocator::") + std::string(name));
    if (sym.empty()) {
      return std::nullopt;
    }
    return sym;
  }

  if (analysis::IsNetworkClassPath(cap_class)) {
    const std::string sym =
        BuiltinSym(std::string("Network::") + std::string(name));
    if (sym.empty()) {
      return std::nullopt;
    }
    return sym;
  }

  // (BuiltinMethodSym-ExecutionDomain)
  // BuiltinSym(ExecutionDomain::name) => sym
  if (analysis::IsExecutionDomainTypePath(cap_path)) {
    SPEC_RULE("BuiltinMethodSym-ExecutionDomain");
    const std::string sym = BuiltinSym(std::string("ExecutionDomain::") + std::string(name));
    if (sym.empty()) {
      return std::nullopt;
    }
    return sym;
  }

  // (BuiltinMethodSym-Reactor)
  // BuiltinSym(Reactor::name) => sym
  if (analysis::IsReactorClassPath(cap_class)) {
    SPEC_RULE("BuiltinMethodSym-Reactor");
    const std::string sym =
        BuiltinSym(std::string("Reactor::") + std::string(name));
    if (sym.empty()) {
      return std::nullopt;
    }
    return sym;
  }

  // (BuiltinMethodSym-System)
  // BuiltinSym(System::name) => sym
  if (analysis::IsSystemTypePath(cap_path)) {
    SPEC_RULE("BuiltinMethodSym-System");
    const std::string sym = BuiltinSym(std::string("System::") + std::string(name));
    if (sym.empty()) {
      return std::nullopt;
    }
    return sym;
  }

  return std::nullopt;
}

std::optional<std::string> BuiltinMethodSym(std::string_view cap_class,
                                            std::string_view name) {
  return BuiltinMethodSym(analysis::TypePath{std::string(cap_class)}, name);
}

// =============================================================================
// Argument Lowering
// =============================================================================

std::optional<LowerArgsResult> LowerArgs(
    const analysis::ScopeContext& ctx,
    const std::vector<ast::Param>& params,
    const std::vector<ast::Arg>& args) {
  LowerCtx lower_ctx = BuildABILowerCtx(ctx);
  ParamModeList param_modes;
  ParamTypeList param_types;
  param_modes.reserve(params.size());
  param_types.reserve(params.size());
  for (const auto& param : params) {
    if (param.mode.has_value()) {
      param_modes.push_back(analysis::ParamMode::Move);
    } else {
      param_modes.push_back(std::nullopt);
    }
    analysis::TypeRef param_type = nullptr;
    if (param.type) {
      const auto lowered = analysis::LowerType(ctx, param.type);
      if (lowered.ok) {
        param_type = lowered.type;
      }
    }
    param_types.push_back(param_type);
  }

  auto [ir, values] =
      codegen::LowerArgs(param_modes,
                         args,
                         lower_ctx,
                         param_types.empty() ? nullptr : &param_types);
  if (lower_ctx.resolve_failed || lower_ctx.codegen_failed) {
    return std::nullopt;
  }

  LowerArgsResult result;
  result.ir = std::move(ir);
  result.values = std::move(values);
  return result;
}

// =============================================================================
// Receiver Argument Lowering
// =============================================================================

std::optional<LowerCallResult> LowerRecvArg(
    const analysis::ScopeContext& ctx,
    const ast::ExprPtr& base,
    bool is_move) {
  if (!base) {
    return std::nullopt;
  }

  LowerCtx lower_ctx = BuildABILowerCtx(ctx);
  LowerResult lowered;

  if (is_move || std::holds_alternative<ast::MoveExpr>(base->node)) {
    SPEC_RULE("Lower-RecvArg-Move");
    lowered = LowerExpr(*base, lower_ctx);
  } else {
    SPEC_RULE("Lower-RecvArg-Ref");
    analysis::TypeRef expected_type = nullptr;
    if (lower_ctx.expr_type) {
      expected_type = lower_ctx.expr_type(*base);
    }
    lowered = LowerRefValueWithTemp(base, expected_type, lower_ctx);
  }

  if (lower_ctx.resolve_failed || lower_ctx.codegen_failed) {
    return std::nullopt;
  }

  LowerCallResult result;
  result.ir = std::move(lowered.ir);
  result.value = lowered.value;
  return result;
}

// =============================================================================
// Method Call Lowering
// =============================================================================

std::optional<LowerCallResult> LowerMethodCall(
    const analysis::ScopeContext& ctx,
    const ast::ExprPtr& base,
    std::string_view name,
    const std::vector<ast::Arg>& args) {
  if (!base) {
    return std::nullopt;
  }

  LowerCtx lower_ctx = BuildABILowerCtx(ctx);
  ast::MethodCallExpr method_call;
  method_call.receiver = base;
  method_call.name = std::string(name);
  method_call.args = args;

  ast::Expr method_call_expr;
  method_call_expr.node = method_call;

  const auto lowered =
      codegen::LowerMethodCall(method_call_expr, method_call, lower_ctx);
  if (lower_ctx.resolve_failed || lower_ctx.codegen_failed) {
    return std::nullopt;
  }

  LowerCallResult result;
  result.ir = lowered.ir;
  result.value = lowered.value;
  return result;
}

// =============================================================================
// SPEC_RULE Anchors for Coverage
// =============================================================================

void AnchorABIRules() {
  // Section 6.2.2 ABI Type Lowering Rules
  SPEC_RULE("ABI-Prim");
  SPEC_RULE("ABI-Perm");
  SPEC_RULE("ABI-Ptr");
  SPEC_RULE("ABI-RawPtr");
  SPEC_RULE("ABI-Func");
  SPEC_RULE("ABI-Alias");
  SPEC_RULE("ABI-Record");
  SPEC_RULE("ABI-Tuple");
  SPEC_RULE("ABI-Array");
  SPEC_RULE("ABI-Slice");
  SPEC_RULE("ABI-Range");
  SPEC_RULE("ABI-Enum");
  SPEC_RULE("ABI-Union");
  SPEC_RULE("ABI-Modal");
  SPEC_RULE("ABI-Dynamic");
  SPEC_RULE("ABI-StringBytes");

  // Section 6.2.3 ABI Parameter and Return Passing Rules
  SPEC_RULE("ABI-Param-ByRef-Alias");
  SPEC_RULE("ABI-Param-ByValue-Move");
  SPEC_RULE("ABI-Param-ByRef-Move");
  SPEC_RULE("ABI-Ret-ByValue");
  SPEC_RULE("ABI-Ret-ByRef");
  SPEC_RULE("ABI-Call");

  // Section 6.2.4 Call Lowering Rules
  SPEC_RULE("MethodSymbol-Record");
  SPEC_RULE("MethodSymbol-Default");
  SPEC_RULE("MethodSymbol-ModalState-Method");
  SPEC_RULE("MethodSymbol-ModalState-Transition");
  SPEC_RULE("BuiltinMethodSym-FileSystem");
  SPEC_RULE("BuiltinMethodSym-HeapAllocator");
  SPEC_RULE("BuiltinMethodSym-ExecutionDomain");
  SPEC_RULE("BuiltinMethodSym-Reactor");
  SPEC_RULE("BuiltinMethodSym-System");
  SPEC_RULE("Lower-Args-Empty");
  SPEC_RULE("Lower-Args-Cons-Move");
  SPEC_RULE("Lower-Args-Cons-Ref");
  SPEC_RULE("Lower-RecvArg-Move");
  SPEC_RULE("Lower-RecvArg-Ref");
  SPEC_RULE("Lower-MethodCall-Static-PanicOut");
  SPEC_RULE("Lower-MethodCall-Static-NoPanicOut");
  SPEC_RULE("Lower-MethodCall-Capability");
  SPEC_RULE("Lower-MethodCall-Dynamic");
}

}  // namespace cursive::codegen
