// =============================================================================
// race_expr.cpp - Race Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
// - Parse-Race-Expr (Lines 5299-5302)
// - Parse-RaceArms-* (Lines 5757-5765)
// - Parse-RaceArm (Lines 5767-5770)
// - Parse-RaceArmsTail-* (Lines 5772-5785)
// - Parse-RaceHandler-* (Lines 5787-5795)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Import token inspection functions from lexer
using lexer::IsKwTok;

// Forward declarations from other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view punc);
void SkipNewlines(Parser& parser);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
ParseElemResult<std::shared_ptr<Pattern>> ParsePattern(Parser parser);

// =============================================================================
// ParseRaceHandler - Parse race arm handler
// =============================================================================
//
// SPEC: Lines 5787-5795
// Handler can be:
// - RaceYield: yield expr (forwards value to outer async)
// - RaceReturn: expr (returns value from race)

ParseElemResult<RaceHandler> ParseRaceHandler(Parser parser) {
  RaceHandler handler;
  if (IsKw(parser, "yield")) {
    SPEC_RULE("Parse-RaceHandler-Yield");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> expr = ParseExpr(next);
    handler.kind = RaceHandlerKind::Yield;
    handler.value = expr.elem;
    return {expr.parser, handler};
  }
  SPEC_RULE("Parse-RaceHandler-Return");
  ParseElemResult<ExprPtr> expr = ParseExpr(parser);
  handler.kind = RaceHandlerKind::Return;
  handler.value = expr.elem;
  return {expr.parser, handler};
}

// =============================================================================
// ParseRaceArm - Parse a single race arm
// =============================================================================
//
// SPEC: Lines 5767-5770
// Syntax: async_expr -> |pattern| handler_expr

ParseElemResult<RaceArm> ParseRaceArm(Parser parser) {
  SPEC_RULE("Parse-RaceArm");
  ParseElemResult<ExprPtr> expr = ParseExpr(parser);
  if (!IsOp(expr.parser, "->")) {
    EmitParseSyntaxErr(expr.parser, TokSpan(expr.parser));
    Parser sync = expr.parser;
    SyncStmt(sync);
    return {sync, RaceArm{}};
  }
  Parser after_arrow = expr.parser;
  Advance(after_arrow);
  if (!IsOp(after_arrow, "|") && !IsPunc(after_arrow, "|")) {
    EmitParseSyntaxErr(after_arrow, TokSpan(after_arrow));
    Parser sync = after_arrow;
    SyncStmt(sync);
    return {sync, RaceArm{}};
  }
  Parser after_bar = after_arrow;
  Advance(after_bar);
  ParseElemResult<std::shared_ptr<Pattern>> pat = ParsePattern(after_bar);
  if (!IsOp(pat.parser, "|") && !IsPunc(pat.parser, "|")) {
    EmitParseSyntaxErr(pat.parser, TokSpan(pat.parser));
    Parser sync = pat.parser;
    SyncStmt(sync);
    return {sync, RaceArm{}};
  }
  Parser after_bar2 = pat.parser;
  Advance(after_bar2);
  ParseElemResult<RaceHandler> handler = ParseRaceHandler(after_bar2);
  RaceArm arm;
  arm.expr = expr.elem;
  arm.pattern = pat.elem;
  arm.handler = handler.elem;
  return {handler.parser, arm};
}

// =============================================================================
// ParseRaceArmsTail - Parse remaining race arms after first
// =============================================================================
//
// SPEC: Lines 5772-5785

