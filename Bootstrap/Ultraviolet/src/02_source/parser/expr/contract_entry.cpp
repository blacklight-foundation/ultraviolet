// =============================================================================
// contract_entry.cpp - Contract Entry Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
// - Parse-Contract-Entry (Lines 5173-5176)
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
using lexer::IsOpTok;
using lexer::IsIdentTok;

// Forward declarations from other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
bool IsPunc(const Parser& parser, std::string_view punc);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);

// =============================================================================
// TryParseContractEntryExpr - Try to parse @entry(expr) expression
// =============================================================================
//
// SPEC: Lines 5173-5176
// IsOp(Tok(P), "@")    IsIdent(Tok(Advance(P)))    Lexeme(Tok(Advance(P))) = "entry"
// IsPunc(Tok(Advance(Advance(P))), "(")
// Gamma |- ParseExpr(Advance(Advance(Advance(P)))) => (P_1, e)
// IsPunc(Tok(P_1), ")")
// ----------------------------------------------------------------------------
// Gamma |- ParsePrimary(P) => (Advance(P_1), ContractEntry(e))
//
// Returns std::nullopt if not "@entry".
// Note: This function only handles @entry; @result is in contract_result.cpp.

std::optional<ParseElemResult<ExprPtr>> TryParseContractEntryExpr(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsOpTok(*tok, "@")) {
    return std::nullopt;
  }

  Parser start = parser;
  Parser next = parser;
  Advance(next);  // past "@"

  const lexer::Token* name_tok = Tok(next);
  if (!name_tok) {
    return std::nullopt;
  }

  // Check if it's "entry" identifier or keyword
  if (!(IsIdentTok(*name_tok) || name_tok->kind == lexer::TokenKind::Keyword)) {
    return std::nullopt;
  }

  if (name_tok->lexeme != "entry") {
    return std::nullopt;  // Not @entry, let other parsers handle
  }

  Parser after_name = next;
  Advance(after_name);  // past "entry"

  // Expect opening parenthesis
  if (!IsPunc(after_name, "(")) {
    EmitParseSyntaxErr(after_name, TokSpan(after_name));
    Parser sync = after_name;
    SyncStmt(sync);
    return ParseElemResult<ExprPtr>{
        sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  SPEC_RULE("Parse-Contract-Entry");
  Parser after_l = after_name;
  Advance(after_l);  // past "("

  // Parse the captured expression
  ParseElemResult<ExprPtr> expr = ParseExpr(after_l);

  // Expect closing parenthesis
  if (!IsPunc(expr.parser, ")")) {
    EmitParseSyntaxErr(expr.parser, TokSpan(expr.parser));
    Parser sync = expr.parser;
    SyncStmt(sync);
    return ParseElemResult<ExprPtr>{
        sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  Parser after_r = expr.parser;
  Advance(after_r);  // past ")"

  EntryExpr entry;
  entry.expr = expr.elem;
  return ParseElemResult<ExprPtr>{
      after_r, MakeExpr(SpanBetween(start, after_r), entry)};
}

}  // namespace ultraviolet::ast
