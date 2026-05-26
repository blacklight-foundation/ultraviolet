#pragma once

#include <string_view>

#include "04_analysis/typing/context.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

const ast::StateFieldDecl* LookupModalFieldDecl(const ast::ModalDecl& decl,
                                                   std::string_view state,
                                                   std::string_view name);

bool ModalFieldVisible(const ScopeContext& ctx, const TypePath& modal_path);

}  // namespace ultraviolet::analysis
