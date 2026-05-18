// =============================================================================
// sync_expr.cpp - Sync Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
// - Parse-Sync-Expr (Lines 5294-5297)
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
ParseElemResult<ExprPtr> ParseExpr(Parser parser);

// =============================================================================
// TryParseSyncExpr - Try to parse a sync expression
// =============================================================================
//
// SPEC: Lines 5294-5297
// IsKw(Tok(P), `sync`)
// Gamma |- ParseExpr(Advance(P)) => (P_1, e)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParsePrimary(P) => (P_1, SyncExpr(e))
//
// Returns std::nullopt if current token is not "sync".

std::optional<ParseElemResult<ExprPtr>> TryParseSyncExpr(Parser parser,
                                                         bool allow_brace) {
  (void)allow_brace;
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsKwTok(*tok, "sync")) {
    return std::nullopt;
  }

  SPEC_RULE("Parse-Sync-Expr");
  Parser next = parser;
  Advance(next);
  ParseElemResult<ExprPtr> expr = ParseExpr(next);
  SyncExpr sync;
  sync.value = expr.elem;
  return ParseElemResult<ExprPtr>{
      expr.parser, MakeExpr(SpanBetween(parser, expr.parser), sync)};
}

// =============================================================================
// ParseSyncExpr - Parse sync expression (assumes sync keyword verified)
// =============================================================================

ParseElemResult<ExprPtr> ParseSyncExpr(Parser parser, bool allow_brace) {
  (void)allow_brace;
  SPEC_RULE("Parse-Sync-Expr");
  Parser next = parser;
  Advance(next);  // consume "sync"
  ParseElemResult<ExprPtr> expr = ParseExpr(next);
  SyncExpr sync;
  sync.value = expr.elem;
  return {expr.parser, MakeExpr(SpanBetween(parser, expr.parser), sync)};
}

}  // namespace ultraviolet::ast
