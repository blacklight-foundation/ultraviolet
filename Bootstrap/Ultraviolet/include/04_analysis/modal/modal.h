#pragma once

#include <string_view>
#include <unordered_set>

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

const ast::ModalDecl* LookupModalDecl(const ScopeContext& ctx,
                                         const TypePath& path);

const ast::ModalDecl* LookupModalDecl(const ScopeContext& ctx,
                                      const ModalRef& modal_ref);

const ast::StateBlock* LookupModalState(const ast::ModalDecl& decl,
                                           std::string_view state);

bool HasState(const ast::ModalDecl& decl, std::string_view state);

std::unordered_set<IdKey> StateNameSet(const ast::ModalDecl& decl);

}  // namespace ultraviolet::analysis
