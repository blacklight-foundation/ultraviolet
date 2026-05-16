// =============================================================================
// MIGRATION MAPPING: function_types.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
// - Section 5.2.3 "Function Types" (lines 8698-8705)
//   - T-Proc-As-Value (lines 8700-8703)
//   - FuncTypeRules (line 8705)
// - Section 5.2.4 "Procedure Calls" (lines 8707-8776)
//   - ArgsOkTJudg (line 8709)
//   - ParamMode, ParamType (lines 8710-8711)
//   - ArgMoved, ArgExpr (lines 8712-8713)
//   - PlaceType, ArgType (lines 8714-8717)
//   - ArgsT-Empty, ArgsT-Cons, ArgsT-Cons-Ref (lines 8719-8731)
//   - T-Call (lines 8733-8736)
//   - Call error rules (lines 8738-8776)
// - Section 5.2.12 referenced for ValuePathType
// - Section 5.2.14 referenced for ProcReturn
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/composite/function_types.cpp
// - Lines 1-387 (entire file)
//
// Key source functions to migrate:
// - ProcType (lines 321-339): Compute procedure type as function value
// - ValuePathType (lines 341-384): Resolve qualified path to value type
//
// Supporting helpers:
// - TypeLowerResult struct (lines 22-26): Type lowering result
// - LowerPermission (lines 33-43): Convert syntax permission
// - LowerParamMode (lines 45-51): Convert syntax param mode
// - LowerRawPtrQual (lines 53-61): Convert syntax raw pointer qualifier
// - LowerStringState (lines 63-75): Convert syntax string state
// - LowerBytesState (lines 77-89): Convert syntax bytes state
// - LowerPtrState (lines 91-105): Convert syntax pointer state
// - LowerTypeLocal: Adapter around canonical LowerType
// - ProcReturn (lines 247-254): Lower procedure return type
// - FindModule (lines 256-264): Find module by path
// - ModuleNamesForContext (lines 266-276): Get module names from context
// - StaticTypeOf (lines 278-302): Get type of static declaration
// - FindProcedure (lines 304-317): Find procedure in module
//
// DEPENDENCIES:
// - cursive/include/04_analysis/resolve/resolve_items.h (CollectNameMaps)
// - cursive/include/04_analysis/resolve/scopes.h (ScopeContext)
// - cursive/include/04_analysis/resolve/scopes_lookup.h (ResolveQualified)
// - cursive/include/04_analysis/resolve/visibility.h (CanAccess)
// - cursive/include/04_analysis/memory/string_bytes.h (LookupStringBytesBuiltinType)
// - cursive/include/04_analysis/caps/cap_concurrency.h (MakeCancelTokenTypeWithState)
// - cursive/include/04_analysis/typing/type_equiv.h (TypeEquiv)
// - cursive/include/00_core/symbols.h (symbol utilities)
// - cursive/include/00_core/assert_spec.h (SPEC_DEF, SPEC_RULE)
//
// REFACTORING NOTES:
// 1. ProcReturn defaults to unit "()" when no return type specified
// 2. ValuePathType handles special cases:
//    - String/bytes builtin methods
//    - CancelToken::new constructor
//    - Static declarations
//    - Procedure declarations
// 3. Qualified name resolution respects visibility
// 4. T-Proc-As-Value: procedure names can be used as function values
// =============================================================================

#include "04_analysis/composite/function_types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/modal/builtin_modal_intrinsics.h"
#include "04_analysis/resolve/resolve_items.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_pattern.h"
#include "04_analysis/memory/string_bytes.h"
#include "04_analysis/resolve/visibility.h"

