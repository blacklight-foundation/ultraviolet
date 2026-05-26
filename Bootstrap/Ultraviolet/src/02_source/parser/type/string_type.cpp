// =============================================================================
// string_type.cpp - String Type and State Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.7, Lines 4788-4791
// Section 3.3.7.1 (String State), Lines 4881-4896
//
// Parses string types: string@Managed, string@View
// - string@Managed: heap-allocated, owned UTF-8 string
// - string@View: borrowed view into string data
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"

namespace ultraviolet::ast {

// =============================================================================
// ParseStringState - Parse Optional String State Annotation
// =============================================================================
// SPEC: Lines 4883-4896

ParseElemResult<std::optional<StringState>> ParseStringState(Parser parser) {
  // Parse-StringState-None: No @ annotation
  if (!IsOpType(parser, "@")) {
    SPEC_RULE("Parse-StringState-None");
    return {parser, std::nullopt};
  }

  Parser next = parser;
  Advance(next);  // consume @
  const Token* tok = Tok(next);

  // Parse-StringState-Managed
  if (tok && IsIdentTok(*tok) && tok->lexeme == "Managed") {
    SPEC_RULE("Parse-StringState-Managed");
    Advance(next);
    return {next, StringState::Managed};
  }

  // Parse-StringState-View
  if (tok && IsIdentTok(*tok) && tok->lexeme == "View") {
    SPEC_RULE("Parse-StringState-View");
    Advance(next);
    return {next, StringState::View};
  }

  // Error: Invalid string state
  EmitParseSyntaxErr(next, TokSpan(next));
  return {next, std::nullopt};
}

}  // namespace ultraviolet::ast
