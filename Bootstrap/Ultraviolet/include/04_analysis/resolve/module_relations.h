#pragma once

// =============================================================================
// module_relations.h - Module Relation Queries
// =============================================================================
//
// SPEC REFERENCE:
//   Docs/SPECIFICATION.md §12735-12736
//     ImplModule(T) = p ⇔ T is declared in ASTModule(P, p)
//     ClassModule(Cl) = p ⇔ Cl is declared in ASTModule(P, p)
//
// =============================================================================

#include <optional>

#include "02_source/ast/ast.h"
#include "04_analysis/typing/context.h"

namespace ultraviolet::analysis {

std::optional<ast::ModulePath> ImplModule(const Sigma& sigma,
                                          std::string_view type_name);

std::optional<ast::ModulePath> ClassModule(const Sigma& sigma,
                                           std::string_view class_name);

}  // namespace ultraviolet::analysis
