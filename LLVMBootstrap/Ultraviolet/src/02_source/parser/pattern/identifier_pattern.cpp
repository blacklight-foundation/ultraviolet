// =============================================================================
// MIGRATION MAPPING: identifier_pattern.cpp
// =============================================================================
// This file should contain parsing logic for identifier patterns, which bind
// a matched value to a name without requiring a type annotation.
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.9, Lines 6102-6105
// =============================================================================
//
// FORMAL RULE:
// -----------------------------------------------------------------------------
// **(Parse-Pattern-Identifier)** Lines 6102-6105
// IsIdent(Tok(P))
// --------------------------------------------------------------------------
// Gamma |- ParsePatternAtom(P) => (Advance(P), IdentifierPattern(Lexeme(Tok(P))))
//
// SEMANTICS:
// - Matches any value and binds it to the identifier
// - Used in let bindings and case clauses:
//   - let x = 42
//   - if opt is { Some(v) { v } None { 0 } }
// - The identifier becomes available in the arm's body/expression
//
// DISAMBIGUATION (priority order in ParsePatternAtom):
// 1. Literal patterns (if token is a literal)
// 2. TypedPattern (identifier followed by `:`)
// 3. WildcardPattern (identifier is `_`)
// 4. EnumPattern (identifier followed by `::`)
// 5. RecordPattern (path followed by `{`)
// 6. IdentifierPattern (fallback for plain identifiers)
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_patterns.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. Identifier pattern branch in ParsePatternAtom (Lines 402-409)
//    -------------------------------------------------------------------------
//    if (IsIdentTok(*tok)) {
//      SPEC_RULE("Parse-Pattern-Identifier");
//      Parser next = parser;
//      Advance(next);
//      IdentifierPattern pat;
//      pat.name = tok->lexeme;
//      return {next, MakePattern(tok->span, pat)};
//    }
//
//    NOTE: This is the last identifier-based pattern check, serving as a
//    fallback when no other pattern type matches.
//
// POSITION IN ParsePatternAtom (Lines 295-413):
// -----------------------------------------------------------------------------
// The identifier pattern check comes AFTER all other identifier-based checks:
// - Line 302: LiteralPattern check
// - Line 313: TypedPattern check (identifier + ":")
// - Line 329: WildcardPattern check (identifier = "_")
// - Line 336: EnumPattern check (identifier + "::")
// - Line 351: TuplePattern check
// - Line 368: RecordPattern check (path + "{")
// - Line 390: ModalPattern check ("@" + identifier)
// - Line 402: IdentifierPattern check (fallback)
//
// DEPENDENCIES:
// =============================================================================
// - IdentifierPattern AST node type (contains name: string field)
// - MakePattern helper (pattern_common.cpp)
// - IsIdentTok, Tok, Advance parser utilities
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - IdentifierPattern is a simple binding pattern
// - Must be checked LAST among identifier-based patterns
// - All the precedence/priority logic is handled by check ordering
// - Span is taken directly from the identifier token
// - The name field stores the lexeme (not the token itself)
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>
#include <string>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Import token inspection functions from lexer
using lexer::IsIdentTok;

// Forward declarations from other modules
PatternPtr MakePattern(const core::Span& span, PatternNode node);

// =============================================================================
// ParseIdentifierPattern - Parse identifier pattern
// =============================================================================
//
// SPEC: Lines 6102-6105 (Parse-Pattern-Identifier)
// Matches any value and binds it to the identifier.

ParseElemResult<PatternPtr> ParseIdentifierPattern(Parser parser) {
  SPEC_RULE("Parse-Pattern-Identifier");
  ParseElemResult<Identifier> name = ParseIdent(parser);
  IdentifierPattern pat;
  pat.name = std::move(name.elem);
  return {name.parser, MakePattern(SpanBetween(parser, name.parser), pat)};
}

// =============================================================================
// TryParseIdentifierPattern - Try to parse identifier pattern
// =============================================================================
//
// This is the LAST check in ParsePatternAtom after all other identifier-based
// patterns have been ruled out. It should only be called when:
// - Token is an identifier
// - Not followed by `:` (TypedPattern)
// - Not `_` (WildcardPattern)
// - Not followed by `::` (EnumPattern)
// - Not followed by `{` after path (RecordPattern)
//
// In practice, the full disambiguation is handled by ParsePatternAtom.
// This function provides a simple check for use in fallback cases.

std::optional<ParseElemResult<PatternPtr>> TryParseIdentifierPattern(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || (!IsIdentTok(*tok) && tok->kind != lexer::TokenKind::Keyword)) {
    return std::nullopt;
  }
  // Note: Full disambiguation (checking for :, ::, {, etc.) is done by
  // ParsePatternAtom. This function is for direct use when other cases
  // have already been ruled out.
  return ParseIdentifierPattern(parser);
}

}  // namespace ultraviolet::ast
