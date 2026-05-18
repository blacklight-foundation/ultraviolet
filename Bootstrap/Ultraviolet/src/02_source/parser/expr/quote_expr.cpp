// =============================================================================
// quote_expr.cpp - Compile-Time Quote Parsing
// =============================================================================

#include "02_source/parser/parser.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

using ultraviolet::lexer::IsIdentTok;
using ultraviolet::lexer::IsKwTok;
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

ExprPtr MakeExpr(const core::Span& span, ExprNode node);
bool IsPunc(const Parser& parser, std::string_view punc);

namespace {

bool IsQuoteHead(const Token& tok) {
  return IsKwTok(tok, "quote") || (IsIdentTok(tok) && tok.lexeme == "quote");
}

bool IsPatternHead(const Token& tok) {
  return (tok.kind == TokenKind::Identifier || tok.kind == TokenKind::Keyword) &&
         tok.lexeme == "pattern";
}

std::vector<Token> SliceTokensBetween(const Parser& start, const Parser& end) {
  std::vector<Token> out;
  if (!start.tokens || !end.tokens || start.tokens != end.tokens) {
    return out;
  }

  const auto [from, to] = TokensBetween(start, end);
  if (to <= from || from >= start.tokens->size()) {
    return out;
  }

  const std::size_t last = std::min(to, start.tokens->size());
  out.reserve(last - from);
  for (std::size_t i = from; i < last; ++i) {
    out.push_back((*start.tokens)[i]);
  }
  return out;
}

bool ScanQuotedBody(Parser& parser) {
  std::size_t brace_depth = 0;
  while (!AtEof(parser)) {
    const Token* tok = Tok(parser);
    if (!tok) {
      break;
    }
    if (tok->kind == TokenKind::Punctuator) {
      if (tok->lexeme == "{") {
        ++brace_depth;
        Advance(parser);
        continue;
      }
      if (tok->lexeme == "}") {
        if (brace_depth == 0) {
          return true;
        }
        --brace_depth;
        Advance(parser);
        continue;
      }
    }
    Advance(parser);
  }
  return false;
}

}  // namespace

std::optional<ParseElemResult<ExprPtr>> TryParseQuoteExpr(Parser parser) {
  const Token* tok = Tok(parser);
  if (!tok || !IsQuoteHead(*tok)) {
    return std::nullopt;
  }

  Parser start = parser;
  Parser cur = parser;
  Advance(cur);

  QuoteKind kind = QuoteKind::Unspecified;
  if (const Token* next = Tok(cur); next && IsKwTok(*next, "type")) {
    kind = QuoteKind::Type;
    Advance(cur);
  } else if (const Token* next = Tok(cur); next && IsPatternHead(*next)) {
    kind = QuoteKind::Pattern;
    Advance(cur);
  }

  if (!IsPunc(cur, "{")) {
    EmitParseSyntaxErr(cur, TokSpan(cur));
    Parser sync = cur;
    SyncStmt(sync);
    return ParseElemResult<ExprPtr>{
        sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  Advance(cur);
  Parser content_start = cur;
  Parser content_end = Clone(cur);
  if (!ScanQuotedBody(content_end) || !IsPunc(content_end, "}")) {
    EmitParseSyntaxErr(cur, TokSpan(cur));
    Parser sync = cur;
    SyncStmt(sync);
    return ParseElemResult<ExprPtr>{
        sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  QuoteExpr quote;
  quote.kind = kind;
  quote.tokens = SliceTokensBetween(content_start, content_end);
  if (kind == QuoteKind::Type) {
    SPEC_RULE("Parse-Quote-Type");
  } else if (kind == QuoteKind::Pattern) {
    SPEC_RULE("Parse-Quote-Pattern");
  } else {
    SPEC_RULE("Parse-Quote-Raw");
  }
  Advance(content_end);
  return ParseElemResult<ExprPtr>{
      content_end, MakeExpr(SpanBetween(start, content_end), quote)};
}

}  // namespace ultraviolet::ast
