// =============================================================================
// bytes_type.cpp - Bytes Type and State Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.7, Lines 4793-4796
// Section 3.3.7.1 (Bytes State), Lines 4898-4913
//
// Parses bytes types: bytes@Managed, bytes@View
// - bytes@Managed: heap-allocated, owned byte sequence
// - bytes@View: borrowed view into byte data
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"

namespace ultraviolet::ast {

// =============================================================================
// ParseBytesState - Parse Optional Bytes State Annotation
// =============================================================================
// SPEC: Lines 4900-4913

ParseElemResult<std::optional<BytesState>> ParseBytesState(Parser parser) {
  // Parse-BytesState-None: No @ annotation
  if (!IsOpType(parser, "@")) {
    SPEC_RULE("Parse-BytesState-None");
    return {parser, std::nullopt};
  }

  Parser next = parser;
  Advance(next);  // consume @
  const Token* tok = Tok(next);

  // Parse-BytesState-Managed
  if (tok && IsIdentTok(*tok) && tok->lexeme == "Managed") {
    SPEC_RULE("Parse-BytesState-Managed");
    Advance(next);
    return {next, BytesState::Managed};
  }

  // Parse-BytesState-View
  if (tok && IsIdentTok(*tok) && tok->lexeme == "View") {
    SPEC_RULE("Parse-BytesState-View");
    Advance(next);
    return {next, BytesState::View};
  }

  // Error: Invalid bytes state
  EmitParseSyntaxErr(next, TokSpan(next));
  return {next, std::nullopt};
}

}  // namespace ultraviolet::ast