ParseElemResult<std::vector<RaceArm>> ParseRaceArmsTail(
    Parser parser, std::vector<RaceArm> xs) {
  SkipNewlines(parser);
  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-RaceArmsTail-End");
    return {parser, xs};
  }
  if (IsPunc(parser, ",")) {
    Parser after = parser;
    Advance(after);
    SkipNewlines(after);
    if (IsPunc(after, "}")) {
      SPEC_RULE("Parse-RaceArmsTail-TrailingComma");
      return {after, xs};
    }
    SPEC_RULE("Parse-RaceArmsTail-Comma");
    ParseElemResult<RaceArm> arm = ParseRaceArm(after);
    xs.push_back(arm.elem);
    return ParseRaceArmsTail(arm.parser, std::move(xs));
  }
  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, xs};
}

// =============================================================================
// ParseRaceArms - Parse list of race arms
// =============================================================================
//
// SPEC: Lines 5757-5765

ParseElemResult<std::vector<RaceArm>> ParseRaceArms(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, "}")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, {}};
  }
  SPEC_RULE("Parse-RaceArms-Cons");
  ParseElemResult<RaceArm> first = ParseRaceArm(parser);
  std::vector<RaceArm> arms;
  arms.push_back(first.elem);
  return ParseRaceArmsTail(first.parser, std::move(arms));
}

// =============================================================================
// TryParseRaceExpr - Try to parse a race expression
// =============================================================================
//
// SPEC: Lines 5299-5302
// race { arm, arm, ... }
// Returns std::nullopt if current token is not "race".

std::optional<ParseElemResult<ExprPtr>> TryParseRaceExpr(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsKwTok(*tok, "race")) {
    return std::nullopt;
  }

  SPEC_RULE("Parse-Race-Expr");
  Parser next = parser;
  Advance(next);
  if (!IsPunc(next, "{")) {
    EmitParseSyntaxErr(next, TokSpan(next));
    Parser sync = next;
    SyncStmt(sync);
    return ParseElemResult<ExprPtr>{
        sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }
  Parser after_l = next;
  Advance(after_l);
  ParseElemResult<std::vector<RaceArm>> arms = ParseRaceArms(after_l);
  if (arms.elem.empty() && IsPunc(arms.parser, "}")) {
    Parser after_r = arms.parser;
    Advance(after_r);
    return ParseElemResult<ExprPtr>{
        after_r, MakeExpr(SpanBetween(parser, after_r), ErrorExpr{})};
  }
  if (!IsPunc(arms.parser, "}")) {
    EmitParseSyntaxErr(arms.parser, TokSpan(arms.parser));
    Parser sync = arms.parser;
    SyncStmt(sync);
    return ParseElemResult<ExprPtr>{
        sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }
  Parser after_r = arms.parser;
  Advance(after_r);
  RaceExpr race;
  race.arms = std::move(arms.elem);
  return ParseElemResult<ExprPtr>{
      after_r, MakeExpr(SpanBetween(parser, after_r), race)};
}

// =============================================================================
// ParseRaceExpr - Parse race expression (assumes race keyword verified)
// =============================================================================

ParseElemResult<ExprPtr> ParseRaceExpr(Parser parser) {
  SPEC_RULE("Parse-Race-Expr");
  Parser next = parser;
  Advance(next);  // consume "race"
  if (!IsPunc(next, "{")) {
    EmitParseSyntaxErr(next, TokSpan(next));
    Parser sync = next;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }
  Parser after_l = next;
  Advance(after_l);
  ParseElemResult<std::vector<RaceArm>> arms = ParseRaceArms(after_l);
  if (arms.elem.empty() && IsPunc(arms.parser, "}")) {
    Parser after_r = arms.parser;
    Advance(after_r);
    return {after_r, MakeExpr(SpanBetween(parser, after_r), ErrorExpr{})};
  }
  if (!IsPunc(arms.parser, "}")) {
    EmitParseSyntaxErr(arms.parser, TokSpan(arms.parser));
    Parser sync = arms.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }
  Parser after_r = arms.parser;
  Advance(after_r);
  RaceExpr race;
  race.arms = std::move(arms.elem);
  return {after_r, MakeExpr(SpanBetween(parser, after_r), race)};
}

}  // namespace ultraviolet::ast
