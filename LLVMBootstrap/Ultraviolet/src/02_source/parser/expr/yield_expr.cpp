// =============================================================================
// yield_expr.cpp - Yield Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
// - Parse-Yield-Expr (Lines 5289-5292)
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
// TryParseYieldExpr - Try to parse a yield expression
// =============================================================================
//
// SPEC: Lines 5289-5292
// IsKw(Tok(P), `yield`)
// P_1 = Advance(P)
// (IsIdent(Tok(P_1)) AND Lexeme(Tok(P_1)) = `release`
//     => release_opt = Release AND P_2 = Advance(P_1))
// (NOT (IsIdent(Tok(P_1)) AND Lexeme(Tok(P_1)) = `release`)
//     => release_opt = bottom AND P_2 = P_1)
// NOT IsKw(Tok(P_2), `from`)  // Distinguishes from yield-from
// Gamma |- ParseExpr(P_2) => (P_3, e)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParsePrimary(P) => (P_3, YieldExpr(release_opt, e))
//
// Returns std::nullopt if:
// - Current token is not "yield", OR
// - After yield (and optional release), the token is "from" (yield-from case)

std::optional<ParseElemResult<ExprPtr>> TryParseYieldExpr(Parser parser) {
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

  // Check for "from" keyword - if present, this is yield-from, not yield
  if (IsKw(next, "from")) {
    return std::nullopt;  // Let TryParseYieldFromExpr handle it
  }

  // Parse yield value expression
  SPEC_RULE("Parse-Yield-Expr");
  ParseElemResult<ExprPtr> expr = ParseExpr(next);
  YieldExpr y;
  y.release = release;
  y.value = expr.elem;
  return ParseElemResult<ExprPtr>{
      expr.parser, MakeExpr(SpanBetween(start, expr.parser), y)};
}

// =============================================================================
// ParseYieldExpr - Parse yield expression (assumes yield keyword verified)
// =============================================================================
//
// Called when we know we're parsing a yield expression (not yield-from).
// The caller has already verified that after yield and optional release,
// there is no "from" keyword.

ParseElemResult<ExprPtr> ParseYieldExpr(Parser parser, bool release_already_parsed, bool release_value) {
  SPEC_RULE("Parse-Yield-Expr");
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

  // Parse yield value expression
  ParseElemResult<ExprPtr> expr = ParseExpr(next);
  YieldExpr y;
  y.release = release;
  y.value = expr.elem;
  return {expr.parser, MakeExpr(SpanBetween(start, expr.parser), y)};
}

}  // namespace ultraviolet::ast
