#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "01_project/project.h"
#include "02_source/module_paths.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/resolve/scopes.h"

namespace ultraviolet::analysis {

using NameMap = Scope;
using NameMapTable = std::map<PathKey, NameMap>;
using AliasMap = std::map<IdKey, ast::ModulePath>;

struct AccessResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

using CanAccessFn = AccessResult (*)(const ScopeContext& ctx,
                                     const ast::ModulePath& module_path,
                                     std::string_view name);

AliasMap AliasMapOf(const NameMap& names);
source::ModuleNames ModuleNamesOf(const project::Project& project);
const ast::ASTModule* FindContextModuleByPath(const ScopeContext& ctx,
                                              const ast::ModulePath& path);
source::ModuleNames VisibleModuleNamesOf(
    const ScopeContext& ctx,
    const source::ModuleNames& all_module_names);

bool ValueKind(const Entity& ent);
bool TypeKind(const Entity& ent);
bool ClassKind(const Entity& ent);
bool ModuleKind(const Entity& ent);
bool RegionAlias(const Entity& ent);

std::optional<Entity> Lookup(const ScopeContext& ctx, std::string_view name);
std::optional<Entity> ResolveValueName(const ScopeContext& ctx,
                                       std::string_view name);
std::optional<Entity> ResolveTypeName(const ScopeContext& ctx,
                                      std::string_view name);
std::optional<Entity> ResolveClassName(const ScopeContext& ctx,
                                       std::string_view name);
std::optional<Entity> ResolveModuleName(const ScopeContext& ctx,
                                        std::string_view name);

bool RegionAliasName(const ScopeContext& ctx, std::string_view name);

struct ResolveModulePathResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<ast::ModulePath> path;
};

ResolveModulePathResult ResolveModulePath(const ScopeContext& ctx,
                                          const ast::ModulePath& path,
                                          const AliasMap& alias,
                                          const source::ModuleNames& module_names);

struct ResolveQualifiedResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<Entity> entity;
};

ResolveQualifiedResult ResolveQualified(const ScopeContext& ctx,
                                        const NameMapTable& name_maps,
                                        const source::ModuleNames& module_names,
                                        const ast::ModulePath& path,
                                        std::string_view name,
                                        EntityKind kind,
                                        CanAccessFn can_access);

}  // namespace ultraviolet::analysis
