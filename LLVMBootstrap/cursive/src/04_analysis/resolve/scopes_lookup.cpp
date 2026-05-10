// =============================================================================
// scopes_lookup.cpp - Scope Lookup Implementation
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md Section 5.1.3 "Lookup" (Lines 6822-6899)
//   CursiveSpecification.md Section 5.1.5 "Top-Level Name Collection" (Lines 6956-7309)
//
// SOURCE FILE (migrated from):
//   cursive-bootstrap/src/03_analysis/resolve/scopes_lookup.cpp
//
// =============================================================================

#include "04_analysis/resolve/scopes_lookup.h"

#include <algorithm>
#include <unordered_set>

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/modal/builtin_modal_intrinsics.h"
#include "04_analysis/resolve/resolve_items.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsLookup() {
  SPEC_DEF("ValueKind", "5.1.3");
  SPEC_DEF("TypeKind", "5.1.3");
  SPEC_DEF("ClassKind", "5.1.3");
  SPEC_DEF("ModuleKind", "5.1.3");
  SPEC_DEF("RegionAlias", "5.1.3");
  SPEC_DEF("RegionAliasName", "5.1.3");
  SPEC_DEF("AliasMap", "5.1.5");
  SPEC_DEF("ModuleNames", "5.1.5");
  SPEC_DEF("StringOfPath", "3.4.1");
}

bool ContainsName(const source::ModuleNames& names, std::string_view name) {
  return names.find(std::string(name)) != names.end();
}

std::optional<ast::ModulePath> ResolveInCurrentAssembly(
    const ScopeContext& ctx,
    const ast::ModulePath& path,
    const source::ModuleNames& module_names) {
  if (path.empty()) {
    return std::nullopt;
  }
  if (CurrentModule(ctx).empty()) {
    return std::nullopt;
  }
  ast::ModulePath candidate;
  candidate.reserve(path.size() + 1);
  candidate.push_back(CurrentModule(ctx).front());
  candidate.insert(candidate.end(), path.begin(), path.end());
  if (ContainsName(module_names, core::StringOfPath(candidate))) {
    return candidate;
  }
  return std::nullopt;
}

std::optional<Entity> ResolveBuiltinValueName(std::string_view name) {
  if (!IsGpuIntrinsicName(name)) {
    return std::nullopt;
  }
  Entity ent{};
  ent.kind = EntityKind::Value;
  ent.origin_opt = std::nullopt;
  ent.target_opt = ast::Identifier{name};
  ent.source = EntitySource::Decl;
  return ent;
}

ast::ModulePath AliasExpand(const ast::ModulePath& path,
                            const AliasMap& alias) {
  SpecDefsLookup();
  if (path.empty()) {
    return path;
  }
  const auto key = IdKeyOf(path.front());
  const auto it = alias.find(key);
  if (it == alias.end()) {
    SPEC_RULE("AliasExpand-None");
    return path;
  }
  SPEC_RULE("AliasExpand-Yes");
  ast::ModulePath out = it->second;
  out.insert(out.end(), path.begin() + 1, path.end());
  return out;
}

AliasMap AliasMapForCurrentModule(const ScopeContext& ctx,
                                  const NameMapTable& name_maps) {
  const auto current_key = PathKeyOf(CurrentModule(ctx));
  const auto it = name_maps.find(current_key);
  if (it == name_maps.end()) {
    return {};
  }
  return AliasMapOf(it->second);
}

void AddVisibleAssemblyName(std::unordered_set<std::string>& assemblies,
                            const ast::ModulePath& module_path) {
  if (module_path.empty()) {
    return;
  }
  assemblies.insert(std::string(module_path.front()));
}

std::string_view FirstAssemblyName(std::string_view module_path) {
  const auto sep = module_path.find("::");
  if (sep == std::string_view::npos) {
    return module_path;
  }
  return module_path.substr(0, sep);
}

source::ModuleNames VisibleModuleNamesFromSigma(
    const ScopeContext& ctx,
    const std::unordered_set<std::string>& visible_assemblies) {
  source::ModuleNames visible;
  visible.reserve(ctx.sigma.mods.size());
  for (const auto& mod : ctx.sigma.mods) {
    if (mod.path.empty()) {
      continue;
    }
    if (visible_assemblies.find(std::string(mod.path.front())) ==
        visible_assemblies.end()) {
      continue;
    }
    visible.insert(core::StringOfPath(mod.path));
  }
  return visible;
}

