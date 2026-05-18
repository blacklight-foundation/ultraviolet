// =============================================================================
// Expression Lowering Context Infrastructure
// =============================================================================

#include "05_codegen/lower/expr/expr_common.h"

#include <algorithm>
#include <iostream>

#include "00_core/assert_spec.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/resolve_items.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/intrinsics/intrinsics_interface.h"

namespace ultraviolet::codegen {

namespace {

analysis::Permission PermissionOfType(const analysis::TypeRef& type) {
  if (!type) {
    return analysis::Permission::Const;
  }
  if (const auto* perm = std::get_if<analysis::TypePerm>(&type->node)) {
    return perm->perm;
  }
  return analysis::Permission::Const;
}

}  // namespace

const analysis::ScopeContext& ScopeForLowering(const LowerCtx& ctx) {
  static const analysis::ScopeContext kEmptyScope{};

  struct ScopeCache {
    const analysis::Sigma* sigma = nullptr;
    analysis::ExprTypeMap* expr_types = nullptr;
    analysis::DynamicRefineExprMap* dynamic_refine_checks = nullptr;
    analysis::GenericCallSubstMap* generic_call_substs = nullptr;
    analysis::SelectedCallTargetMap* selected_call_targets = nullptr;
    std::vector<std::string> module_path;
    std::optional<project::TargetProfile> target_profile;
    analysis::NameMapTable name_maps;
    analysis::ScopeContext scope;
  };

  thread_local ScopeCache cache;
  if (!ctx.sigma) {
    return kEmptyScope;
  }

  const bool sigma_changed = cache.sigma != ctx.sigma;
  if (sigma_changed || cache.expr_types != ctx.expr_types ||
      cache.dynamic_refine_checks != ctx.dynamic_refine_checks ||
      cache.generic_call_substs != ctx.generic_call_substs ||
      cache.selected_call_targets != ctx.selected_call_targets ||
      cache.module_path != ctx.module_path ||
      cache.target_profile != ctx.target_profile) {
    cache.sigma = ctx.sigma;
    cache.expr_types = ctx.expr_types;
    cache.dynamic_refine_checks = ctx.dynamic_refine_checks;
    cache.generic_call_substs = ctx.generic_call_substs;
    cache.selected_call_targets = ctx.selected_call_targets;
    cache.module_path = ctx.module_path;
    cache.target_profile = ctx.target_profile;
    cache.scope = analysis::ScopeContext{};
    cache.scope.sigma = *ctx.sigma;
    cache.scope.sigma_source = ctx.sigma;
    cache.scope.current_module = ctx.module_path;
    cache.scope.target_profile = ctx.target_profile;
    cache.scope.expr_types = ctx.expr_types;
    cache.scope.dynamic_refine_checks = ctx.dynamic_refine_checks;
    cache.scope.generic_call_substs = ctx.generic_call_substs;
    cache.scope.selected_call_targets = ctx.selected_call_targets;

    if (sigma_changed || cache.name_maps.empty()) {
      analysis::ScopeContext name_ctx = cache.scope;
      cache.name_maps = analysis::CollectNameMaps(name_ctx).name_maps;
    }

    analysis::Scope module_scope;
    const auto module_key = analysis::PathKeyOf(cache.scope.current_module);
    const auto module_scope_it = cache.name_maps.find(module_key);
    if (module_scope_it != cache.name_maps.end()) {
      module_scope = module_scope_it->second;
    }
    cache.scope.scopes = analysis::ScopeList{
        analysis::Scope{},
        std::move(module_scope),
        analysis::UniverseBindings()};
  }

  return cache.scope;
}

const analysis::ScopeContext& ScopeForLowering(const LowerCtx* ctx) {
  static const analysis::ScopeContext kEmptyScope{};
  if (!ctx) {
    return kEmptyScope;
  }
  return ScopeForLowering(*ctx);
}

IRValue USizeConstValue(std::uint64_t value) {
  IRValue v;
  v.kind = IRValue::Kind::Immediate;
  v.name = std::to_string(value);
  v.bytes.reserve(8);
  for (std::size_t i = 0; i < 8; ++i) {
    v.bytes.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
  }
  return v;
}

IRPtr EmitRuntimeScopeEnter(std::uint64_t scope_id, LowerCtx& ctx) {
  IRCall call;
  call.callee.kind = IRValue::Kind::Symbol;
  call.callee.name = BuiltinModalSymRegionScopeEnter();
  call.args.push_back(USizeConstValue(scope_id));
  IRValue result = ctx.FreshTempValue("scope_enter");
  call.result = result;
  ctx.RegisterValueType(result, analysis::MakeTypePrim("()"));
  return MakeIR(std::move(call));
}

void LowerCtx::PushScope(bool is_loop, bool is_region) {
  ScopeInfo scope;
  scope.is_loop = is_loop;
  scope.is_region = is_region;
  if (!next_runtime_scope_id) {
    next_runtime_scope_id = std::make_shared<std::uint64_t>(1);
  }
  scope.runtime_scope_id = (*next_runtime_scope_id)++;
  scope_stack.push_back(std::move(scope));
}

void LowerCtx::PopScope() {
  if (scope_stack.empty()) {
    return;
  }

  const auto exiting_scope_id = scope_stack.back().runtime_scope_id;
  const auto& aliases = scope_stack.back().aliases;
  for (auto it = aliases.rbegin(); it != aliases.rend(); ++it) {
    auto map_it = local_addr_aliases.find(*it);
    if (map_it == local_addr_aliases.end()) {
      continue;
    }
    if (!map_it->second.empty()) {
      map_it->second.pop_back();
    }
    if (map_it->second.empty()) {
      local_addr_aliases.erase(map_it);
    }
  }

  const auto& vars = scope_stack.back().variables;
  for (auto it = vars.rbegin(); it != vars.rend(); ++it) {
    auto map_it = binding_states.find(*it);
    if (map_it == binding_states.end()) {
      continue;
    }
    if (!map_it->second.empty()) {
      map_it->second.pop_back();
    }
    if (map_it->second.empty()) {
      binding_states.erase(map_it);
    }
  }

  active_key_scopes.erase(
      std::remove_if(active_key_scopes.begin(), active_key_scopes.end(),
                     [&](const ActiveKeyScopeInfo& scope) {
                       return scope.scope_runtime_id == exiting_scope_id;
                     }),
      active_key_scopes.end());
  implicit_key_scope_names.erase(exiting_scope_id);

  scope_stack.pop_back();
}

std::vector<std::string> LowerCtx::CurrentScopeVars() const {
  if (scope_stack.empty()) {
    return {};
  }
  return scope_stack.back().variables;
}

std::vector<std::string> LowerCtx::VarsToLoopScope() const {
  std::vector<std::string> vars;

  for (auto it = scope_stack.rbegin(); it != scope_stack.rend(); ++it) {
    for (const auto& var : it->variables) {
      vars.push_back(var);
    }
    if (it->is_loop) {
      break;
    }
  }

  return vars;
}

std::vector<std::string> LowerCtx::VarsToFunctionRoot() const {
  std::vector<std::string> vars;

  for (auto it = scope_stack.rbegin(); it != scope_stack.rend(); ++it) {
    for (const auto& var : it->variables) {
      vars.push_back(var);
    }
  }

  return vars;
}

void LowerCtx::RegisterVar(const std::string& name,
                           analysis::TypeRef type,
                           bool has_responsibility,
                           bool is_immovable,
                           analysis::ProvenanceKind prov,
                           std::optional<std::string> prov_region,
                           bool preserve_addr_provenance,
                           std::optional<std::string> prov_region_tag) {
  if (!scope_stack.empty()) {
    scope_stack.back().variables.push_back(name);
    if (has_responsibility) {
      CleanupItem item;
      item.kind = CleanupItem::Kind::DropBinding;
      item.name = name;
      scope_stack.back().cleanup_items.push_back(std::move(item));
    }
  }

  BindingState state;
  state.type = type;
  state.binding_id = next_binding_id++;
  state.stable_name =
      "__bind_" + std::to_string(state.binding_id) + "_" + name;
  state.has_responsibility = has_responsibility;
  state.is_immovable = is_immovable;
  state.is_moved = false;
  state.prov = prov;
  state.prov_region = std::move(prov_region);
  state.prov_region_tag = std::move(prov_region_tag);
  state.preserve_addr_provenance = preserve_addr_provenance;
  if (!scope_stack.empty()) {
    state.scope_runtime_id = scope_stack.back().runtime_scope_id;
  }

  binding_states[name].push_back(std::move(state));
}

void LowerCtx::RegisterRuntimeScopeExit() {
  if (scope_stack.empty()) {
    return;
  }
  if (scope_stack.back().runtime_scope_exit_registered) {
    return;
  }
  CleanupItem item;
  item.kind = CleanupItem::Kind::RuntimeScopeExit;
  item.scope_runtime_id = scope_stack.back().runtime_scope_id;
  scope_stack.back().cleanup_items.push_back(std::move(item));
  scope_stack.back().runtime_scope_exit_registered = true;
}

void LowerCtx::RequireRuntimeScope(std::uint64_t scope_id) {
  if (scope_id == 0) {
    return;
  }
  if (!runtime_scope_materialization) {
    runtime_scope_materialization = std::make_shared<
        std::unordered_map<std::uint64_t, RuntimeScopeMaterialization>>();
  }
  (*runtime_scope_materialization)[scope_id].required = true;
}

void LowerCtx::RequireCurrentRuntimeScope() {
  if (const auto scope_id = CurrentRuntimeScopeId()) {
    RequireRuntimeScope(*scope_id);
  }
}

bool LowerCtx::ScopeRequiresRuntime(std::uint64_t scope_id) const {
  if (scope_id == 0 || !runtime_scope_materialization) {
    return false;
  }
  const auto it = runtime_scope_materialization->find(scope_id);
  return it != runtime_scope_materialization->end() && it->second.required;
}

bool LowerCtx::CurrentScopeRequiresRuntime() const {
  if (scope_stack.empty()) {
    return false;
  }
  return ScopeRequiresRuntime(scope_stack.back().runtime_scope_id);
}

void LowerCtx::RegisterRuntimeScopeExitIfRequired() {
  if (!CurrentScopeRequiresRuntime()) {
    return;
  }
  RegisterRuntimeScopeExit();
}

std::optional<std::uint64_t> LowerCtx::CurrentRuntimeScopeId() const {
  if (scope_stack.empty()) {
    return std::nullopt;
  }
  return scope_stack.back().runtime_scope_id;
}

void LowerCtx::MarkMoved(const std::string& name) {
  auto it = binding_states.find(name);
  if (it == binding_states.end() || it->second.empty()) {
    for (auto& [source_name, states] : binding_states) {
      if (states.empty()) {
        continue;
      }
      BindingState& state = states.back();
      if (state.stable_name == name) {
        SPEC_RULE("UpdateValid-MoveRoot");
        state.is_moved = true;
        return;
      }
    }
    SPEC_RULE("UpdateValid-Err");
    ReportCodegenFailure();
    return;
  }
  SPEC_RULE("UpdateValid-MoveRoot");
  it->second.back().is_moved = true;
}

void LowerCtx::MarkMoved(const std::vector<std::string>& names) {
  SPEC_RULE("MarkMoved");
  for (const auto& name : names) {
    auto it = binding_states.find(name);
    if (it != binding_states.end() && !it->second.empty()) {
      MarkMoved(name);
      continue;
    }
    if (LookupCapture(name) != nullptr) {
      continue;
    }
    MarkMoved(name);
  }
}

void LowerCtx::MarkFieldMoved(const std::string& name, const std::string& field) {
  auto it = binding_states.find(name);
  if (it == binding_states.end() || it->second.empty()) {
    SPEC_RULE("UpdateValid-Err");
    ReportCodegenFailure();
    return;
  }
  SPEC_RULE("UpdateValid-PartialMove-Init");
  SPEC_RULE("UpdateValid-PartialMove-Step");
  it->second.back().moved_fields.push_back(field);
}

const BindingState* LowerCtx::GetBindingState(const std::string& name) const {
  auto it = binding_states.find(name);
  if (it != binding_states.end() && !it->second.empty()) {
    return &it->second.back();
  }
  for (const auto& [source_name, states] : binding_states) {
    (void)source_name;
    if (states.empty()) {
      continue;
    }
    const BindingState& state = states.back();
    if (state.stable_name == name) {
      return &state;
    }
  }
  return nullptr;
}

const BindingState* LowerCtx::GetBindingStateById(
    const std::string& name,
    std::uint64_t binding_id) const {
  auto it = binding_states.find(name);
  if (it == binding_states.end()) {
    return nullptr;
  }
  for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
    if (rit->binding_id == binding_id) {
      return &*rit;
    }
  }
  return nullptr;
}

