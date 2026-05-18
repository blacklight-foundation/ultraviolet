// =============================================================================
// identifier.cpp - Identifier Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
// - Parse-Identifier-Expr (Lines 5184-5187)
// - Parse-Receiver-Ref (receiver reference ~)
// - Parse-Wait-Expr (contextual keyword wait)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/keywords.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Import token inspection functions from lexer
using lexer::IsIdentTok;
using lexer::IsOpTok;

// Forward declarations from other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);

namespace {

bool IsCompileTimeCapabilityName(std::string_view lexeme) {
  return lexeme == "emitter" || lexeme == "introspect" || lexeme == "files" ||
         lexeme == "diagnostics";
}

}  // namespace

// =============================================================================
// ParseReceiverRef - Parse receiver reference (~)
// =============================================================================
//
// SPEC: Receiver reference creates an IdentifierExpr with name "~"
// Used inside method bodies to access the receiver (self).
// Returns std::nullopt if current token is not ~.

std::optional<ParseElemResult<ExprPtr>> TryParseReceiverRef(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsOpTok(*tok, "~")) {
    return std::nullopt;
  }
  SPEC_RULE("Parse-Receiver-Ref");
  Parser next = parser;
  Advance(next);
  IdentifierExpr ident;
  ident.name = "~";
  return ParseElemResult<ExprPtr>{next, MakeExpr(tok->span, ident)};
}

// =============================================================================
// TryParseIdentifierExpr - Try to parse a simple identifier expression
// =============================================================================
//
// SPEC: Lines 5184-5187
// IsIdent(Tok(P))
//     ¬ IsOp(Tok(Advance(P)), "::")
//     ¬ IsOp(Tok(Advance(P)), "@")
//     ¬ IsPunc(Tok(Advance(P)), "{")
// ─────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParsePrimary(P) ⇓ (Advance(P), Identifier(Lexeme(Tok(P))))
//
// Parameters:
// - parser: Current parser state at identifier token
// - allow_brace: If true, lookahead for modal (@), record ({), and generic (<)
//                prevents identifier parsing. If false, these are ignored.
//
// Returns:
// - std::nullopt if not a simple identifier (should try qualified path)
// - ParseElemResult<ExprPtr> with IdentifierExpr if successful

std::optional<ParseElemResult<ExprPtr>> TryParseIdentifierExpr(
    Parser parser, bool allow_brace) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsIdentTok(*tok)) {
    return std::nullopt;
  }

  Parser next = parser;
  Advance(next);
  const lexer::Token* look = Tok(next);

  // Check if this should be parsed as a qualified name/apply instead
  const bool is_qual = look && IsOpTok(*look, "::");
  const bool is_modal = look && IsOpTok(*look, "@");
  const bool is_record =
      look && look->kind == lexer::TokenKind::Punctuator && look->lexeme == "{";
  // If lookahead indicates a more complex expression, return nullopt
  // unless allow_brace prevents those lookaheads from blocking
  if (look && (is_qual || (allow_brace && is_modal) ||
               (allow_brace && is_record))) {
    return std::nullopt;
  }

  SPEC_RULE("Parse-Identifier-Expr");
  if (IsCompileTimeCapabilityName(tok->lexeme)) {
    SPEC_RULE("Parse-CtCapRef");
  }

  IdentifierExpr ident;
  ident.name = tok->lexeme;
  return ParseElemResult<ExprPtr>{next, MakeExpr(tok->span, ident)};
}

// =============================================================================
// ParseIdentifierExpr - Parse identifier expression (must succeed)
// =============================================================================
//
// Unconditionally parses the current token as an identifier.
// Use TryParseIdentifierExpr for optional/lookahead parsing.

ParseElemResult<ExprPtr> ParseIdentifierExpr(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsIdentTok(*tok)) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    Parser sync = parser;
    SyncStmt(sync);
    return {sync, MakeExpr(TokSpan(parser), ErrorExpr{})};
  }

  SPEC_RULE("Parse-Identifier-Expr");
  if (IsCompileTimeCapabilityName(tok->lexeme)) {
    SPEC_RULE("Parse-CtCapRef");
  }

  Parser next = parser;
  Advance(next);

  IdentifierExpr ident;
  ident.name = tok->lexeme;
  return {next, MakeExpr(tok->span, ident)};
}

}  // namespace ultraviolet::ast
