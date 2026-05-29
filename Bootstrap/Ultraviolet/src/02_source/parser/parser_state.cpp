// =============================================================================
// parser_state.cpp - Core Parser State Primitives
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 3.3.3 (Lines 2966-3018)
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

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::DocComment;
using ultraviolet::lexer::MakeEofToken;
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

namespace {

core::Span PointSpanAtEnd(const Token& token) {
  core::Span span = token.span;
  span.start_offset = token.span.end_offset;
  span.start_line = token.span.end_line;
  span.start_col = token.span.end_col;
  return span;
}

std::size_t VirtualTokenCount(const Parser& parser) {
  if (!parser.tokens) {
    return 0;
  }
  const std::size_t split_count =
      parser.split_shift_right_indices
          ? parser.split_shift_right_indices->size()
          : 0;
  return parser.tokens->size() + split_count;
}

struct TokenLookup {
  const Token* token = nullptr;
  std::optional<Token> split_token;
};

Token SplitShiftRightToken(const Token& original, bool is_right) {
  Token split = original;
  split.kind = TokenKind::Operator;
  split.lexeme = ">";
  const core::Span span = original.span;
  split.span.start_offset = span.start_offset + (is_right ? 1 : 0);
  split.span.end_offset = span.start_offset + (is_right ? 2 : 1);
  split.span.start_line = span.start_line;
  split.span.end_line = span.start_line;
  split.span.start_col = span.start_col + (is_right ? 1 : 0);
  split.span.end_col = span.start_col + (is_right ? 2 : 1);
  return split;
}

TokenLookup TokenAtVirtualIndex(const Parser& parser,
                                std::size_t virtual_index) {
  TokenLookup lookup;
  if (!parser.tokens) {
    return lookup;
  }

  std::size_t split_count_before = 0;
  if (parser.split_shift_right_indices) {
    const auto& split_indices = *parser.split_shift_right_indices;
    std::size_t first_covering_or_after = 0;
    std::size_t last = split_indices.size();
    while (first_covering_or_after < last) {
      const std::size_t mid = first_covering_or_after +
          (last - first_covering_or_after) / 2;
      const std::size_t split_virtual_end = split_indices[mid] + mid + 1;
      if (split_virtual_end < virtual_index) {
        first_covering_or_after = mid + 1;
      } else {
        last = mid;
      }
    }

    if (first_covering_or_after < split_indices.size()) {
      const std::size_t split_index =
          split_indices[first_covering_or_after];
      const std::size_t split_virtual_index =
          split_index + first_covering_or_after;
      if (virtual_index == split_virtual_index ||
          virtual_index == split_virtual_index + 1) {
        if (split_index >= parser.tokens->size()) {
          return lookup;
        }
        lookup.split_token = SplitShiftRightToken(
            (*parser.tokens)[split_index],
            virtual_index == split_virtual_index + 1);
        return lookup;
      }
      if (virtual_index < split_virtual_index) {
        split_count_before = first_covering_or_after;
      } else {
        split_count_before = first_covering_or_after + 1;
      }
    } else {
      split_count_before = split_indices.size();
    }
  }

  const std::size_t underlying_index = virtual_index - split_count_before;
  if (underlying_index < parser.tokens->size()) {
    lookup.token = &(*parser.tokens)[underlying_index];
  }
  return lookup;
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
  return parser.index >= VirtualTokenCount(parser);
}

// =============================================================================
// Tok - Get current token
// =============================================================================
//
// SPEC: Section 3.3.3 lines 2981-2983
// Returns K[i] if i < |K|, else EOF token
// Returns a pointer to K[i], or to the explicit EOF token when i = |K|

const Token* Tok(const Parser& parser) {
  if (!parser.tokens || AtEof(parser)) {
    Token& eof = ParserEofTokenCache();
    eof = MakeParserEofToken(parser);
    return &eof;
  }
  TokenLookup lookup = TokenAtVirtualIndex(parser, parser.index);
  if (lookup.split_token.has_value()) {
    Token& split = ParserEofTokenCache();
    split = *lookup.split_token;
    return &split;
  }
  if (lookup.token) {
    return lookup.token;
  }
  Token& eof = ParserEofTokenCache();
  eof = MakeParserEofToken(parser);
  return &eof;
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

}  // namespace ultraviolet::ast
