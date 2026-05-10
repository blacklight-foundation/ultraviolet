// =============================================================================
// MIGRATION: item/using_decl.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 4: Module System
//   - using declaration grammar
//   - Direct scope injection
//   - Glob imports
//
// SOURCE: cursive-bootstrap/src/03_analysis/resolve/imports.cpp
//
// =============================================================================

#include "04_analysis/typing/type_decls.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "01_project/language_profile.h"
#include "04_analysis/typing/context.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/resolve/visibility.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsUsingDecl() {
  SPEC_DEF("Using-Resolve", "4");
  SPEC_DEF("Using-Vis", "4");
  SPEC_DEF("Using-Alias", "4");
  SPEC_DEF("Using-Glob", "4");
  SPEC_DEF("Using-Conflict", "4");
}

// =============================================================================
// HELPERS
// =============================================================================

// Check if module path is reserved
static bool IsReservedModulePath(const ast::ModulePath& path) {
  if (path.empty()) {
    return false;
  }
  return IdEq(path[0], project::ActiveLanguageProfile().runtime_root);
}

// Find a module in the sigma by path
static const ast::ASTModule* FindModule(const Sigma& sigma,
                                         const ast::ModulePath& path) {
  for (const auto& mod : sigma.mods) {
    if (mod.path == path) {
      return &mod;
    }
  }
  return nullptr;
}

static ast::ModulePath ResolveLocalModulePath(const Sigma& sigma,
                                              const ast::ModulePath& path,
                                              const ast::ModulePath& current_module) {
  if (FindModule(sigma, path) != nullptr) {
    return path;
  }
  if (path.empty() || current_module.empty()) {
    return path;
  }
  ast::ModulePath candidate;
  candidate.reserve(path.size() + 1);
  candidate.push_back(current_module.front());
  candidate.insert(candidate.end(), path.begin(), path.end());
  if (FindModule(sigma, candidate) != nullptr) {
    return candidate;
  }
  return path;
}

static bool SameAssembly(const ast::ModulePath& lhs,
                         const ast::ModulePath& rhs) {
  if (lhs.empty() || rhs.empty()) {
    return false;
  }
  return lhs.front() == rhs.front();
}

static bool PathPrefix(const ast::ModulePath& prefix,
                       const ast::ModulePath& path) {
  if (prefix.size() > path.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (prefix[i] != path[i]) {
      return false;
    }
  }
  return true;
}

static bool UsingDeclImportRequired(const ast::ModulePath& from_module,
                                    const ast::ModulePath& target_module) {
  if (from_module.empty() || target_module.empty()) {
    return false;
  }
  return !SameAssembly(from_module, target_module);
}

static bool UsingDeclImportCovers(const ScopeContext& ctx,
                                  const ast::ModulePath& from_module,
                                  const ast::ModulePath& target_module) {
  const auto* from_decl = FindModule(ctx.sigma, from_module);
  if (!from_decl) {
    return false;
  }
  for (const auto& item : from_decl->items) {
    const auto* import_decl = std::get_if<ast::ImportDecl>(&item);
    if (!import_decl) {
      continue;
    }
    const auto import_path =
        ResolveLocalModulePath(ctx.sigma, import_decl->path, from_module);
    if (PathPrefix(import_path, target_module)) {
      return true;
    }
  }
  return false;
}

// Check if an item exists as a type
static bool IsTypeItem(const Sigma& sigma, const PathKey& key) {
  return sigma.types.find(key) != sigma.types.end();
}

// Check if an item exists as a class
static bool IsClassItem(const Sigma& sigma, const PathKey& key) {
  return sigma.classes.find(key) != sigma.classes.end();
}

// Check if an item exists as a value (procedure, static, or extern procedure)
// in the module's AST. The Sigma does not have a values collection, so we
// look at the module items directly (matching ResolveGlobImport's logic).
static bool IsValueItem(const Sigma& sigma,
                        const ast::ModulePath& module_path,
                        const std::string& item_name) {
  const auto* mod = FindModule(sigma, module_path);
  if (!mod) {
    return false;
  }
  for (const auto& item : mod->items) {
    bool found = false;
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
            if (node.name == item_name) {
              found = true;
            }
          } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
            for (const auto& ext_item : node.items) {
              std::visit(
                  [&](const auto& ext_decl) {
                    using ET = std::decay_t<decltype(ext_decl)>;
                    if constexpr (std::is_same_v<ET, ast::ExternProcDecl>) {
                      if (ext_decl.name == item_name) {
                        found = true;
                      }
                    }
                  },
                  ext_item);
            }
          } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
            if (node.binding.pat) {
              std::visit(
                  [&](const auto& pat) {
                    using P = std::decay_t<decltype(pat)>;
                    if constexpr (std::is_same_v<P, ast::IdentifierPattern>) {
                      if (pat.name == item_name) {
                        found = true;
                      }
                    } else if constexpr (std::is_same_v<P,
                                                        ast::TypedPattern>) {
                      if (pat.name == "_") {
                        return;
                      }
                      if (pat.name == item_name) {
                        found = true;
                      }
                    }
                  },
                  node.binding.pat->node);
            }
          }
        },
        item);
    if (found) {
      return true;
    }
  }
  return false;
}

