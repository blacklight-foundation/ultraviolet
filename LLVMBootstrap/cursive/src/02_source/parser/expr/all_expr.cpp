// =============================================================================
// all_expr.cpp - All Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
// - Parse-All-Expr (Lines 5304-5307)
// - Parse-AllExprList-* (Lines 5799-5807)
// - Parse-AllExprListTail-* (Lines 5809-5822)
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

namespace cursive::ast {

// Import token inspection functions from lexer
using lexer::IsKwTok;

// Forward declarations from other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
bool IsPunc(const Parser& parser, std::string_view punc);
void SkipNewlines(Parser& parser);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);

// =============================================================================
// ParseAllExprListTail - Parse remaining expressions after first
// =============================================================================
//
// SPEC: Lines 5809-5822

ParseElemResult<std::vector<ExprPtr>> ParseAllExprListTail(
    Parser parser, std::vector<ExprPtr> xs) {
  SkipNewlines(parser);
  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-AllExprListTail-End");
    return {parser, xs};
  }
  if (IsPunc(parser, ",")) {
    Parser after = parser;
    Advance(after);
    SkipNewlines(after);
    if (IsPunc(after, "}")) {
      SPEC_RULE("Parse-AllExprListTail-TrailingComma");
      return {after, xs};
    }
    SPEC_RULE("Parse-AllExprListTail-Comma");
    ParseElemResult<ExprPtr> elem = ParseExpr(after);
    xs.push_back(elem.elem);
    return ParseAllExprListTail(elem.parser, std::move(xs));
  }
  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, xs};
}

// =============================================================================
// ParseAllExprList - Parse list of expressions in all block
// =============================================================================
//
// SPEC: Lines 5799-5807

ParseElemResult<std::vector<ExprPtr>> ParseAllExprList(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, "}")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, {}};
  }
  SPEC_RULE("Parse-AllExprList-Cons");
  ParseElemResult<ExprPtr> first = ParseExpr(parser);
  std::vector<ExprPtr> elems;
  elems.push_back(first.elem);
  return ParseAllExprListTail(first.parser, std::move(elems));
}

// =============================================================================
// TryParseAllExpr - Try to parse an all expression
// =============================================================================
//
// SPEC: Lines 5304-5307
// all { expr, expr, ... }
// Returns std::nullopt if current token is not "all".

std::optional<ParseElemResult<ExprPtr>> TryParseAllExpr(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsKwTok(*tok, "all")) {
    return std::nullopt;
  }

  SPEC_RULE("Parse-All-Expr");
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
  ParseElemResult<std::vector<ExprPtr>> elems = ParseAllExprList(after_l);
  if (elems.elem.empty() && IsPunc(elems.parser, "}")) {
    Parser after_r = elems.parser;
    Advance(after_r);
    return ParseElemResult<ExprPtr>{
        after_r, MakeExpr(SpanBetween(parser, after_r), ErrorExpr{})};
  }
  if (!IsPunc(elems.parser, "}")) {
    EmitParseSyntaxErr(elems.parser, TokSpan(elems.parser));
    Parser sync = elems.parser;
    SyncStmt(sync);
    return ParseElemResult<ExprPtr>{
        sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }
  Parser after_r = elems.parser;
  Advance(after_r);
  AllExpr all;
  all.exprs = std::move(elems.elem);
  return ParseElemResult<ExprPtr>{
      after_r, MakeExpr(SpanBetween(parser, after_r), all)};
}

// =============================================================================
// ParseAllExpr - Parse all expression (assumes all keyword verified)
// =============================================================================

ParseElemResult<ExprPtr> ParseAllExpr(Parser parser) {
  SPEC_RULE("Parse-All-Expr");
  Parser next = parser;
  Advance(next);  // consume "all"
  if (!IsPunc(next, "{")) {
    EmitParseSyntaxErr(next, TokSpan(next));
    Parser sync = next;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }
  Parser after_l = next;
  Advance(after_l);
  ParseElemResult<std::vector<ExprPtr>> elems = ParseAllExprList(after_l);
  if (elems.elem.empty() && IsPunc(elems.parser, "}")) {
    Parser after_r = elems.parser;
    Advance(after_r);
    return {after_r, MakeExpr(SpanBetween(parser, after_r), ErrorExpr{})};
  }
  if (!IsPunc(elems.parser, "}")) {
    EmitParseSyntaxErr(elems.parser, TokSpan(elems.parser));
    Parser sync = elems.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }
  Parser after_r = elems.parser;
  Advance(after_r);
  AllExpr all;
  all.exprs = std::move(elems.elem);
  return {after_r, MakeExpr(SpanBetween(parser, after_r), all)};
}

}  // namespace cursive::ast