std::string LowerCtx::StableBindingName(const std::string& name) const {
  if (const BindingState* state = GetBindingState(name)) {
    if (!state->stable_name.empty()) {
      return state->stable_name;
    }
  }
  return name;
}

void LowerCtx::RegisterLocalAddrAlias(const std::string& alias,
                                      const std::string& source_name) {
  LocalAddrAlias state;
  if (auto alias_target = LookupLocalAddrAlias(source_name)) {
    state = *alias_target;
  } else if (const BindingState* binding = GetBindingState(source_name)) {
    state.kind = LocalAddrAlias::Kind::Binding;
    state.binding_name = source_name;
    state.binding_id = binding->binding_id;
    state.stable_name = binding->stable_name;
  } else if (LookupCapture(source_name)) {
    state.kind = LocalAddrAlias::Kind::Capture;
    state.capture_name = source_name;
  } else if (resolve_name) {
    auto resolved = resolve_name(source_name);
    if (resolved.has_value() && !resolved->empty()) {
      state.kind = LocalAddrAlias::Kind::Static;
      state.static_name = resolved->back();
      state.static_path = *resolved;
      state.static_path.pop_back();
    } else {
      ReportResolveFailure(source_name);
      ReportCodegenFailure();
      return;
    }
  } else {
    ReportResolveFailure(source_name);
    ReportCodegenFailure();
    return;
  }

  if (!scope_stack.empty()) {
    scope_stack.back().aliases.push_back(alias);
  }
  local_addr_aliases[alias].push_back(std::move(state));
}

