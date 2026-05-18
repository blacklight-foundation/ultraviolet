// =============================================================================
// MIGRATION MAPPING: literal.cpp
// =============================================================================
// This file implements parsing for literal expressions (all primitive literal
// types: integers, floats, strings, characters, booleans, and null).
//
// =============================================================================
// SPEC REFERENCE: Docs/SPECIFICATION.md
// =============================================================================
// Lines 5158-5161:
//
// **(Parse-Literal-Expr)**
// Tok(P).kind ∈ {IntLiteral, FloatLiteral, StringLiteral, CharLiteral, BoolLiteral, NullLiteral}
// ───────────────────────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParsePrimary(P) ⇓ (Advance(P), Literal(Tok(P)))
//
// SEMANTICS:
// - Current token must be one of the literal token kinds
// - Returns LiteralExpr containing the literal token
// - Parser advances past the literal token
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_expr.cpp
// =============================================================================
//
// 1. HELPER FUNCTION: IsLiteralToken
//    Source lines: 49-56
//    -------------------------------------------------------------------------
//    bool IsLiteralToken(const Token& tok) {
//      return tok.kind == TokenKind::IntLiteral ||
//             tok.kind == TokenKind::FloatLiteral ||
//             tok.kind == TokenKind::StringLiteral ||
//             tok.kind == TokenKind::CharLiteral ||
//             tok.kind == TokenKind::BoolLiteral ||
//             tok.kind == TokenKind::NullLiteral;
//    }
//
// 2. LITERAL PARSING (within ParsePrimary)
//    Source lines: 1003-1010
//    -------------------------------------------------------------------------
//    if (tok && IsLiteralToken(*tok)) {
//      SPEC_RULE("Parse-Literal-Expr");
//      LiteralExpr lit;
//      lit.literal = *tok;
//      Parser next = parser;
//      Advance(next);
//      return {next, MakeExpr(tok->span, lit)};
//    }
//
// =============================================================================
// DEPENDENCIES
// =============================================================================
// FROM expr_common.cpp:
// - MakeExpr(Span, ExprNode) -> ExprPtr
//
// FROM parser_common.cpp:
// - Parser state type
// - Advance(Parser&) -> void
// - Tok(Parser) -> const Token*
//
// FROM lexer types:
// - Token struct
// - TokenKind enum (IntLiteral, FloatLiteral, StringLiteral, CharLiteral,
//                   BoolLiteral, NullLiteral)
//
// FROM AST types:
// - LiteralExpr struct { Token literal; }
// - ExprPtr = std::shared_ptr<Expr>
//
// =============================================================================
// REFACTORING NOTES
// =============================================================================
// 1. Extract IsLiteralToken helper into this file or a shared helper location.
//
// 2. Export a standalone function:
//    std::optional<ParseElemResult<ExprPtr>> TryParseLiteralExpr(Parser parser);
//    Returns std::nullopt if current token is not a literal.
//
// 3. Alternative signature for direct use:
//    ParseElemResult<ExprPtr> ParseLiteralExpr(Parser parser);
//    Assumes caller has already verified IsLiteralToken(*Tok(parser)).
//
// 4. The NullLiteral case here is for the bare `null` keyword.
//    Note: Ptr::null() syntax is handled separately in null_ptr.cpp.
//
// 5. LiteralExpr stores the entire token, preserving:
//    - Token kind for type discrimination
//    - Lexeme for the literal value
//    - Span for source location
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Forward declarations from expr_common.cpp
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
bool IsLiteralToken(const Token& tok);

// =============================================================================
// TryParseLiteralExpr - Try to parse a literal expression
// =============================================================================
//
// SPEC: Lines 5158-5161
// Returns std::nullopt if current token is not a literal.
// Otherwise returns the parsed LiteralExpr.

std::optional<ParseElemResult<ExprPtr>> TryParseLiteralExpr(Parser parser) {
  const Token* tok = Tok(parser);
  if (!tok || !IsLiteralToken(*tok)) {
    return std::nullopt;
  }
  SPEC_RULE("Parse-Literal-Expr");
  LiteralExpr lit;
  lit.literal = *tok;
  Parser next = parser;
  Advance(next);
  return ParseElemResult<ExprPtr>{next, MakeExpr(tok->span, lit)};
}

// =============================================================================
// ParseLiteralExpr - Parse a literal expression
// =============================================================================
//
// SPEC: Lines 5158-5161
// Assumes caller has already verified IsLiteralToken(*Tok(parser)).
// Returns the parsed LiteralExpr.

ParseElemResult<ExprPtr> ParseLiteralExpr(Parser parser) {
  SPEC_RULE("Parse-Literal-Expr");
  const Token* tok = Tok(parser);
  LiteralExpr lit;
  lit.literal = tok ? *tok : Token{};
  core::Span span = tok ? tok->span : TokSpan(parser);
  Parser next = parser;
  Advance(next);
  return {next, MakeExpr(span, lit)};
}

}  // namespace ultraviolet::ast