source::ModuleNames ComputeVisibleModuleNames(
    const ScopeContext& ctx,
    const source::ModuleNames& all_module_names) {
  if (CurrentModule(ctx).empty()) {
    return all_module_names;
  }

  std::unordered_set<std::string> visible_assemblies;
  visible_assemblies.reserve(4);
  AddVisibleAssemblyName(visible_assemblies, CurrentModule(ctx));

  const auto* current_module = FindContextModuleByPath(ctx, CurrentModule(ctx));
  if (!current_module) {
    return all_module_names;
  }

  for (const auto& item : current_module->items) {
    const auto* import_decl = std::get_if<ast::ImportDecl>(&item);
    if (!import_decl) {
      continue;
    }
    const auto resolved_path =
        source::ResolveImportModulePath(CurrentModule(ctx),
                                        all_module_names,
                                        import_decl->path);
    if (!resolved_path.has_value()) {
      continue;
    }
    AddVisibleAssemblyName(visible_assemblies, *resolved_path);
  }

  if (!ctx.project) {
    return VisibleModuleNamesFromSigma(ctx, visible_assemblies);
  }

  source::ModuleNames visible;
  visible.reserve(ctx.project->modules.size());
  for (const auto& module : ctx.project->modules) {
    const auto assembly_name = FirstAssemblyName(module.path);
    if (assembly_name.empty()) {
      continue;
    }
    if (visible_assemblies.find(std::string(assembly_name)) ==
        visible_assemblies.end()) {
      continue;
    }
    visible.insert(module.path);
  }
  if (!visible.empty()) {
    return visible;
  }

  return VisibleModuleNamesFromSigma(ctx, visible_assemblies);
}

}  // namespace

AliasMap AliasMapOf(const NameMap& names) {
  SpecDefsLookup();
  AliasMap out;
  for (const auto& [key, ent] : names) {
    if (ent.kind != EntityKind::ModuleAlias) {
      continue;
    }
    if (!ent.origin_opt.has_value()) {
      continue;
    }
    out.emplace(key, *ent.origin_opt);
  }
  return out;
}

source::ModuleNames ModuleNamesOf(const project::Project& project) {
  SpecDefsLookup();
  source::ModuleNames out;
  out.reserve(project.modules.size());
  for (const auto& module : project.modules) {
    out.insert(module.path);
  }
  return out;
}

const ast::ASTModule* FindContextModuleByPath(const ScopeContext& ctx,
                                              const ast::ModulePath& path) {
  SpecDefsLookup();
  for (const auto& mod : ctx.sigma.mods) {
    if (PathEq(mod.path, path)) {
      return &mod;
    }
  }
  return nullptr;
}

source::ModuleNames VisibleModuleNamesOf(
    const ScopeContext& ctx,
    const source::ModuleNames& all_module_names) {
  SpecDefsLookup();
  return ComputeVisibleModuleNames(ctx, all_module_names);
}

bool ValueKind(const Entity& ent) {
  SpecDefsLookup();
  return ent.kind == EntityKind::Value;
}

bool TypeKind(const Entity& ent) {
  SpecDefsLookup();
  return ent.kind == EntityKind::Type;
}

bool ClassKind(const Entity& ent) {
  SpecDefsLookup();
  return ent.kind == EntityKind::Class;
}

bool ModuleKind(const Entity& ent) {
  SpecDefsLookup();
  return ent.kind == EntityKind::ModuleAlias;
}

bool RegionAlias(const Entity& ent) {
  SpecDefsLookup();
  return ent.source == EntitySource::RegionAlias;
}

std::optional<Entity> Lookup(const ScopeContext& ctx, std::string_view name) {
  SpecDefsLookup();
  const auto key = IdKeyOf(name);
  const auto& scopes = Scopes(ctx);
  for (const auto& scope : scopes) {
    const auto it = scope.find(key);
    if (it != scope.end()) {
      SPEC_RULE("Lookup-Unqualified");
      return it->second;
    }
  }
  SPEC_RULE("Lookup-Unqualified-None");
  return std::nullopt;
}

std::optional<Entity> ResolveValueName(const ScopeContext& ctx,
                                       std::string_view name) {
  SpecDefsLookup();
  // Map ~ (receiver reference) to self
  const std::string_view lookup_name = (name == "~") ? "self" : name;
  const auto ent = Lookup(ctx, lookup_name);
  if (!ent.has_value()) {
    return ResolveBuiltinValueName(lookup_name);
  }
  if (!ValueKind(*ent)) {
    return ResolveBuiltinValueName(lookup_name);
  }
  SPEC_RULE("Resolve-Value-Name");
  return ent;
}

