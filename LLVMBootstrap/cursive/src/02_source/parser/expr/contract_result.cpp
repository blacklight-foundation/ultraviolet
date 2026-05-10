// =============================================================================
// contract_result.cpp - Contract Result Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
// - Parse-Contract-Result (Lines 5168-5171)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Import token inspection functions from lexer
using lexer::IsOpTok;
using lexer::IsIdentTok;

// Forward declarations from other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);

// =============================================================================
// TryParseContractResultExpr - Try to parse @result expression
// =============================================================================
//
// SPEC: Lines 5168-5171
// IsOp(Tok(P), "@")    IsIdent(Tok(Advance(P)))    Lexeme(Tok(Advance(P))) = "result"
// ----------------------------------------------------------------------------
// Gamma |- ParsePrimary(P) => (Advance(Advance(P)), ContractResult)
//
// Returns std::nullopt if not "@result".
// Note: This function only handles @result; @entry is in contract_entry.cpp.

std::optional<ParseElemResult<ExprPtr>> TryParseContractResultExpr(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsOpTok(*tok, "@")) {
    return std::nullopt;
  }

  Parser next = parser;
  Advance(next);  // past "@"

  const lexer::Token* name_tok = Tok(next);
  if (!name_tok) {
    return std::nullopt;
  }

  // Check if it's "result" identifier or keyword
  if (!(IsIdentTok(*name_tok) || name_tok->kind == lexer::TokenKind::Keyword)) {
    return std::nullopt;
  }

  if (name_tok->lexeme != "result") {
    return std::nullopt;  // Not @result, let other parsers handle
  }

  SPEC_RULE("Parse-Contract-Result");
  Parser after_name = next;
  Advance(after_name);  // past "result"

  ResultExpr res;
  return ParseElemResult<ExprPtr>{
      after_name, MakeExpr(SpanBetween(parser, after_name), res)};
}

}  // namespace cursive::ast
