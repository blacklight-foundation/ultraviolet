// =============================================================================
// MIGRATION MAPPING: record_pattern.cpp
// =============================================================================
// This file should contain parsing logic for record patterns, which destructure
// record values by matching named fields.
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.9, Lines 6112-6115
// HELPER RULES: Section 3.3.9.1, Lines 6178-6218
// =============================================================================
//
// FORMAL RULES:
// -----------------------------------------------------------------------------
// **(Parse-Pattern-Record)** Lines 6112-6115
// Gamma |- ParseTypePath(P) => (P_1, path)
// IsPunc(Tok(P_1), "{")
// Gamma |- ParseFieldPatternList(Advance(P_1)) => (P_2, fields)
// IsPunc(Tok(P_2), "}")
// --------------------------------------------------------------------------
// Gamma |- ParsePatternAtom(P) => (Advance(P_2), RecordPattern(path, fields))
//
// FIELD PATTERN LIST RULES (Lines 6180-6218):
// -----------------------------------------------------------------------------
// **(Parse-FieldPatternList-Empty)** Lines 6180-6183
// IsPunc(Tok(P), "}")
// --------------------------------------------------------------------------
// Gamma |- ParseFieldPatternList(P) => (P, [])
//
// **(Parse-FieldPatternList-Cons)** Lines 6185-6188
// Gamma |- ParseFieldPattern(P) => (P_1, f)
// Gamma |- ParseFieldPatternTail(P_1, [f]) => (P_2, io)
// --------------------------------------------------------------------------
// Gamma |- ParseFieldPatternList(P) => (P_2, io)
//
// **(Parse-FieldPattern)** Lines 6190-6193
// Gamma |- ParseIdent(P) => (P_1, name)
// Gamma |- ParseFieldPatternTailOpt(P_1, name) => (P_2, pat_opt)
// --------------------------------------------------------------------------
// Gamma |- ParseFieldPattern(P) => (P_2, <name, pat_opt, SpanBetween(P, P_2)>)
//
// **(Parse-FieldPatternTailOpt-None)** Lines 6195-6198
// NOT IsPunc(Tok(P), ":")
// --------------------------------------------------------------------------
// Gamma |- ParseFieldPatternTailOpt(P, name) => (P, null)
//
// **(Parse-FieldPatternTailOpt-Yes)** Lines 6200-6203
// IsPunc(Tok(P), ":")    Gamma |- ParsePattern(Advance(P)) => (P_1, pat)
// --------------------------------------------------------------------------
// Gamma |- ParseFieldPatternTailOpt(P, name) => (P_1, pat)
//
// **(Parse-FieldPatternTail-End)** Lines 6205-6208
// IsPunc(Tok(P), "}")
// --------------------------------------------------------------------------
// Gamma |- ParseFieldPatternTail(P, xs) => (P, xs)
//
// **(Parse-FieldPatternTail-TrailingComma)** Lines 6210-6213
// IsPunc(Tok(P), ",")    IsPunc(Tok(Advance(P)), "}")
// TrailingCommaAllowed(P_0, P, {Punctuator("}")})
// --------------------------------------------------------------------------
// Gamma |- ParseFieldPatternTail(P, xs) => (Advance(P), xs)
//
// **(Parse-FieldPatternTail-Comma)** Lines 6215-6218
// IsPunc(Tok(P), ",")
// Gamma |- ParseFieldPattern(Advance(P)) => (P_1, f)
// Gamma |- ParseFieldPatternTail(P_1, xs ++ [f]) => (P_2, ys)
// --------------------------------------------------------------------------
// Gamma |- ParseFieldPatternTail(P, xs) => (P_2, ys)
//
// SEMANTICS:
// - `Point{ x, y }` - destructure with field punning (name = pattern)
// - `Point{ x: a, y: b }` - destructure with explicit pattern bindings
// - `Point{ x: 0, y }` - mixed literal and punning
// - The path identifies the record type being matched
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_patterns.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. FieldPatternTailOptResult struct (not shown, implied)
//    -------------------------------------------------------------------------
//    struct FieldPatternTailOptResult {
//      Parser parser;
//      PatternPtr pattern_opt;
//    };
//
// 2. ParseFieldPatternTailOpt function (Lines 201-211)
//    -------------------------------------------------------------------------
//    FieldPatternTailOptResult ParseFieldPatternTailOpt(Parser parser) {
//      if (!IsPunc(parser, ":")) {
//        SPEC_RULE("Parse-FieldPatternTailOpt-None");
//        return {parser, nullptr};
//      }
//      SPEC_RULE("Parse-FieldPatternTailOpt-Yes");
//      Parser after = parser;
//      Advance(after);
//      ParseElemResult<PatternPtr> pat = ParsePattern(after);
//      return {pat.parser, pat.elem};
//    }
//
// 3. ParseFieldPattern function (Lines 213-223)
//    -------------------------------------------------------------------------
//    ParseElemResult<FieldPattern> ParseFieldPattern(Parser parser) {
//      SPEC_RULE("Parse-FieldPattern");
//      Parser start = parser;
//      ParseElemResult<Identifier> name = ParseIdent(parser);
//      FieldPatternTailOptResult tail = ParseFieldPatternTailOpt(name.parser);
//      FieldPattern field;
//      field.name = name.elem;
//      field.pattern_opt = tail.pattern_opt;
//      field.span = SpanBetween(start, tail.parser);
//      return {tail.parser, field};
//    }
//
// 4. ParseFieldPatternTail function (Lines 173-199)
//    -------------------------------------------------------------------------
//    ParseElemResult<std::vector<FieldPattern>> ParseFieldPatternTail(
//        Parser parser, std::vector<FieldPattern> xs) {
//      SkipNewlines(parser);
//      if (IsPunc(parser, "}")) {
//        SPEC_RULE("Parse-FieldPatternTail-End");
//        return {parser, xs};
//      }
//      if (IsPunc(parser, ",")) {
//        const TokenKindMatch end_set[] = {MatchPunct("}")};
//        Parser after = parser;
//        Advance(after);
//        SkipNewlines(after);
//        if (IsPunc(after, "}")) {
//          SPEC_RULE("Parse-FieldPatternTail-TrailingComma");
//          EmitTrailingCommaErr(parser, end_set);
//          after.diags = parser.diags;
//          return {after, xs};
//        }
//        SPEC_RULE("Parse-FieldPatternTail-Comma");
//        ParseElemResult<FieldPattern> field = ParseFieldPattern(after);
//        xs.push_back(field.elem);
//        return ParseFieldPatternTail(field.parser, std::move(xs));
//      }
//      EmitParseSyntaxErr(parser, TokSpan(parser));
//      return {parser, xs};
//    }
//
// 5. ParseFieldPatternList function (Lines 225-236)
//    -------------------------------------------------------------------------
//    ParseElemResult<std::vector<FieldPattern>> ParseFieldPatternList(Parser parser) {
//      SkipNewlines(parser);
//      if (IsPunc(parser, "}")) {
//        SPEC_RULE("Parse-FieldPatternList-Empty");
//        return {parser, {}};
//      }
//      SPEC_RULE("Parse-FieldPatternList-Cons");
//      ParseElemResult<FieldPattern> first = ParseFieldPattern(parser);
//      std::vector<FieldPattern> fields;
//      fields.push_back(first.elem);
//      return ParseFieldPatternTail(first.parser, std::move(fields));
//    }
//
// 6. RecordPattern branch in ParsePatternAtom (Lines 368-388)
//    -------------------------------------------------------------------------
//    if (IsIdentTok(*tok)) {
//      Parser start = parser;
//      ParseElemResult<TypePath> path = ParseTypePath(parser);
//      if (IsPunc(path.parser, "{")) {
//        SPEC_RULE("Parse-Pattern-Record");
//        Parser after = path.parser;
//        Advance(after);
//        ParseElemResult<std::vector<FieldPattern>> fields = ParseFieldPatternList(after);
//        if (!IsPunc(fields.parser, "}")) {
//          EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
//          return {fields.parser,
//                  MakePattern(SpanBetween(start, fields.parser), WildcardPattern{})};
//        }
//        Parser done = fields.parser;
//        Advance(done);
//        RecordPattern pat;
//        pat.path = path.elem;
//        pat.fields = std::move(fields.elem);
//        return {done, MakePattern(SpanBetween(start, done), pat)};
//      }
//    }
//
// DEPENDENCIES:
// =============================================================================
// - RecordPattern AST node type:
//   - path: TypePath
//   - fields: vector<FieldPattern>
// - FieldPattern AST node type:
//   - name: Identifier
//   - pattern_opt: PatternPtr (nullable, for field punning)
//   - span: Span
// - ParseTypePath function (type parsing module)
// - ParseIdent, ParsePattern functions
// - MakePattern, SpanBetween, SkipNewlines helpers
// - EmitTrailingCommaErr, EmitParseSyntaxErr error handlers
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Field punning: `{ x }` is shorthand for `{ x: x }`
// - The path distinguishes record patterns from tuple patterns
// - ParseFieldPatternList/Tail are also used by enum/modal record payloads
// - Consider placing field pattern helpers in pattern_common.cpp for reuse
// - Trailing comma validation follows standard rules
// - SkipNewlines allows multi-line record patterns
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>
#include <vector>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Import token inspection functions from lexer
using lexer::IsIdentTok;

