// =============================================================================
// MIGRATION MAPPING: typed_pattern.cpp
// =============================================================================
// This file should contain parsing logic for typed patterns, which bind a
// matched value to a name with an explicit type annotation.
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.9, Lines 6097-6100
// =============================================================================
//
// FORMAL RULE:
// -----------------------------------------------------------------------------
// **(Parse-Pattern-Typed)** Lines 6097-6100
// IsIdent(Tok(P))    IsPunc(Tok(Advance(P)), ":")
// Gamma |- ParseType(Advance(Advance(P))) => (P_1, ty)
// --------------------------------------------------------------------------
// Gamma |- ParsePatternAtom(P) => (P_1, TypedPattern(Lexeme(Tok(P)), ty))
//
// SEMANTICS:
// - Parses patterns of the form `name: Type`
// - Creates a binding with explicit type annotation
// - Used in case clauses for type-based dispatch:
//   - if u is { x: i32 { x } b: bool { if b { 1 } else { 0 } } }
// - The `_` identifier is also valid: `_: Type` matches but doesn't bind
//
// EXAMPLES:
// - `x: i32` - bind to x with type i32
// - `_: bool` - match bool without binding
// - `value: Point` - bind with record type
// - `n: i32 | bool` - bind with union type
//
// =============================================================================
// SOURCE FILE: cursive-bootstrap/src/02_syntax/parser_patterns.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. TypedPattern branch in ParsePatternAtom (Lines 311-326)
//    -------------------------------------------------------------------------
//    // Check typed pattern BEFORE wildcard - lookahead for ":" takes precedence
//    // This allows `_: Type` to parse as TypedPattern rather than WildcardPattern
//    if (IsIdentTok(*tok)) {
//      Parser next = parser;
//      Advance(next);
//      if (IsPunc(next, ":")) {
//        SPEC_RULE("Parse-Pattern-Typed");
//        Parser after = next;
//        Advance(after);
//        ParseElemResult<std::shared_ptr<Type>> ty = ParseType(after);
//        TypedPattern pat;
//        pat.name = tok->lexeme;
//        pat.type = ty.elem;
//        return {ty.parser, MakePattern(SpanBetween(parser, ty.parser), pat)};
//      }
//    }
//
// CRITICAL ORDERING:
// -----------------------------------------------------------------------------
// This check MUST come before:
// - WildcardPattern check (to handle `_: Type`)
// - IdentifierPattern check (to handle `name: Type`)
//
// The lookahead for `:` after the identifier distinguishes TypedPattern from
// plain IdentifierPattern or WildcardPattern.
//
// DEPENDENCIES:
// =============================================================================
// - TypedPattern AST node type:
//   - name: string (the identifier lexeme)
//   - type: shared_ptr<Type> (the type annotation)
// - ParseType function (from type parsing module)
// - MakePattern, SpanBetween helpers (pattern_common.cpp, parser utilities)
// - IsIdentTok, IsPunc, Tok, Advance parser utilities
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Requires one-token lookahead to distinguish from IdentifierPattern
// - The type is parsed using the full type parser (supports complex types)
// - Span covers from identifier to end of type
// - Both `_` and named identifiers are valid before the `:`
// - This is the primary pattern type used in union discrimination
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <string>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Import token inspection functions from lexer
using lexer::IsIdentTok;

// Forward declarations from other modules
bool IsPunc(const Parser& parser, std::string_view p);
PatternPtr MakePattern(const core::Span& span, PatternNode node);
ParseElemResult<std::shared_ptr<Type>> ParseType(Parser parser);

// =============================================================================
// ParseTypedPattern - Parse typed pattern (identifier: Type)
// =============================================================================
//
// SPEC: Lines 6097-6100 (Parse-Pattern-Typed)
// Parses patterns of the form `name: Type`

ParseElemResult<PatternPtr> ParseTypedPattern(Parser parser) {
  SPEC_RULE("Parse-Pattern-Typed");
  Parser start = parser;
  const lexer::Token* tok = Tok(parser);
  Parser after_name = parser;
  Advance(after_name);  // consume identifier
  Parser after_colon = after_name;
  Advance(after_colon);  // consume ":"

  ParseElemResult<std::shared_ptr<Type>> ty = ParseType(after_colon);
  TypedPattern pat;
  pat.name = std::string(tok->lexeme);
  pat.type = ty.elem;
  return {ty.parser, MakePattern(SpanBetween(start, ty.parser), pat)};
}

// =============================================================================
// TryParseTypedPattern - Try to parse typed pattern
// =============================================================================
//
// Returns std::nullopt if token is not identifier followed by `:`.

std::optional<ParseElemResult<PatternPtr>> TryParseTypedPattern(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsIdentTok(*tok)) {
    return std::nullopt;
  }
  Parser next = parser;
  Advance(next);
  if (!IsPunc(next, ":")) {
    return std::nullopt;
  }
  return ParseTypedPattern(parser);
}

}  // namespace cursive::ast
