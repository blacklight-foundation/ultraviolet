// =============================================================================
// MIGRATION MAPPING: tuple_pattern.cpp
// =============================================================================
// This file should contain parsing logic for tuple patterns, which destructure
// tuple values into their components.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.9, Lines 6107-6110
// HELPER RULES: Section 3.3.9.1, Lines 6156-6176
// =============================================================================
//
// FORMAL RULES:
// -----------------------------------------------------------------------------
// **(Parse-Pattern-Tuple)** Lines 6107-6110
// IsPunc(Tok(P), "(")
// Gamma |- ParseTuplePatternElems(Advance(P)) => (P_1, elems)
// IsPunc(Tok(P_1), ")")
// --------------------------------------------------------------------------
// Gamma |- ParsePatternAtom(P) => (Advance(P_1), TuplePattern(elems))
//
// TUPLE ELEMENT PARSING RULES (Lines 6158-6176):
// -----------------------------------------------------------------------------
// **(Parse-TuplePatternElems-Empty)** Lines 6158-6161
// IsPunc(Tok(P), ")")
// --------------------------------------------------------------------------
// Gamma |- ParseTuplePatternElems(P) => (P, [])
//
// **(Parse-TuplePatternElems-Single)** Lines 6163-6166
// Gamma |- ParsePattern(P) => (P_1, p)    IsPunc(Tok(P_1), ";")
// --------------------------------------------------------------------------
// Gamma |- ParseTuplePatternElems(P) => (Advance(P_1), [p])
//
// **(Parse-TuplePatternElems-TrailingComma)** Lines 6168-6171
// Gamma |- ParsePattern(P) => (P_1, p)
// IsPunc(Tok(P_1), ",")    IsPunc(Tok(Advance(P_1)), ")")
// TrailingCommaAllowed(P_0, P_1, {Punctuator(")")})
// --------------------------------------------------------------------------
// Gamma |- ParseTuplePatternElems(P) => (Advance(P_1), [p])
//
// **(Parse-TuplePatternElems-Many)** Lines 6173-6176
// Gamma |- ParsePattern(P) => (P_1, p_1)    IsPunc(Tok(P_1), ",")
// Gamma |- ParsePattern(Advance(P_1)) => (P_2, p_2)
// Gamma |- ParsePatternListTail(P_2, [p_2]) => (P_3, ps)
// --------------------------------------------------------------------------
// Gamma |- ParseTuplePatternElems(P) => (P_3, [p_1] ++ ps)
//
// SEMANTICS:
// - `()` - unit pattern (empty tuple)
// - `(p;)` - single-element tuple (semicolon required)
// - `(p1, p2)` - multi-element tuple (comma-separated)
// - Elements can be any pattern type (nested tuples, typed, etc.)
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_patterns.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. ParseTuplePatternElems function (Lines 133-171)
//    -------------------------------------------------------------------------
//    ParseElemResult<std::vector<PatternPtr>> ParseTuplePatternElems(Parser parser) {
//      SkipNewlines(parser);
//      if (IsPunc(parser, ")")) {
//        SPEC_RULE("Parse-TuplePatternElems-Empty");
//        return {parser, {}};
//      }
//      ParseElemResult<PatternPtr> first = ParsePattern(parser);
//      Parser after_first = first.parser;
//      SkipNewlines(after_first);
//      if (IsPunc(after_first, ";")) {
//        SPEC_RULE("Parse-TuplePatternElems-Single");
//        Parser after = after_first;
//        Advance(after);
//        return {after, {first.elem}};
//      }
//      if (IsPunc(after_first, ",")) {
//        const TokenKindMatch end_set[] = {MatchPunct(")")};
//        Parser after = after_first;
//        Advance(after);
//        SkipNewlines(after);
//        if (IsPunc(after, ")")) {
//          SPEC_RULE("Parse-TuplePatternElems-TrailingComma");
//          EmitTrailingCommaErr(after_first, end_set);
//          after.diags = after_first.diags;
//          return {after, {first.elem}};
//        }
//        SPEC_RULE("Parse-TuplePatternElems-Many");
//        ParseElemResult<PatternPtr> second = ParsePattern(after);
//        ParseElemResult<std::vector<PatternPtr>> tail =
//            ParsePatternListTail(second.parser, {second.elem});
//        std::vector<PatternPtr> elems;
//        elems.reserve(1 + tail.elem.size());
//        elems.push_back(first.elem);
//        elems.insert(elems.end(), tail.elem.begin(), tail.elem.end());
//        return {tail.parser, elems};
//      }
//      EmitParseSyntaxErr(after_first, TokSpan(after_first));
//      return {after_first, {first.elem}};
//    }
//
// 2. TuplePattern branch in ParsePatternAtom (Lines 351-366)
//    -------------------------------------------------------------------------
//    if (IsPunc(parser, "(")) {
//      SPEC_RULE("Parse-Pattern-Tuple");
//      Parser next = parser;
//      Advance(next);
//      ParseElemResult<std::vector<PatternPtr>> elems = ParseTuplePatternElems(next);
//      if (!IsPunc(elems.parser, ")")) {
//        EmitParseSyntaxErr(elems.parser, TokSpan(elems.parser));
//        return {elems.parser,
//                MakePattern(SpanBetween(parser, elems.parser), WildcardPattern{})};
//      }
//      Parser after = elems.parser;
//      Advance(after);
//      TuplePattern pat;
//      pat.elements = std::move(elems.elem);
//      return {after, MakePattern(SpanBetween(parser, after), pat)};
//    }
//
// DEPENDENCIES:
// =============================================================================
// - TuplePattern AST node type (contains elements: vector<PatternPtr>)
// - ParsePattern function (recursive call for elements)
// - ParsePatternListTail function (pattern_common.cpp)
// - MakePattern, SpanBetween, SkipNewlines helpers
// - EmitTrailingCommaErr, EmitParseSyntaxErr error handlers
// - IsPunc, Tok, Advance parser utilities
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Single-element tuples require `;` to distinguish from parenthesized patterns
// - Trailing commas on single line emit error (TrailingCommaAllowed check)
// - Empty tuple `()` matches unit type
// - ParseTuplePatternElems handles the special cases; ParsePatternListTail
//   handles the recursive multi-element case
// - Span covers from opening `(` to closing `)`
// - SkipNewlines called at start and after commas for multi-line support
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>
#include <vector>

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// Forward declarations from other modules
bool IsPunc(const Parser& parser, std::string_view p);
PatternPtr MakePattern(const core::Span& span, PatternNode node);
ParseElemResult<std::vector<PatternPtr>> ParseTuplePatternElems(Parser parser);

