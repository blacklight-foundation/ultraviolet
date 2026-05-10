// =============================================================================
// parser_state.cpp - Core Parser State Primitives
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.3.3 (Lines 2966-3018)
//
// This file implements the fundamental parser state operations:
//   - MakeParser: Initialize parser state (PState = <K, 0, D, 0, 0, []>)
//   - AtEof: Check if at end of token stream
//   - Tok: Get current token (K[i] if i < |K|, else EOF)
//   - TokSpan: Get span of current token
//   - Advance: Move to next token (index += 1)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include "02_source/lexer/token.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::DocComment;
using cursive::lexer::MakeEofToken;
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

namespace {

core::Span PointSpanAtEnd(const Token& token) {
  core::Span span = token.span;
  span.start_offset = token.span.end_offset;
  span.start_line = token.span.end_line;
  span.start_col = token.span.end_col;
  return span;
}

Token MakeParserEofToken(const Parser& parser) {
  Token eof;
  eof.kind = TokenKind::Eof;
  eof.lexeme.clear();

  if (parser.source) {
    return MakeEofToken(*parser.source);
  }

  if (parser.tokens && !parser.tokens->empty()) {
    eof.span = PointSpanAtEnd(parser.tokens->back());
    return eof;
  }

  eof.span = {};
  return eof;
}

Token& ParserEofTokenCache() {
  thread_local Token eof;
  return eof;
}

}  // namespace

// =============================================================================
// MakeParser - Initialize parser state
// =============================================================================
//
// SPEC: Section 3.3.3 - Initializes PState = <K, 0, D, 0, 0, []>
//   - K: Token stream
//   - i: Token index (0)
//   - D: Doc comment stream
//   - j: Doc index (0)
//   - d: Depth counter (0)
//   - Delta: Diagnostic stream (empty)

Parser MakeParser(const std::vector<Token>& tokens,
                  const std::vector<DocComment>& docs,
                  const core::SourceFile& source) {
  Parser parser;
  parser.tokens = &tokens;
  parser.source = &source;
  parser.index = 0;
  parser.docs = &docs;
  parser.doc_index = 0;
  parser.depth = 0;
  return parser;
}

Parser MakeParser(const std::vector<Token>& tokens,
                  const core::SourceFile& source) {
  static const std::vector<DocComment> kEmptyDocs;
  return MakeParser(tokens, kEmptyDocs, source);
}

// =============================================================================
// AtEof - End of file check
// =============================================================================
//
// SPEC: Section 3.3.3 line 2983 - EOF condition check
// Returns true if tokens is null OR index >= tokens->size()

bool AtEof(const Parser& parser) {
  if (!parser.tokens) {
    return true;
  }
  return parser.index >= parser.tokens->size();
}

// =============================================================================
// Tok - Get current token
// =============================================================================
//
// SPEC: Section 3.3.3 lines 2981-2983
// Returns K[i] if i < |K|, else EOF token
// Returns a pointer to K[i], or to the explicit EOF token when i = |K|

const Token* Tok(const Parser& parser) {
  if (!parser.tokens || parser.index >= parser.tokens->size()) {
    Token& eof = ParserEofTokenCache();
    eof = MakeParserEofToken(parser);
    return &eof;
  }
  return &(*parser.tokens)[parser.index];
}

// =============================================================================
// TokSpan - Get span of current token
// =============================================================================
//
// Returns span of current token, or EOF span if at end

const core::Span& TokSpan(const Parser& parser) {
  return Tok(parser)->span;
}

// =============================================================================
// TokensBetween - Token index span between parser states
// =============================================================================
//
// SPEC: Section 5.5 line 2917
// TokensBetween(P_0, P) = <TokIndex(P_0), TokIndex(P)>

std::pair<std::size_t, std::size_t> TokensBetween(const Parser& start,
                                                  const Parser& end) {
  SPEC_DEF("TokensBetween", "5.5");
  return std::pair<std::size_t, std::size_t>{start.index, end.index};
}

// =============================================================================
// Advance - Move to next token
// =============================================================================
//
// SPEC: Section 3.3.3 line 2988
// Advance(P) = <K, i+1, D, j, d, Delta>
// Increments parser.index if not at EOF

void Advance(Parser& parser) {
  if (AtEof(parser)) {
    return;
  }
  parser.index += 1;
}

}  // namespace cursive::ast
