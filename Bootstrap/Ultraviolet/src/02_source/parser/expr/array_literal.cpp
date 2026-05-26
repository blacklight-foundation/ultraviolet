// =============================================================================
// array_literal.cpp - Array Literal Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
// - Parse-Array-Literal
// - Parse-Array-Segment-*
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <array>
#include <memory>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Forward declarations from other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
bool IsPunc(const Parser& parser, std::string_view punc);
void SkipNewlines(Parser& parser);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);

ParseElemResult<ArraySegment> ParseArraySegment(Parser parser) {
  ParseElemResult<ExprPtr> value = ParseExpr(parser);
  Parser after_value = value.parser;
  SkipNewlines(after_value);

  if (IsPunc(after_value, ";")) {
    SPEC_RULE("Parse-Array-Segment-Repeat");
    Parser count_start = after_value;
    Advance(count_start);
    ParseElemResult<ExprPtr> count = ParseExpr(count_start);
    ArrayRepeatSegment segment;
    segment.value = value.elem;
    segment.count = count.elem;
    return {count.parser, ArraySegment(std::move(segment))};
  }

  SPEC_RULE("Parse-Array-Segment-Elem");
  return {after_value, ArraySegment(ArrayElemSegment(value.elem))};
}

ParseElemResult<std::vector<ArraySegment>> ParseArraySegmentList(Parser parser) {
  SkipNewlines(parser);
  const std::array<EndSetToken, 1> end_set = {EndPunct("]")};
  ListState<ArraySegment> state = ListStart<ArraySegment>(parser);

  if (ListDone(state, end_set)) {
    SPEC_RULE("Parse-Array-Segment-List-Empty");
    return {state.parser, {}};
  }

  for (;;) {
    state = ListCons(std::move(state), ParseArraySegment);

    Parser after_segment = state.parser;
    SkipNewlines(after_segment);
    state.parser = after_segment;
    if (ListDone(state, end_set)) {
      SPEC_RULE("Parse-Array-Segment-List-Single");
      return {state.parser, std::move(state.elems)};
    }

    if (!IsPunc(state.parser, ",")) {
      SPEC_RULE("Parse-Array-Segment-List-Single");
      return {state.parser, std::move(state.elems)};
    }

    SPEC_RULE("Parse-Array-Segment-List-Comma");
    Parser after_comma = state.parser;
    Advance(after_comma);
    SkipNewlines(after_comma);
    if (IsPunc(after_comma, "]")) {
      EmitTrailingCommaErr(state.parser, end_set);
      after_comma.diags = state.parser.diags;
      return {after_comma, std::move(state.elems)};
    }
    state.parser = after_comma;
  }
}

// =============================================================================
// ParseArrayLiteralExpr - Parse segmented array literal
// =============================================================================
//
// Assumes parser is at "[".
// Handles:
// - [] empty array
// - [e] single element
// - [e1, e2, ...] element list
// - [value; count] repeated segment
// - [0; 4, 1, 0; 22] mixed segments

ParseElemResult<ExprPtr> ParseArrayLiteralExpr(Parser parser) {
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "["
  SkipNewlines(next);

  ParseElemResult<std::vector<ArraySegment>> segments = ParseArraySegmentList(next);
  if (!IsPunc(segments.parser, "]")) {
    EmitParseSyntaxErr(segments.parser, TokSpan(segments.parser));
    Parser sync = segments.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  SPEC_RULE("Parse-Array-Literal");
  Parser after = segments.parser;
  Advance(after);
  ArrayExpr arr;
  arr.elements = std::move(segments.elem);
  return {after, MakeExpr(SpanBetween(start, after), arr)};
}

}  // namespace ultraviolet::ast