// Resolve a single item from a module
static UsingItemResult ResolveUsingItem(
    const ScopeContext& ctx,
    const ast::ModulePath& module_path,
    const std::string& item_name,
    const std::optional<std::string>& alias_opt,
    const ast::ModulePath& current_module) {
  SpecDefsUsingDecl();
  UsingItemResult result;
  result.ok = true;
  result.item_name = item_name;
  result.alias = alias_opt.value_or(item_name);
  const auto resolved_module_path =
      ResolveLocalModulePath(ctx.sigma, module_path, current_module);

  // Build full path to item
  ast::Path item_path;
  for (const auto& seg : resolved_module_path) {
    item_path.push_back(seg);
  }
  item_path.push_back(item_name);

  // Look up the item - check types, classes, then values
  const auto item_key = PathKeyOf(item_path);
  if (IsTypeItem(ctx.sigma, item_key)) {
    result.resolved = true;
    result.is_type = true;
  } else if (IsClassItem(ctx.sigma, item_key)) {
    result.resolved = true;
    result.is_type = true;  // Classes are treated as types
  } else if (IsValueItem(ctx.sigma, resolved_module_path, item_name)) {
    result.resolved = true;
    result.is_type = false;  // Procedures and statics are values
  } else {
    // Check if it's a reserved path
    if (!IsReservedModulePath(resolved_module_path)) {
      SPEC_RULE("Using-Item-NotFound");
      result.ok = false;
      result.diag_id = "Import-Using-Missing";
      return result;
    }
    result.resolved = false;
  }

  // Check visibility
  if (result.resolved) {
    if (!IsItemVisible(ctx, item_path, current_module)) {
      SPEC_RULE("Using-Vis-Err");
      result.ok = false;
      result.diag_id = "E-MOD-1207";
      return result;
    }
  }

  result.full_path = item_path;
  SPEC_RULE("Using-Item-Ok");
  return result;
}

// Resolve all exported items from a module (glob import)
static std::vector<UsingItemResult> ResolveGlobImport(
    const ScopeContext& ctx,
    const ast::ModulePath& module_path,
    const ast::ModulePath& current_module) {
  SpecDefsUsingDecl();
  std::vector<UsingItemResult> results;
  const auto resolved_module_path =
      ResolveLocalModulePath(ctx.sigma, module_path, current_module);

  // Look up the module
  const auto* found_module = FindModule(ctx.sigma, resolved_module_path);
  if (found_module == nullptr) {
    // Reserved paths may not exist yet
    if (!IsReservedModulePath(resolved_module_path)) {
      UsingItemResult err;
      err.ok = false;
      err.diag_id = "Resolve-Using-None";
      results.push_back(err);
    }
    return results;
  }

  // Collect all public items from the module
  for (const auto& item : found_module->items) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          std::string name;
          bool is_public = false;

          if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
            name = node.name;
            is_public = node.vis == ast::Visibility::Public;
          } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
            name = node.name;
            is_public = node.vis == ast::Visibility::Public;
          } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
            name = node.name;
            is_public = node.vis == ast::Visibility::Public;
          } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
            name = node.name;
            is_public = node.vis == ast::Visibility::Public;
          } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
            name = node.name;
            is_public = node.vis == ast::Visibility::Public;
          } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
            name = node.name;
            is_public = node.vis == ast::Visibility::Public;
          } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
            for (const auto& ext_item : node.items) {
              const auto* proc = std::get_if<ast::ExternProcDecl>(&ext_item);
              if (!proc || proc->vis != ast::Visibility::Public) {
                continue;
              }

              UsingItemResult item_result;
              item_result.ok = true;
              item_result.item_name = proc->name;
              item_result.alias = proc->name;
              item_result.resolved = true;

              ast::Path item_path;
              for (const auto& seg : resolved_module_path) {
                item_path.push_back(seg);
              }
              item_path.push_back(proc->name);
              item_result.full_path = item_path;
              item_result.is_type = false;

              results.push_back(std::move(item_result));
            }
            return;
          } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
            // Extract name from binding pattern
            if (node.binding.pat) {
              std::visit([&](const auto& pat) {
                using P = std::decay_t<decltype(pat)>;
                if constexpr (std::is_same_v<P, ast::IdentifierPattern>) {
                  name = pat.name;
                } else if constexpr (std::is_same_v<P, ast::TypedPattern>) {
                  if (pat.name == "_") {
                    return;
                  }
                  name = pat.name;
                }
              }, node.binding.pat->node);
            }
            is_public = node.vis == ast::Visibility::Public;
          }

          if (is_public && !name.empty()) {
            UsingItemResult item_result;
            item_result.ok = true;
            item_result.item_name = name;
            item_result.alias = name;
            item_result.resolved = true;

            // Build full path
            ast::Path item_path;
            for (const auto& seg : resolved_module_path) {
              item_path.push_back(seg);
            }
            item_path.push_back(name);
            item_result.full_path = item_path;

            // Check if it's a type
            const auto item_key = PathKeyOf(item_path);
            item_result.is_type = IsTypeItem(ctx.sigma, item_key) ||
                                  IsClassItem(ctx.sigma, item_key);

            results.push_back(item_result);
          }
        },
        item);
  }

  SPEC_RULE("Using-Glob-Ok");
  return results;
}

}  // namespace

