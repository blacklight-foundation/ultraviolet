// ===========================================================================
// builder_common.cpp - Common Span Construction and Builder Infrastructure
// ===========================================================================
//
// PURPOSE:
//   Common span construction utilities and shared builder infrastructure.
//   Provides foundational functions used by all category-specific builders.
//
// ===========================================================================
// SPEC REFERENCE: CursiveSpecification.md Section 3.3.2 (Lines 2626-2638)
// ===========================================================================
//
//   SpanDefault(P, P') = SpanBetween(P, P')                              (2626)
//   DocDefault = []                                                       (2627)
//   DocOptDefault = bottom                                                (2628)
//   FillSpan(n, P, P') = n[span := SpanDefault(P, P')] if SpanMissing(n) (2629-2631)
//   FillDoc(n) = n[doc := DocDefault] if DocMissing(n)                   (2632-2634)
//   FillDocOpt(n) = n[doc_opt := DocOptDefault] if DocOptMissing(n)      (2635-2637)
//   ParseCtor(n, P, P') = FillDocOpt(FillDoc(FillSpan(n, P, P')))        (2638)
//
// ===========================================================================
// SOURCE: cursive-bootstrap/src/02_syntax/parser.cpp Lines 30-85
// ===========================================================================

#include "02_source/ast/ast_builder.h"

#include <cassert>
#include <optional>

#include "00_core/span.h"
#include "02_source/lexer/token.h"

namespace cursive::ast {

// ===========================================================================
// SPAN CONSTRUCTION
// ===========================================================================

// Creates a span from a start token to an end token.
// Takes start position from start token, end position from end token.
//
// SOURCE: parser.cpp lines 30-36
// core::Span SpanFrom(const Token& start, const Token& end) {
//   core::Span span = start.span;
//   span.end_offset = end.span.end_offset;
//   span.end_line = end.span.end_line;
//   span.end_col = end.span.end_col;
//   return span;
// }
core::Span span_from(const lexer::Token& start, const lexer::Token& end) {
  core::Span span = start.span;
  span.end_offset = end.span.end_offset;
  span.end_line = end.span.end_line;
  span.end_col = end.span.end_col;
  return span;
}

// Creates a span between two parser positions.
// Handles EOF token specially (uses last valid position).
//
// SOURCE: parser.cpp lines 76-85
// core::Span SpanBetween(const Parser& start, const Parser& end) {
//   Token start_tok = Tok(start) ? *Tok(start) : EofAsToken(start);
//   const std::vector<Token>* tokens =
//       end.tokens ? end.tokens : start.tokens;
//   Token end_tok = start_tok;
//   if (tokens && end.index > start.index && end.index - 1 < tokens->size()) {
//     end_tok = (*tokens)[end.index - 1];
//   }
//   return SpanFrom(start_tok, end_tok);
// }
core::Span span_between(const Parser& start, const Parser& end) {
  lexer::Token start_tok = *Tok(start);
  const std::vector<lexer::Token>* tokens =
      end.tokens ? end.tokens : start.tokens;
  lexer::Token end_tok = start_tok;
  if (tokens && end.index > start.index && end.index - 1 < tokens->size()) {
    end_tok = (*tokens)[end.index - 1];
  }
  return span_from(start_tok, end_tok);
}

// ===========================================================================
// DEFAULT INITIALIZATION
// ===========================================================================

// Returns the default (empty) documentation list.
// Per spec: DocDefault = []
DocList doc_default() {
  return DocList{};
}

// Returns the default (absent) optional documentation.
// Per spec: DocOptDefault = bottom (none)
std::optional<DocList> doc_opt_default() {
  return std::nullopt;
}

// ===========================================================================
// SPAN VALIDATION (DEBUG)
// ===========================================================================

// Validates that a span has correct ordering (start <= end).
// Only performs assertion in debug builds.
bool validate_span_ordering(const core::Span& span) {
  bool valid = span.start_offset <= span.end_offset;
  assert(valid && "span start must not exceed end");
  return valid;
}

// Validates that child span is contained within parent span.
// Only performs assertion in debug builds.
bool validate_span_containment(const core::Span& parent,
                                const core::Span& child) {
  bool valid = parent.start_offset <= child.start_offset &&
               child.end_offset <= parent.end_offset;
  assert(valid && "child span must be contained within parent");
  return valid;
}

// ===========================================================================
// BUILDER HELPERS
// ===========================================================================
// These are convenience wrappers used by individual expression/statement
// builders to construct nodes with proper spans.

// Extend a span to include an additional token's end position.
// Useful when parsing multi-token constructs incrementally.
core::Span extend_span(const core::Span& base, const lexer::Token& end) {
  core::Span result = base;
  result.end_offset = end.span.end_offset;
  result.end_line = end.span.end_line;
  result.end_col = end.span.end_col;
  return result;
}

// Extend a span to include another span's end position.
core::Span extend_span(const core::Span& base, const core::Span& end) {
  core::Span result = base;
  result.end_offset = end.end_offset;
  result.end_line = end.end_line;
  result.end_col = end.end_col;
  return result;
}

// Create a span covering multiple child spans.
// Takes the start of the first span and end of the last span.
core::Span span_covering(const core::Span& first, const core::Span& last) {
  core::Span result = first;
  result.end_offset = last.end_offset;
  result.end_line = last.end_line;
  result.end_col = last.end_col;
  return result;
}

// Create a zero-length span at a specific position.
// Useful for synthesized nodes with no source text.
core::Span point_span(const core::SourceFile& source,
                      std::size_t offset,
                      std::size_t line,
                      std::size_t col) {
  core::Span span;
  span.file = source.path;
  span.start_offset = offset;
  span.end_offset = offset;
  span.start_line = line;
  span.end_line = line;
  span.start_col = col;
  span.end_col = col;
  return span;
}

}  // namespace cursive::ast
