// =============================================================================
// null_ptr.cpp - Ptr::null() Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
// - Parse-Null-Ptr (Lines 5163-5166)
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
using lexer::IsIdentTok;

// Forward declarations from other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view punc);

// =============================================================================
// TryParsePtrNullExpr - Try to parse Ptr::null() expression
// =============================================================================
//
// SPEC: Lines 5163-5166
// IsIdent(Tok(P))
//     Lexeme(Tok(P)) = `Ptr`
//     IsOp(Tok(Advance(P)), "::")
//     Tok(Advance(Advance(P))).kind = NullLiteral
//     IsPunc(Tok(Advance(Advance(Advance(P)))), "(")
//     IsPunc(Tok(Advance(Advance(Advance(Advance(P))))), ")")
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParsePrimary(P) => (Advance(Advance(Advance(Advance(Advance(P))))), PtrNullExpr)
//
// Returns std::nullopt if token sequence doesn't match Ptr :: null ( ).
// On partial match that fails, returns std::nullopt to allow fallthrough
// to qualified name parsing.

std::optional<ParseElemResult<ExprPtr>> TryParsePtrNullExpr(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsIdentTok(*tok) || tok->lexeme != "Ptr") {
    return std::nullopt;
  }

  Parser next = parser;
  Advance(next);  // past "Ptr"

  if (!IsOp(next, "::")) {
    return std::nullopt;
  }

  Parser after_colon = next;
  Advance(after_colon);  // past "::"

  const lexer::Token* lit = Tok(after_colon);
  if (!lit || lit->kind != lexer::TokenKind::NullLiteral) {
    return std::nullopt;
  }

  Parser after_lit = after_colon;
  Advance(after_lit);  // past "null"

  if (!IsPunc(after_lit, "(")) {
    return std::nullopt;
  }

  Parser after_l = after_lit;
  Advance(after_l);  // past "("

  if (!IsPunc(after_l, ")")) {
    return std::nullopt;
  }

  SPEC_RULE("Parse-Null-Ptr");
  Parser after = after_l;
  Advance(after);  // past ")"
  PtrNullExpr ptr;
  return ParseElemResult<ExprPtr>{after, MakeExpr(SpanBetween(parser, after), ptr)};
}

}  // namespace ultraviolet::ast
