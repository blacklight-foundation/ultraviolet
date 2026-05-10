// =============================================================================
// MIGRATION MAPPING: wildcard_pattern.cpp
// =============================================================================
// This file should contain parsing logic for wildcard patterns (`_`), which
// match any value without binding it to a name.
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.9, Lines 6092-6095
// =============================================================================
//
// FORMAL RULE:
// -----------------------------------------------------------------------------
// **(Parse-Pattern-Wildcard)** Lines 6092-6095
// IsIdent(Tok(P))    Lexeme(Tok(P)) = "_"
// --------------------------------------------------------------------------
// Gamma |- ParsePatternAtom(P) => (Advance(P), WildcardPattern)
//
// SEMANTICS:
// - Matches the `_` identifier specifically
// - Does NOT match `_` followed by `:` (that would be a TypedPattern)
// - Matches any value without creating a binding
// - Commonly used as catch-all arm in if-case expressions:
//   - if x is { 0 { "zero" } else { "other" } }
//
// DISAMBIGUATION:
// - `_` alone -> WildcardPattern
// - `_: Type` -> TypedPattern (see typed_pattern.cpp)
// - The TypedPattern check MUST come first due to lookahead requirement
//
// =============================================================================
// SOURCE FILE: cursive-bootstrap/src/02_syntax/parser_patterns.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. Wildcard pattern branch in ParsePatternAtom (Lines 328-334)
//    -------------------------------------------------------------------------
//    // Wildcard pattern: `_` not followed by `:`
//    if (IsIdentTok(*tok) && tok->lexeme == "_") {
//      SPEC_RULE("Parse-Pattern-Wildcard");
//      Parser next = parser;
//      Advance(next);
//      return {next, MakePattern(tok->span, WildcardPattern{})};
//    }
//
//    IMPORTANT: This check appears AFTER the TypedPattern check (Lines 311-326)
//    because `_: Type` should parse as TypedPattern, not WildcardPattern.
//
// 2. TypedPattern lookahead that protects WildcardPattern (Lines 311-326)
//    -------------------------------------------------------------------------
//    // Check typed pattern BEFORE wildcard - lookahead for ":" takes precedence
//    // This allows `_: Type` to parse as TypedPattern rather than WildcardPattern
//    if (IsIdentTok(*tok)) {
//      Parser next = parser;
//      Advance(next);
//      if (IsPunc(next, ":")) {
//        SPEC_RULE("Parse-Pattern-Typed");
//        // ... TypedPattern parsing ...
//      }
//    }
//
//    NOTE: This ordering is critical for correct disambiguation.
//
// DEPENDENCIES:
// =============================================================================
// - WildcardPattern AST node type (likely empty struct)
// - MakePattern helper (pattern_common.cpp)
// - IsIdentTok, Tok, Advance parser utilities
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - WildcardPattern is a singleton pattern type (no fields)
// - The ordering in ParsePatternAtom is critical:
//   1. Check for TypedPattern first (identifier followed by `:`)
//   2. Then check for wildcard (`_` not followed by `:`)
// - Span is taken directly from the `_` token
// - Used both as explicit catch-all and in irrefutable positions
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Import token inspection functions from lexer
using lexer::IsIdentTok;

// Forward declarations from other modules
PatternPtr MakePattern(const core::Span& span, PatternNode node);
bool IsPunc(const Parser& parser, std::string_view p);

// =============================================================================
// ParseWildcardPattern - Parse wildcard pattern
// =============================================================================
//
// SPEC: Lines 6092-6095 (Parse-Pattern-Wildcard)
// Matches `_` identifier without binding.

ParseElemResult<PatternPtr> ParseWildcardPattern(Parser parser) {
  SPEC_RULE("Parse-Pattern-Wildcard");
  const lexer::Token* tok = Tok(parser);
  Parser next = parser;
  Advance(next);
  return {next, MakePattern(tok->span, WildcardPattern{})};
}

// =============================================================================
// TryParseWildcardPattern - Try to parse wildcard pattern
// =============================================================================
//
// Returns std::nullopt if not at `_` token, or if `_` is followed by `:`.
// The `:` lookahead is handled by the caller (TypedPattern has priority).

std::optional<ParseElemResult<PatternPtr>> TryParseWildcardPattern(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsIdentTok(*tok) || tok->lexeme != "_") {
    return std::nullopt;
  }
  // Check for TypedPattern (`:` lookahead) - let caller handle that case
  Parser next = parser;
  Advance(next);
  if (IsPunc(next, ":")) {
    return std::nullopt;  // TypedPattern takes precedence
  }
  return ParseWildcardPattern(parser);
}

}  // namespace cursive::ast