// =============================================================================
// EXPORTED: TypeUsingDecl
// =============================================================================

UsingDeclResult TypeUsingDecl(
    const ScopeContext& ctx,
    const ast::UsingDecl& decl,
    const ast::ModulePath& current_module) {
  SpecDefsUsingDecl();
  UsingDeclResult result;
  result.ok = true;

  const auto attr_validation =
      ValidateUnsupportedAttributeTarget(ast::AttrListOf(decl.attrs_opt),
                                         "using declarations");
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }

  // Handle different using forms based on clause
  std::visit(
      [&](const auto& clause) {
        using T = std::decay_t<decltype(clause)>;

        if constexpr (std::is_same_v<T, ast::UsingItem>) {
          // Single item: using foo::bar
          const auto item_result = ResolveUsingItem(
              ctx, clause.module_path, clause.name, clause.alias_opt, current_module);
          if (!item_result.ok) {
            result.ok = false;
            result.diag_id = item_result.diag_id;
          } else {
            result.items.push_back(item_result);
          }

        } else if constexpr (std::is_same_v<T, ast::UsingList>) {
          // List: using foo::{bar, baz}
          for (const auto& spec : clause.specs) {
            const auto item_result = ResolveUsingItem(
                ctx, clause.module_path, spec.name, spec.alias_opt, current_module);
            if (!item_result.ok) {
              result.ok = false;
              result.diag_id = item_result.diag_id;
              return;
            }
            result.items.push_back(item_result);
          }

        } else if constexpr (std::is_same_v<T, ast::UsingWildcard>) {
          // Glob: using foo::*
          result.is_glob = true;
          const auto glob_results = ResolveGlobImport(
              ctx, clause.module_path, current_module);
          for (const auto& item_result : glob_results) {
            if (!item_result.ok) {
              result.ok = false;
              result.diag_id = item_result.diag_id;
              return;
            }
            result.items.push_back(item_result);
          }
        }
      },
      decl.clause);

  if (result.ok) {
    SPEC_RULE("Using-Ok");
  }
  return result;
}

// =============================================================================
// EXPORTED: CheckUsingConflict
// =============================================================================

UsingConflictResult CheckUsingConflict(
    const ScopeContext& ctx,
    const std::vector<UsingItemResult>& existing_items,
    const UsingItemResult& new_item) {
  SpecDefsUsingDecl();
  UsingConflictResult result;
  result.ok = true;

  for (const auto& existing : existing_items) {
    if (existing.alias == new_item.alias) {
      // Same alias - check if same target
      if (existing.full_path != new_item.full_path) {
        SPEC_RULE("Using-Conflict-Err");
        result.ok = false;
        result.diag_id = "Import-Using-Name-Conflict";
        result.conflicting_alias = new_item.alias;
        result.existing_path = existing.full_path;
        result.new_path = new_item.full_path;
        return result;
      }
    }
  }

  (void)ctx;
  SPEC_RULE("Using-NoConflict");
  return result;
}

// =============================================================================
// EXPORTED: InjectUsingItems
// =============================================================================

