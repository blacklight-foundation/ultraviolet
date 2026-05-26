#pragma once

#include <optional>
#include <string>
#include <unordered_set>

#include "02_source/ast/ast.h"

namespace ultraviolet::source {

using ModuleNames = std::unordered_set<std::string>;

bool HasModuleName(const ModuleNames& module_names, const ast::ModulePath& path);

std::optional<ast::ModulePath> ResolveImportModulePath(
    const ast::ModulePath& current_module,
    const ModuleNames& module_names,
    const ast::ModulePath& path);

}  // namespace ultraviolet::source
