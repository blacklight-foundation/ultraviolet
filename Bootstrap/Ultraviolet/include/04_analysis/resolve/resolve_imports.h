#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "00_core/diagnostics.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/module_paths.h"

namespace ultraviolet::analysis {

struct ImportValidationResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
};

ImportValidationResult ValidateImportDecl(
    const ast::ImportDecl& import,
    const ast::ModulePath& current_module,
    const source::ModuleNames& module_names);

core::DiagnosticStream ValidateModuleImports(
    const ast::ASTModule& module,
    const source::ModuleNames& module_names);

}  // namespace ultraviolet::analysis
