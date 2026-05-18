#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "00_core/span.h"

namespace ultraviolet::lexer
{

  enum class TokenKind
  {
    Identifier,
    Keyword,
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    CharLiteral,
    BoolLiteral,
    NullLiteral,
    Operator,
    Punctuator,
    Newline,
    Eof,
    Unknown,
  };

  // UTF-8 surface encoding carried on tokens for parser-facing comparisons.
  using Lexeme = std::string;
  // Exact scalar slice for the spec helper Lexeme(T, i, j) = T[i..j).
  using LexemeScalars = core::Scalars;

  struct RawToken
  {
    TokenKind kind = TokenKind::Unknown;
    Lexeme lexeme;
    std::size_t start_offset = 0;
    std::size_t end_offset = 0;
  };

  struct Token
  {
    TokenKind kind = TokenKind::Unknown;
    Lexeme lexeme;
    core::Span span;
  };

  enum class DocKind
  {
    LineDoc,
    ModuleDoc,
  };

  struct DocComment
  {
    DocKind kind = DocKind::LineDoc;
    std::string text;
    core::Span span;
  };

  bool NoUnknownOk(const std::vector<Token> &tokens);

  Token AttachSpan(const core::SourceFile &source, const RawToken &raw);

  std::vector<Token> AttachSpans(const core::SourceFile &source,
                                 const std::vector<RawToken> &raws);

  std::optional<std::pair<std::size_t, std::size_t>> TokenRange(
      const core::SourceFile &source, const Token &token);

  Token MakeEofToken(const core::SourceFile &source);

} // namespace ultraviolet::lexer
