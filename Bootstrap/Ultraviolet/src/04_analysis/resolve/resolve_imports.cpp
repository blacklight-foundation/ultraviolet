// =============================================================================
// resolve_imports.cpp - Import Declaration Validation
// =============================================================================
//
// SPEC REFERENCE:
//   SPECIFICATION.md §11.5.4 "Module Path Resolution"
//     - ResolveImportPath (Resolve-Import-Direct, Resolve-Import-Current,
//       Resolve-Import-Err)
//     - ImportRequired, ImportCovers, ImportOk
//   SPECIFICATION.md §7.4 "Visibility and Accessibility"
//     - CanAccess (Access-Public, Access-Internal, Access-Private, Access-Err)
//
// Import path resolution is implemented by:
//   source::ResolveImportModulePath  (module_paths.cpp)
// Import accessibility is implemented by:
//   ImportRequired, ImportCovers, ImportOk  (resolve_items.h / collect_toplevel.cpp)
// Visibility is implemented by:
//   CanAccessVis  (visibility.cpp)
// Assembly-level import graph validation is implemented by:
//   BuildAssemblyImportGraph, ValidateAssemblyImportGraphStructure
//     (assembly_import_graph.cpp)
//
// This file provides per-declaration validation that composes those primitives.
// =============================================================================

#include "04_analysis/resolve/resolve_imports.h"

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsImports() {
  SPEC_DEF("ResolveImportPath", "11.5.4");
  SPEC_DEF("ImportRequired", "11.5.4");
  SPEC_DEF("ImportOk", "11.5.4");
}

}  // namespace

ImportValidationResult ValidateImportDecl(
    const ast::ImportDecl& import,
    const ast::ModulePath& current_module,
    const source::ModuleNames& module_names) {
  SpecDefsImports();

  if (import.path.empty()) {
    SPEC_RULE("Resolve-Import-Err");
    return {false, "Resolve-Import-Err", import.span};
  }

  if (import.path == current_module) {
    return {false, "Resolve-Import-Err", import.span};
  }

  const auto resolved =
      source::ResolveImportModulePath(current_module, module_names,
                                      import.path);
  if (!resolved.has_value()) {
    SPEC_RULE("Resolve-Import-Err");
    return {false, "Resolve-Import-Err", import.span};
  }

  return {true, std::nullopt, std::nullopt};
}

core::DiagnosticStream ValidateModuleImports(
    const ast::ASTModule& module,
    const source::ModuleNames& module_names) {
  SpecDefsImports();
  core::DiagnosticStream diags;
  for (const auto& item : module.items) {
    const auto* import = std::get_if<ast::ImportDecl>(&item);
    if (!import) {
      continue;
    }
    const auto result =
        ValidateImportDecl(*import, module.path, module_names);
    if (!result.ok && result.diag_id.has_value()) {
      if (auto diag = core::MakeDiagnosticById(*result.diag_id, result.span)) {
        core::Emit(diags, *diag);
      }
    }
  }
  return diags;
}

}  // namespace ultraviolet::analysis
