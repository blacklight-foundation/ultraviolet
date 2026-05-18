/*
 * =============================================================================
 * modal.cpp - Modal Type Lookup Implementation
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - SPECIFICATION.md, Section 5.4 "Modal Types"
 *   - SPECIFICATION.md, ModalDeclOf(modal_ref) = M (line 10433)
 *   - SPECIFICATION.md, States(M) (line 12464)
 *   - SPECIFICATION.md, Section 8.7 "E-MOD Errors"
 *
 * MIGRATED FROM:
 *   - ultraviolet-bootstrap/src/03_analysis/modal/modal.cpp (lines 1-60)
 *
 * FUNCTIONS:
 *   - LookupModalDecl(ctx, modal_ref) -> ModalDecl*
 *       Find modal declaration by ModalRefPath(modal_ref) in Sigma.Types
 *   - LookupModalState(decl, state) -> StateBlock*
 *       Find specific state within a modal declaration
 *   - HasState(decl, state) -> bool
 *       Check if modal has a given state
 *   - StateNameSet(decl) -> unordered_set<IdKey>
 *       Get all state names for a modal type
 *
 * DEPENDENCIES:
 *   - ModalDecl, StateBlock from AST (ast_items.h)
 *   - ScopeContext, TypePath, IdKey from typing/context.h
 *   - Scope lookup utilities from resolve/scopes.h
 *
 * IMPLEMENTATION NOTES:
 *   1. Modal types have states prefixed with @ (e.g., @Connected, @Disconnected)
 *   2. Each modal declaration has exactly one state active at any time
 *   3. States contain state-specific fields and methods
 *   4. Built-in modals: Region (@Active, @Frozen, @Freed), CancelToken, Spawned, etc.
 *   5. State names are unique within a modal declaration (enforced by parser)
 *
 * DIAGNOSTIC CODES:
 *   - E-MOD-0001: Unknown modal type
 *   - E-MOD-0002: Unknown modal state
 *   - E-MOD-0003: Duplicate state name
 *
 * =============================================================================
 */

#include "04_analysis/modal/modal.h"

#include <utility>

#include "04_analysis/resolve/scopes.h"

namespace ultraviolet::analysis {

const ast::ModalDecl* LookupModalDecl(const ScopeContext& ctx,
                                       const TypePath& path) {
  ModalRef modal_ref = path;
  return LookupModalDecl(ctx, modal_ref);
}

// SPEC_DEF: ModalDeclOf(modal_ref) = M (SPECIFICATION.md, line 10433)
// ModalDeclOf is intentionally a direct Sigma.Types lookup by
// ModalRefPath(modal_ref). Name resolution belongs to ResolveModalRef before
// this definition is applied.
const ast::ModalDecl* LookupModalDecl(const ScopeContext& ctx,
                                      const ModalRef& modal_ref) {
  SPEC_RULE("ModalDeclOf");
  const auto& path = ModalRefPath(modal_ref);

  ast::Path ast_path;
  ast_path.reserve(path.size());
  for (const auto& comp : path) {
    ast_path.push_back(comp);
  }

  const auto it = ctx.sigma.types.find(PathKeyOf(ast_path));
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }

  return std::get_if<ast::ModalDecl>(&it->second);
}

// SPEC_DEF: LookupStateMethod(M, S, name) (SPECIFICATION.md, line 12290)
// Looks up a specific state block within a modal declaration by state name.
// State names are matched case-sensitively using IdKeyOf for normalization.
const ast::StateBlock* LookupModalState(const ast::ModalDecl& decl,
                                         std::string_view state) {
  const auto key = IdKeyOf(state);
  for (const auto& block : decl.states) {
    if (IdKeyOf(block.name) == key) {
      return &block;
    }
  }
  return nullptr;
}

// SPEC_RULE: S ∈ States(M) (SPECIFICATION.md, line 12464)
// Returns true if the modal declaration contains a state with the given name.
bool HasState(const ast::ModalDecl& decl, std::string_view state) {
  return LookupModalState(decl, state) != nullptr;
}

// SPEC_DEF: States(M) (SPECIFICATION.md, line 12464)
// Returns the set of all state names in the modal declaration.
// Used for exhaustiveness checking in if-case expressions over modal types.
std::unordered_set<IdKey> StateNameSet(const ast::ModalDecl& decl) {
  std::unordered_set<IdKey> out;
  out.reserve(decl.states.size());
  for (const auto& state : decl.states) {
    out.insert(IdKeyOf(state.name));
  }
  return out;
}

}  // namespace ultraviolet::analysis
