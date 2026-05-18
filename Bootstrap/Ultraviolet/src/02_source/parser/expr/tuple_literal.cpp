// =============================================================================
// tuple_literal.cpp - Tuple Literal and Parenthesized Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
// - Parse-Parenthesized-Expr (Lines 5204-5207)
// - Parse-Tuple-Literal (Lines 5209-5212)
// - Parse-TupleExprElems-* (Lines 5838-5856)
// - TupleParen disambiguation (Lines 5858-5877)
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
bool IsPuncTok(const lexer::Token& tok, std::string_view punc);
void SkipNewlines(Parser& parser);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);

ParseElemResult<std::vector<ExprPtr>> ParseExprListTail(ListState<ExprPtr> state) {
  const std::array<EndSetToken, 1> end_set = {EndPunct(")")};

  for (;;) {
    SkipNewlines(state.parser);
    if (ListDone(state, end_set)) {
      SPEC_RULE("Parse-ExprListTail-End");
      return {state.parser, std::move(state.elems)};
    }

    if (!IsPunc(state.parser, ",")) {
      EmitParseSyntaxErr(state.parser, TokSpan(state.parser));
      return {state.parser, std::move(state.elems)};
    }

    Parser after_comma = state.parser;
    Advance(after_comma);
    SkipNewlines(after_comma);
    if (IsPunc(after_comma, ")")) {
      if (TrailingCommaAllowed(state.parser, end_set)) {
        SPEC_RULE("Parse-ExprListTail-TrailingComma");
      }
      EmitTrailingCommaErr(state.parser, end_set);
      after_comma.diags = state.parser.diags;
      return {after_comma, std::move(state.elems)};
    }

    SPEC_RULE("Parse-ExprListTail-Comma");
    state.parser = after_comma;
    state = ListCons(std::move(state), ParseExpr);
  }
}

// =============================================================================
// TupleScanResult - Result of tuple/paren disambiguation scan
// =============================================================================

struct TupleScanResult {
  bool is_tuple = false;
};

// =============================================================================
// ParenDelta - Compute paren depth delta for a token
// =============================================================================
//
// SPEC: Lines 5858-5862
// ParenDelta(Punctuator("(")) = 1
// ParenDelta(Punctuator(")")) = -1
// ParenDelta(t) = 0 if t.kind ∉ {Punctuator("("), Punctuator(")")}

static int ParenDelta(const lexer::Token& tok) {
  if (tok.kind != lexer::TokenKind::Punctuator) {
    return 0;
  }
  if (tok.lexeme == "(") {
    return 1;
  }
  if (tok.lexeme == ")") {
    return -1;
  }
  return 0;
}

struct TupleScanDepth {
  int paren = 1;
  int bracket = 0;
  int brace = 0;
};

static void StepNonParenNesting(const lexer::Token& tok, TupleScanDepth& depth) {
  if (tok.kind != lexer::TokenKind::Punctuator) {
    return;
  }
  if (tok.lexeme == "[") {
    depth.bracket += 1;
    return;
  }
  if (tok.lexeme == "]" && depth.bracket > 0) {
    depth.bracket -= 1;
    return;
  }
  if (tok.lexeme == "{") {
    depth.brace += 1;
    return;
  }
  if (tok.lexeme == "}" && depth.brace > 0) {
    depth.brace -= 1;
    return;
  }
}

// =============================================================================
// TupleScan - Scan ahead to determine if parens contain tuple or single expr
// =============================================================================
//
// SPEC: Lines 5864-5877
// - Returns false if EOF
// - Returns false if ) found at depth 1 (no separator seen)
// - Returns true if ; is found at outer tuple depth, or if , is followed by
//   another element after newline skipping
// - Recurses with adjusted delimiter depths otherwise

static TupleScanResult TupleScan(Parser parser) {
  TupleScanResult result;
  Parser cur = parser;
  TupleScanDepth depth;
  for (;;) {
    if (AtEof(cur)) {
      result.is_tuple = false;
      return result;
    }
    const lexer::Token* tok = Tok(cur);
    if (!tok) {
      result.is_tuple = false;
      return result;
    }
    if (tok->kind == lexer::TokenKind::Punctuator && tok->lexeme == ")" &&
        depth.paren == 1) {
      result.is_tuple = false;
      return result;
    }
    if (tok->kind == lexer::TokenKind::Punctuator &&
        (tok->lexeme == "," || tok->lexeme == ";") && depth.paren == 1 &&
        depth.brace == 0 && depth.bracket == 0) {
      if (tok->lexeme == ",") {
        Parser after_sep = AdvanceOrEOF(cur);
        SkipNewlines(after_sep);
        const lexer::Token* next_tok = Tok(after_sep);
        if (next_tok && next_tok->kind == lexer::TokenKind::Punctuator &&
            next_tok->lexeme == ")") {
          result.is_tuple = false;
          return result;
        }
      }
      result.is_tuple = true;
      return result;
    }
    depth.paren += ParenDelta(*tok);
    StepNonParenNesting(*tok, depth);
    Advance(cur);
  }
}