namespace cursive::analysis {

namespace {

struct TypeLowerResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
};

static inline void SpecDefsFunctionTypes() {
  SPEC_DEF("ValuePathType", "5.2.12");
  SPEC_DEF("ProcReturn", "5.2.14");
}

static Permission LowerPermission(ast::TypePerm perm) {
  switch (perm) {
    case ast::TypePerm::Const:
      return Permission::Const;
    case ast::TypePerm::Unique:
      return Permission::Unique;
    case ast::TypePerm::Shared:
      return Permission::Shared;
  }
  return Permission::Const;
}

static std::optional<ParamMode> LowerParamMode(
    const std::optional<ast::ParamMode>& mode) {
  if (!mode.has_value()) {
    return std::nullopt;
  }
  return ParamMode::Move;
}

static RawPtrQual LowerRawPtrQual(ast::RawPtrQual qual) {
  switch (qual) {
    case ast::RawPtrQual::Imm:
      return RawPtrQual::Imm;
    case ast::RawPtrQual::Mut:
      return RawPtrQual::Mut;
  }
  return RawPtrQual::Imm;
}

static std::optional<StringState> LowerStringState(
    const std::optional<ast::StringState>& state) {
  if (!state.has_value()) {
    return std::nullopt;
  }
  switch (*state) {
    case ast::StringState::Managed:
      return StringState::Managed;
    case ast::StringState::View:
      return StringState::View;
  }
  return std::nullopt;
}

static std::optional<BytesState> LowerBytesState(
    const std::optional<ast::BytesState>& state) {
  if (!state.has_value()) {
    return std::nullopt;
  }
  switch (*state) {
    case ast::BytesState::Managed:
      return BytesState::Managed;
    case ast::BytesState::View:
      return BytesState::View;
  }
  return std::nullopt;
}

static std::optional<PtrState> LowerPtrState(
    const std::optional<ast::PtrState>& state) {
  if (!state.has_value()) {
    return std::nullopt;
  }
  switch (*state) {
    case ast::PtrState::Valid:
      return PtrState::Valid;
    case ast::PtrState::Null:
      return PtrState::Null;
    case ast::PtrState::Expired:
      return PtrState::Expired;
  }
  return std::nullopt;
}

static TypeLowerResult LowerTypeLocal(const ScopeContext& ctx,
                                      const std::shared_ptr<ast::Type>& type) {
  const auto lowered = ::cursive::analysis::LowerType(ctx, type);
  return {lowered.ok, lowered.diag_id, lowered.type};
}

static TypeLowerResult ProcReturn(const ScopeContext& ctx,
                                  const std::shared_ptr<ast::Type>& ret_opt) {
  SpecDefsFunctionTypes();
  if (!ret_opt) {
    return {true, std::nullopt, MakeTypePrim("()")};
  }
  return LowerTypeLocal(ctx, ret_opt);
}

static const ast::ASTModule* FindModule(const ScopeContext& ctx,
                                         const ast::ModulePath& path) {
  for (const auto& module : ctx.sigma.mods) {
    if (PathEq(module.path, path)) {
      return &module;
    }
  }
  return nullptr;
}

static source::ModuleNames ModuleNamesForContext(const ScopeContext& ctx) {
  if (ctx.project) {
    return ModuleNamesOf(*ctx.project);
  }
  source::ModuleNames names;
  names.reserve(ctx.sigma.mods.size());
  for (const auto& mod : ctx.sigma.mods) {
    names.insert(core::StringOfPath(mod.path));
  }
  return names;
}

struct ValuePathNameMapCache {
  const ScopeContext* ctx = nullptr;
  const project::Project* project = nullptr;
  std::uint64_t sigma_fingerprint = 0;
  std::size_t mods_size = 0;
  std::size_t types_size = 0;
  std::size_t classes_size = 0;
  NameMapTable name_maps;
};

static std::uint64_t MixFingerprint(std::uint64_t h, std::uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  return h;
}

static std::uint64_t SigmaFingerprint(const ScopeContext& ctx) {
  std::uint64_t h = 0x84222325cbf29ce4ull;
  h = MixFingerprint(h, static_cast<std::uint64_t>(ctx.sigma.mods.size()));
  h = MixFingerprint(h, static_cast<std::uint64_t>(ctx.sigma.types.size()));
  h = MixFingerprint(h, static_cast<std::uint64_t>(ctx.sigma.classes.size()));
  h = MixFingerprint(
      h, static_cast<std::uint64_t>(
             reinterpret_cast<std::uintptr_t>(ctx.sigma.mods.data())));
  for (const auto& mod : ctx.sigma.mods) {
    h = MixFingerprint(h, static_cast<std::uint64_t>(mod.path.size()));
    for (const auto& seg : mod.path) {
      h = MixFingerprint(h, static_cast<std::uint64_t>(IdKeyOf(seg).size()));
    }
    h = MixFingerprint(h, static_cast<std::uint64_t>(mod.items.size()));
    h = MixFingerprint(
        h, static_cast<std::uint64_t>(
               reinterpret_cast<std::uintptr_t>(mod.items.data())));
  }
  return h;
}

static bool ValuePathNameMapCacheValid(const ScopeContext& ctx,
                                       const ValuePathNameMapCache& cache) {
  return cache.ctx == &ctx && cache.project == ctx.project &&
         cache.sigma_fingerprint == SigmaFingerprint(ctx) &&
         cache.mods_size == ctx.sigma.mods.size() &&
         cache.types_size == ctx.sigma.types.size() &&
         cache.classes_size == ctx.sigma.classes.size();
}

static const NameMapTable& CachedNameMapsForValuePath(const ScopeContext& ctx) {
  static thread_local ValuePathNameMapCache cache;
  if (ValuePathNameMapCacheValid(ctx, cache)) {
    return cache.name_maps;
  }

  auto& mutable_ctx = const_cast<ScopeContext&>(ctx);
  const ast::ModulePath saved_module = mutable_ctx.current_module;
  const auto built = CollectNameMaps(mutable_ctx);
  mutable_ctx.current_module = saved_module;

  cache.ctx = &ctx;
  cache.project = ctx.project;
  cache.sigma_fingerprint = SigmaFingerprint(ctx);
  cache.mods_size = ctx.sigma.mods.size();
  cache.types_size = ctx.sigma.types.size();
  cache.classes_size = ctx.sigma.classes.size();
  cache.name_maps = built.name_maps;
  return cache.name_maps;
}

static ModuleStaticLookupResult LookupModuleStaticInModule(
    const ScopeContext& ctx,
    const ast::ASTModule& module,
    std::string_view name) {
  for (const auto& item : module.items) {
    const auto* decl = std::get_if<ast::StaticDecl>(&item);
    if (!decl || !decl->binding.pat) {
      continue;
    }
    bool binds_name = false;
    for (const auto& bound_name : PatNames(decl->binding.pat)) {
      if (IdEq(bound_name, name)) {
        binds_name = true;
        break;
      }
    }
    if (!binds_name) {
      continue;
    }

    const auto ann_type = ast::BindingAnnotationTypeOpt(decl->binding);
    if (!ann_type) {
      continue;
    }

    const auto lowered = LowerTypeLocal(ctx, ann_type);
    if (!lowered.ok) {
      return {false, lowered.diag_id, {}, false};
    }

    const auto pattern_result =
        TypePatternAgainstType(ctx, decl->binding.pat, lowered.type);
    if (!pattern_result.ok) {
      return {false, pattern_result.diag_id, {}, false};
    }

    for (const auto& binding : pattern_result.bindings) {
      if (IdEq(binding.first, name)) {
        return {true, std::nullopt, binding.second,
                decl->mut == ast::Mutability::Var};
      }
    }
  }
  return {true, std::nullopt, {}, false};
}

struct ProcedureLookupResult {
  const ast::ProcedureDecl* proc = nullptr;
  const ast::ComptimeProcedureDecl* comptime_proc = nullptr;
  const ast::ExternProcDecl* extern_proc = nullptr;
};

static ProcedureLookupResult FindProcedure(
    const ast::ASTModule& module,
    std::string_view name) {
  for (const auto& decl : module.comptime_procedures) {
    if (IdEq(decl.name, name)) {
      return ProcedureLookupResult{nullptr, &decl, nullptr};
    }
  }
  for (const auto& item : module.items) {
    if (const auto* decl = std::get_if<ast::ProcedureDecl>(&item)) {
      if (IdEq(decl->name, name)) {
        return ProcedureLookupResult{decl, nullptr, nullptr};
      }
      continue;
    }
    if (const auto* decl = std::get_if<ast::ComptimeProcedureDecl>(&item)) {
      if (IdEq(decl->name, name)) {
        return ProcedureLookupResult{nullptr, decl, nullptr};
      }
      continue;
    }
    if (const auto* block = std::get_if<ast::ExternBlock>(&item)) {
      for (const auto& ext_item : block->items) {
        if (const auto* ext_decl = std::get_if<ast::ExternProcDecl>(&ext_item);
            ext_decl && IdEq(ext_decl->name, name)) {
          return ProcedureLookupResult{nullptr, nullptr, ext_decl};
        }
      }
    }
  }
  return {};
}

static bool ModulePathEq(const ast::ModulePath& lhs,
                         const ast::ModulePath& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!IdEq(lhs[i], rhs[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace

ValuePathTypeResult ProcType(const ScopeContext& ctx,
                             const ast::ProcedureDecl& decl) {
  SpecDefsFunctionTypes();
  std::vector<TypeFuncParam> params;
  params.reserve(decl.params.size());
  for (const auto& param : decl.params) {
    const auto lowered = LowerTypeLocal(ctx, param.type);
    if (!lowered.ok) {
      return {false, lowered.diag_id, {}};
    }
    params.push_back(TypeFuncParam{
        ::cursive::analysis::LowerParamMode(param.mode), lowered.type});
  }
  const auto ret = ProcReturn(ctx, decl.return_type_opt);
  if (!ret.ok) {
    return {false, ret.diag_id, {}};
  }
  SPEC_RULE("T-Proc-As-Value");
  return {true, std::nullopt, MakeTypeFunc(std::move(params), ret.type)};
}

ValuePathTypeResult ProcType(const ScopeContext& ctx,
                             const ast::ComptimeProcedureDecl& decl) {
  SpecDefsFunctionTypes();
  std::vector<TypeFuncParam> params;
  params.reserve(decl.params.size());
  for (const auto& param : decl.params) {
    const auto lowered = LowerTypeLocal(ctx, param.type);
    if (!lowered.ok) {
      return {false, lowered.diag_id, {}};
    }
    params.push_back(TypeFuncParam{
        ::cursive::analysis::LowerParamMode(param.mode), lowered.type});
  }
  const auto ret = ProcReturn(ctx, decl.return_type_opt);
  if (!ret.ok) {
    return {false, ret.diag_id, {}};
  }
  SPEC_RULE("T-Proc-As-Value");
  return {true, std::nullopt, MakeTypeFunc(std::move(params), ret.type)};
}

ValuePathTypeResult ProcType(const ScopeContext& ctx,
                             const ast::ExternProcDecl& decl) {
  SpecDefsFunctionTypes();
  std::vector<TypeFuncParam> params;
  params.reserve(decl.params.size());
  for (const auto& param : decl.params) {
    const auto lowered = LowerTypeLocal(ctx, param.type);
    if (!lowered.ok) {
      return {false, lowered.diag_id, {}};
    }
    params.push_back(TypeFuncParam{
        ::cursive::analysis::LowerParamMode(param.mode), lowered.type});
  }
  const auto ret = ProcReturn(ctx, decl.return_type_opt);
  if (!ret.ok) {
    return {false, ret.diag_id, {}};
  }
  SPEC_RULE("T-Proc-As-Value");
  return {true, std::nullopt, MakeTypeFunc(std::move(params), ret.type)};
}

ValuePathTypeResult ValuePathType(const ScopeContext& ctx,
                                  const ast::ModulePath& path,
                                  std::string_view name) {
  SpecDefsFunctionTypes();
  auto direct_module_lookup =
      [&]() -> std::optional<ValuePathTypeResult> {
    const ast::ModulePath module_path = path.empty() ? ctx.current_module : path;
    const auto* module = FindModule(ctx, module_path);
    if (!module) {
      return std::nullopt;
    }
    const auto static_lookup = LookupModuleStaticInModule(ctx, *module, name);
    if (!static_lookup.ok) {
      return ValuePathTypeResult{false, static_lookup.diag_id, {}};
    }
    if (static_lookup.type) {
      return ValuePathTypeResult{true, std::nullopt, static_lookup.type};
    }
    const auto proc_lookup = FindProcedure(*module, name);
    if (proc_lookup.proc) {
      return ProcType(ctx, *proc_lookup.proc);
    }
    if (proc_lookup.comptime_proc) {
      return ProcType(ctx, *proc_lookup.comptime_proc);
    }
    if (proc_lookup.extern_proc) {
      return ProcType(ctx, *proc_lookup.extern_proc);
    }
    return std::nullopt;
  };

  if (const auto builtin = LookupStringBytesBuiltinType(path, name)) {
    return {true, std::nullopt, *builtin};
  }
  if (const auto builtin_sig = LookupBuiltinModalStaticFuncSig(path, name)) {
    std::vector<TypeFuncParam> params = builtin_sig->params;
    return {true, std::nullopt,
            MakeTypeFunc(std::move(params), builtin_sig->ret)};
  }
  const auto& name_maps = CachedNameMapsForValuePath(ctx);
  const auto module_names = ModuleNamesForContext(ctx);
  const auto resolved = ResolveQualified(
      ctx, name_maps, module_names, path, name, EntityKind::Value,
      CanAccess);
  if (!resolved.ok) {
    if (!resolved.diag_id) {
      if (const auto direct = direct_module_lookup()) {
        return *direct;
      }
    }
    if (!resolved.diag_id && ModulePathEq(path, ctx.current_module)) {
      if (const auto gpu_intrinsic = LookupGpuIntrinsicType(name)) {
        return {true, std::nullopt, *gpu_intrinsic};
      }
    }
    return {false, resolved.diag_id, {}};
  }
  if (!resolved.entity || !resolved.entity->origin_opt) {
    if (const auto direct = direct_module_lookup()) {
      return *direct;
    }
    return {true, std::nullopt, {}};
  }
  const auto resolved_name =
      resolved.entity->target_opt.value_or(std::string(name));
  const auto* module = FindModule(ctx, *resolved.entity->origin_opt);
  if (!module) {
    return {true, std::nullopt, {}};
  }
  const auto static_lookup =
      LookupModuleStaticInModule(ctx, *module, resolved_name);
  if (!static_lookup.ok) {
    return {false, static_lookup.diag_id, {}};
  }
  if (static_lookup.type) {
    return {true, std::nullopt, static_lookup.type};
  }
  const auto proc_lookup = FindProcedure(*module, resolved_name);
  if (!proc_lookup.proc && !proc_lookup.comptime_proc && !proc_lookup.extern_proc) {
    if (ModulePathEq(path, ctx.current_module)) {
      if (const auto gpu_intrinsic = LookupGpuIntrinsicType(name)) {
        return {true, std::nullopt, *gpu_intrinsic};
      }
    }
    return {true, std::nullopt, {}};
  }
  if (proc_lookup.proc) {
    return ProcType(ctx, *proc_lookup.proc);
  }
  if (proc_lookup.comptime_proc) {
    return ProcType(ctx, *proc_lookup.comptime_proc);
  }
  return ProcType(ctx, *proc_lookup.extern_proc);
}

ModuleStaticLookupResult LookupModuleStatic(const ScopeContext& ctx,
                                            const ast::ModulePath& path,
                                            std::string_view name) {
  const auto* module = FindModule(ctx, path);
  if (!module) {
    return {true, std::nullopt, {}, false};
  }
  return LookupModuleStaticInModule(ctx, *module, name);
}

}  // namespace cursive::analysis