InjectUsingResult InjectUsingItems(
    ScopeContext& ctx,
    const UsingDeclResult& using_result,
    const ast::ModulePath& current_module) {
  SpecDefsUsingDecl();
  InjectUsingResult result;
  result.ok = true;

  if (!using_result.ok) {
    result.ok = false;
    result.diag_id = using_result.diag_id;
    return result;
  }

  // Inject items into current scope
  auto& module_scope = ModuleScope(ctx.scopes);

  for (const auto& item : using_result.items) {
    if (!item.resolved) {
      continue;
    }

    // Check for existing binding with same alias
    const auto existing_it = module_scope.find(item.alias);
    if (existing_it != module_scope.end()) {
      // Already exists - check if from same origin
      if (existing_it->second.source == EntitySource::Decl) {
        // Local declaration shadows using
        result.shadowed_by_local.push_back(item.alias);
        continue;
      }
    }

    // Inject the binding
    Entity entity;
    entity.kind = item.is_type ? EntityKind::Type : EntityKind::Value;
    entity.source = EntitySource::Using;
    entity.origin_opt = ast::ModulePath(item.full_path.begin(),
                                         item.full_path.end() - 1);
    entity.target_opt = item.full_path.back();

    module_scope[item.alias] = entity;
    result.injected_names.push_back(item.alias);
  }

  (void)current_module;
  SPEC_RULE("Using-Inject-Ok");
  return result;
}

// =============================================================================
// EXPORTED: IsItemVisible
// =============================================================================

bool IsItemVisible(const ScopeContext& ctx,
                   const ast::Path& item_path,
                   const ast::ModulePath& from_module) {
  SpecDefsUsingDecl();

  if (item_path.empty()) {
    return false;
  }

  // Get the module path (all segments except last)
  ast::ModulePath module_path(item_path.begin(), item_path.end() - 1);
  module_path = ResolveLocalModulePath(ctx.sigma, module_path, from_module);

  // Find the module
  const auto* found_module = FindModule(ctx.sigma, module_path);
  if (found_module == nullptr) {
    // Reserved paths may not exist yet
    return IsReservedModulePath(module_path);
  }

  // Find the item and check visibility
  const auto& item_name = item_path.back();
  for (const auto& item : found_module->items) {
    bool found = false;
    ast::Visibility vis = ast::Visibility::Private;

    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
            if (node.name == item_name) {
              found = true;
              vis = node.vis;
            }
          } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
            for (const auto& ext_item : node.items) {
              if (const auto* proc = std::get_if<ast::ExternProcDecl>(&ext_item);
                  proc && proc->name == item_name) {
                found = true;
                vis = proc->vis;
                break;
              }
            }
          } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
            if (node.binding.pat) {
              std::visit(
                  [&](const auto& pat) {
                    using P = std::decay_t<decltype(pat)>;
                    if constexpr (std::is_same_v<P, ast::IdentifierPattern> ||
                                  std::is_same_v<P, ast::TypedPattern>) {
                      if (pat.name == "_") {
                        return;
                      }
                      if (pat.name == item_name) {
                        found = true;
                        vis = node.vis;
                      }
                    }
                  },
                  node.binding.pat->node);
            }
          } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
            if (node.name == item_name) {
              found = true;
              vis = node.vis;
            }
          } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
            if (node.name == item_name) {
              found = true;
              vis = node.vis;
            }
          } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
            if (node.name == item_name) {
              found = true;
              vis = node.vis;
            }
          } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
            if (node.name == item_name) {
              found = true;
              vis = node.vis;
            }
          } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
            if (node.name == item_name) {
              found = true;
              vis = node.vis;
            }
          }
        },
        item);

    if (found) {
      return IsModuleVisible(ctx, module_path, from_module) &&
             (vis == ast::Visibility::Public ||
              (vis == ast::Visibility::Internal &&
               SameAssembly(module_path, from_module)) ||
              module_path == from_module);
    }
  }

  return false;
}

// =============================================================================
// EXPORTED: IsModuleVisible
// =============================================================================

bool IsModuleVisible(const ScopeContext& ctx,
                     const ast::ModulePath& module_path,
                     const ast::ModulePath& from_module) {
  SpecDefsUsingDecl();

  // Reserved paths are always accessible
  if (IsReservedModulePath(module_path)) {
    return true;
  }
  const auto resolved = ResolveLocalModulePath(ctx.sigma, module_path, from_module);
  if (FindModule(ctx.sigma, resolved) == nullptr) {
    return false;
  }
  if (!UsingDeclImportRequired(from_module, resolved)) {
    return true;
  }
  return UsingDeclImportCovers(ctx, from_module, resolved);
}

}  // namespace cursive::analysis
