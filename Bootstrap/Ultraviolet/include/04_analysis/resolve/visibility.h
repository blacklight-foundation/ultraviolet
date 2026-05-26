#pragma once

#include <string_view>

#include "00_core/diagnostics.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

AccessResult CanAccess(const ScopeContext& ctx,
                       const ast::ModulePath& module_path,
                       std::string_view name);

AccessResult CanAccessVis(const ast::ModulePath& accessor_module,
                          const ast::ModulePath& decl_module,
                          ast::Visibility vis);

AccessResult TopLevelVis(const ast::ASTItem& item);

core::DiagnosticStream CheckModuleVisibility(const ScopeContext& ctx,
                                             const ast::ASTModule& module);

}  // namespace ultraviolet::analysis
