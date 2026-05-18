#include "00_core/spec_trace.h"
#include "02_source/parser/parser.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef UV_TEST_WORK_ROOT
#error "UV_TEST_WORK_ROOT must be defined"
#endif

namespace {

using ultraviolet::ast::ConsumeKind;
using ultraviolet::ast::ConsumeKeyword;
using ultraviolet::ast::ConsumeOperator;
using ultraviolet::ast::ConsumePunct;
using ultraviolet::ast::MakeParser;
using ultraviolet::ast::Parser;
using ultraviolet::core::Conformance;
using ultraviolet::core::SourceFile;
using ultraviolet::core::Span;
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

Token MakeToken(TokenKind kind, std::string lexeme, std::size_t offset) {
  const std::size_t end_offset = offset + lexeme.size();
  Token token;
  token.kind = kind;
  token.lexeme = std::move(lexeme);
  token.span = Span{
      "parser_token_consumption.uv",
      offset,
      end_offset,
      1,
      offset + 1,
      1,
      end_offset + 1,
  };
  return token;
}

SourceFile MakeSource() {
  SourceFile source;
  source.path = "parser_token_consumption.uv";
  source.text = "name public + ;";
  source.byte_len = source.text.size();
  source.line_starts = {0};
  source.line_count = 1;
  return source;
}

std::optional<std::string> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::cerr << "failed to open " << path << " for reading\n";
    return std::nullopt;
  }
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::size_t CountRule(std::string_view log, std::string_view rule) {
  const std::string needle = "\tparse\t" + std::string(rule) + "\t";
  std::size_t count = 0;
  std::size_t pos = 0;
  while (true) {
    pos = log.find(needle, pos);
    if (pos == std::string_view::npos) {
      return count;
    }
    ++count;
    pos += needle.size();
  }
}

bool ExpectAdvanced(std::string_view name, bool consumed, const Parser& parser) {
  if (!consumed) {
    std::cerr << name << " did not consume a matching token\n";
    return false;
  }
  if (parser.index != 1) {
    std::cerr << name << " left parser at index " << parser.index << "\n";
    return false;
  }
  return true;
}

}  // namespace

int main() {
  const std::filesystem::path work_root = UV_TEST_WORK_ROOT;
  std::error_code ec;
  std::filesystem::create_directories(work_root, ec);
  if (ec) {
    std::cerr << "failed to create test work root: " << ec.message() << "\n";
    return 1;
  }

  const std::filesystem::path success_log =
      work_root / "parser_token_consumption_success.conformance.log";
  const std::filesystem::path failure_log =
      work_root / "parser_token_consumption_failure.conformance.log";
  const std::filesystem::path closed_log =
      work_root / "parser_token_consumption_closed.conformance.log";

  const SourceFile source = MakeSource();
  Conformance::Init(success_log.string(), "compile");
  Conformance::SetRoot(work_root.string());
  Conformance::SetPhase("parse");

  {
    std::vector<Token> tokens = {MakeToken(TokenKind::Identifier, "name", 0)};
    Parser parser = MakeParser(tokens, source);
    if (!ExpectAdvanced("ConsumeKind",
                        ConsumeKind(parser, TokenKind::Identifier),
                        parser)) {
      return 1;
    }
  }
  {
    std::vector<Token> tokens = {MakeToken(TokenKind::Keyword, "public", 5)};
    Parser parser = MakeParser(tokens, source);
    if (!ExpectAdvanced("ConsumeKeyword",
                        ConsumeKeyword(parser, "public"),
                        parser)) {
      return 1;
    }
  }
  {
    std::vector<Token> tokens = {MakeToken(TokenKind::Operator, "+", 12)};
    Parser parser = MakeParser(tokens, source);
    if (!ExpectAdvanced("ConsumeOperator",
                        ConsumeOperator(parser, "+"),
                        parser)) {
      return 1;
    }
  }
  {
    std::vector<Token> tokens = {MakeToken(TokenKind::Punctuator, ";", 14)};
    Parser parser = MakeParser(tokens, source);
    if (!ExpectAdvanced("ConsumePunct", ConsumePunct(parser, ";"), parser)) {
      return 1;
    }
  }

  Conformance::Init(failure_log.string(), "compile");
  Conformance::SetRoot(work_root.string());
  Conformance::SetPhase("parse");

  {
    std::vector<Token> tokens = {MakeToken(TokenKind::Identifier, "name", 0)};
    Parser parser = MakeParser(tokens, source);
    if (ConsumeKeyword(parser, "public")) {
      std::cerr << "ConsumeKeyword consumed a non-keyword token\n";
      return 1;
    }
    if (parser.index != 0) {
      std::cerr << "failed ConsumeKeyword advanced parser to index "
                << parser.index << "\n";
      return 1;
    }
  }

  Conformance::Init(closed_log.string(), "compile");

  const auto success_text = ReadFile(success_log);
  const auto failure_text = ReadFile(failure_log);
  if (!success_text.has_value() || !failure_text.has_value()) {
    return 1;
  }

  for (std::string_view rule : {
           "Tok-Consume-Kind",
           "Tok-Consume-Keyword",
           "Tok-Consume-Operator",
           "Tok-Consume-Punct",
       }) {
    const std::size_t count = CountRule(*success_text, rule);
    if (count != 1) {
      std::cerr << rule << " recorded " << count
                << " successful consumption traces\n";
      return 1;
    }
  }

  if (CountRule(*failure_text, "Tok-Consume-Keyword") != 0) {
    std::cerr << "failed keyword consumption recorded Tok-Consume-Keyword\n";
    return 1;
  }

  return 0;
}
