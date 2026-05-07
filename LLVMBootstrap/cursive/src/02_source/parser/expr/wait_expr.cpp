// =============================================================================
// wait_expr.cpp - Wait Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.8.6, Lines 5274-5277
//
// FORMAL RULE - Parse-Wait-Expr (Lines 5274-5277):
// -----------------------------------------------------------------------------
// IsIdent(Tok(P))    Lexeme(Tok(P)) = `wait`
// Gamma |- ParseExpr(Advance(P)) => (P_1, handle)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParsePrimary(P) => (P_1, WaitExpr(handle))
//
// NOTE: `wait` is a CONTEXTUAL KEYWORD, not a reserved keyword.
// This means it can be used as an identifier in other contexts.
// The parser must check IsIdent && Lexeme == "wait" (not IsKw).
//
// SEMANTICS:
// - `wait handle_expr`
// - Blocks until the Spawned<T> handle completes
// - Returns the value of type T from the spawned task
// - Must appear within a parallel block context
// - CRITICAL: Keys MUST NOT be held across wait suspension points (E-CON-0206)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::Ctx;
using cursive::lexer::Token;

// Forward declarations from other parser modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
bool IsExprStart(const Token& tok);

// =============================================================================
// IsWaitContextualKeyword - Check for "wait" contextual keyword
// =============================================================================
//
// CRITICAL: The wait expression check MUST appear before general identifier
// parsing to prevent "wait" from being consumed as a simple IdentifierExpr.
// This helper makes the intent clear and routes through the shared Ctx
// predicate so every parser site agrees on contextual keyword handling.

bool IsWaitContextualKeyword(const Token* tok) {
  return tok && Ctx(*tok, "wait");
}

// =============================================================================
// TryParseWaitExpr - Parse wait expression if present
// =============================================================================
//
// SPEC: Lines 5274-5277
// Returns nullopt if current token is not "wait" contextual keyword.
// Must be called BEFORE general identifier parsing in ParsePrimary.

std::optional<ParseElemResult<ExprPtr>> TryParseWaitExpr(Parser parser) {
  const Token* tok = Tok(parser);
  if (!IsWaitContextualKeyword(tok)) {
    return std::nullopt;
  }
  Parser next = parser;
  Advance(next);

  // `wait` remains a contextual keyword: if no expression follows, this is
  // parsed as an identifier expression by ParsePrimary.
  const Token* next_tok = Tok(next);
  if (!next_tok || !IsExprStart(*next_tok)) {
    return std::nullopt;
  }

  SPEC_RULE("Parse-Wait-Expr");
  // Parse handle expression
  ParseElemResult<ExprPtr> handle = ParseExpr(next);
  WaitExpr wait;
  wait.handle = handle.elem;
  return ParseElemResult<ExprPtr>{
      handle.parser,
      MakeExpr(SpanBetween(parser, handle.parser), wait)};
}

}  // namespace cursive::ast
