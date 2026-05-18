// =============================================================================
// state_specific_type.cpp - State-Specific Modal Type Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.7, Lines 4803-4806
// Lines 4688-4690 (Modal Type References)
//
// Parses state-specific modal types: ModalType@State
// - Pins a modal type to a known state
// - Examples: Connection@Connected, CancelToken@Active, Region@Frozen
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// =============================================================================
// ParseModalStateType - Parse Modal State Suffix @StateName
// =============================================================================
// SPEC: Lines 4803-4806
// Called when '@' token is found after a type path with optional generic args.
// Parses the state identifier and constructs a TypeModalState node.

ParseElemResult<std::shared_ptr<Type>> ParseModalStateType(
    Parser parser,
    const Parser& start,
    TypePath path,
    std::vector<std::shared_ptr<Type>> generic_args) {
  SPEC_RULE("Parse-Modal-State-Type");

  // Consume '@'
  Parser after_at = parser;
  Advance(after_at);

  // Parse state identifier
  ParseElemResult<Identifier> state = ParseIdent(after_at);

  TypeModalState modal;
  modal.path = std::move(path);
  modal.generic_args = std::move(generic_args);
  SyncTypeModalStateFromFields(modal);
  modal.state = state.elem;

  return {state.parser,
          MakeTypeNode(SpanBetween(start, state.parser), modal)};
}

}  // namespace ultraviolet::ast
