// =============================================================================
// MIGRATION MAPPING: range_pattern.cpp
// =============================================================================
// This file should contain parsing logic for range patterns, which match
// values within a specified range (exclusive or inclusive).
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.9, Lines 6077-6085
// =============================================================================
//
// FORMAL RULES:
// -----------------------------------------------------------------------------
// **(Parse-Pattern-Range-None)** Lines 6077-6080
// Gamma |- ParsePatternAtom(P) => (P_1, p)
// NOT (IsOp(Tok(P_1), "..") OR IsOp(Tok(P_1), "..="))
// --------------------------------------------------------------------------
// Gamma |- ParsePatternRange(P) => (P_1, p)
//
// **(Parse-Pattern-Range)** Lines 6082-6085
// Gamma |- ParsePatternAtom(P) => (P_1, p_0)
// Tok(P_1) = op IN {"..", "..="}
// Gamma |- ParsePatternAtom(Advance(P_1)) => (P_2, p_1)
// kind = (Exclusive if op = ".." else Inclusive)
// --------------------------------------------------------------------------
// Gamma |- ParsePatternRange(P) => (P_2, RangePattern(kind, p_0, p_1))
//
// SEMANTICS:
// - `0..10` - exclusive range, matches 0 through 9
// - `0..=10` - inclusive range, matches 0 through 10
// - Range bounds are typically literal patterns but can be any atom
// - Used in case clauses for numeric dispatch:
//   - if score is { 0..60 { "F" } 60..70 { "D" } 90..=100 { "A" } else { "other" } }
//
// EXAMPLES:
// - `0..100` - exclusive range [0, 100)
// - `'a'..='z'` - inclusive character range
// - `-10..10` - signed range (if negative literals supported)
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_patterns.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. ParsePatternRange function (Lines 415-431)
//    -------------------------------------------------------------------------
//    ParseElemResult<PatternPtr> ParsePatternRange(Parser parser) {
//      ParseElemResult<PatternPtr> lhs = ParsePatternAtom(parser);
//      if (IsOp(lhs.parser, "..") || IsOp(lhs.parser, "..=")) {
//        SPEC_RULE("Parse-Pattern-Range");
//        Parser after = lhs.parser;
//        const bool inclusive = IsOp(after, "..=");
//        Advance(after);
//        ParseElemResult<PatternPtr> rhs = ParsePatternAtom(after);
//        RangePattern pat;
//        pat.kind = inclusive ? RangeKind::Inclusive : RangeKind::Exclusive;
//        pat.lo = lhs.elem;
//        pat.hi = rhs.elem;
//        return {rhs.parser, MakePattern(SpanBetween(parser, rhs.parser), pat)};
//      }
//      SPEC_RULE("Parse-Pattern-Range-None");
//      return lhs;
//    }
//
// 2. RangeKind enum (likely in AST definitions)
//    -------------------------------------------------------------------------
//    enum class RangeKind {
//      Exclusive,  // ..
//      Inclusive   // ..=
//    };
//
// ENTRY POINT:
// -----------------------------------------------------------------------------
// ParsePatternRange is called from ParsePattern (Lines 435-444):
//
//    ParseElemResult<PatternPtr> ParsePattern(Parser parser) {
//      const Token* tok = Tok(parser);
//      if (!tok || !IsPatternStart(*tok)) {
//        SPEC_RULE("Parse-Pattern-Err");
//        EmitParseSyntaxErr(parser, TokSpan(parser));
//        return {parser, MakePattern(TokSpan(parser), WildcardPattern{})};
//      }
//      SPEC_RULE("Parse-Pattern");
//      return ParsePatternRange(parser);
//    }
//
// DEPENDENCIES:
// =============================================================================
// - RangePattern AST node type:
//   - kind: RangeKind (Exclusive or Inclusive)
//   - lo: PatternPtr (lower bound pattern)
//   - hi: PatternPtr (upper bound pattern)
// - RangeKind enum
// - ParsePatternAtom function (dispatches to specific pattern types)
// - MakePattern, SpanBetween helpers
// - IsOp parser utility
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - ParsePatternRange sits between ParsePattern and ParsePatternAtom in hierarchy
// - This is the only pattern parsing level that handles binary-like constructs
// - Both bounds must be atom patterns (no nested ranges)
// - The range operators `..' and `..=` are the same as in range expressions
// - Span covers from lower bound start to upper bound end
// - If no range operator follows the first atom, just returns the atom unchanged
// - Range patterns are commonly used with literal patterns as bounds
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// Forward declarations from other modules
bool IsOp(const Parser& parser, std::string_view op);
PatternPtr MakePattern(const core::Span& span, PatternNode node);
ParseElemResult<PatternPtr> ParsePatternAtom(Parser parser);

// =============================================================================
// ParsePatternRange - Parse range pattern (atom..atom or atom..=atom)
// =============================================================================
//
// SPEC: Lines 6077-6085 (Parse-Pattern-Range, Parse-Pattern-Range-None)
// Wraps ParsePatternAtom and checks for range operators.
// This is implemented in pattern_common.cpp as the main entry point.
// This file provides a standalone version.

ParseElemResult<PatternPtr> ParsePatternRangeStandalone(Parser parser) {
  ParseElemResult<PatternPtr> lhs = ParsePatternAtom(parser);
  if (IsOp(lhs.parser, "..") || IsOp(lhs.parser, "..=")) {
    SPEC_RULE("Parse-Pattern-Range");
    Parser after = lhs.parser;
    const bool inclusive = IsOp(after, "..=");
    Advance(after);
    ParseElemResult<PatternPtr> rhs = ParsePatternAtom(after);
    RangePattern pat;
    pat.kind = inclusive ? RangeKind::Inclusive : RangeKind::Exclusive;
    pat.lo = lhs.elem;
    pat.hi = rhs.elem;
    return {rhs.parser, MakePattern(SpanBetween(parser, rhs.parser), pat)};
  }
  SPEC_RULE("Parse-Pattern-Range-None");
  return lhs;
}

// =============================================================================
// CheckForRangePattern - Check if pattern could be a range
// =============================================================================
//
// Returns the lhs pattern and whether a range operator follows.
// This is useful for lookahead without committing to range parsing.

struct RangeCheckResult {
  ParseElemResult<PatternPtr> lhs;
  bool has_range_operator;
};

RangeCheckResult CheckForRangePattern(Parser parser) {
  ParseElemResult<PatternPtr> lhs = ParsePatternAtom(parser);
  bool has_range = IsOp(lhs.parser, "..") || IsOp(lhs.parser, "..=");
  return {lhs, has_range};
}

}  // namespace ultraviolet::ast
