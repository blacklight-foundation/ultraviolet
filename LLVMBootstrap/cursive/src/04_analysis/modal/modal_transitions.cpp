/*
 * =============================================================================
 * modal_transitions.cpp - Modal State Transition Checking
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - CursiveSpecification.md, Section 5.4.5 "Transitions" (lines 12760-12810)
 *   - CursiveSpecification.md, Section 5.4.6 "State Methods" (lines 12820-12900)
 *   - CursiveSpecification.md, Section 8.7 "E-MOD Errors" (lines 21600-21700)
 *
 * SOURCE FILE:
 *   - Migrated from cursive-bootstrap/src/03_analysis/modal/modal_transitions.cpp
 *
 * FUNCTIONS:
 *   - LookupStateMethodDecl: Find method declaration within a specific modal state
 *   - LookupTransitionDecl: Find transition declaration (state -> state)
 *   - StateMemberVisible: Check if method/transition is accessible from current scope
 *
 * DEPENDENCIES:
 *   - ModalState, TransitionDecl, StateMethodDecl from AST
 *   - LookupModalState from modal.h
 *   - ScopeContext, TypePath from typing/context.h
 *
 * DIAGNOSTIC CODES:
 *   - E-MOD-0020: Method not accessible in current state
 *   - E-MOD-0021: Transition target state invalid
 *   - E-MOD-0022: Transition does not return state type
 *   - E-MOD-0023: Unreachable state in modal
 *
 * =============================================================================
 */

#include "04_analysis/modal/modal_transitions.h"

#include "00_core/assert_spec.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsModalTransitions() {
  SPEC_DEF("Methods", "5.6");
  SPEC_DEF("Transitions", "5.6");
  SPEC_DEF("LookupStateMethod", "5.6");
  SPEC_DEF("LookupTransition", "5.6");
  SPEC_DEF("StateMemberVisible", "5.6");
  SPEC_DEF("MethodSig", "5.6");
  SPEC_DEF("TransitionSig", "5.6");
}

static ast::ModulePath ModuleOfModalPath(const TypePath& path) {
  if (path.size() <= 1) {
    return {};
  }
  return ast::ModulePath(path.begin(), path.end() - 1);
}

}  // namespace

const ast::StateMethodDecl* LookupStateMethodDecl(const ast::ModalDecl& decl,
                                                     std::string_view state,
                                                     std::string_view name) {
  SpecDefsModalTransitions();
  const auto* block = LookupModalState(decl, state);
  if (!block) {
    return nullptr;
  }
  for (const auto& member : block->members) {
    const auto* method = std::get_if<ast::StateMethodDecl>(&member);
    if (!method) {
      continue;
    }
    if (IdEq(method->name, name)) {
      return method;
    }
  }
  return nullptr;
}

const ast::TransitionDecl* LookupTransitionDecl(const ast::ModalDecl& decl,
                                                   std::string_view state,
                                                   std::string_view name) {
  SpecDefsModalTransitions();
  const auto* block = LookupModalState(decl, state);
  if (!block) {
    return nullptr;
  }
  for (const auto& member : block->members) {
    const auto* transition = std::get_if<ast::TransitionDecl>(&member);
    if (!transition) {
      continue;
    }
    if (IdEq(transition->name, name)) {
      return transition;
    }
  }
  return nullptr;
}

bool StateMemberVisible(const ScopeContext& ctx,
                        const TypePath& modal_path,
                        ast::Visibility vis) {
  SpecDefsModalTransitions();
  if (vis == ast::Visibility::Public || vis == ast::Visibility::Internal) {
    return true;
  }
  const auto modal_module = ModuleOfModalPath(modal_path);
  return PathEq(modal_module, ctx.current_module);
}

}  // namespace cursive::analysis