// =============================================================================
// TupleParen - Entry point for tuple vs paren disambiguation
// =============================================================================
//
// SPEC: Lines 5858-5862
// TupleParen(P) ⇔ IsPunc(Tok(P), "(") ∧
//                 (IsPunc(Tok(Advance(P)), ")") ∨ TupleScan(Advance(P), 1) ⇓ true)

bool TupleParen(Parser parser) {
  if (!IsPunc(parser, "(")) {
    return false;
  }
  Parser next = parser;
  Advance(next);
  if (IsPunc(next, ")")) {
    return true;  // () is the empty tuple
  }
  TupleScanResult scan = TupleScan(next);
  return scan.is_tuple;
}

// =============================================================================
// ParseTupleExprElems - Parse tuple expression elements
// =============================================================================
//
// SPEC: Lines 5838-5856
// Handles:
// - Empty: ()
// - Single with semicolon: (e;)
// - The non-canonical singleton comma form `(e,)` is always an error
// - Multi-element: (e1, e2, ...)

ParseElemResult<std::vector<ExprPtr>> ParseTupleExprElems(Parser parser) {
  SkipNewlines(parser);
  if (IsPunc(parser, ")")) {
    SPEC_RULE("Parse-TupleExprElems-Empty");
    return {parser, {}};
  }
  ParseElemResult<ExprPtr> first = ParseExpr(parser);
  Parser after_first = first.parser;
  SkipNewlines(after_first);
  if (IsPunc(after_first, ";")) {
    SPEC_RULE("Parse-TupleExprElems-Single");
    Parser after = after_first;
    Advance(after);
    std::vector<ExprPtr> elems;
    elems.push_back(first.elem);
    return {after, elems};
  }
  if (IsPunc(after_first, ",")) {
    Parser after_comma = after_first;
    Advance(after_comma);
    SkipNewlines(after_comma);
    if (IsPunc(after_comma, ")")) {
      EmitParseSyntaxErr(after_first, TokSpan(after_first));
      std::vector<ExprPtr> elems;
      elems.push_back(first.elem);
      return {after_comma, elems};
    }
    SPEC_RULE("Parse-TupleExprElems-Many");
    ParseElemResult<ExprPtr> second = ParseExpr(after_comma);
    ParseElemResult<std::vector<ExprPtr>> tail =
        ParseExprListTail(ListSeed(second.parser, second.elem));
    std::vector<ExprPtr> elems;
    elems.reserve(1 + tail.elem.size());
    elems.push_back(first.elem);
    for (auto& elem : tail.elem) {
      elems.push_back(std::move(elem));
    }
    return {tail.parser, std::move(elems)};
  }
  EmitParseSyntaxErr(first.parser, TokSpan(first.parser));
  return {first.parser, {}};
}

// =============================================================================
// ParseParenthesizedExpr - Parse parenthesized expression (not a tuple)
// =============================================================================
//
// SPEC: Lines 5204-5207
// Assumes parser is at "(" and TupleParen returned false.

ParseElemResult<ExprPtr> ParseParenthesizedExpr(Parser parser) {
  SPEC_RULE("Parse-Parenthesized-Expr");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "("
  ParseElemResult<ExprPtr> expr = ParseExpr(next);
  if (!IsPunc(expr.parser, ")")) {
    EmitParseSyntaxErr(expr.parser, TokSpan(expr.parser));
    Parser sync = expr.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }
  Parser after = expr.parser;
  Advance(after);  // consume ")"
  return {after, expr.elem};
}

// =============================================================================
// ParseTupleLiteralExpr - Parse tuple literal expression
// =============================================================================
//
// SPEC: Lines 5209-5212
// Assumes parser is at "(" and TupleParen returned true.

ParseElemResult<ExprPtr> ParseTupleLiteralExpr(Parser parser) {
  SPEC_RULE("Parse-Tuple-Literal");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "("
  ParseElemResult<std::vector<ExprPtr>> elems = ParseTupleExprElems(next);
  if (!IsPunc(elems.parser, ")")) {
    EmitParseSyntaxErr(elems.parser, TokSpan(elems.parser));
    Parser sync = elems.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }
  Parser after = elems.parser;
  Advance(after);  // consume ")"
  TupleExpr tup;
  tup.elements = std::move(elems.elem);
  return {after, MakeExpr(SpanBetween(start, after), tup)};
}

// =============================================================================
// ParseTupleOrParenExpr - Entry point for tuple/paren disambiguation
// =============================================================================
//
// Assumes parser is at "(".
// Uses TupleParen to decide between tuple and parenthesized expression.

ParseElemResult<ExprPtr> ParseTupleOrParenExpr(Parser parser) {
  if (TupleParen(parser)) {
    return ParseTupleLiteralExpr(parser);
  }
  return ParseParenthesizedExpr(parser);
}

}  // namespace ultraviolet::ast
