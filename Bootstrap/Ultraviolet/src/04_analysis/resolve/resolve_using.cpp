// =============================================================================
// resolve_using.cpp - Using Declaration Resolution
// =============================================================================
//
// SPEC REFERENCE:
//   Docs/SPECIFICATION.md §5.1.5 "Top-Level Name Collection" (Lines 6956-7309)
//   Docs/SPECIFICATION.md §5.1.4 "Visibility and Accessibility" (Lines 6900-6955)
//
// SOURCE FILE:
//   Migrated from ultraviolet-bootstrap/src/03_analysis/resolve/collect_toplevel.cpp
//   (Lines 900-1064)
//
// =============================================================================

#include "04_analysis/resolve/resolve_items.h"

#include <algorithm>
#include <set>
#include <type_traits>

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/resolve/visibility.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsResolveUsing() {
  SPEC_DEF("ResolveUsing", "5.1.5");
  SPEC_DEF("ResolveUsingSingle", "5.1.5");
  SPEC_DEF("ResolveUsingMultiple", "5.1.5");
  SPEC_DEF("ResolveUsingGlob", "5.1.5");
  SPEC_DEF("UsingNames", "5.1.5");
  SPEC_DEF("UsingSpecName", "5.1.5");
  SPEC_DEF("UsingSpecNames", "5.1.5");
}

std::optional<ast::ModulePath> ResolveVisibleModulePath(
    const ScopeContext& ctx,
    const source::ModuleNames& module_names,
    const ast::ModulePath& path) {
  return source::ResolveImportModulePath(CurrentModule(ctx), module_names, path);
}

std::optional<std::pair<ast::ModulePath, ast::Identifier>> SplitLast(
    const ast::ModulePath& path) {
  SpecDefsResolveUsing();
  if (path.size() < 2) {
    return std::nullopt;
  }
  ast::ModulePath prefix(path.begin(), path.end() - 1);
  return std::make_pair(prefix, path.back());
}

std::optional<ast::Visibility> ItemVisibility(const ast::ASTItem& item) {
  return std::visit(
      [](const auto& it) -> std::optional<ast::Visibility> {
        using T = std::decay_t<decltype(it)>;
        if constexpr (std::is_same_v<T, ast::ErrorItem>) {
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
          return ast::Visibility::Internal;
        } else {
          return it.vis;
        }
      },
      item);
}

bool NameMatches(const IdKey& key, std::string_view candidate) {
  return key == IdKeyOf(candidate);
}

bool PatternBindsName(const ast::Pattern& pattern, const IdKey& key);

bool PatternBindsName(const ast::PatternPtr& pattern, const IdKey& key) {
  if (!pattern) {
    return false;
  }
  return PatternBindsName(*pattern, key);
}

bool FieldPatternBindsName(const ast::FieldPattern& field, const IdKey& key) {
  if (field.pattern_opt) {
    return PatternBindsName(field.pattern_opt, key);
  }
  return NameMatches(key, field.name);
}

bool RecordFieldsBindName(const std::vector<ast::FieldPattern>& fields,
                          const IdKey& key) {
  for (const auto& field : fields) {
    if (FieldPatternBindsName(field, key)) {
      return true;
    }
  }
  return false;
}

bool PatternBindsName(const ast::Pattern& pattern, const IdKey& key) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          return NameMatches(key, node.name);
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          if (node.name == "_") {
            return false;
          }
          return NameMatches(key, node.name);
        } else if constexpr (std::is_same_v<T, ast::WildcardPattern> ||
                             std::is_same_v<T, ast::LiteralPattern>) {
          return false;
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& elem : node.elements) {
            if (PatternBindsName(elem, key)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          return RecordFieldsBindName(node.fields, key);
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (!node.payload_opt) {
            return false;
          }
          return std::visit(
              [&](const auto& payload) -> bool {
                using P = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                  for (const auto& elem : payload.elements) {
                    if (PatternBindsName(elem, key)) {
                      return true;
                    }
                  }
                  return false;
                } else {
                  return RecordFieldsBindName(payload.fields, key);
                }
              },
              *node.payload_opt);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (!node.fields_opt) {
            return false;
          }
          return RecordFieldsBindName(node.fields_opt->fields, key);
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          return PatternBindsName(node.lo, key) || PatternBindsName(node.hi, key);
        } else {
          return false;
        }
      },
      pattern.node);
}

