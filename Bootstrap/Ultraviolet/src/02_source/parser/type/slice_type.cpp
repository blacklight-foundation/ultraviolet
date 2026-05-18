// =============================================================================
// slice_type.cpp - Slice Type Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.7, Lines 4768-4771
//
// Parses slice types: [T]
// - Represents a dynamically-sized view into contiguous memory
// - Distinguishable from arrays by absence of semicolon
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// =============================================================================
// ParseSliceType - Parse Slice Type [T]
// =============================================================================
// SPEC: Lines 4768-4771
// Assumes the opening '[' has been consumed and element type parsed.
// Parses the closing ']'.

ParseElemResult<std::shared_ptr<Type>> ParseSliceType(
    Parser parser,
    const Parser& start,
    std::shared_ptr<Type> element) {
  SPEC_RULE("Parse-Slice-Type");

  // Consume ']'
  Parser after_r = parser;
  Advance(after_r);

  TypeSlice slice;
  slice.element = std::move(element);
  return {after_r, MakeTypeNode(SpanBetween(start, after_r), slice)};
}

}  // namespace ultraviolet::ast