// Forward declarations from other modules
bool IsPunc(const Parser& parser, std::string_view p);
PatternPtr MakePattern(const core::Span& span, PatternNode node);
ParseElemResult<TypePath> ParseTypePath(Parser parser);
ParseElemResult<std::vector<FieldPattern>> ParseFieldPatternList(Parser parser);

// =============================================================================
// ParseRecordPatternFromPath - Parse record pattern with pre-parsed path
// =============================================================================
//
// SPEC: Lines 6112-6115 (Parse-Pattern-Record)
// Parses patterns like `Point{ x, y }`, `Point{ x: a, y: b }`
// Called when path has already been parsed and `{` detected.

ParseElemResult<PatternPtr> ParseRecordPatternFromPath(Parser start,
                                                        const TypePath& path,
                                                        Parser at_brace) {
  SPEC_RULE("Parse-Pattern-Record");
  Parser after = at_brace;
  Advance(after);  // consume "{"

  ParseElemResult<std::vector<FieldPattern>> fields = ParseFieldPatternList(after);
  if (!IsPunc(fields.parser, "}")) {
    EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
    return {fields.parser,
            MakePattern(SpanBetween(start, fields.parser), WildcardPattern{})};
  }
  Parser done = fields.parser;
  Advance(done);  // consume "}"

  RecordPattern pat;
  pat.path = path;
  pat.fields = std::move(fields.elem);
  return {done, MakePattern(SpanBetween(start, done), pat)};
}

// =============================================================================
// TryParseRecordPattern - Try to parse record pattern
// =============================================================================
//
// Returns std::nullopt if not identifier followed (eventually) by `{`.
// Note: Full disambiguation is complex due to path parsing.

std::optional<ParseElemResult<PatternPtr>> TryParseRecordPattern(Parser parser) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsIdentTok(*tok)) {
    return std::nullopt;
  }

  Parser start = parser;
  ParseElemResult<TypePath> path = ParseTypePath(parser);
  if (!IsPunc(path.parser, "{")) {
    return std::nullopt;
  }
  return ParseRecordPatternFromPath(start, path.elem, path.parser);
}

}  // namespace ultraviolet::ast
