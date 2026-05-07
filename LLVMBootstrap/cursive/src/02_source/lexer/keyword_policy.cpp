// =============================================================================
// MIGRATION MAPPING: keyword_policy.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 3.2.3 - Reserved Lexemes (lines 2086-2106)
//   Section 3.2.7 - Identifier and Keyword Lexing (ClassifyIdent at lines 2388-2392)
//
// SOURCE FILE: cursive-bootstrap/src/02_syntax/keyword_policy.cpp
//   Lines 1-101 (entire file)
//
// =============================================================================

#include "02_source/lexer/keyword_policy.h"

#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/keywords.h"

namespace cursive::lexer {

bool IsIdentTok(const Token& tok) {
  return tok.kind == TokenKind::Identifier;
}

bool IsKwTok(const Token& tok, std::string_view s) {
  return tok.kind == TokenKind::Keyword && tok.lexeme == s;
}

bool IsOpTok(const Token& tok, std::string_view s) {
  return tok.kind == TokenKind::Operator && tok.lexeme == s;
}

bool IsPuncTok(const Token& tok, std::string_view s) {
  return tok.kind == TokenKind::Punctuator && tok.lexeme == s;
}

std::string_view LexemeText(const Token& tok) {
  return tok.lexeme;
}

// Implements spec Keyword predicate from line 2094:
// Keyword(s) = s in Reserved
bool IsKeyword(std::string_view s) {
  return core::IsKeyword(s);
}

bool IsFixedIdentifier(std::string_view s) {
  return core::IsFixedIdentifier(s);
}

bool IsFixedIdentTok(const Token& tok, std::string_view s) {
  return IsIdentTok(tok) && tok.lexeme == s && IsFixedIdentifier(s);
}

// Contextual keywords: "in", "key", "wait"
// NOT reserved; identifier in most contexts
// These are valid identifiers except in specific syntactic positions
bool IsCtxKeyword(std::string_view s) {
  return s == "in" || s == "key" || s == "wait";
}

// Check token is contextual keyword
// Token must be Identifier AND lexeme in {in, key, wait}
bool Ctx(const Token& tok, std::string_view s) {
  return IsIdentTok(tok) && tok.lexeme == s && IsCtxKeyword(s);
}

// Check for "?" operator (union propagation)
bool UnionPropTok(const Token& tok) {
  return IsOpTok(tok, "?");
}

// Check for "where" keyword in type contexts.
bool TypeWhereTok(const Token& tok) {
  return tok.kind == TokenKind::Keyword && tok.lexeme == "where";
}

// Check for "opaque" as identifier (type context)
// Note: "opaque" is NOT reserved; used as type specifier contextually
bool OpaqueTypeTok(const Token& tok) {
  return IsIdentTok(tok) && tok.lexeme == "opaque";
}

}  // namespace cursive::lexer
