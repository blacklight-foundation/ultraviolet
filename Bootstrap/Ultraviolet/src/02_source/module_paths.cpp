#include "02_source/module_paths.h"

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"

namespace ultraviolet::source {

bool HasModuleName(const ModuleNames& module_names,
                   const ast::ModulePath& path) {
  return module_names.find(core::StringOfPath(path)) != module_names.end();
}

std::optional<ast::ModulePath> ResolveImportModulePath(
    const ast::ModulePath& current_module,
    const ModuleNames& module_names,
    const ast::ModulePath& path) {
  SPEC_DEF("Resolve-Import-Direct", "5.1.5");
  SPEC_DEF("Resolve-Import-Current", "5.1.5");
  SPEC_DEF("Resolve-Import-Err", "5.1.5");

  if (path.empty()) {
    return std::nullopt;
  }
  if (HasModuleName(module_names, path)) {
    SPEC_RULE("Resolve-Import-Direct");
    return path;
  }
  if (!current_module.empty()) {
    ast::ModulePath candidate;
    candidate.reserve(path.size() + 1);
    candidate.push_back(current_module.front());
    candidate.insert(candidate.end(), path.begin(), path.end());
    if (HasModuleName(module_names, candidate)) {
      SPEC_RULE("Resolve-Import-Current");
      return candidate;
    }
  }
  SPEC_RULE("Resolve-Import-Err");
  return std::nullopt;
}

}  // namespace ultraviolet::source