bool UsingClauseBindsName(const ast::UsingClause& clause, const IdKey& key) {
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::UsingItem>) {
          if (node.alias_opt) {
            return NameMatches(key, *node.alias_opt);
          }
          return NameMatches(key, node.name);
        } else if constexpr (std::is_same_v<T, ast::UsingWildcard>) {
          // Wildcard binds all visible names from the module.
          (void)node;
          return true;
        } else {
          for (const auto& spec : node.specs) {
            if (spec.alias_opt) {
              if (NameMatches(key, *spec.alias_opt)) {
                return true;
              }
              continue;
            }
            if (NameMatches(key, spec.name)) {
              return true;
            }
          }
          return false;
        }
      },
      clause);
}

bool ItemBindsName(const ast::ASTItem& item, const IdKey& key) {
  return std::visit(
      [&](const auto& it) -> bool {
        using T = std::decay_t<decltype(it)>;
        if constexpr (std::is_same_v<T, ast::UsingDecl>) {
          return UsingClauseBindsName(it.clause, key);
        } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          if (!it.binding.pat) {
            return false;
          }
          return PatternBindsName(*it.binding.pat, key);
        } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
          for (const auto& ext_item : it.items) {
            if (const auto* proc = std::get_if<ast::ExternProcDecl>(&ext_item);
                proc && NameMatches(key, proc->name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::ProcedureDecl> ||
                             std::is_same_v<T, ast::RecordDecl> ||
                             std::is_same_v<T, ast::EnumDecl> ||
                             std::is_same_v<T, ast::ModalDecl> ||
                             std::is_same_v<T, ast::ClassDecl> ||
                             std::is_same_v<T, ast::TypeAliasDecl>) {
          return NameMatches(key, it.name);
        } else {
          return false;
        }
      },
      item);
}

std::optional<ast::Visibility> DeclVisibility(const ast::ASTItem& item,
                                              std::string_view name) {
  return std::visit(
      [&](const auto& it) -> std::optional<ast::Visibility> {
        using T = std::decay_t<decltype(it)>;
        if constexpr (std::is_same_v<T, ast::ErrorItem>) {
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
          for (const auto& ext_item : it.items) {
            if (const auto* proc = std::get_if<ast::ExternProcDecl>(&ext_item);
                proc && proc->name == name) {
              return proc->vis;
            }
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          if (it.binding.pat && PatternBindsName(*it.binding.pat, IdKeyOf(name))) {
            return it.vis;
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
          if (ItemBindsName(item, IdKeyOf(name))) {
            return ast::Visibility::Internal;
          }
          return std::nullopt;
        } else {
          if (ItemBindsName(item, IdKeyOf(name))) {
            return it.vis;
          }
          return std::nullopt;
        }
      },
      item);
}

std::optional<ast::Visibility> FindDeclVisibility(const ScopeContext& ctx,
                                                  const ast::ModulePath& module_path,
                                                  std::string_view name) {
  const auto* module = FindContextModuleByPath(ctx, module_path);
  if (!module) {
    return std::nullopt;
  }
  const auto key = IdKeyOf(name);
  for (const auto& item : module->items) {
    if (auto vis = DeclVisibility(item, name); vis.has_value()) {
      return vis;
    }
  }
  for (const auto& item : module->items) {
    if (std::holds_alternative<ast::UsingDecl>(item) && ItemBindsName(item, key)) {
      return ItemVisibility(item);
    }
  }
  return std::nullopt;
}

IdKey UsingSpecName(const ast::UsingSpec& spec) {
  if (spec.alias_opt.has_value()) {
    return IdKeyOf(*spec.alias_opt);
  }
  return IdKeyOf(spec.name);
}

std::vector<IdKey> UsingSpecNames(const std::vector<ast::UsingSpec>& specs) {
  SpecDefsResolveUsing();
  std::vector<IdKey> names;
  names.reserve(specs.size());
  for (const auto& spec : specs) {
    names.push_back(UsingSpecName(spec));
  }
  return names;
}

bool DistinctUsingSpecNames(const std::vector<ast::UsingSpec>& specs) {
  std::vector<IdKey> names = UsingSpecNames(specs);
  std::sort(names.begin(), names.end());
  auto dup = std::adjacent_find(names.begin(), names.end());
  return dup == names.end();
}

std::set<IdKey> ItemNamesOf(const NameMapTable& name_maps,
                            const ast::ModulePath& module_path) {
  SpecDefsResolveUsing();
  std::set<IdKey> names;
  const auto it = name_maps.find(PathKeyOf(module_path));
  if (it == name_maps.end()) {
    return names;
  }
  for (const auto& [key, ent] : it->second) {
    if (ent.kind == EntityKind::Value || ent.kind == EntityKind::Type ||
        ent.kind == EntityKind::Class) {
      names.insert(key);
    }
  }
  return names;
}

}  // namespace

// -----------------------------------------------------------------------------
// ResolveUsingPath
// -----------------------------------------------------------------------------
// Resolves a using path to an item (module path + terminal item name).

ResolveUsingPathResult ResolveUsingPath(const ScopeContext& ctx,
                                        const NameMapTable& name_maps,
                                        const source::ModuleNames& module_names,
                                        const ast::ModulePath& path) {
  SpecDefsResolveUsing();
  ResolveUsingPathResult result;
  const auto item = ItemOfPath(ctx, name_maps, module_names, path);
  if (item.ok) {
    SPEC_RULE("Resolve-Using-Ok");
    SPEC_RULE("Resolve-Using-Item");
    result.ok = true;
    result.module_path = item.module_path;
    result.item = item.name;
    return result;
  }
  SPEC_RULE("Resolve-Using-Err");
  SPEC_RULE("Resolve-Using-None");
  result.ok = false;
  result.diag_id = "Resolve-Using-None";
  return result;
}

// -----------------------------------------------------------------------------
// ItemOfPath
// -----------------------------------------------------------------------------
// Determines if a path refers to an item within a module.
// Returns the module path and item name if found.

ItemOfPathResult ItemOfPath(const ScopeContext& ctx,
                            const NameMapTable& name_maps,
                            const source::ModuleNames& module_names,
                            const ast::ModulePath& path) {
  SpecDefsResolveUsing();
  ItemOfPathResult result;
  const auto split = SplitLast(path);
  if (!split.has_value()) {
    SPEC_RULE("ItemOfPath-None");
    return result;
  }
  const auto& module_path = split->first;
  const auto& name = split->second;
  const auto resolved_module =
      ResolveVisibleModulePath(ctx, module_names, module_path);
  if (!resolved_module.has_value()) {
    SPEC_RULE("ItemOfPath-None");
    return result;
  }
  if (!FindContextModuleByPath(ctx, *resolved_module)) {
    SPEC_RULE("ItemOfPath-None");
    return result;
  }
  const auto item_names = ItemNamesOf(name_maps, *resolved_module);
  if (item_names.find(IdKeyOf(name)) == item_names.end()) {
    SPEC_RULE("ItemOfPath-None");
    return result;
  }
  SPEC_RULE("ItemOfPath");
  result.ok = true;
  result.module_path = *resolved_module;
  result.name = name;
  return result;
}

// -----------------------------------------------------------------------------
// UsingNames
// -----------------------------------------------------------------------------
// Resolves a using declaration to a list of bound names.
// Implements (Resolve-Using):
//   - UsingItem: Resolves a single imported item
//   - UsingList: Resolves multiple items from a module
//   - UsingWildcard: Resolves all public names from a module

UsingNamesResult UsingNames(const ScopeContext& ctx,
                            const NameMapTable& name_maps,
                            const source::ModuleNames& module_names,
                            const ast::UsingDecl& decl) {
  SpecDefsResolveUsing();
  UsingNamesResult result;
  return std::visit(
      [&](const auto& clause) -> UsingNamesResult {
        using T = std::decay_t<decltype(clause)>;
        if constexpr (std::is_same_v<T, ast::UsingItem>) {
          ast::ModulePath raw_path = clause.module_path;
          raw_path.push_back(clause.name);
          const auto resolved =
              ResolveUsingPath(ctx, name_maps, module_names, raw_path);
          if (!resolved.ok) {
            return {false, resolved.diag_id, decl.span, {}};
          }
          if (!resolved.item.has_value()) {
            return {false, "Resolve-Using-None", decl.span, {}};
          }
          if (!ImportOk(ctx, module_names, CurrentModule(ctx),
                        resolved.module_path)) {
            return {false, "Import-Using-Missing", decl.span, {}};
          }
          const auto access =
              CanAccess(ctx, resolved.module_path, *resolved.item);
          if (!access.ok) {
            return {false, access.diag_id, decl.span, {}};
          }
          if (decl.vis == ast::Visibility::Public) {
            const auto item_vis =
                FindDeclVisibility(ctx, resolved.module_path, *resolved.item);
            if (!item_vis.has_value() || *item_vis != ast::Visibility::Public) {
              SPEC_RULE("Using-Item-Public-Err");
              return {false, "Using-Path-Item-Public-Err", decl.span, {}};
            }
          }
          const auto map_it = name_maps.find(PathKeyOf(resolved.module_path));
          if (map_it == name_maps.end()) {
            return {false, "Resolve-Using-None", decl.span, {}};
          }
          const auto ent_it = map_it->second.find(IdKeyOf(*resolved.item));
          if (ent_it == map_it->second.end()) {
            return {false, "Resolve-Using-None", decl.span, {}};
          }
          const auto kind = ent_it->second.kind;
          if (kind != EntityKind::Value && kind != EntityKind::Type &&
              kind != EntityKind::Class) {
            return {false, "Resolve-Using-None", decl.span, {}};
          }
          SPEC_RULE("Using-Path-Item");
          BindingList bindings;
          bindings.push_back(BoundName{
              IdKeyOf(clause.alias_opt.value_or(*resolved.item)),
              Entity{kind, resolved.module_path, *resolved.item,
                     EntitySource::Using},
              decl.span,
          });
          return {true, std::nullopt, std::nullopt, bindings};
        } else if constexpr (std::is_same_v<T, ast::UsingWildcard>) {
          const auto module_path =
              ResolveVisibleModulePath(ctx, module_names, clause.module_path);
          if (!module_path.has_value()) {
            return {false, "Resolve-Using-None", decl.span, {}};
          }
          if (!ImportOk(ctx, module_names, CurrentModule(ctx), *module_path)) {
            return {false, "Import-Using-Missing", decl.span, {}};
          }
          const auto map_it = name_maps.find(PathKeyOf(*module_path));
          if (map_it == name_maps.end()) {
            return {false, "Resolve-Using-None", decl.span, {}};
          }
          std::vector<std::pair<std::string, EntityKind>> items;
          items.reserve(map_it->second.size());
          for (const auto& [key, ent] : map_it->second) {
            if (ent.kind != EntityKind::Value && ent.kind != EntityKind::Type &&
                ent.kind != EntityKind::Class) {
              continue;
            }
            const std::string name = key;
            const auto access = CanAccess(ctx, *module_path, name);
            if (!access.ok) {
              continue;
            }
            items.emplace_back(name, ent.kind);
          }
          if (decl.vis == ast::Visibility::Public) {
            for (const auto& [name, _kind] : items) {
              const auto item_vis = FindDeclVisibility(ctx, *module_path, name);
              if (!item_vis.has_value() || *item_vis != ast::Visibility::Public) {
                SPEC_RULE("Using-List-Public-Err");
                return {false, "Using-List-Public-Err", decl.span, {}};
              }
            }
          }
          BindingList bindings;
          for (const auto& [name, kind] : items) {
            bindings.push_back(BoundName{
                IdKeyOf(name),
                Entity{kind, *module_path, name, EntitySource::Using},
                decl.span,
            });
          }
          SPEC_RULE("Using-Wildcard");
          return {true, std::nullopt, std::nullopt, bindings};
        } else {
          // UsingList
          const auto module_path =
              ResolveVisibleModulePath(ctx, module_names, clause.module_path);
          if (!module_path.has_value()) {
            return {false, "Resolve-Using-None", decl.span, {}};
          }
          if (!ImportOk(ctx, module_names, CurrentModule(ctx), *module_path)) {
            return {false, "Import-Using-Missing", decl.span, {}};
          }
          if (!DistinctUsingSpecNames(clause.specs)) {
            SPEC_RULE("Using-List-Dup");
            return {false, "Using-List-Dup", decl.span, {}};
          }
          const auto map_it = name_maps.find(PathKeyOf(*module_path));
          if (map_it == name_maps.end()) {
            return {false, "Resolve-Using-None", decl.span, {}};
          }
          BindingList bindings;
          for (const auto& spec : clause.specs) {
            const auto ent_it = map_it->second.find(IdKeyOf(spec.name));
            if (ent_it == map_it->second.end()) {
              return {false, "Resolve-Using-None", decl.span, {}};
            }
            const auto kind = ent_it->second.kind;
            if (kind != EntityKind::Value && kind != EntityKind::Type &&
                kind != EntityKind::Class) {
              return {false, "Resolve-Using-None", decl.span, {}};
            }
            const auto access = CanAccess(ctx, *module_path, spec.name);
            if (!access.ok) {
              return {false, access.diag_id, decl.span, {}};
            }
            if (decl.vis == ast::Visibility::Public) {
              const auto item_vis =
                  FindDeclVisibility(ctx, *module_path, spec.name);
              if (!item_vis.has_value() || *item_vis != ast::Visibility::Public) {
                SPEC_RULE("Using-List-Public-Err");
                return {false, "Using-List-Public-Err", decl.span, {}};
              }
            }
            const auto bind_name = spec.alias_opt.value_or(spec.name);
            bindings.push_back(BoundName{
                IdKeyOf(bind_name),
                Entity{kind, *module_path, spec.name, EntitySource::Using},
                decl.span,
            });
          }
          SPEC_RULE("Using-List");
          return {true, std::nullopt, std::nullopt, bindings};
        }
      },
      decl.clause);
}

}  // namespace ultraviolet::analysis
