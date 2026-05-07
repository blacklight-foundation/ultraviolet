// =============================================================================
// MIGRATION MAPPING: tokenize.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 3.2.11 - Tokenization (Small-Step) (lines 2499-2567)
//   Section 3.2.12 - Tokenize (Big-Step) (lines 2568-2586)
//
// SOURCE FILE: cursive-bootstrap/src/02_syntax/tokenize.cpp
//   Lines 1-337 (entire file)
//
// =============================================================================

#include "02_source/lexer/lexer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/source_text.h"
#include "00_core/span.h"
#include "00_core/unicode.h"

namespace cursive::lexer {

LexemeScalars LexemeSliceScalars(
    const std::vector<core::UnicodeScalar>& scalars,
    std::size_t i,
    std::size_t j) {
  if (j < i || j > scalars.size()) {
    return {};
  }
  return LexemeScalars(scalars.begin() + static_cast<std::ptrdiff_t>(i),
                       scalars.begin() + static_cast<std::ptrdiff_t>(j));
}

namespace {

// Debug output for lex failures.
// Triggered by CURSIVE0_DEBUG_LEX environment variable.
// Outputs scalar index, byte index, codepoint, context window.
void DebugLexFail(const core::SourceFile& source,
                  const std::vector<core::UnicodeScalar>& scalars,
                  const std::vector<std::size_t>& offsets,
                  std::size_t index) {
  if (!core::IsDebugEnabled("lex")) {
    return;
  }

  const std::size_t n = scalars.size();
  const std::size_t byte_index = index < offsets.size() ? offsets[index] : 0;
  const core::UnicodeScalar cp =
      index < n ? scalars[index] : core::UnicodeScalar{};

  std::cerr << "[cursive] lex: Max-Munch-Err at scalar=" << index
            << " byte=" << byte_index
            << " codepoint=U+"
            << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
            << static_cast<std::uint32_t>(cp)
            << std::dec << "\n";

  const std::size_t lo = index > 16 ? index - 16 : 0;
  const std::size_t hi = std::min(n, index + 17);

  std::string context;
  context.reserve((hi - lo) + 8);
  for (std::size_t i = lo; i < hi; ++i) {
    const core::UnicodeScalar c = scalars[i];
    if (c == '\n') {
      context += "\\n";
    } else if (c >= 0x20 && c <= 0x7E) {
      context.push_back(static_cast<char>(c));
    } else {
      context.push_back('.');
    }
  }

  std::cerr << "[cursive] lex: context=\"" << context << "\"\n";
  std::cerr << "[cursive] lex: window=[";
  for (std::size_t i = lo; i < hi; ++i) {
    if (i > lo) {
      std::cerr << " ";
    }
    std::cerr << "U+"
              << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
              << static_cast<std::uint32_t>(scalars[i])
              << std::dec;
    if (i == index) {
      std::cerr << "*";
    }
  }
  std::cerr << "]\n";
}

// Used for string/char literal terminator finding.
struct TerminatorResult {
  std::size_t index = 0;
  bool closed = false;
};

// Multi-char prefix matching.
// Used for comment detection ("//", "/*", "///", "//!").
bool MatchPrefix(const std::vector<core::UnicodeScalar>& scalars,
                 std::size_t start,
                 std::string_view lexeme) {
  if (start + lexeme.size() > scalars.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lexeme.size(); ++i) {
    if (scalars[start + i] != static_cast<unsigned char>(lexeme[i])) {
      return false;
    }
  }
  return true;
}

// Find literal terminator.
// Implements: StringTerminator/CharTerminator from spec 3.2.6.
// Handles backslash escape counting for quote detection.
// SPEC: BackslashCount(T, p) = max{k | ... T[r] = "\\"}
// SPEC: UnescapedQuote(T, p) = T[p] = '"' AND BackslashCount mod 2 = 0
TerminatorResult FindTerminator(const std::vector<core::UnicodeScalar>& scalars,
                                std::size_t start,
                                core::UnicodeScalar quote) {
  TerminatorResult result;
  const std::size_t n = scalars.size();
  std::size_t backslashes = 0;
  for (std::size_t p = start + 1; p < n; ++p) {
    const core::UnicodeScalar c = scalars[p];
    if (c == core::kLF) {
      result.index = p;
      result.closed = false;
      return result;
    }
    if (c == '\\') {
      ++backslashes;
      continue;
    }
    if (c == quote && (backslashes % 2 == 0)) {
      result.index = p;
      result.closed = true;
      return result;
    }
    backslashes = 0;
  }
  result.index = n;
  result.closed = false;
  return result;
}

// StringTerminator wrapper.
std::size_t StringTerminator(const std::vector<core::UnicodeScalar>& scalars,
                             std::size_t start) {
  return FindTerminator(scalars, start, '"').index;
}

// CharTerminator wrapper.
std::size_t CharTerminator(const std::vector<core::UnicodeScalar>& scalars,
                           std::size_t start) {
  return FindTerminator(scalars, start, '\'').index;
}

// Implements spec predicate from line 2308.
// Used to detect unterminated literals.
bool LineFeedOrEOFBeforeClose(const std::vector<core::UnicodeScalar>& scalars,
                              std::size_t start,
                              core::UnicodeScalar quote) {
  return !FindTerminator(scalars, start, quote).closed;
}

// Implements spec SpanOfText(S, i, j) from line 2044.
// Span from scalar indices.
core::Span SpanOfText(const core::SourceFile& source,
                      const std::vector<std::size_t>& offsets,
                      std::size_t i,
                      std::size_t j) {
  return core::SpanOf(source, offsets[i], offsets[j]);
}

// Implements spec Lexeme(T, i, j) from line 2046.
// Extract lexeme substring.
std::string LexemeSlice(const LexerInput& input,
                        const std::vector<std::size_t>& offsets,
                        std::size_t i,
                        std::size_t j) {
  if (input.scalars == nullptr) {
    return {};
  }
  const std::size_t start = offsets[i];
  const std::size_t end = offsets[j];
  if (end < start || end > input.byte_len) {
    return {};
  }
  const LexemeScalars lexeme = LexemeSliceScalars(*input.scalars, i, j);
  return core::EncodeUtf8(lexeme);
}

// Diagnostic stream merge.
void AppendDiags(core::DiagnosticStream& out,
                 const core::DiagnosticStream& add) {
  for (const auto& diag : add) {
    core::Emit(out, diag);
  }
}

// Implements spec SensitiveInSpan(T, i, j) from line 2460.
// Collect sensitive positions.
void AppendSensitiveInSpan(const std::vector<core::UnicodeScalar>& scalars,
                           std::size_t i,
                           std::size_t j,
                           std::vector<std::size_t>& sens) {
  for (std::size_t p = i; p < j; ++p) {
    if (core::IsSensitive(scalars[p])) {
      sens.push_back(p);
    }
  }
}

bool PrevSignificantTokenIsDot(const std::vector<Token>& tokens) {
  for (std::size_t i = tokens.size(); i > 0; --i) {
    const auto& tok = tokens[i - 1];
    if (tok.kind == TokenKind::Newline) {
      continue;
    }
    return tok.kind == TokenKind::Punctuator && tok.lexeme == ".";
  }
  return false;
}

std::vector<RawToken> ToRawTokens(const std::vector<Token>& tokens) {
  std::vector<RawToken> raws;
  raws.reserve(tokens.size());
  for (const auto& tok : tokens) {
    RawToken raw;
    raw.kind = tok.kind;
    raw.lexeme = tok.lexeme;
    raw.start_offset = tok.span.start_offset;
    raw.end_offset = tok.span.end_offset;
    raws.push_back(std::move(raw));
  }
  return raws;
}

}  // namespace

LexerInput MakeLexerInput(const core::SourceFile& source) {
  return LexerInput{&source.scalars, source.text, source.byte_len};
}

// Small-step tokenization.
// Implements spec rules from 3.2.11:
// - Lex-Start (line 2505-2507)
// - Lex-End (line 2509-2512)
// - Lex-Whitespace (line 2514-2517)
// - Lex-Newline (line 2519-2522)
// - Lex-Doc-Comment (line 2529-2532)
// - Lex-Line-Comment (line 2524-2527)
// - Lex-Block-Comment (line 2534-2537)
// - Lex-String-Unterminated-Recover (line 2539-2542)
// - Lex-Char-Unterminated-Recover (line 2544-2547)
// - Lex-Sensitive (line 2549-2552)
// - Lex-Token (line 2558-2561)
// - Lex-Token-Err (line 2563-2566)
LexSmallStepResult LexSmallStep(const core::SourceFile& source) {
  SPEC_RULE("Lex-Start");
  LexSmallStepResult result;
  const LexerInput input = MakeLexerInput(source);
  const auto& scalars = *input.scalars;
  const auto offsets = core::Utf8Offsets(scalars);

  std::size_t i = 0;
  std::vector<Token> tokens;
  std::vector<DocComment> docs;
  std::vector<std::size_t> sensitive;

  while (true) {
    if (i >= scalars.size()) {
      SPEC_RULE("Lex-End");
      result.ok = true;
      break;
    }

    const core::UnicodeScalar c = scalars[i];

    if (IsWhitespace(c)) {
      SPEC_RULE("Lex-Whitespace");
      ++i;
      continue;
    }

    if (IsLineFeed(c)) {
      SPEC_RULE("Lex-Newline");
      Token tok;
      tok.kind = TokenKind::Newline;
      tok.lexeme = LexemeSlice(input, offsets, i, i + 1);
      tok.span = SpanOfText(source, offsets, i, i + 1);
      tokens.push_back(tok);
      ++i;
      continue;
    }

    if (MatchPrefix(scalars, i, "///") || MatchPrefix(scalars, i, "//!")) {
      SPEC_RULE("Lex-Doc-Comment");
      CommentScanResult doc = ScanDocComment(source, i);
      AppendDiags(result.diags, doc.diags);
      if (doc.ok && doc.doc.has_value()) {
        docs.push_back(*doc.doc);
        i = doc.next;
        continue;
      }
    }

    if (MatchPrefix(scalars, i, "//")) {
      SPEC_RULE("Lex-Line-Comment");
      CommentScanResult line = ScanLineComment(source, i);
      AppendDiags(result.diags, line.diags);
      if (line.ok) {
        i = line.next;
        continue;
      }
    }

    if (MatchPrefix(scalars, i, "/*")) {
      SPEC_RULE("Lex-Block-Comment");
      CommentScanResult block = ScanBlockComment(source, i);
      AppendDiags(result.diags, block.diags);
      if (!block.ok) {
        result.ok = false;
        result.error_code = "E-SRC-0306";
        break;
      }
      i = block.next;
      continue;
    }

    if (c == '"' && LineFeedOrEOFBeforeClose(scalars, i, '"')) {
      SPEC_RULE("Lex-String-Unterminated-Recover");
      const auto span = SpanOfText(source, offsets, i, i + 1);
      if (auto diag = core::MakeDiagnosticById("E-SRC-0301", span)) {
        core::Emit(result.diags, *diag);
      }
      const std::size_t term = StringTerminator(scalars, i);
      i = term;
      continue;
    }

    if (c == '\'' && LineFeedOrEOFBeforeClose(scalars, i, '\'')) {
      SPEC_RULE("Lex-Char-Unterminated-Recover");
      const auto span = SpanOfText(source, offsets, i, i + 1);
      if (auto diag = core::MakeDiagnosticById("E-SRC-0303", span)) {
        core::Emit(result.diags, *diag);
      }
      const std::size_t term = CharTerminator(scalars, i);
      i = term;
      continue;
    }

    if (core::IsSensitive(c)) {
      SPEC_RULE("Lex-Sensitive");
      sensitive.push_back(i);
      ++i;
      continue;
    }

    NextTokenResult next = NextToken(source, i);
    if (!next.ok) {
      DebugLexFail(source, scalars, offsets, i);
      SPEC_RULE("Lex-Token-Err");
      AppendDiags(result.diags, next.diags);
      result.ok = false;
      if (!next.diags.empty()) {
        result.error_code = next.diags.front().code;
      } else {
        result.error_code = "E-SRC-0309";
      }
      break;
    }

    SPEC_RULE("Lex-Token");
    AppendDiags(result.diags, next.diags);

    // Spec tuple-projection lexical disambiguation: if a decimal-float token
    // candidate appears immediately after a `.` token, emit the shorter integer
    // token instead so `t.0.0` tokenizes as `t` `.` `0` `.` `0`.
    if (next.kind == TokenKind::FloatLiteral && PrevSignificantTokenIsDot(tokens)) {
      LiteralScanResult int_scan = ScanIntLiteral(source, i);
      if ((int_scan.ok || int_scan.next > i) && int_scan.next < next.next) {
        AppendDiags(result.diags, int_scan.diags);
        Token int_tok;
        int_tok.kind = TokenKind::IntLiteral;
        int_tok.lexeme = LexemeSlice(input, offsets, i, int_scan.next);
        int_tok.span = SpanOfText(source, offsets, i, int_scan.next);
        tokens.push_back(int_tok);
        AppendSensitiveInSpan(scalars, i, int_scan.next, sensitive);
        i = int_scan.next;
        continue;
      }
    }

    Token tok;
    tok.kind = next.kind;
    tok.lexeme = LexemeSlice(input, offsets, i, next.next);
    tok.span = SpanOfText(source, offsets, i, next.next);
    tokens.push_back(tok);

    // SensitiveTok helper (spec line 2554-2556): excludes string/char literals.
    if (tok.kind != TokenKind::StringLiteral &&
        tok.kind != TokenKind::CharLiteral) {
      AppendSensitiveInSpan(scalars, i, next.next, sensitive);
    }

    i = next.next;
  }

  result.output.tokens = std::move(tokens);
  result.output.docs = std::move(docs);
  result.sensitive = std::move(sensitive);
  return result;
}

// Big-step tokenization.
// Implements spec rules from 3.2.12:
// - Tokenize-Err (line 2580-2583)
// - Tokenize-Secure-Err (line 2575-2578)
// - Tokenize-Ok (line 2570-2573)
//
// Steps:
//   1. Run LexSmallStep
//   2. If failed, return error (Tokenize-Err)
//   3. Run LexSecure on result
//   4. Run ConfusableCheck on identifier tokens
//   5. If either security check fails, return error (Tokenize-Secure-Err)
//   6. Return success (Tokenize-Ok)
TokenizeDiagnosticResult TokenizeWithDiagnostics(const core::SourceFile& source) {
  TokenizeDiagnosticResult result;
  LexSmallStepResult lexed = LexSmallStep(source);
  result.diags = lexed.diags;

  if (!lexed.ok) {
    SPEC_RULE("Tokenize-Err");
    result.output.reset();
    return result;
  }

  LexSecureResult secure =
      LexSecure(source, lexed.output.tokens, LexSensitivePos(source));
  AppendDiags(result.diags, secure.diags);
  if (!secure.ok) {
    SPEC_RULE("Tokenize-Secure-Err");
    result.output.reset();
    return result;
  }

  if (core::IsDebugEnabled("lex") || core::IsDebugEnabled("parse")) {
    std::cerr << "[cursive] unicode: token-count=" << lexed.output.tokens.size()
              << " running ConfusableCheck\n";
  }
  LexSecureResult confusable = ConfusableCheck(source, lexed.output.tokens);
  AppendDiags(result.diags, confusable.diags);
  if (!confusable.ok) {
    SPEC_RULE("Tokenize-Secure-Err");
    result.output.reset();
    return result;
  }

  // Enforce the section 1.6.2 token attachment/validation pipeline in
  // production tokenization flow.
  auto attached_tokens = AttachSpans(source, ToRawTokens(lexed.output.tokens));
  if (!NoUnknownOk(attached_tokens)) {
    const auto it = std::find_if(attached_tokens.begin(), attached_tokens.end(),
                                 [](const Token& tok) {
                                   return tok.kind == TokenKind::Unknown;
                                 });
    if (it != attached_tokens.end()) {
      if (auto diag = core::MakeDiagnosticById("E-SRC-0309", it->span)) {
        core::Emit(result.diags, *diag);
      }
    }
    SPEC_RULE("Tokenize-Err");
    result.output.reset();
    return result;
  }
  lexed.output.tokens = std::move(attached_tokens);

  SPEC_RULE("Tokenize-Ok");
  result.output = std::move(lexed.output);
  return result;
}

TokenizeResult Tokenize(const core::SourceFile& source) {
  return TokenizeWithDiagnostics(source).output;
}

}  // namespace cursive::lexer