// =============================================================================
// ParseTuplePattern - Parse tuple pattern ((p1, p2, ...))
// =============================================================================
//
// SPEC: Lines 6107-6110 (Parse-Pattern-Tuple)
// Parses patterns like `()`, `(p;)`, `(p1, p2)`

ParseElemResult<PatternPtr> ParseTuplePattern(Parser parser) {
  SPEC_RULE("Parse-Pattern-Tuple");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "("

  ParseElemResult<std::vector<PatternPtr>> elems = ParseTuplePatternElems(next);
  if (!IsPunc(elems.parser, ")")) {
    EmitParseSyntaxErr(elems.parser, TokSpan(elems.parser));
    return {elems.parser,
            MakePattern(SpanBetween(start, elems.parser), WildcardPattern{})};
  }
  Parser after = elems.parser;
  Advance(after);  // consume ")"

  TuplePattern pat;
  pat.elements = std::move(elems.elem);
  return {after, MakePattern(SpanBetween(start, after), pat)};
}

// =============================================================================
// TryParseTuplePattern - Try to parse tuple pattern
// =============================================================================
//
// Returns std::nullopt if not at `(` token.

std::optional<ParseElemResult<PatternPtr>> TryParseTuplePattern(Parser parser) {
  if (!IsPunc(parser, "(")) {
    return std::nullopt;
  }
  return ParseTuplePattern(parser);
}

}  // namespace ultraviolet::ast
