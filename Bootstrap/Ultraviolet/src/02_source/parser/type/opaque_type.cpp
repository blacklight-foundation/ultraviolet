// =============================================================================
// opaque_type.cpp - Opaque Type Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.7, Lines 4808-4811
//
// Parses opaque types: opaque TypePath
// - Used for abstract data types and module encapsulation
// - The "opaque" keyword indicates the type's structure is not exposed
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// =============================================================================
// ParseOpaqueType - Parse Opaque Type: opaque Path
// =============================================================================
// SPEC: Lines 4808-4811
// Assumes current token is 'opaque' identifier.

ParseElemResult<std::shared_ptr<Type>> ParseOpaqueType(Parser parser) {
  Parser start = parser;
  Parser after_kw = parser;
  Advance(after_kw);  // consume 'opaque'

  ParseElemResult<TypePath> opaque_path = ParseTypePath(after_kw);
  SPEC_RULE("Parse-Opaque-Type");

  TypeOpaque opaque;
  opaque.path = std::move(opaque_path.elem);
  return {opaque_path.parser,
          MakeTypeNode(SpanBetween(start, opaque_path.parser), opaque)};
}

}  // namespace ultraviolet::ast
