// =============================================================================
// MIGRATION MAPPING: modal_pattern.cpp
// =============================================================================
// This file should contain parsing logic for modal patterns, which match
// modal type states and optionally destructure state-specific fields.
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.9, Lines 6122-6125
// HELPER RULES: Section 3.3.9.1, Lines 6236-6246
// =============================================================================
//
// FORMAL RULES:
// -----------------------------------------------------------------------------
// **(Parse-Pattern-Modal)** Lines 6122-6125
// IsOp(Tok(P), "@")
// Gamma |- ParseIdent(Advance(P)) => (P_1, name)
// Gamma |- ParseModalPatternPayloadOpt(P_1) => (P_2, fields_opt)
// --------------------------------------------------------------------------
// Gamma |- ParsePatternAtom(P) => (P_2, ModalPattern(name, fields_opt))
//
// MODAL PAYLOAD PARSING RULES (Lines 6238-6246):
// -----------------------------------------------------------------------------
// **(Parse-ModalPatternPayloadOpt-None)** Lines 6238-6241
// NOT IsPunc(Tok(P), "{")
// --------------------------------------------------------------------------
// Gamma |- ParseModalPatternPayloadOpt(P) => (P, null)
//
// **(Parse-ModalPatternPayloadOpt-Record)** Lines 6243-6246
// IsPunc(Tok(P), "{")
// Gamma |- ParseFieldPatternList(Advance(P)) => (P_1, fs)
// IsPunc(Tok(P_1), "}")
// --------------------------------------------------------------------------
// Gamma |- ParseModalPatternPayloadOpt(P) => (Advance(P_1), ModalRecordPayload(fs))
//
// SEMANTICS:
// - `@Connected` - match modal state without field destructuring
// - `@Connected{ socket }` - match state and destructure fields
// - `@Disconnected{ host: h }` - match state with explicit bindings
// - The `@` prefix distinguishes modal patterns from other patterns
// - Modal patterns only have record-style payloads (no tuple payloads)
//
// EXAMPLES:
// - `@Active` - match Active state
// - `@Ready{ value }` - destructure Ready state's value field
// - `@Pending` - match Pending state (Spawned<T>)
// - `@Suspended{ output }` - match Async suspended state
//
// =============================================================================
// SOURCE FILE: cursive-bootstrap/src/02_syntax/parser_patterns.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. ModalPayloadOptResult struct (Lines 71-74)
//    -------------------------------------------------------------------------
//    struct ModalPayloadOptResult {
//      Parser parser;
//      std::optional<ModalRecordPayload> fields_opt;
//    };
//
// 2. ParseModalPatternPayloadOpt function (Lines 275-293)
//    -------------------------------------------------------------------------
//    ModalPayloadOptResult ParseModalPatternPayloadOpt(Parser parser) {
//      if (!IsPunc(parser, "{")) {
//        SPEC_RULE("Parse-ModalPatternPayloadOpt-None");
//        return {parser, std::nullopt};
//      }
//      SPEC_RULE("Parse-ModalPatternPayloadOpt-Record");
//      Parser next = parser;
//      Advance(next);
//      ParseElemResult<std::vector<FieldPattern>> fields = ParseFieldPatternList(next);
//      if (!IsPunc(fields.parser, "}")) {
//        EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
//        return {fields.parser, std::nullopt};
//      }
//      Parser after = fields.parser;
//      Advance(after);
//      ModalRecordPayload payload;
//      payload.fields = std::move(fields.elem);
//      return {after, payload};
//    }
//
// 3. ModalPattern branch in ParsePatternAtom (Lines 390-400)
//    -------------------------------------------------------------------------
//    if (IsOp(parser, "@")) {
//      SPEC_RULE("Parse-Pattern-Modal");
//      Parser next = parser;
//      Advance(next);
//      ParseElemResult<Identifier> name = ParseIdent(next);
//      ModalPayloadOptResult payload = ParseModalPatternPayloadOpt(name.parser);
//      ModalPattern pat;
//      pat.state = name.elem;
//      pat.fields_opt = payload.fields_opt;
//      return {payload.parser, MakePattern(SpanBetween(parser, payload.parser), pat)};
//    }
//
// DEPENDENCIES:
// =============================================================================
// - ModalPattern AST node type:
//   - state: Identifier (the state name, e.g., "Connected")
//   - fields_opt: optional<ModalRecordPayload>
// - ModalRecordPayload: contains fields: vector<FieldPattern>
// - ParseIdent function
// - ParseFieldPatternList function (record_pattern.cpp)
// - MakePattern, SpanBetween helpers
// - IsOp parser utility
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Modal patterns are distinguished by the `@` prefix
// - Unlike enum patterns, modal patterns only support record payloads (no tuples)
// - The state name is just an identifier, not a qualified path
// - Used in if-case expressions over modal types (Connection, Region, CancelToken, etc.)
// - Payload is optional - states without fields don't need `{}`
// - Reuses ParseFieldPatternList from record pattern parsing
// - Span covers from `@` to end of payload (or state name if no payload)
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>
#include <vector>

#include "00_core/assert_spec.h"

namespace cursive::ast {

// Forward declarations from other modules
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view p);
PatternPtr MakePattern(const core::Span& span, PatternNode node);
ParseElemResult<Identifier> ParseIdent(Parser parser);
ParseElemResult<std::vector<FieldPattern>> ParseFieldPatternList(Parser parser);

// Result type for modal payload parsing (also defined in pattern_common.cpp)
struct ModalPayloadOptResult {
  Parser parser;
  std::optional<ModalRecordPayload> fields_opt;
};
ModalPayloadOptResult ParseModalPatternPayloadOpt(Parser parser);

// =============================================================================
// ParseModalPatternPayloadOpt - Parse optional modal pattern payload
// =============================================================================
//
// SPEC: Lines 6238-6246

// =============================================================================
// ParseModalPattern - Parse modal pattern (@State or @State{ fields })
// =============================================================================
//
// SPEC: Lines 6122-6125 (Parse-Pattern-Modal)
// Parses patterns like `@Connected`, `@Ready{ value }`

ParseElemResult<PatternPtr> ParseModalPattern(Parser parser) {
  SPEC_RULE("Parse-Pattern-Modal");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "@"

  ParseElemResult<Identifier> name = ParseIdent(next);
  ModalPayloadOptResult payload = ParseModalPatternPayloadOpt(name.parser);

  ModalPattern pat;
  pat.state = name.elem;
  pat.fields_opt = payload.fields_opt;
  return {payload.parser, MakePattern(SpanBetween(start, payload.parser), pat)};
}

// =============================================================================
// TryParseModalPattern - Try to parse modal pattern
// =============================================================================
//
// Returns std::nullopt if not at `@` token.

std::optional<ParseElemResult<PatternPtr>> TryParseModalPattern(Parser parser) {
  if (!IsOp(parser, "@")) {
    return std::nullopt;
  }
  return ParseModalPattern(parser);
}

}  // namespace cursive::ast
