// =============================================================================
// array_type.cpp - Array Type Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.7, Lines 4763-4766
//
// Parses array types: [T; n]
// - T is the element type
// - n is a compile-time length expression
// - Distinguishable from slices by presence of semicolon and length
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"

namespace ultraviolet::ast {

// =============================================================================
// ParseArrayType - Parse Array Type [T; n]
// =============================================================================
// SPEC: Lines 4763-4766
// Assumes the opening '[' has been consumed and element type parsed.
// Parses from ';' through closing ']'.

ParseElemResult<std::shared_ptr<Type>> ParseArrayType(
    Parser parser,
    const Parser& start,
    std::shared_ptr<Type> element) {
  SPEC_RULE("Parse-Array-Type");

  // Consume ';'
  Parser after_semi = parser;
  Advance(after_semi);

  // Parse length expression
  ParseElemResult<ExprPtr> len = ParseExpr(after_semi);

  // Expect ']'
  if (!IsPuncType(len.parser, "]")) {
    EmitParseSyntaxErr(len.parser, TokSpan(len.parser));
    return {len.parser, MakeTypePrim(SpanBetween(start, len.parser), "!")};
  }

  Parser after_r = len.parser;
  Advance(after_r);

  TypeArray arr;
  arr.element = std::move(element);
  arr.length = len.elem;
  return {after_r, MakeTypeNode(SpanBetween(start, after_r), arr)};
}

}  // namespace ultraviolet::ast
