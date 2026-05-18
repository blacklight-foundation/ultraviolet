// =============================================================================
// MIGRATION MAPPING: token.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 1.6.2 - Token Spans (lines 581-612)
//   Section 3.2.1 - Inputs, Outputs, and Records (lines 1997-2046)
//   Section 3.2.4 - Token Kinds (lines 2107-2118)
//
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/token.cpp
//   Lines 1-53 (entire file)
//
// DEPENDENCIES:
//   - ultraviolet/include/02_source/token.h (Token, RawToken, TokenKind, EofToken)
//   - ultraviolet/src/00_core/assert_spec.cpp (SPEC_DEF, SPEC_RULE)
//   - ultraviolet/src/00_core/span.cpp (SpanOf)
//   - ultraviolet/src/01_input/source.cpp (SourceFile)
//
// =============================================================================
// CONTENT TO MIGRATE:
// =============================================================================
//
// 1. SpecDefsTokenTypes() - Spec definition helper (source lines 7-11)
//    SPEC_DEF("TokenKind", "1.6.2")
//    SPEC_DEF("RawToken", "1.6.2")
//    SPEC_DEF("Token", "1.6.2")
//
// 2. NoUnknownOk() - Token validation (source lines 13-22)
//    Implements: No-Unknown-Ok rule from spec 1.6.2
//    Returns false if any token has Unknown kind
//
// 3. AttachSpan() - Single token span attachment (source lines 24-32)
//    Implements: Attach-Token-Ok rule from spec 1.6.2
//    Converts RawToken (byte offsets) to Token (with Span)
//    Uses core::SpanOf(source, start_offset, end_offset)
//
// 4. AttachSpans() - Batch span attachment (source lines 34-44)
//    Implements: Attach-Tokens-Ok rule from spec 1.6.2
//    Applies AttachSpan to vector of RawTokens
//
// 5. MakeEofToken() - EOF token construction (source lines 46-51)
//    Implements: TokenEOF from spec 3.2.1 line 2011
//    EOFSpan(S) = SpanOfText(S, |T|, |T|)
//    Creates token at end of source with zero-width span
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
//
// - Consider renaming AttachSpan/AttachSpans to more descriptive names
// - The parser-facing EOF token is represented explicitly as TokenKind::Eof.
//   Unknown remains the lexer-error sentinel for real token streams.
//   Ensure TokenKind enum includes all spec-defined kinds
// - Spec 3.2.4 line 2109 defines:
//   TokenKind in {Identifier, Keyword(k), IntLiteral, FloatLiteral,
//                 StringLiteral, CharLiteral, BoolLiteral, NullLiteral,
//                 Operator(o), Punctuator(p), Newline, Unknown}
// - OperatorSet defined at line 2112
// - PunctuatorSet defined at line 2115
//
// =============================================================================

#include "02_source/lexer/token.h"

#include <algorithm>

#include "00_core/assert_spec.h"

namespace ultraviolet::lexer {

namespace {

void SpecDefsTokenTypes() {
  SPEC_DEF("TokenKind", "1.6.2");
  SPEC_DEF("RawToken", "1.6.2");
  SPEC_DEF("Token", "1.6.2");
}

}  // namespace

bool NoUnknownOk(const std::vector<Token>& tokens) {
  SPEC_RULE("No-Unknown-Ok");
  SpecDefsTokenTypes();
  for (const auto& tok : tokens) {
    if (tok.kind == TokenKind::Unknown) {
      return false;
    }
  }
  return true;
}

Token AttachSpan(const core::SourceFile& source, const RawToken& raw) {
  SPEC_RULE("Attach-Token-Ok");
  SpecDefsTokenTypes();
  Token tok;
  tok.kind = raw.kind;
  tok.lexeme = raw.lexeme;
  tok.span = core::SpanOf(source, raw.start_offset, raw.end_offset);
  return tok;
}

std::vector<Token> AttachSpans(const core::SourceFile& source,
                               const std::vector<RawToken>& raws) {
  SPEC_RULE("Attach-Tokens-Ok");
  SpecDefsTokenTypes();
  std::vector<Token> out;
  out.reserve(raws.size());
  for (const auto& raw : raws) {
    out.push_back(AttachSpan(source, raw));
  }
  return out;
}

std::optional<std::pair<std::size_t, std::size_t>> TokenRange(
    const core::SourceFile& source,
    const Token& token) {
  const auto offsets = core::Utf8Offsets(source.scalars);
  const auto start_it =
      std::find(offsets.begin(), offsets.end(), token.span.start_offset);
  const auto end_it =
      std::find(offsets.begin(), offsets.end(), token.span.end_offset);
  if (start_it == offsets.end() || end_it == offsets.end()) {
    return std::nullopt;
  }

  const std::size_t i = static_cast<std::size_t>(start_it - offsets.begin());
  const std::size_t j = static_cast<std::size_t>(end_it - offsets.begin());
  const core::Span expected = core::SpanOf(source, offsets[i], offsets[j]);
  if (token.span.file != expected.file ||
      token.span.start_offset != expected.start_offset ||
      token.span.end_offset != expected.end_offset ||
      token.span.start_line != expected.start_line ||
      token.span.start_col != expected.start_col ||
      token.span.end_line != expected.end_line ||
      token.span.end_col != expected.end_col) {
    return std::nullopt;
  }

  return std::pair<std::size_t, std::size_t>{i, j};
}

Token MakeEofToken(const core::SourceFile& source) {
  SPEC_DEF("TokenEOF", "3.2.1");
  Token eof;
  eof.kind = TokenKind::Eof;
  eof.lexeme.clear();
  eof.span = core::SpanOf(source, source.byte_len, source.byte_len);
  return eof;
}

}  // namespace ultraviolet::lexer
