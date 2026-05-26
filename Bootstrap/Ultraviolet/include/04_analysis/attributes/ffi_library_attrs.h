#pragma once

#include <optional>
#include <vector>

#include "01_project/ffi_library.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

std::optional<project::FfiLibrarySpec> NormalizeLibraryAttribute(
    const ast::AttributeItem& attr);

std::vector<project::FfiLibrarySpec> CollectExternLibrarySpecs(
    const std::vector<ast::ASTModule>& modules);

}  // namespace ultraviolet::analysis
