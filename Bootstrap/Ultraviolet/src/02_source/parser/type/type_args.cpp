// =============================================================================
// type_args.cpp - Type List and Generic Argument Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.6.13, Lines 3936-3949
// Section 3.3.7.1 (Type Lists), Lines 4937-4962
//
// Parses type lists used in generic arguments and tuple types.
// Generic arguments use commas (,) as separators.
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"

namespace ultraviolet::ast {

namespace {

bool IsTypeListEnd(const Parser& parser,
                   std::span<const EndSetToken> end_set) {
  const Token* tok = Tok(parser);
  return tok && TokenInEndSet(*tok, end_set);
}

ParseElemResult<std::vector<std::shared_ptr<Type>>> ParseTypeListTailImpl(
    Parser parser,
    std::vector<std::shared_ptr<Type>> xs,
    std::span<const EndSetToken> end_set) {
  SkipNewlinesType(parser);

  // Parse-TypeListTail-End: End of list
  if (IsTypeListEnd(parser, end_set)) {
    SPEC_RULE("Parse-TypeListTail-End");
    return {parser, xs};
  }

  // Check for comma separator
  if (IsPuncType(parser, ",")) {
    Parser after = parser;
    Advance(after);  // consume ','
    SkipNewlinesType(after);

    // Parse-TypeListTail-TrailingComma
    if (IsTypeListEnd(after, end_set)) {
      if (TrailingCommaAllowed(parser, end_set)) {
        SPEC_RULE("Parse-TypeListTail-TrailingComma");
      }
      EmitTrailingCommaErr(parser, end_set);
      after.diags = parser.diags;
      return {after, xs};
    }

    // Parse-TypeListTail-Comma: More elements
    SPEC_RULE("Parse-TypeListTail-Comma");
    ParseElemResult<std::shared_ptr<Type>> elem = ParseType(after);
    xs.push_back(elem.elem);
    return ParseTypeListTailImpl(elem.parser, std::move(xs), end_set);
  }

  // Error: unexpected token
  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, xs};
}

}  // namespace

// =============================================================================
// ParseTypeListTail - Parse Tail of Type List
// =============================================================================
// SPEC: Lines 4949-4962

ParseElemResult<std::vector<std::shared_ptr<Type>>> ParseTypeListTail(
    Parser parser, std::vector<std::shared_ptr<Type>> xs) {
  const EndSetToken end_set[] = {EndPunct(")"), EndPunct("}")};
  return ParseTypeListTailImpl(parser, std::move(xs), end_set);
}

ParseElemResult<std::vector<std::shared_ptr<Type>>> ParseTypeListTailWithEndSet(
    Parser parser,
    std::vector<std::shared_ptr<Type>> xs,
    std::span<const EndSetToken> end_set) {
  return ParseTypeListTailImpl(parser, std::move(xs), end_set);
}

}  // namespace ultraviolet::ast
