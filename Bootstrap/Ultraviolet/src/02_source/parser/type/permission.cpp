// =============================================================================
// permission.cpp - Permission Qualifier Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.7, Lines 4644-4662
//
// Parses permission qualifiers: const, unique, shared
//
// Permission Lattice:
//   unique <: shared <: const
//   - unique: exclusive mutable access (no aliases)
//   - shared: synchronized shared access (requires key system)
//   - const: read-only access (unlimited aliases)
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// =============================================================================
// ParsePermOpt - Parse Optional Permission Qualifier
// =============================================================================
// SPEC: Lines 4644-4662

PermOptResult ParsePermOpt(Parser parser) {
  const Token* tok = Tok(parser);

  // Parse-Perm-Const
  if (tok && IsKwTok(*tok, "const")) {
    SPEC_RULE("Parse-Perm-Const");
    Parser next = parser;
    Advance(next);
    return {next, TypePerm::Const};
  }

  // Parse-Perm-Unique
  if (tok && IsKwTok(*tok, "unique")) {
    SPEC_RULE("Parse-Perm-Unique");
    Parser next = parser;
    Advance(next);
    return {next, TypePerm::Unique};
  }

  // Parse-Perm-Shared
  if (tok && IsKwTok(*tok, "shared")) {
    SPEC_RULE("Parse-Perm-Shared");
    Parser next = parser;
    Advance(next);
    return {next, TypePerm::Shared};
  }

  // Parse-Perm-None
  SPEC_RULE("Parse-Perm-None");
  return {parser, std::nullopt};
}

}  // namespace ultraviolet::ast
