// =============================================================================
// unsafe_block_expr.cpp - Unsafe Block Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
// - Parse-Unsafe-Expr (Lines 5374-5377)
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
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

// =============================================================================
// TryParseUnsafeBlockExpr - Try to parse an unsafe block expression
// =============================================================================
//
// SPEC: Lines 5374-5377
// IsKw(Tok(P), `unsafe`)    G |- ParseBlock(Advance(P)) => (P_1, b)
// -----------------------------------------------------------------------
// G |- ParsePrimary(P) => (P_1, UnsafeBlockExpr(b))
//
// Returns std::nullopt if current token is not "unsafe".

std::optional<ParseElemResult<ExprPtr>> TryParseUnsafeBlockExpr(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsKwTok(*tok, "unsafe")) {
    return std::nullopt;
  }

  SPEC_RULE("Parse-Unsafe-Expr");
  Parser next = parser;
  Advance(next);  // past "unsafe"

  ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(next);
  UnsafeBlockExpr unsafe;
  unsafe.block = block.elem;
  return ParseElemResult<ExprPtr>{
      block.parser, MakeExpr(SpanBetween(parser, block.parser), unsafe)};
}

// =============================================================================
// ParseUnsafeBlockExpr - Parse unsafe block (assumes keyword verified)
// =============================================================================
//
// Called when we know we're parsing an unsafe block expression.
// The caller has already verified that the current token is "unsafe".

ParseElemResult<ExprPtr> ParseUnsafeBlockExpr(Parser parser) {
  SPEC_RULE("Parse-Unsafe-Expr");
  Parser next = parser;
  Advance(next);  // past "unsafe"

  ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(next);
  UnsafeBlockExpr unsafe;
  unsafe.block = block.elem;
  return {block.parser, MakeExpr(SpanBetween(parser, block.parser), unsafe)};
}

}  // namespace ultraviolet::ast
