// =============================================================================
// range.cpp - Range Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
// - Parse-Range-To (Lines 4992-4995)
// - Parse-Range-ToInc (Lines 4997-5000)
// - Parse-Range-Full (Lines 5002-5005)
// - Parse-Range-Lhs (Lines 5007-5010)
// - Parse-RangeTail-None (Lines 5012-5015)
// - Parse-RangeTail-From (Lines 5017-5020)
// - Parse-RangeTail-Exclusive (Lines 5022-5025)
// - Parse-RangeTail-Inclusive (Lines 5027-5030)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Import token inspection functions from lexer
using lexer::IsIdentTok;

// Forward declarations from other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
bool IsOp(const Parser& parser, std::string_view op);
ParseElemResult<ExprPtr> ParseLogicalOr(Parser parser, bool allow_brace,
                                        bool allow_bracket);

// IsExprStart is defined in expr_common.cpp (canonical location)
bool IsExprStart(const lexer::Token& tok);

// =============================================================================
// ParseRangeTail - Parse the tail of a range expression after LHS
// =============================================================================
//
// SPEC: Lines 5012-5030
// Handles:
// - None: no range operator follows
// - From: e..
// - Exclusive: e..f
// - Inclusive: e..=f

ParseElemResult<ExprPtr> ParseRangeTail(Parser parser, const ExprPtr& lhs,
                                        const Parser& start, bool allow_brace,
                                        bool allow_bracket) {
  if (!(IsOp(parser, "..") || IsOp(parser, "..="))) {
    SPEC_RULE("Parse-RangeTail-None");
    return {parser, lhs};
  }

  if (IsOp(parser, "..")) {
    Parser after = parser;
    Advance(after);
    const lexer::Token* next = Tok(after);
    if (!next || !IsExprStart(*next)) {
      SPEC_RULE("Parse-RangeTail-From");
      RangeExpr range;
      range.kind = RangeKind::From;
      range.lhs = lhs;
      range.rhs = nullptr;
      return {after, MakeExpr(SpanBetween(start, after), range)};
    }
    SPEC_RULE("Parse-RangeTail-Exclusive");
    ParseElemResult<ExprPtr> rhs =
        ParseLogicalOr(after, allow_brace, allow_bracket);
    RangeExpr range;
    range.kind = RangeKind::Exclusive;
    range.lhs = lhs;
    range.rhs = rhs.elem;
    return {rhs.parser, MakeExpr(SpanBetween(start, rhs.parser), range)};
  }

  // Must be "..="
  SPEC_RULE("Parse-RangeTail-Inclusive");
  Parser after = parser;
  Advance(after);
  ParseElemResult<ExprPtr> rhs =
      ParseLogicalOr(after, allow_brace, allow_bracket);
  RangeExpr range;
  range.kind = RangeKind::Inclusive;
  range.lhs = lhs;
  range.rhs = rhs.elem;
  return {rhs.parser, MakeExpr(SpanBetween(start, rhs.parser), range)};
}

// =============================================================================
// ParseRange - Parse range expression (entry point)
// =============================================================================
//
// SPEC: Lines 4992-5010
// Handles:
// - ToInclusive: ..=e
// - Full: ..
// - To: ..e
// - LHS-first: e followed by ParseRangeTail

ParseElemResult<ExprPtr> ParseRange(Parser parser, bool allow_brace,
                                    bool allow_bracket) {
  SPEC_RULE("ParseRangeFamily");
  Parser start = parser;

  // Check for ToInclusive range: ..=e
  if (IsOp(parser, "..=")) {
    SPEC_RULE("Parse-Range-ToInc");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> rhs =
        ParseLogicalOr(next, allow_brace, allow_bracket);
    RangeExpr range;
    range.kind = RangeKind::ToInclusive;
    range.lhs = nullptr;
    range.rhs = rhs.elem;
    return {rhs.parser, MakeExpr(SpanBetween(start, rhs.parser), range)};
  }

  // Check for Full or To range: .. or ..e
  if (IsOp(parser, "..")) {
    Parser next = parser;
    Advance(next);
    const lexer::Token* after = Tok(next);
    if (!after || !IsExprStart(*after)) {
      SPEC_RULE("Parse-Range-Full");
      RangeExpr range;
      range.kind = RangeKind::Full;
      range.lhs = nullptr;
      range.rhs = nullptr;
      return {next, MakeExpr(SpanBetween(start, next), range)};
    }
    SPEC_RULE("Parse-Range-To");
    ParseElemResult<ExprPtr> rhs =
        ParseLogicalOr(next, allow_brace, allow_bracket);
    RangeExpr range;
    range.kind = RangeKind::To;
    range.lhs = nullptr;
    range.rhs = rhs.elem;
    return {rhs.parser, MakeExpr(SpanBetween(start, rhs.parser), range)};
  }

  // LHS-first: parse expression then check for range tail
  SPEC_RULE("Parse-Range-Lhs");
  ParseElemResult<ExprPtr> lhs =
      ParseLogicalOr(parser, allow_brace, allow_bracket);
  return ParseRangeTail(lhs.parser, lhs.elem, start, allow_brace, allow_bracket);
}

}  // namespace ultraviolet::ast