std::optional<Entity> ResolveTypeName(const ScopeContext& ctx,
                                      std::string_view name) {
  SpecDefsLookup();
  const auto ent = Lookup(ctx, name);
  if (!ent.has_value()) {
    return std::nullopt;
  }
  if (!TypeKind(*ent)) {
    return std::nullopt;
  }
  SPEC_RULE("Resolve-Type-Name");
  return ent;
}

std::optional<Entity> ResolveClassName(const ScopeContext& ctx,
                                       std::string_view name) {
  SpecDefsLookup();
  const auto ent = Lookup(ctx, name);
  if (!ent.has_value()) {
    return std::nullopt;
  }
  if (!ClassKind(*ent)) {
    return std::nullopt;
  }
  SPEC_RULE("Resolve-Class-Name");
  return ent;
}

std::optional<Entity> ResolveModuleName(const ScopeContext& ctx,
                                        std::string_view name) {
  SpecDefsLookup();
  const auto ent = Lookup(ctx, name);
  if (!ent.has_value()) {
    return std::nullopt;
  }
  if (!ModuleKind(*ent)) {
    return std::nullopt;
  }
  SPEC_RULE("Resolve-Module-Name");
  return ent;
}

bool RegionAliasName(const ScopeContext& ctx, std::string_view name) {
  SpecDefsLookup();
  const auto ent = ResolveValueName(ctx, name);
  return ent.has_value() && RegionAlias(*ent);
}

ResolveModulePathResult ResolveModulePath(const ScopeContext& ctx,
                                          const ast::ModulePath& path,
                                          const AliasMap& alias,
                                          const source::ModuleNames& module_names) {
  SpecDefsLookup();
  const source::ModuleNames visible_module_names =
      VisibleModuleNamesOf(ctx, module_names);
  const auto expanded = AliasExpand(path, alias);
  if (ContainsName(visible_module_names, core::StringOfPath(expanded))) {
    SPEC_RULE("Resolve-ModulePath-Direct");
    return {true, std::nullopt, expanded};
  }
  if (const auto local =
          ResolveInCurrentAssembly(ctx, expanded, visible_module_names);
      local.has_value()) {
    SPEC_RULE("Resolve-ModulePath-Current");
    return {true, std::nullopt, *local};
  }
  SPEC_RULE("ResolveModulePath-Err");
  return {false, "ResolveModulePath-Err", std::nullopt};
}

ResolveQualifiedResult ResolveQualified(const ScopeContext& ctx,
                                        const NameMapTable& name_maps,
                                        const source::ModuleNames& module_names,
                                        const ast::ModulePath& path,
                                        std::string_view name,
                                        EntityKind kind,
                                        CanAccessFn can_access) {
  SpecDefsLookup();
  if (kind == EntityKind::Value && path.size() == 1) {
    TypePath modal_path;
    modal_path.reserve(path.size());
    for (const auto& seg : path) {
      modal_path.push_back(seg);
    }
    if (IsBuiltinModalTypePath(modal_path) &&
        IsBuiltinModalMemberName(modal_path, name)) {
      Entity ent;
      ent.kind = EntityKind::Value;
      ent.origin_opt = ast::ModulePath{path[0]};
      ent.target_opt = ast::Identifier(name);
      ent.source = EntitySource::Decl;
      SPEC_RULE("Resolve-Qualified");
      return {true, std::nullopt, ent};
    }
  }

  const AliasMap alias = AliasMapForCurrentModule(ctx, name_maps);
  const auto module_result = ResolveModulePath(ctx, path, alias, module_names);
  if (!module_result.ok) {
    return {false, module_result.diag_id, std::nullopt};
  }
  const auto& module_path = *module_result.path;
  const auto map_it = name_maps.find(PathKeyOf(module_path));
  if (map_it == name_maps.end()) {
    return {false, std::nullopt, std::nullopt};
  }
  const auto& names = map_it->second;
  const auto ent_it = names.find(IdKeyOf(name));
  if (ent_it == names.end()) {
    return {false, std::nullopt, std::nullopt};
  }
  if (ent_it->second.kind != kind) {
    return {false, std::nullopt, std::nullopt};
  }
  if (can_access) {
    const auto access = can_access(ctx, module_path, name);
    if (!access.ok) {
      return {false, access.diag_id, std::nullopt};
    }
  }
  SPEC_RULE("Resolve-Qualified");
  return {true, std::nullopt, ent_it->second};
}

}  // namespace cursive::analysis
