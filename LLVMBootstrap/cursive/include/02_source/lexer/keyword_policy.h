#pragma once

#include <string_view>

#include "02_source/lexer/token.h"

namespace cursive::lexer {

bool IsIdentTok(const Token& tok);
bool IsKwTok(const Token& tok, std::string_view s);
bool IsOpTok(const Token& tok, std::string_view s);
bool IsPuncTok(const Token& tok, std::string_view s);
std::string_view LexemeText(const Token& tok);

// Implements spec Keyword predicate from line 2094:
// Keyword(s) = s in Reserved
bool IsKeyword(std::string_view s);

// Fixed identifiers - identifiers with special meaning in context
bool IsFixedIdentifier(std::string_view s);
bool IsFixedIdentTok(const Token& tok, std::string_view s);

// Contextual keywords: "in", "key", "wait"
// NOT reserved; identifier in most contexts
bool IsCtxKeyword(std::string_view s);
bool Ctx(const Token& tok, std::string_view s);

// Check for "?" operator (union propagation)
bool UnionPropTok(const Token& tok);

// Check for "where" as identifier (type context)
bool TypeWhereTok(const Token& tok);

// Check for "opaque" as identifier (type context)
bool OpaqueTypeTok(const Token& tok);

}  // namespace cursive::lexer
