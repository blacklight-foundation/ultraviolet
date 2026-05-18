// =============================================================================
// transmute_expr.cpp - Transmute Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
// - Parse-Transmute-Expr (Lines 5384-5387)
// - Parse-Transmute-Expr-ShiftSplit (Lines 5379-5382)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Import token inspection functions from lexer
using lexer::IsKwTok;

// Forward declarations from other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view punc);
Parser SplitShiftR(const Parser& parser);
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);

// =============================================================================
// TryParseTransmuteExpr - Try to parse a transmute expression
// =============================================================================
//
// SPEC: Lines 5379-5387
// Syntax: transmute<T1, T2>(expr)
//
// Handles both normal case (single >) and shift-split case (>> that needs
// splitting when nested generics are involved).
//
// Returns std::nullopt if current token is not "transmute".

std::optional<ParseElemResult<ExprPtr>> TryParseTransmuteExpr(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsKwTok(*tok, "transmute")) {
    return std::nullopt;
  }

  SPEC_RULE("ParseTransmuteExprFamily");
  Parser next = parser;
  Advance(next);  // past "transmute"

  // Expect <
  if (!IsOp(next, "<")) {
    EmitParseSyntaxErr(next, TokSpan(next));
    Parser sync = next;
    SyncStmt(sync);
    return ParseElemResult<ExprPtr>{
        sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }

  Parser after_lt = next;
  Advance(after_lt);  // past "<"

  // Parse first type (source type)
  ParseElemResult<std::shared_ptr<Type>> t1 = ParseType(after_lt);

  // Expect comma between types
  if (!IsPunc(t1.parser, ",")) {
    EmitParseSyntaxErr(t1.parser, TokSpan(t1.parser));
    Parser sync = t1.parser;
    SyncStmt(sync);
    return ParseElemResult<ExprPtr>{
        sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }

  Parser after_comma = t1.parser;
  Advance(after_comma);  // past ","

  // Parse second type (destination type)
  ParseElemResult<std::shared_ptr<Type>> t2 = ParseType(after_comma);

  // Handle >> case (shift-split for nested generics)
  if (IsOp(t2.parser, ">>")) {
    SPEC_RULE("Parse-Transmute-Expr-ShiftSplit");
    Parser split = SplitShiftR(t2.parser);
    Parser at_second = split;
    Advance(at_second);  // past first ">"

    if (!IsOp(at_second, ">")) {
      EmitParseSyntaxErr(at_second, TokSpan(at_second));
      Parser sync = at_second;
      SyncStmt(sync);
      return ParseElemResult<ExprPtr>{
          sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }

    Parser after_gt = at_second;
    Advance(after_gt);  // past second ">"

    if (!IsPunc(after_gt, "(")) {
      EmitParseSyntaxErr(after_gt, TokSpan(after_gt));
      Parser sync = after_gt;
      SyncStmt(sync);
      return ParseElemResult<ExprPtr>{
          sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }

    Parser after_l = after_gt;
    Advance(after_l);  // past "("

    ParseElemResult<ExprPtr> expr = ParseExpr(after_l);

    if (!IsPunc(expr.parser, ")")) {
      EmitParseSyntaxErr(expr.parser, TokSpan(expr.parser));
      Parser sync = expr.parser;
      SyncStmt(sync);
      return ParseElemResult<ExprPtr>{
          sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }

    Parser after = expr.parser;
    Advance(after);  // past ")"

    TransmuteExpr trans;
    trans.from = t1.elem;
    trans.to = t2.elem;
    trans.value = expr.elem;
    return ParseElemResult<ExprPtr>{
        after, MakeExpr(SpanBetween(parser, after), trans)};
  }

  // Normal case (single >)
  if (IsOp(t2.parser, ">")) {
    SPEC_RULE("Parse-Transmute-Expr");
    Parser after_gt = t2.parser;
    Advance(after_gt);  // past ">"

    if (!IsPunc(after_gt, "(")) {
      EmitParseSyntaxErr(after_gt, TokSpan(after_gt));
      Parser sync = after_gt;
      SyncStmt(sync);
      return ParseElemResult<ExprPtr>{
          sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }

    Parser after_l = after_gt;
    Advance(after_l);  // past "("

    ParseElemResult<ExprPtr> expr = ParseExpr(after_l);

    if (!IsPunc(expr.parser, ")")) {
      EmitParseSyntaxErr(expr.parser, TokSpan(expr.parser));
      Parser sync = expr.parser;
      SyncStmt(sync);
      return ParseElemResult<ExprPtr>{
          sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }

    Parser after = expr.parser;
    Advance(after);  // past ")"

    TransmuteExpr trans;
    trans.from = t1.elem;
    trans.to = t2.elem;
    trans.value = expr.elem;
    return ParseElemResult<ExprPtr>{
        after, MakeExpr(SpanBetween(parser, after), trans)};
  }

  // Error: expected > or >> after second type
  EmitParseSyntaxErr(t2.parser, TokSpan(t2.parser));
  Parser sync = t2.parser;
  SyncStmt(sync);
  return ParseElemResult<ExprPtr>{
      sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
}

}  // namespace ultraviolet::ast