std::optional<LocalAddrAlias> LowerCtx::LookupLocalAddrAlias(
    const std::string& alias) const {
  auto it = local_addr_aliases.find(alias);
  if (it == local_addr_aliases.end() || it->second.empty()) {
    return std::nullopt;
  }
  return it->second.back();
}

std::optional<analysis::ProvenanceKind> LowerCtx::LookupExprProv(
    const ast::Expr& expr) const {
  if (!expr_prov) {
    return std::nullopt;
  }
  const auto it = expr_prov->find(&expr);
  if (it == expr_prov->end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::string> LowerCtx::LookupExprRegion(
    const ast::Expr& expr) const {
  if (!expr_region) {
    return std::nullopt;
  }
  const auto it = expr_region->find(&expr);
  if (it == expr_region->end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::string> LowerCtx::LookupExprRegionTag(
    const ast::Expr& expr) const {
  if (!expr_region_tags) {
    return std::nullopt;
  }
  const auto it = expr_region_tags->find(&expr);
  if (it == expr_region_tags->end()) {
    return std::nullopt;
  }
  return it->second;
}

const std::vector<analysis::TypeRef>* LowerCtx::LookupDynamicRefinementTypes(
    const ast::Expr& expr) const {
  if (!dynamic_refine_checks) {
    return nullptr;
  }
  const auto it = dynamic_refine_checks->find(&expr);
  if (it == dynamic_refine_checks->end()) {
    return nullptr;
  }
  return &it->second;
}

void LowerCtx::RegisterDefer(const IRPtr& defer_ir) {
  if (!scope_stack.empty()) {
    CleanupItem item;
    item.kind = CleanupItem::Kind::DeferBlock;
    item.defer_ir = defer_ir;
    scope_stack.back().cleanup_items.push_back(std::move(item));
  }
}

void LowerCtx::RegisterRegionRelease(const std::string& name) {
  if (!scope_stack.empty()) {
    CleanupItem item;
    item.kind = CleanupItem::Kind::ReleaseRegion;
    item.name = name;
    scope_stack.back().cleanup_items.push_back(std::move(item));
  }
}

void LowerCtx::RegisterKeyScopeExit(const std::string& scope_name) {
  if (!scope_stack.empty()) {
    CleanupItem item;
    item.kind = CleanupItem::Kind::ReleaseKeyScope;
    item.name = scope_name;
    scope_stack.back().cleanup_items.push_back(std::move(item));
  }
}

void LowerCtx::RegisterReleasedKeyReacquire(const std::string& handle_name) {
  if (!scope_stack.empty()) {
    CleanupItem item;
    item.kind = CleanupItem::Kind::ReacquireReleasedKey;
    item.name = handle_name;
    scope_stack.back().cleanup_items.push_back(std::move(item));
  }
}

void LowerCtx::RegisterParallelJoin(const IRValue& parallel_ctx) {
  if (scope_stack.empty() || parallel_ctx.name.empty()) {
    return;
  }
  CleanupItem item;
  item.kind = CleanupItem::Kind::ParallelJoin;
  item.name = parallel_ctx.name;
  scope_stack.back().cleanup_items.push_back(std::move(item));
}

void LowerCtx::RegisterTempValue(const IRValue& value, const analysis::TypeRef& type) {
  if (!temp_sink) {
    return;
  }
  TempValue temp;
  temp.value = value;
  temp.type = type;
  temp_sink->push_back(std::move(temp));
}

void LowerCtx::RegisterDerivedValue(const IRValue& value, const DerivedValueInfo& info) {
  auto derived_key = [](const IRValue& v) -> std::string {
    if (v.kind == IRValue::Kind::Opaque) {
      return "o:" + v.name;
    }
    if (v.kind == IRValue::Kind::Local) {
      return "l:" + v.name;
    }
    return "";
  };
  const std::string key = derived_key(value);
  if (key.empty()) {
    return;
  }
  values.derived_values[key] = info;
}

const DerivedValueInfo* LowerCtx::LookupDerivedValue(const IRValue& value) const {
  auto derived_key = [](const IRValue& v) -> std::string {
    if (v.kind == IRValue::Kind::Opaque) {
      return "o:" + v.name;
    }
    if (v.kind == IRValue::Kind::Local) {
      return "l:" + v.name;
    }
    return "";
  };
  const std::string key = derived_key(value);
  if (key.empty()) {
    return nullptr;
  }
  auto it = values.derived_values.find(key);
  if (it == values.derived_values.end()) {
    if (values.parent != nullptr) {
      return values.parent->LookupDerivedValue(value);
    }
    return nullptr;
  }
  return &it->second;
}

const CaptureAccess* LowerCtx::LookupCapture(const std::string& name) const {
  if (!capture_env.has_value()) {
    return nullptr;
  }
  auto it = capture_env->captures.find(name);
  if (it == capture_env->captures.end()) {
    return nullptr;
  }
  return &it->second;
}

CaptureEnvInfo LowerCtx::LoadEnv(
    const IRValue& env_param,
    analysis::TypeRef env_type,
    const std::unordered_map<std::string, CaptureAccess>& captures) const {
  CaptureEnvInfo env;
  env.env_param = env_param;
  env.env_type = std::move(env_type);
  env.captures = captures;
  return env;
}

void LowerCtx::BindAll(CaptureEnvInfo env) {
  capture_env = std::move(env);
}

void LowerCtx::BindEnv(CaptureEnvInfo env) {
  BindAll(std::move(env));
}

LoweredCaptureEnv LowerCtx::LowerParallelCaptureEnv(
    const std::vector<ParallelCaptureBinding>& captures,
    std::string_view env_prefix) {
  LoweredCaptureEnv lowered;

  std::vector<analysis::TypeRef> env_fields;
  env_fields.reserve(captures.size());
  for (const auto& cap : captures) {
    const auto perm = PermissionOfType(cap.type);
    const bool by_ref = perm == analysis::Permission::Const ||
                        perm == analysis::Permission::Shared;
    if (by_ref) {
      env_fields.push_back(
          analysis::MakeTypePtr(cap.type, analysis::PtrState::Valid));
    } else {
      env_fields.push_back(cap.type);
    }
  }

  auto env_type = analysis::MakeTypeTuple(env_fields);
  const auto env_layout =
      analysis::layout::RecordLayoutOf(ScopeForLowering(*this), env_fields);
  lowered.env_info.env_type = env_type;

  IRValue env_ptr = FreshTempValue(std::string(env_prefix) + "_env_ptr");
  RegisterValueType(env_ptr,
                    analysis::MakeTypePtr(env_type, analysis::PtrState::Valid));

  IRValue env_zero = FreshTempValue(std::string(env_prefix) + "_env_zero");
  RegisterValueType(env_zero, env_type);

  if (!active_region_aliases.empty()) {
    IRAlloc alloc_env;
    alloc_env.value = env_zero;
    alloc_env.result = env_ptr;
    alloc_env.type = env_type;
    IRValue region_local;
    region_local.kind = IRValue::Kind::Local;
    region_local.name = active_region_aliases.back();
    alloc_env.region = region_local;
    lowered.ir_parts.push_back(MakeIR(std::move(alloc_env)));
  } else {
    IRValue env_storage;
    env_storage.kind = IRValue::Kind::Local;
    env_storage.name = FreshTempValue(std::string(env_prefix) + "_env_storage").name;
    RegisterValueType(env_storage, env_type);

    IRBindVar bind_env;
    bind_env.name = env_storage.name;
    bind_env.value = env_zero;
    bind_env.type = env_type;
    lowered.ir_parts.push_back(MakeIR(std::move(bind_env)));

    DerivedValueInfo env_addr;
    env_addr.kind = DerivedValueInfo::Kind::AddrLocal;
    env_addr.name = env_storage.name;
    RegisterDerivedValue(env_ptr, env_addr);
  }

  for (std::size_t i = 0; i < captures.size(); ++i) {
    const auto& cap = captures[i];
    const auto perm = PermissionOfType(cap.type);
    const bool by_ref = perm == analysis::Permission::Const ||
                        perm == analysis::Permission::Shared;

    CaptureAccess access;
    access.index = i;
    if (env_layout && i < env_layout->offsets.size()) {
      access.byte_offset = env_layout->offsets[i];
    }
    access.value_type = cap.type;
    access.by_ref = by_ref;
    access.field_type = by_ref
                            ? analysis::MakeTypePtr(cap.type,
                                                    analysis::PtrState::Valid)
                            : cap.type;
    lowered.env_info.captures[cap.name] = access;

    IRValue field_val;
    IRPtr field_ir = EmptyIR();
    if (by_ref) {
      IRValue ptr = FreshTempValue("capture_addr");
      DerivedValueInfo addr_info;
      addr_info.kind = DerivedValueInfo::Kind::AddrLocal;
      addr_info.name = cap.name;
      RegisterDerivedValue(ptr, addr_info);
      RegisterValueType(
          ptr, analysis::MakeTypePtr(cap.type, analysis::PtrState::Valid));
      field_val = ptr;
    } else if (cap.explicit_move) {
      ast::Expr ident_expr;
      ident_expr.node = ast::IdentifierExpr{cap.name};
      auto move_res = LowerMovePlace(ident_expr, *this);
      field_val = move_res.value;
      field_ir = move_res.ir;
    } else {
      field_val.kind = IRValue::Kind::Local;
      field_val.name = cap.name;
    }

    if (field_ir && !std::holds_alternative<IROpaque>(field_ir->node)) {
      lowered.ir_parts.push_back(field_ir);
    }

    IRValue field_ptr = FreshTempValue("capture_field_ptr");
    DerivedValueInfo field_info;
    field_info.kind = DerivedValueInfo::Kind::AddrTuple;
    field_info.base = env_ptr;
    field_info.tuple_index = i;
    field_info.byte_offset = access.byte_offset;
    RegisterDerivedValue(field_ptr, field_info);
    RegisterValueType(
        field_ptr,
        analysis::MakeTypeRawPtr(analysis::RawPtrQual::Mut, access.field_type));

    IRWritePtr store_field;
    store_field.ptr = field_ptr;
    store_field.value = field_val;
    lowered.ir_parts.push_back(MakeIR(std::move(store_field)));
  }

  lowered.env_info.env_param = env_ptr;
  return lowered;
}

IRValue LowerCtx::CaptureFieldPtr(const CaptureAccess& access) {
  IRValue ptr = FreshTempValue("capture_ptr");
  if (!capture_env.has_value()) {
    return ptr;
  }
  DerivedValueInfo info;
  info.kind = DerivedValueInfo::Kind::AddrTuple;
  info.base = capture_env->env_param;
  info.tuple_index = access.index;
  info.byte_offset = access.byte_offset;
  RegisterDerivedValue(ptr, info);
  auto elem_ptr = analysis::MakeTypeRawPtr(analysis::RawPtrQual::Mut, access.field_type);
  RegisterValueType(ptr, elem_ptr);
  return ptr;
}

IRValue LowerCtx::FreshTempValue(std::string_view prefix) {
  IRValue value;
  value.kind = IRValue::Kind::Opaque;
  const std::uint64_t next_id = (*temp_counter)++;
  if (current_proc_symbol.has_value() && !current_proc_symbol->empty()) {
    value.name = *current_proc_symbol + "$tmp$" + std::string(prefix) + "_" +
                 std::to_string(next_id);
  } else {
    value.name = std::string(prefix) + "_" + std::to_string(next_id);
  }
  return value;
}

std::string LowerCtx::FreshRegionAlias() {
  auto name_used = [this](const std::string& name) -> bool {
    auto it = binding_states.find(name);
    if (it != binding_states.end() && !it->second.empty()) {
      return true;
    }
    for (const auto& scope : scope_stack) {
      if (std::find(scope.region_tags.begin(), scope.region_tags.end(), name) !=
          scope.region_tags.end()) {
        return true;
      }
    }
    return false;
  };

  for (std::size_t i = 0;; ++i) {
    std::string name = std::string("region$") + std::to_string(i);
    if (!name_used(name)) {
      return name;
    }
  }
}

void LowerCtx::ReserveRegionTag(const std::string& name) {
  if (!scope_stack.empty()) {
    scope_stack.back().region_tags.push_back(name);
  }
}

void LowerCtx::ReportResolveFailure(const std::string& name) {
  resolve_failed = true;
  if (std::find(resolve_failures.begin(), resolve_failures.end(), name) ==
      resolve_failures.end()) {
    resolve_failures.push_back(name);
  }
}

void LowerCtx::ReportCodegenFailure(std::source_location loc) {
  codegen_failed = true;
  std::cerr << "[uv] codegen failure at " << loc.file_name() << ":"
            << loc.line() << "\n";
}

void LowerCtx::RegisterValueType(const IRValue& value,
                                 analysis::TypeRef type,
                                 std::source_location loc) {
  if (!type) {
    return;
  }
  if (value.kind == IRValue::Kind::Symbol) {
    std::string key = "sym:";
    key += value.name;
    const auto [it, inserted] = values.value_types.emplace(key, type);
    if (!inserted) {
      it->second = type;
    } else if (values.value_type_insert_sink) {
      values.value_type_insert_sink->push_back(key);
    }
    return;
  }
  if (value.kind == IRValue::Kind::Opaque) {
    const auto [it, inserted] = values.value_types.emplace(value.name, type);
    if (!inserted) {
      it->second = type;
    } else if (values.value_type_insert_sink) {
      values.value_type_insert_sink->push_back(value.name);
    }
    return;
  }
  if (value.kind == IRValue::Kind::Immediate) {
    std::string key = "imm:";
    key += std::to_string(value.literal_id);
    const auto [it, inserted] = values.value_types.emplace(key, type);
    if (!inserted) {
      it->second = type;
    } else if (values.value_type_insert_sink) {
      values.value_type_insert_sink->push_back(key);
    }
  }
}

analysis::TypeRef LowerCtx::LookupValueType(const IRValue& value) const {
  if (value.kind == IRValue::Kind::Local) {
    if (const auto* state = GetBindingState(value.name)) {
      return state->type;
    }
    if (values.parent != nullptr) {
      return values.parent->LookupValueType(value);
    }
    return nullptr;
  }
  if (value.kind == IRValue::Kind::Symbol) {
    std::string key = "sym:";
    key += value.name;
    auto sym_it = values.value_types.find(key);
    if (sym_it != values.value_types.end()) {
      return sym_it->second;
    }
    if (values.parent != nullptr) {
      if (analysis::TypeRef inherited = values.parent->LookupValueType(value)) {
        return inherited;
      }
    }
    if (analysis::TypeRef static_type = LookupStaticType(value.name)) {
      return static_type;
    }
    if (const auto* state = GetBindingState(value.name)) {
      return state->type;
    }
    if (const auto* capture = LookupCapture(value.name)) {
      return capture->value_type;
    }
    return nullptr;
  }
  if (value.kind == IRValue::Kind::Opaque) {
    auto it = values.value_types.find(value.name);
    if (it != values.value_types.end()) {
      return it->second;
    }
    if (values.parent != nullptr) {
      return values.parent->LookupValueType(value);
    }
    return nullptr;
  }
  if (value.kind == IRValue::Kind::Immediate && value.literal_id != 0) {
    std::string key = "imm:";
    key += std::to_string(value.literal_id);
    auto it = values.value_types.find(key);
    if (it != values.value_types.end()) {
      return it->second;
    }
    if (values.parent != nullptr) {
      return values.parent->LookupValueType(value);
    }
  }
  return nullptr;
}

void LowerCtx::FreezeLookupTables() {
  if (baseline_tables != nullptr) {
    return;
  }
  auto tables = std::make_shared<LookupTables>();
  tables->static_types = std::move(values.static_types);
  tables->static_modules = std::move(static_modules);
  tables->record_ctor_paths = std::move(record_ctor_paths);
  tables->proc_sigs = std::move(proc_sigs);
  tables->proc_linkages = std::move(proc_linkages);
  tables->proc_visibilities = std::move(proc_visibilities);
  tables->proc_modules = std::move(proc_modules);
  tables->export_unwind_modes = std::move(export_unwind_modes);
  tables->foreign_contracts = std::move(foreign_contracts);
  tables->local_contracts = std::move(local_contracts);
  tables->async_procs = std::move(async_procs);
  baseline_tables = std::move(tables);
}

void LowerCtx::RegisterStaticType(const std::string& sym, analysis::TypeRef type) {
  if (!type) {
    return;
  }
  if (baseline_tables != nullptr &&
      baseline_tables->static_types.find(sym) !=
          baseline_tables->static_types.end()) {
    return;
  }
  values.static_types[sym] = type;
}

analysis::TypeRef LowerCtx::LookupStaticType(const std::string& sym) const {
  auto it = values.static_types.find(sym);
  if (it != values.static_types.end()) {
    return it->second;
  }
  if (baseline_tables != nullptr) {
    const auto base_it = baseline_tables->static_types.find(sym);
    if (base_it != baseline_tables->static_types.end()) {
      return base_it->second;
    }
  }
  return nullptr;
}

void LowerCtx::RegisterStaticModule(const std::string& sym,
                                    const ast::ModulePath& module_path) {
  if (baseline_tables != nullptr &&
      baseline_tables->static_modules.find(sym) !=
          baseline_tables->static_modules.end()) {
    return;
  }
  static_modules[sym] = module_path;
}

const std::vector<std::string>* LowerCtx::LookupStaticModule(
    const std::string& sym) const {
  auto it = static_modules.find(sym);
  if (it != static_modules.end()) {
    return &it->second;
  }
  if (baseline_tables != nullptr) {
    const auto base_it = baseline_tables->static_modules.find(sym);
    if (base_it != baseline_tables->static_modules.end()) {
      return &base_it->second;
    }
  }
  return nullptr;
}

void LowerCtx::RegisterDropGlueType(const std::string& sym, analysis::TypeRef type) {
  if (!type) {
    return;
  }
  values.drop_glue_types[sym] = type;
}

analysis::TypeRef LowerCtx::LookupDropGlueType(const std::string& sym) const {
  auto it = values.drop_glue_types.find(sym);
  if (it != values.drop_glue_types.end()) {
    return it->second;
  }
  if (values.parent != nullptr) {
    if (analysis::TypeRef inherited = values.parent->LookupDropGlueType(sym)) {
      return inherited;
    }
  }
  return nullptr;
}

void LowerCtx::RegisterRequiredVTable(const std::string& sym,
                                      analysis::TypeRef type,
                                      const analysis::TypePath& class_path) {
  if (sym.empty() || !type || class_path.empty()) {
    return;
  }

  analysis::TypePath resolved_class_path = class_path;
  if (sigma != nullptr) {
    auto exists_in_sigma = [&](const analysis::TypePath& path) -> bool {
      analysis::PathKey key(path.begin(), path.end());
      return sigma->classes.find(key) != sigma->classes.end();
    };

    if (!exists_in_sigma(resolved_class_path)) {
      analysis::TypePath module_qualified = module_path;
      module_qualified.insert(
          module_qualified.end(), class_path.begin(), class_path.end());
      if (exists_in_sigma(module_qualified)) {
        resolved_class_path = std::move(module_qualified);
      } else {
        analysis::TypePath unique_match;
        bool ambiguous_match = false;
        for (const auto& [candidate_key, _decl] : sigma->classes) {
          if (candidate_key.size() < class_path.size()) {
            continue;
          }
          const auto suffix_begin =
              candidate_key.end() - static_cast<std::ptrdiff_t>(class_path.size());
          if (!std::equal(class_path.begin(),
                          class_path.end(),
                          suffix_begin,
                          candidate_key.end())) {
            continue;
          }
          if (!unique_match.empty()) {
            ambiguous_match = true;
            break;
          }
          unique_match.assign(candidate_key.begin(), candidate_key.end());
        }
        if (!ambiguous_match && !unique_match.empty()) {
          resolved_class_path = std::move(unique_match);
        }
      }
    }
  }

  LowerValueState::RequiredVTableInfo info;
  info.type = type;
  info.class_path = std::move(resolved_class_path);
  values.required_vtables[sym] = std::move(info);
}

const LowerValueState::RequiredVTableInfo* LowerCtx::LookupRequiredVTable(
    const std::string& sym) const {
  auto it = values.required_vtables.find(sym);
  if (it != values.required_vtables.end()) {
    return &it->second;
  }
  if (values.parent != nullptr) {
    return values.parent->LookupRequiredVTable(sym);
  }
  return nullptr;
}

void LowerCtx::RegisterRecordCtor(const std::string& sym,
                                  const std::vector<std::string>& path) {
  if (baseline_tables != nullptr &&
      baseline_tables->record_ctor_paths.find(sym) !=
          baseline_tables->record_ctor_paths.end()) {
    return;
  }
  record_ctor_paths[sym] = path;
}

const std::vector<std::string>* LowerCtx::LookupRecordCtor(
    const std::string& sym) const {
  auto it = record_ctor_paths.find(sym);
  if (it != record_ctor_paths.end()) {
    return &it->second;
  }
  if (baseline_tables != nullptr) {
    const auto base_it = baseline_tables->record_ctor_paths.find(sym);
    if (base_it != baseline_tables->record_ctor_paths.end()) {
      return &base_it->second;
    }
  }
  return nullptr;
}

void LowerCtx::RegisterProcSig(const ProcIR& proc) {
  ProcSigInfo info;
  info.params = proc.params;
  info.ret = proc.ret;
  info.abi = proc.abi;
  info.aggregate_copy_elision = proc.aggregate_copy_elision;

  if (auto existing = proc_sigs.find(proc.symbol); existing != proc_sigs.end()) {
    info.ffi_import = existing->second.ffi_import;
    info.ffi_import_unwind_mode = existing->second.ffi_import_unwind_mode;
    if (!info.aggregate_copy_elision.has_value()) {
      info.aggregate_copy_elision = existing->second.aggregate_copy_elision;
    }
    if (!info.abi.has_value()) {
      info.abi = existing->second.abi;
    }
  } else if (baseline_tables != nullptr) {
    const auto base_it = baseline_tables->proc_sigs.find(proc.symbol);
    if (base_it != baseline_tables->proc_sigs.end()) {
      info.ffi_import = base_it->second.ffi_import;
      info.ffi_import_unwind_mode = base_it->second.ffi_import_unwind_mode;
      if (!info.aggregate_copy_elision.has_value()) {
        info.aggregate_copy_elision = base_it->second.aggregate_copy_elision;
      }
      if (!info.abi.has_value()) {
        info.abi = base_it->second.abi;
      }
    }
  }

  proc_sigs[proc.symbol] = std::move(info);
}

const LowerCtx::ProcSigInfo* LowerCtx::LookupProcSig(const std::string& sym) const {
  auto it = proc_sigs.find(sym);
  if (it != proc_sigs.end()) {
    return &it->second;
  }
  if (baseline_tables != nullptr) {
    const auto base_it = baseline_tables->proc_sigs.find(sym);
    if (base_it != baseline_tables->proc_sigs.end()) {
      return &base_it->second;
    }
  }
  return nullptr;
}

void LowerCtx::RegisterProcLinkage(const std::string& sym,
                                   LinkageKind linkage) {
  proc_linkages[sym] = linkage;
}

std::optional<LinkageKind> LowerCtx::LookupProcLinkage(
    const std::string& sym) const {
  auto it = proc_linkages.find(sym);
  if (it != proc_linkages.end()) {
    return it->second;
  }
  if (baseline_tables != nullptr) {
    const auto base_it = baseline_tables->proc_linkages.find(sym);
    if (base_it != baseline_tables->proc_linkages.end()) {
      return base_it->second;
    }
  }
  return std::nullopt;
}

const std::unordered_map<std::string, LinkageKind>& LowerCtx::AllProcLinkages()
    const {
  if (baseline_tables != nullptr) {
    return baseline_tables->proc_linkages;
  }
  return proc_linkages;
}

void LowerCtx::RegisterProcVisibility(const std::string& sym,
                                      ast::Visibility visibility) {
  proc_visibilities[sym] = visibility;
}

std::optional<ast::Visibility> LowerCtx::LookupProcVisibility(
    const std::string& sym) const {
  auto it = proc_visibilities.find(sym);
  if (it != proc_visibilities.end()) {
    return it->second;
  }
  if (baseline_tables != nullptr) {
    const auto base_it = baseline_tables->proc_visibilities.find(sym);
    if (base_it != baseline_tables->proc_visibilities.end()) {
      return base_it->second;
    }
  }
  return std::nullopt;
}

void LowerCtx::RegisterProcFfiImport(const std::string& sym,
                                     FfiImportUnwindMode mode) {
  auto mark_import = [&](ProcSigInfo& info) {
    info.ffi_import = true;
    info.ffi_import_unwind_mode = mode;
  };

  auto it = proc_sigs.find(sym);
  if (it != proc_sigs.end()) {
    mark_import(it->second);
    return;
  }

  if (baseline_tables != nullptr) {
    const auto base_it = baseline_tables->proc_sigs.find(sym);
    if (base_it != baseline_tables->proc_sigs.end()) {
      ProcSigInfo copied = base_it->second;
      mark_import(copied);
      proc_sigs[sym] = std::move(copied);
      return;
    }
  }

  ProcSigInfo info;
  mark_import(info);
  proc_sigs[sym] = std::move(info);
}

bool LowerCtx::NeedsPanicOutForSymbol(const std::string& sym) const {
  if (LookupRecordCtor(sym) != nullptr) {
    return false;
  }
  if (IsRuntimeFunction(sym)) {
    return false;
  }
  if (const auto* sig = LookupProcSig(sym)) {
    return !sig->params.empty() &&
           sig->params.back().name == std::string(kPanicOutName);
  }
  return NeedsPanicOut(sym);
}

void LowerCtx::RegisterProcModule(const std::string& sym, const ast::ModulePath& module_path) {
  if (proc_modules.find(sym) != proc_modules.end()) {
    return;
  }
  if (proc_modules.find(sym) == proc_modules.end() &&
      baseline_tables != nullptr) {
    if (baseline_tables->proc_modules.find(sym) !=
        baseline_tables->proc_modules.end()) {
      return;
    }
  }
  proc_modules[sym] = module_path;
}

const std::vector<std::string>* LowerCtx::LookupProcModule(const std::string& sym) const {
  auto it = proc_modules.find(sym);
  if (it != proc_modules.end()) {
    return &it->second;
  }
  if (baseline_tables != nullptr) {
    const auto base_it = baseline_tables->proc_modules.find(sym);
    if (base_it != baseline_tables->proc_modules.end()) {
      return &base_it->second;
    }
  }
  return nullptr;
}

void LowerCtx::QueueExtraProc(ProcIR proc,
                              std::optional<LinkageKind> linkage,
                              const ast::ModulePath* module_path) {
  if (proc.defining_module_path.empty()) {
    proc.defining_module_path =
        module_path != nullptr ? *module_path : this->module_path;
  }
  RegisterProcSig(proc);
  if (linkage.has_value()) {
    RegisterProcLinkage(proc.symbol, *linkage);
  }
  if (!proc.defining_module_path.empty()) {
    RegisterProcModule(proc.symbol, proc.defining_module_path);
  }
  extra_procs.push_back(std::move(proc));
}

void LowerCtx::MergeGeneratedProcsFrom(LowerCtx& branch) {
  if (branch.extra_procs.empty()) {
    return;
  }

  for (auto& proc : branch.extra_procs) {
    const std::string symbol = proc.symbol;
    const bool already_registered = LookupProcModule(symbol) != nullptr;

    if (const auto it = branch.proc_sigs.find(symbol);
        it != branch.proc_sigs.end()) {
      proc_sigs.emplace(symbol, it->second);
    }
    if (const auto it = branch.proc_linkages.find(symbol);
        it != branch.proc_linkages.end()) {
      proc_linkages.emplace(symbol, it->second);
    }
    if (const auto it = branch.proc_visibilities.find(symbol);
        it != branch.proc_visibilities.end()) {
      proc_visibilities.emplace(symbol, it->second);
    }
    if (const auto it = branch.proc_modules.find(symbol);
        it != branch.proc_modules.end()) {
      proc_modules.emplace(symbol, it->second);
    }
    if (const auto it = branch.export_unwind_modes.find(symbol);
        it != branch.export_unwind_modes.end()) {
      export_unwind_modes.emplace(symbol, it->second);
    }
    if (const auto it = branch.foreign_contracts.find(symbol);
        it != branch.foreign_contracts.end()) {
      foreign_contracts.emplace(symbol, it->second);
    }
    if (const auto it = branch.local_contracts.find(symbol);
        it != branch.local_contracts.end()) {
      local_contracts.emplace(symbol, it->second);
    }
    if (const auto it = branch.async_procs.find(symbol);
        it != branch.async_procs.end()) {
      async_procs.emplace(symbol, it->second);
    }

    if (!already_registered) {
      extra_procs.push_back(std::move(proc));
    }
  }
  branch.extra_procs.clear();
}

void LowerCtx::RegisterExportUnwindMode(const std::string& sym,
                                        ExportUnwindMode mode) {
  export_unwind_modes[sym] = mode;
}

std::optional<LowerCtx::ExportUnwindMode> LowerCtx::LookupExportUnwindMode(
    const std::string& sym) const {
  auto it = export_unwind_modes.find(sym);
  if (it != export_unwind_modes.end()) {
    return it->second;
  }
  if (baseline_tables != nullptr) {
    const auto base_it = baseline_tables->export_unwind_modes.find(sym);
    if (base_it != baseline_tables->export_unwind_modes.end()) {
      return base_it->second;
    }
  }
  return std::nullopt;
}

void LowerCtx::RegisterForeignContractInfo(const std::string& sym,
                                           ForeignContractInfo info) {
  foreign_contracts[sym] = std::move(info);
}

const LowerCtx::ForeignContractInfo* LowerCtx::LookupForeignContractInfo(
    const std::string& sym) const {
  auto it = foreign_contracts.find(sym);
  if (it != foreign_contracts.end()) {
    return &it->second;
  }
  if (baseline_tables != nullptr) {
    const auto base_it = baseline_tables->foreign_contracts.find(sym);
    if (base_it != baseline_tables->foreign_contracts.end()) {
      return &base_it->second;
    }
  }
  return nullptr;
}

void LowerCtx::RegisterLocalContractInfo(const std::string& sym,
                                         LocalContractInfo info) {
  local_contracts[sym] = std::move(info);
}

const LowerCtx::LocalContractInfo* LowerCtx::LookupLocalContractInfo(
    const std::string& sym) const {
  auto it = local_contracts.find(sym);
  if (it != local_contracts.end()) {
    return &it->second;
  }
  if (baseline_tables != nullptr) {
    const auto base_it = baseline_tables->local_contracts.find(sym);
    if (base_it != baseline_tables->local_contracts.end()) {
      return &base_it->second;
    }
  }
  return nullptr;
}

const LowerCtx::AsyncProcInfo* LowerCtx::LookupAsyncProc(const std::string& sym) const {
  auto it = async_procs.find(sym);
  if (it != async_procs.end()) {
    return &it->second;
  }
  if (baseline_tables != nullptr) {
    const auto base_it = baseline_tables->async_procs.find(sym);
    if (base_it != baseline_tables->async_procs.end()) {
      return &base_it->second;
    }
  }
  return nullptr;
}

}  // namespace ultraviolet::codegen
