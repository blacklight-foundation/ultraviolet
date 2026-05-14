// =============================================================================
// MIGRATION: item/import_decl.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 4: Module System
//   - import declaration grammar
//   - Module path resolution
//   - Namespace aliasing
//
// SOURCE: cursive-bootstrap/src/03_analysis/types/imports.cpp
//
// =============================================================================

#include "04_analysis/typing/type_decls.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/symbols.h"
#include "04_analysis/typing/context.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/resolve/visibility.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsImportDecl() {
  SPEC_DEF("Import-Resolve", "4");
  SPEC_DEF("Import-Vis", "4");
  SPEC_DEF("Import-Alias", "4");
}

// =============================================================================
// HELPERS
// =============================================================================

// Get the alias name for an import (last segment or explicit alias)
static std::string GetImportAlias(const ast::ImportDecl& decl) {
  if (decl.alias_opt.has_value() && !decl.alias_opt->empty()) {
    return *decl.alias_opt;
  }
  if (!decl.path.empty()) {
    return decl.path.back();
  }
  return "";
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

static source::ModuleNames ModuleNamesForContext(const ScopeContext& ctx) {
  source::ModuleNames names;
  names.reserve(ctx.sigma.mods.size());
  for (const auto& mod : ctx.sigma.mods) {
    names.insert(core::StringOfPath(mod.path));
  }
  if (!names.empty()) {
    return names;
  }
  if (ctx.project != nullptr) {
    return ModuleNamesOf(*ctx.project);
  }
  return names;
}

}  // namespace

// =============================================================================
// EXPORTED: TypeImportDecl
// =============================================================================

ImportDeclResult TypeImportDecl(
    const ScopeContext& ctx,
    const ast::ImportDecl& decl,
    const ast::ModulePath& current_module) {
  SpecDefsImportDecl();
  ImportDeclResult result;
  result.ok = true;

  const auto attr_validation =
      ValidateUnsupportedAttributeTarget(ast::AttrListOf(decl.attrs_opt),
                                         "import declarations");
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }
  result.alias = GetImportAlias(decl);

  // Check for empty path
  if (decl.path.empty()) {
    SPEC_RULE("Import-Empty-Path");
    result.ok = false;
    result.diag_id = "Import-Using-Missing";
    return result;
  }

  // Check for valid alias name
  if (result.alias.empty()) {
    SPEC_RULE("Import-No-Alias");
    result.ok = false;
    result.diag_id = "Import-Using-Missing";
    return result;
  }

  const source::ModuleNames module_names = ModuleNamesForContext(ctx);
  const auto resolved_path =
      source::ResolveImportModulePath(current_module, module_names, decl.path);
  if (!resolved_path.has_value()) {
    result.ok = false;
    result.diag_id = "Resolve-Import-Err";
    result.path = decl.path;
    return result;
  }
  result.path = *resolved_path;

  // Resolve the module path
  const auto* found_module = FindModule(ctx.sigma, *resolved_path);
  if (found_module == nullptr) {
    result.ok = false;
    result.diag_id = "Resolve-Import-Err";
    return result;
  }

  result.resolved = true;

  // Check visibility - module must be accessible from current location
  if (!IsModuleVisible(ctx, *resolved_path, current_module)) {
    SPEC_RULE("Import-Vis-Err");
    result.ok = false;
    result.diag_id = "Resolve-Import-Err";
    return result;
  }

  SPEC_RULE("Import-Path");
  return result;
}

// =============================================================================
// EXPORTED: ResolveImportPath
// =============================================================================

ImportResolveResult ResolveImportPath(
    const ScopeContext& ctx,
    const ast::ModulePath& path) {
  SpecDefsImportDecl();
  ImportResolveResult result;
  result.ok = true;

  if (path.empty()) {
    result.ok = false;
    result.diag_id = "Import-Using-Missing";
    return result;
  }

  const source::ModuleNames module_names = ModuleNamesForContext(ctx);
  const auto resolved_path =
      source::ResolveImportModulePath(CurrentModule(ctx), module_names, path);
  if (!resolved_path.has_value()) {
    result.ok = false;
    result.diag_id = "Resolve-Import-Err";
    result.path = path;
    return result;
  }
  result.path = *resolved_path;

  // Look up the module
  const auto* found_module = FindModule(ctx.sigma, *resolved_path);
  if (found_module == nullptr) {
    result.ok = false;
    result.diag_id = "Resolve-Import-Err";
    return result;
  }

  result.resolved = true;
  // Collect exported items from the module
  for (const auto& item : found_module->items) {
    // Extract name from item and check if public
    std::visit(
        [&result](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
            if (node.vis == ast::Visibility::Public) {
              result.exported_names.push_back(node.name);
            }
          } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
            if (node.vis == ast::Visibility::Public) {
              result.exported_names.push_back(node.name);
            }
          } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
            if (node.vis == ast::Visibility::Public) {
              result.exported_names.push_back(node.name);
            }
          } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
            if (node.vis == ast::Visibility::Public) {
              result.exported_names.push_back(node.name);
            }
          } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
            if (node.vis == ast::Visibility::Public) {
              result.exported_names.push_back(node.name);
            }
          } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
            if (node.vis == ast::Visibility::Public) {
              result.exported_names.push_back(node.name);
            }
          } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
            if (node.vis == ast::Visibility::Public) {
              // Extract name from binding pattern
              if (node.binding.pat) {
                std::visit([&result](const auto& pat) {
                  using P = std::decay_t<decltype(pat)>;
                  if constexpr (std::is_same_v<P, ast::IdentifierPattern>) {
                    result.exported_names.push_back(pat.name);
                  } else if constexpr (std::is_same_v<P, ast::TypedPattern>) {
                    if (pat.name == "_") {
                      return;
                    }
                    result.exported_names.push_back(pat.name);
                  }
                }, node.binding.pat->node);
              }
            }
          }
        },
        item);
  }

  SPEC_RULE("Import-Resolve");
  return result;
}

// =============================================================================
// EXPORTED: CheckImportCycle
// =============================================================================

bool CheckImportCycle(
    const ScopeContext& ctx,
    const ast::ModulePath& importing_module,
    const ast::ModulePath& imported_module) {
  SpecDefsImportDecl();

  // Check for direct self-import
  if (importing_module == imported_module) {
    return true;  // Cycle detected
  }

  // For now, a simplified cycle check
  // A full implementation would track the import graph
  // and detect cycles via DFS

  return false;  // No cycle detected
}

}  // namespace cursive::analysis
