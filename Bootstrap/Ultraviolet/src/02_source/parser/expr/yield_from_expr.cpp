// =============================================================================
// yield_from_expr.cpp - Yield From Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
// - Parse-Yield-From-Expr (Lines 5284-5287)
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
using lexer::IsIdentTok;

// Forward declarations from other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
bool IsKw(const Parser& parser, std::string_view kw);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);

// =============================================================================
// TryParseYieldFromExpr - Try to parse a yield from expression
// =============================================================================
//
// SPEC: Lines 5284-5287
// IsKw(Tok(P), `yield`)
// P_1 = Advance(P)
// (IsIdent(Tok(P_1)) AND Lexeme(Tok(P_1)) = `release`
//     => release_opt = Release AND P_2 = Advance(P_1))
// (NOT (IsIdent(Tok(P_1)) AND Lexeme(Tok(P_1)) = `release`)
//     => release_opt = bottom AND P_2 = P_1)
// IsKw(Tok(P_2), `from`)  // This IS the yield-from case
// Gamma |- ParseExpr(Advance(P_2)) => (P_3, e)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParsePrimary(P) => (P_3, YieldFromExpr(release_opt, e))
//
// Returns std::nullopt if:
// - Current token is not "yield", OR
// - After yield (and optional release), the token is NOT "from"

std::optional<ParseElemResult<ExprPtr>> TryParseYieldFromExpr(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsKwTok(*tok, "yield")) {
    return std::nullopt;
  }

  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "yield"

  // Check for optional "release" modifier
  bool release = false;
  const lexer::Token* maybe_release = Tok(next);
  if (maybe_release && IsIdentTok(*maybe_release) &&
      maybe_release->lexeme == "release") {
    release = true;
    Advance(next);
  }

  // Check for "from" keyword - must be present for yield-from
  if (!IsKw(next, "from")) {
    return std::nullopt;  // This is a plain yield, not yield-from
  }

  // Parse yield-from expression
  SPEC_RULE("Parse-Yield-From-Expr");
  Parser after_from = next;
  Advance(after_from);  // consume "from"
  ParseElemResult<ExprPtr> expr = ParseExpr(after_from);
  YieldFromExpr yf;
  yf.release = release;
  yf.value = expr.elem;
  return ParseElemResult<ExprPtr>{
      expr.parser, MakeExpr(SpanBetween(start, expr.parser), yf)};
}

// =============================================================================
// ParseYieldFromExpr - Parse yield from expression (assumes verified)
// =============================================================================
//
// Called when we know we're parsing a yield-from expression.
// The caller has already verified that "yield [release] from" is present.

ParseElemResult<ExprPtr> ParseYieldFromExpr(Parser parser, bool release_already_parsed, bool release_value) {
  SPEC_RULE("Parse-Yield-From-Expr");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "yield"

  bool release = release_value;
  if (!release_already_parsed) {
    // Parse optional "release" modifier
    const lexer::Token* maybe_release = Tok(next);
    if (maybe_release && IsIdentTok(*maybe_release) &&
        maybe_release->lexeme == "release") {
      release = true;
      Advance(next);
    }
  }

  // Advance past "from" keyword (caller verified it's present)
  Advance(next);

  // Parse delegate expression
  ParseElemResult<ExprPtr> expr = ParseExpr(next);
  YieldFromExpr yf;
  yf.release = release;
  yf.value = expr.elem;
  return {expr.parser, MakeExpr(SpanBetween(start, expr.parser), yf)};
}

}  // namespace ultraviolet::ast
