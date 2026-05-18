// =============================================================================
// alloc_expr.cpp - Allocation Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
// - Parse-Alloc-Implicit (Lines 5178-5181)
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

// Forward declarations from other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);

// =============================================================================
// TryParseAllocExpr - Try to parse an allocation expression
// =============================================================================
//
// SPEC: Lines 5178-5181
// IsOp(Tok(P), "^")    G |- ParseExpr(Advance(P)) => (P_1, e)
// -----------------------------------------------------------------------
// G |- ParsePrimary(P) => (P_1, AllocExpr(bottom, e))
//
// Returns std::nullopt if current token is not "^".
// Otherwise parses the value expression and returns AllocExpr.

std::optional<ParseElemResult<ExprPtr>> TryParseAllocExpr(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsOpTok(*tok, "^")) {
    return std::nullopt;
  }

  SPEC_RULE("Parse-Alloc-Implicit");
  Parser next = parser;
  Advance(next);
  ParseElemResult<ExprPtr> expr = ParseExpr(next);
  AllocExpr alloc;
  alloc.region_opt = std::nullopt;  // Implicit region
  alloc.value = expr.elem;
  return ParseElemResult<ExprPtr>{
      expr.parser, MakeExpr(SpanBetween(parser, expr.parser), alloc)};
}

// =============================================================================
// ParseAllocExpr - Parse allocation expression (assumes ^ already verified)
// =============================================================================
//
// Unconditionally parses an allocation expression.
// Use TryParseAllocExpr for optional/lookahead parsing.

ParseElemResult<ExprPtr> ParseAllocExpr(Parser parser) {
  SPEC_RULE("Parse-Alloc-Implicit");
  Parser next = parser;
  Advance(next);  // consume "^"
  ParseElemResult<ExprPtr> expr = ParseExpr(next);
  AllocExpr alloc;
  alloc.region_opt = std::nullopt;  // Implicit region
  alloc.value = expr.elem;
  return {expr.parser, MakeExpr(SpanBetween(parser, expr.parser), alloc)};
}

// =============================================================================
// ParseExplicitAllocExpr - Parse explicit region allocation (r ^ expr)
// =============================================================================
//
// Called when identifier followed by "^" is detected.
// The region identifier has already been parsed by the caller.

ParseElemResult<ExprPtr> ParseExplicitAllocExpr(Parser parser,
                                                const Identifier& region_name,
                                                const Parser& start) {
  SPEC_RULE("Parse-Alloc-Explicit");
  Parser next = parser;
  Advance(next);  // consume "^"
  ParseElemResult<ExprPtr> expr = ParseExpr(next);
  AllocExpr alloc;
  alloc.region_opt = region_name;
  alloc.value = expr.elem;
  return {expr.parser, MakeExpr(SpanBetween(start, expr.parser), alloc)};
}

}  // namespace ultraviolet::ast
