// =============================================================================
// MIGRATION MAPPING: lexer_literals.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   Section 3.2.6 - Literal Lexing (lines 2170-2364)
//
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/lexer_literals.cpp
//   Lines 1-641 (entire file)
//
// DEPENDENCIES:
//   - ultraviolet/include/02_source/lexer.h (LiteralScanResult, ScalarRange)
//   - ultraviolet/src/00_core/unicode.cpp (UnicodeScalar, kLF, Utf8Offsets)
//   - ultraviolet/src/00_core/diagnostic_messages.cpp (MakeDiagnostic, Emit)
//   - ultraviolet/src/00_core/span.cpp (SpanOf)
//
// =============================================================================
// CONTENT TO MIGRATE:
// =============================================================================
//
// INTERNAL HELPERS (anonymous namespace, source lines 18-383):
//
// 1. DigitScanResult struct (lines 22-26)
//    {ok, malformed, next} - result of digit sequence scan
//
// 2. TerminatorResult struct (lines 28-31)
//    {index, closed} - result of literal terminator search
//
// 3. Digit predicates (lines 33-47):
//    - IsDecDigit(c): '0'-'9'  [spec line 2077]
//    - IsHexDigit(c): dec OR 'a'-'f' OR 'A'-'F'  [spec line 2078]
//    - IsOctDigit(c): '0'-'7'  [spec line 2079]
//    - IsBinDigit(c): '0' or '1'  [spec line 2080]
//
// 4. IsUnicodeScalarValue() (lines 49-54)
//    Valid range: 0 to 0x10FFFF, excluding surrogates 0xD800-0xDFFF
//    [spec line 2329: CharValueRange]
//
// 5. HexValue() (lines 56-64)
//    Convert hex char to numeric value
//
// 6. StartsWith(), EndsWith() (lines 66-73)
//    String prefix/suffix utilities
//
// 7. NumericUnderscoreOk() (lines 75-123)
//    Implements spec NumericUnderscoreOk from lines 2220-2225:
//    - NOT StartsWithUnderscore
//    - NOT EndsWithUnderscore
//    - NOT AfterBasePrefixUnderscore (0x_, 0o_, 0b_)
//    - NOT AdjacentExponentUnderscore (_e, e_, _E, E_)
//    - NOT BeforeSuffixUnderscore (_i8, _f32, etc.)
//
// 8. DecimalLeadingZero() (lines 125-134)
//    Implements spec DecimalLeadingZero from lines 2282-2283:
//    Multi-digit decimal starting with '0' (after removing underscores)
//    Triggers warning W-SRC-0301
//
// 9. ScanDigits() (lines 136-169)
//    Generic digit sequence scanner with underscore support
//    Returns {ok, malformed, next}
//    Implements spec DecRun/HexRun/OctRun/BinRun from lines 2229-2232
//
// 10. ScanEscapeMatch() (lines 171-225)
//     Implements spec EscapeMatch from line 2309:
//     Valid escapes: \\, \", \', \n, \r, \t, \0, \xHH, \u{H+}
//     [spec SimpleEscape at line 2205]
//     [spec EscapeOk predicates at lines 2206-2208]
//
// 11. FirstBadEscape() (lines 227-244)
//     Shared scanner used for spec FirstBadStringEscape / FirstBadCharEscape:
//     Find first invalid escape sequence in a terminated quoted literal
//
// 12. CharScalarCount() (lines 246-267)
//     Implements spec CharScalarCount from line 2341:
//     Count scalar values in char literal (should be exactly 1)
//
// 13. FindTerminator() (lines 269-296)
//     Find closing quote handling backslash escapes
//     Implements spec BackslashCount/UnescapedQuote from lines 2305-2306
//
// 14. MatchSuffix() (lines 298-310)
//     Match suffix string at position
//
// 15. MatchIntSuffix() (lines 312-326)
//     Implements spec IntSuffixSet from line 2217:
//     {i8, i16, i32, i64, i128, u8, u16, u32, u64, u128, isize, usize}
//
// 16. MatchFloatSuffix() (lines 328-350)
//     Implements spec FloatSuffixSet from line 2218:
//     {f, f16, f32, f64}
//
// 17. SpanOfText(), LexemeSlice() (lines 352-371)
//     Utility functions for span/lexeme extraction
//
// 18. EmitDiag() (lines 373-381)
//     Diagnostic emission helper
//
// MAIN FUNCTIONS:
//
// 19. ScanIntLiteral() (lines 385-464)
//     Implements spec (Lex-Int) from lines 2263-2266:
//       DecDigit(T[i]) AND j = NumericScanEnd(T, i) AND NumericKind = IntLiteral
//
//     Handles:
//     - Decimal: digit+
//     - Hex: 0x digit+
//     - Octal: 0o digit+
//     - Binary: 0b digit+
//     - Optional suffix from IntSuffixSet
//
//     Errors:
//     - E-SRC-0304 for malformed literals
//     - W-SRC-0301 for decimal leading zeros
//
// 20. ScanFloatLiteral() (lines 466-555)
//     Implements spec (Lex-Float) from lines 2268-2271:
//       DecDigit(T[i]) AND j = NumericScanEnd(T, i) AND NumericKind = FloatLiteral
//
//     Format: integer_part "." fractional_part? exponent? float_suffix?
//
//     Handles:
//     - Decimal core: digit+ "." digit*
//     - Exponent: [eE][+-]?digit+
//     - Optional suffix: f, f16, f32, f64
//
//     Special case: "1.." (range) NOT float - check T[p+1] != '.'
//
// 21. ScanStringLiteral() (lines 557-593)
//     Implements spec (Lex-String) for terminated quoted spans.
//
//     Related rules:
//     - (Lex-String-Unterminated) line 2313-2316
//     - (Lex-String-BadEscape) line 2318-2321
//
//     Errors:
//     - E-SRC-0301 for unterminated
//     - E-SRC-0302 for bad escape
//
// 22. ScanCharLiteral() (lines 595-638)
//     Implements spec (Lex-Char) for terminated quoted spans.
//
//     Related rules:
//     - (Lex-Char-Unterminated) line 2343-2346
//     - (Lex-Char-BadEscape) line 2348-2351
//     - (Lex-Char-Invalid) line 2353-2356
//
//     Errors:
//     - E-SRC-0303 for unterminated or invalid (not exactly 1 scalar)
//     - E-SRC-0302 for bad escape
//
// =============================================================================
// SPEC GRAMMAR (lines 2176-2202):
// =============================================================================
//
// integer_literal  ::= (decimal_integer | hex_integer | octal_integer | binary_integer) int_suffix?
// decimal_integer  ::= dec_digit ("_"* dec_digit)*
// hex_integer      ::= "0x" hex_digit ("_"* hex_digit)*
// octal_integer    ::= "0o" oct_digit ("_"* oct_digit)*
// binary_integer   ::= "0b" bin_digit ("_"* bin_digit)*
// int_suffix       ::= "i8" | "i16" | "i32" | "i64" | "i128" | "u8" | "u16" | "u32" | "u64" | "u128" | "isize" | "usize"
//
// float_literal ::= decimal_integer "." decimal_integer? exponent? float_suffix?
// exponent      ::= ("e" | "E") ("+" | "-")? decimal_integer
// float_suffix  ::= "f" | "f16" | "f32" | "f64"
//
// string_literal   ::= '"' (string_char | escape_sequence)* '"'
// char_literal     ::= "'" (char_content | escape_sequence) "'"
// escape_sequence  ::= "\n" | "\r" | "\t" | "\\" | "\"" | "\'" | "\0" | "\x" hex hex | "\u{" hex+ "}"
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
//
// - Consider using std::from_chars for hex parsing where available
// - ScanDigits uses callback predicate; could be template parameter
// - Escape sequence validation is thorough; consider table-driven approach
// - CharScalarCount handles escape sequences correctly
//
// - Unsuffixed decimal float literals are valid and default to f32 only in
//   synthesis contexts without an expected type.
//   This avoids ambiguity with tuple access (t.0.0)
//
// - Leading zero warning (W-SRC-0301) only for decimal, not based integers
//
// - Unicode escape \u{...} validates:
//   1. 1-6 hex digits
//   2. Value is valid Unicode scalar (not surrogate)
//
// =============================================================================

#include "02_source/lexer/lexer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/source_text.h"
#include "00_core/span.h"

namespace ultraviolet::lexer {

namespace {

using core::UnicodeScalar;

struct DigitScanResult {
  bool ok = false;
  bool malformed = false;
  std::size_t next = 0;
};

struct TerminatorResult {
  std::size_t index = 0;
  bool closed = false;
};

bool IsDecDigit(UnicodeScalar c) {
  return c >= '0' && c <= '9';
}

bool IsHexDigit(UnicodeScalar c) {
  return IsDecDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool IsOctDigit(UnicodeScalar c) {
  return c >= '0' && c <= '7';
}

bool IsBinDigit(UnicodeScalar c) {
  return c == '0' || c == '1';
}

bool IsUnicodeScalarValue(std::uint32_t value) {
  return UnicodeScalar::IsValue(value);
}

unsigned int DecDigitValue(UnicodeScalar c) {
  return static_cast<unsigned int>(c - '0');
}

unsigned int OctDigitValue(UnicodeScalar c) {
  return static_cast<unsigned int>(c - '0');
}

unsigned int BinDigitValue(UnicodeScalar c) {
  return static_cast<unsigned int>(c - '0');
}

std::uint64_t DecValue(std::span<const UnicodeScalar> digits) {
  std::uint64_t value = 0;
  for (UnicodeScalar digit : digits) {
    value = (value * 10u) + DecDigitValue(digit);
  }
  return value;
}

std::uint64_t OctValue(std::span<const UnicodeScalar> digits) {
  std::uint64_t value = 0;
  for (UnicodeScalar digit : digits) {
    value = (value * 8u) + OctDigitValue(digit);
  }
  return value;
}

std::uint64_t BinValue(std::span<const UnicodeScalar> digits) {
  std::uint64_t value = 0;
  for (UnicodeScalar digit : digits) {
    value = (value * 2u) + BinDigitValue(digit);
  }
  return value;
}

unsigned int HexValue(UnicodeScalar c) {
  if (c >= '0' && c <= '9') {
    return DecDigitValue(c);
  }
  if (c >= 'a' && c <= 'f') {
    return 10u + static_cast<unsigned int>(c - 'a');
  }
  return 10u + static_cast<unsigned int>(c - 'A');
}

std::uint64_t HexValue(std::span<const UnicodeScalar> digits) {
  std::uint64_t value = 0;
  for (UnicodeScalar digit : digits) {
    value = (value << 4) | HexValue(digit);
  }
  return value;
}

bool StartsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() &&
         s.substr(s.size() - suffix.size()) == suffix;
}

char At(std::string_view s, std::size_t i) {
  return s[i];
}

std::string Remove(std::string_view s, char c) {
  std::string out;
  out.reserve(s.size());
  for (char x : s) {
    if (x != c) {
      out.push_back(x);
    }
  }
  return out;
}

std::string ConcatSuffix(
    std::initializer_list<std::string_view> parts,
    std::initializer_list<std::string_view>::const_iterator first) {
  auto next = first;
  ++next;
  if (next == parts.end()) {
    return std::string(*first);
  }
  return std::string(*first) + ConcatSuffix(parts, next);
}

std::string Concat(std::initializer_list<std::string_view> parts) {
  if (parts.size() == 0) {
    return std::string();
  }
  if (parts.size() == 1) {
    return std::string(*parts.begin());
  }
  auto tail = parts.begin();
  ++tail;
  return std::string(*parts.begin()) + ConcatSuffix(parts, tail);
}

constexpr std::string_view kIntSuffixes[] = {
    "isize", "usize", "i128", "u128", "i64", "u64",
    "i32",   "u32",   "i16",  "u16",  "i8",  "u8",
};

constexpr std::string_view kFloatSuffixes[] = {
    "f16",
    "f32",
    "f64",
    "f",
};

bool NumericUnderscoreOk(std::string_view s) {
  if (s.empty()) {
    return true;
  }
  if (At(s, 0) == '_' || At(s, s.size() - 1) == '_') {
    return false;
  }
  if (StartsWith(s, Concat({"0x", "_"})) ||
      StartsWith(s, Concat({"0o", "_"})) ||
      StartsWith(s, Concat({"0b", "_"}))) {
    return false;
  }

  const bool has_non_decimal_prefix =
      StartsWith(s, "0x") || StartsWith(s, "0X") ||
      StartsWith(s, "0o") || StartsWith(s, "0O") ||
      StartsWith(s, "0b") || StartsWith(s, "0B");
  if (!has_non_decimal_prefix) {
    for (std::size_t i = 0; i < s.size(); ++i) {
      if (At(s, i) != '_') {
        continue;
      }
      if (i > 0 && (At(s, i - 1) == 'e' || At(s, i - 1) == 'E')) {
        return false;
      }
      if (i + 1 < s.size() &&
          (At(s, i + 1) == 'e' || At(s, i + 1) == 'E')) {
        return false;
      }
    }
  }

  for (std::string_view suf : kIntSuffixes) {
    if (s.size() > suf.size() + 1 && EndsWith(s, Concat({"_", suf}))) {
      return false;
    }
  }
  for (std::string_view suf : kFloatSuffixes) {
    if (s.size() > suf.size() + 1 && EndsWith(s, Concat({"_", suf}))) {
      return false;
    }
  }

  return true;
}

bool DecimalLeadingZero(std::string_view s) {
  const std::string digits = Remove(s, '_');
  return digits.size() > 1 && At(digits, 0) == '0';
}

DigitScanResult ScanDigits(const std::vector<UnicodeScalar>& scalars,
                           std::size_t start,
                           bool (*pred)(UnicodeScalar)) {
  DigitScanResult result;
  const std::size_t n = scalars.size();
  if (start >= n || !pred(scalars[start])) {
    result.ok = false;
    result.next = start;
    return result;
  }
  result.ok = true;
  std::size_t p = start + 1;
  while (p < n) {
    if (pred(scalars[p])) {
      ++p;
      continue;
    }
    if (scalars[p] == '_') {
      while (p < n && scalars[p] == '_') {
        ++p;
      }
      if (p < n && pred(scalars[p])) {
        ++p;
        continue;
      }
      result.malformed = true;
      result.next = p;
      return result;
    }
    break;
  }
  result.next = p;
  return result;
}

std::size_t ScanRunWithUnderscore(const std::vector<UnicodeScalar>& scalars,
                                  std::size_t start,
                                  bool (*pred)(UnicodeScalar)) {
  std::size_t p = start;
  while (p < scalars.size() && (pred(scalars[p]) || scalars[p] == '_')) {
    ++p;
  }
  return p;
}

bool HasDot(const std::vector<UnicodeScalar>& scalars,
            std::size_t start,
            std::size_t end) {
  for (std::size_t p = start; p < end; ++p) {
    if (scalars[p] == '.') {
      return true;
    }
  }
  return false;
}

bool HasExp(const std::vector<UnicodeScalar>& scalars,
            std::size_t start,
            std::size_t end) {
  for (std::size_t p = start; p < end; ++p) {
    if (scalars[p] == 'e' || scalars[p] == 'E') {
      return true;
    }
  }
  return false;
}

bool HasFloatCore(const std::vector<UnicodeScalar>& scalars,
                  std::size_t start,
                  std::size_t end) {
  return HasDot(scalars, start, end);
}

std::size_t ScanExponentTail(const std::vector<UnicodeScalar>& scalars,
                             std::size_t exp_marker) {
  if (exp_marker >= scalars.size() ||
      (scalars[exp_marker] != 'e' && scalars[exp_marker] != 'E')) {
    return exp_marker;
  }
  std::size_t p = exp_marker + 1;
  if (p < scalars.size() && (scalars[p] == '+' || scalars[p] == '-')) {
    ++p;
  }
  return ScanRunWithUnderscore(scalars, p, IsDecDigit);
}

bool IsDecDigitChar(char c) {
  return c >= '0' && c <= '9';
}

bool IsHexDigitChar(char c) {
  return IsDecDigitChar(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool IsOctDigitChar(char c) {
  return c >= '0' && c <= '7';
}

bool IsBinDigitChar(char c) {
  return c == '0' || c == '1';
}

bool MatchDigitRunChars(std::string_view s, bool (*pred)(char)) {
  if (s.empty()) {
    return false;
  }
  bool saw_digit = false;
  bool last_was_underscore = false;
  for (char c : s) {
    if (c == '_') {
      if (!saw_digit) {
        return false;
      }
      last_was_underscore = true;
      continue;
    }
    if (!pred(c)) {
      return false;
    }
    saw_digit = true;
    last_was_underscore = false;
  }
  return saw_digit && !last_was_underscore;
}

std::optional<std::string_view> MatchIntSuffixLexeme(std::string_view lexeme) {
  for (std::string_view suffix : kIntSuffixes) {
    if (lexeme.size() <= suffix.size()) {
      continue;
    }
    if (EndsWith(lexeme, suffix)) {
      return suffix;
    }
  }
  return std::nullopt;
}

bool MatchesIntegerLiteralLexeme(std::string_view lexeme) {
  std::string_view core = lexeme;
  if (const auto suffix = MatchIntSuffixLexeme(lexeme)) {
    core = lexeme.substr(0, lexeme.size() - suffix->size());
  }
  if (core.size() >= 2 && core[0] == '0') {
    if (core[1] == 'x') {
      return MatchDigitRunChars(core.substr(2), IsHexDigitChar);
    }
    if (core[1] == 'o') {
      return MatchDigitRunChars(core.substr(2), IsOctDigitChar);
    }
    if (core[1] == 'b') {
      return MatchDigitRunChars(core.substr(2), IsBinDigitChar);
    }
  }
  return MatchDigitRunChars(core, IsDecDigitChar);
}

bool MatchesDecimalIntegerLexeme(std::string_view lexeme) {
  if (lexeme.size() >= 2 && lexeme[0] == '0' &&
      (lexeme[1] == 'x' || lexeme[1] == 'o' || lexeme[1] == 'b')) {
    return false;
  }
  return MatchDigitRunChars(lexeme, IsDecDigitChar);
}

bool MatchesExponentLexeme(std::string_view lexeme) {
  if (lexeme.empty()) {
    return false;
  }
  if (lexeme.front() == '+' || lexeme.front() == '-') {
    lexeme.remove_prefix(1);
  }
  return !lexeme.empty() && MatchesDecimalIntegerLexeme(lexeme);
}

bool MatchesFloatCoreLexeme(std::string_view lexeme) {
  const std::size_t dot = lexeme.find('.');
  if (dot == std::string_view::npos) {
    return false;
  }

  const std::string_view int_part = lexeme.substr(0, dot);
  if (!MatchesDecimalIntegerLexeme(int_part)) {
    return false;
  }

  const std::string_view after_dot = lexeme.substr(dot + 1);
  const std::size_t exp_pos = after_dot.find_first_of("eE");
  const std::string_view frac_part =
      exp_pos == std::string_view::npos ? after_dot : after_dot.substr(0, exp_pos);
  if (!frac_part.empty() && !MatchesDecimalIntegerLexeme(frac_part)) {
    return false;
  }

  if (exp_pos == std::string_view::npos) {
    return true;
  }
  return MatchesExponentLexeme(after_dot.substr(exp_pos + 1));
}

bool MatchesFloatLiteralLexeme(std::string_view lexeme) {
  for (std::string_view suffix : kFloatSuffixes) {
    if (lexeme.size() <= suffix.size() || !EndsWith(lexeme, suffix)) {
      continue;
    }
    if (MatchesFloatCoreLexeme(
            lexeme.substr(0, lexeme.size() - suffix.size()))) {
      return true;
    }
  }
  return MatchesFloatCoreLexeme(lexeme);
}

std::optional<std::size_t> ScanEscapeMatch(
    const std::vector<UnicodeScalar>& scalars,
    std::size_t start) {
  if (start + 1 >= scalars.size() || scalars[start] != '\\') {
    return std::nullopt;
  }
  const UnicodeScalar next = scalars[start + 1];
  switch (next) {
    case '\\':
    case '"':
    case '\'':
    case 'n':
    case 'r':
    case 't':
    case '0':
      return start + 2;
    case 'x': {
      if (start + 3 >= scalars.size()) {
        return std::nullopt;
      }
      if (!IsHexDigit(scalars[start + 2]) || !IsHexDigit(scalars[start + 3])) {
        return std::nullopt;
      }
      return start + 4;
    }
    case 'u': {
      if (start + 2 >= scalars.size() || scalars[start + 2] != '{') {
        return std::nullopt;
      }
      const std::size_t digits_start = start + 3;
      std::size_t p = digits_start;
      while (p < scalars.size() && IsHexDigit(scalars[p])) {
        ++p;
      }
      const std::size_t digits = p - digits_start;
      if (digits == 0) {
        return std::nullopt;
      }
      if (p >= scalars.size() || scalars[p] != '}') {
        return std::nullopt;
      }
      const auto hex_digits =
          std::span<const UnicodeScalar>(scalars.data() + digits_start, digits);
      const std::uint64_t value = HexValue(hex_digits);
      if (value > 0x10FFFFu ||
          !IsUnicodeScalarValue(static_cast<std::uint32_t>(value))) {
        return std::nullopt;
      }
      return p + 1;
    }
    default:
      return std::nullopt;
  }
}

std::optional<std::size_t> FirstBadEscape(
    const std::vector<UnicodeScalar>& scalars,
    std::size_t start,
    std::size_t terminator) {
  std::size_t p = start + 1;
  while (p < terminator) {
    if (scalars[p] != '\\') {
      ++p;
      continue;
    }
    const auto match = ScanEscapeMatch(scalars, p);
    if (!match.has_value() || *match > terminator) {
      return p;
    }
    p = *match;
  }
  return std::nullopt;
}

std::size_t CharScalarCount(const std::vector<UnicodeScalar>& scalars,
                            std::size_t start,
                            std::size_t terminator) {
  std::size_t count = 0;
  std::size_t p = start + 1;
  while (p < terminator) {
    if (scalars[p] != '\\') {
      ++count;
      ++p;
      continue;
    }
    const auto match = ScanEscapeMatch(scalars, p);
    if (match.has_value() && *match <= terminator) {
      ++count;
      p = *match;
    } else {
      ++count;
      ++p;
    }
  }
  return count;
}

TerminatorResult FindTerminator(const std::vector<UnicodeScalar>& scalars,
                                std::size_t start,
                                UnicodeScalar quote) {
  TerminatorResult result;
  const std::size_t n = scalars.size();
  std::size_t backslashes = 0;
  for (std::size_t p = start + 1; p < n; ++p) {
    const UnicodeScalar c = scalars[p];
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

std::size_t MatchSuffix(const std::vector<UnicodeScalar>& scalars,
                        std::size_t start,
                        std::string_view suffix) {
  if (start + suffix.size() > scalars.size()) {
    return 0;
  }
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    if (scalars[start + i] != static_cast<UnicodeScalar>(suffix[i])) {
      return 0;
    }
  }
  return suffix.size();
}

std::size_t SuffixMatch(const std::vector<UnicodeScalar>& scalars,
                        std::size_t start,
                        std::span<const std::string_view> suffixes) {
  std::size_t longest = 0;
  for (std::string_view suffix : suffixes) {
    const std::size_t len = MatchSuffix(scalars, start, suffix);
    if (len > longest) {
      longest = len;
    }
  }
  return longest;
}

std::size_t MatchIntSuffix(const std::vector<UnicodeScalar>& scalars,
                           std::size_t start) {
  return SuffixMatch(scalars, start, kIntSuffixes);
}

std::size_t MatchFloatSuffix(const std::vector<UnicodeScalar>& scalars,
                             std::size_t start) {
  return SuffixMatch(scalars, start, kFloatSuffixes);
}

core::Span SpanOfText(const core::SourceFile& source,
                      const std::vector<std::size_t>& offsets,
                      std::size_t i,
                      std::size_t j) {
  const std::size_t start = offsets[i];
  const std::size_t end = offsets[j];
  return core::SpanOf(source, start, end);
}

std::string LexemeSlice(const core::SourceFile& source,
                        const std::vector<std::size_t>& offsets,
                        std::size_t i,
                        std::size_t j) {
  const std::size_t start = offsets[i];
  const std::size_t end = offsets[j];
  if (start >= source.text.size() || end < start) {
    return std::string();
  }
  return core::EncodeUtf8(LexemeSliceScalars(source.scalars, i, j));
}

bool DecimalLeadingZero(const core::SourceFile& source,
                        const std::vector<std::size_t>& offsets,
                        std::size_t i,
                        std::size_t j) {
  const std::string lexeme = LexemeSlice(source, offsets, i, j);
  return MatchesDecimalIntegerLexeme(lexeme) && DecimalLeadingZero(lexeme);
}

void EmitDiag(core::DiagnosticStream& diags,
              std::string_view code,
              const core::Span& span) {
  const auto diag = core::MakeDiagnosticById(code, span);
  if (!diag.has_value()) {
    return;
  }
  core::Emit(diags, *diag);
}

}  // namespace

LiteralScanResult ScanIntLiteral(const core::SourceFile& source,
                                 std::size_t start) {
  SPEC_RULE("Lex-Int");
  SPEC_RULE("Lex-Numeric-Err");
  LiteralScanResult result;
  result.ok = false;
  result.next = start;

  const auto& scalars = source.scalars;
  const std::size_t n = scalars.size();
  if (start >= n || !IsDecDigit(scalars[start])) {
    return result;
  }

  const auto offsets = core::Utf8Offsets(scalars);
  std::size_t p = start;
  bool is_based = false;
  bool saw_exp = false;
  bool used_int_suffix = false;
  bool used_float_suffix = false;

  if (scalars[start] == '0' && start + 1 < n) {
    const UnicodeScalar next = scalars[start + 1];
    if (next == 'x' || next == 'o' || next == 'b') {
      is_based = true;
      std::size_t run_start = start + 2;
      if (next == 'x') {
        p = ScanRunWithUnderscore(scalars, run_start, IsHexDigit);
      } else if (next == 'o') {
        p = ScanRunWithUnderscore(scalars, run_start, IsOctDigit);
      } else {
        p = ScanRunWithUnderscore(scalars, run_start, IsBinDigit);
      }
    }
  }

  if (!is_based) {
    p = ScanRunWithUnderscore(scalars, start, IsDecDigit);
    if (p == start) {
      return result;
    }
    if (p < n && (scalars[p] == 'e' || scalars[p] == 'E')) {
      p = ScanExponentTail(scalars, p);
    }
    saw_exp = HasExp(scalars, start, p);
  }

  const std::size_t int_suffix_len = MatchIntSuffix(scalars, p);
  const std::size_t float_suffix_len = MatchFloatSuffix(scalars, p);
  const std::size_t suffix_len =
      int_suffix_len >= float_suffix_len ? int_suffix_len : float_suffix_len;
  used_int_suffix = int_suffix_len > 0 && int_suffix_len >= float_suffix_len;
  used_float_suffix = float_suffix_len > 0 && float_suffix_len > int_suffix_len;
  const std::size_t j = p + suffix_len;

  result.ok = true;
  result.next = j;

  const std::string lexeme = LexemeSlice(source, offsets, start, j);
  const bool underscore_ok = NumericUnderscoreOk(lexeme);
  const bool int_grammar_ok = MatchesIntegerLiteralLexeme(lexeme);
  if (!underscore_ok || !int_grammar_ok) {
    EmitDiag(result.diags, "E-SRC-0304", SpanOfText(source, offsets, start, j));
  }

  if (!is_based && !saw_exp && !used_int_suffix && !used_float_suffix &&
      underscore_ok && DecimalLeadingZero(source, offsets, start, j)) {
    SPEC_RULE("Warn-DecimalLeadingZero");
    EmitDiag(result.diags, "W-SRC-0301", SpanOfText(source, offsets, start, j));
  }

  return result;
}

LiteralScanResult ScanFloatLiteral(const core::SourceFile& source,
                                   std::size_t start) {
  SPEC_RULE("Lex-Float");
  SPEC_RULE("Lex-Numeric-Err");
  LiteralScanResult result;
  result.ok = false;
  result.next = start;

  const auto& scalars = source.scalars;
  const std::size_t n = scalars.size();
  if (start >= n || !IsDecDigit(scalars[start])) {
    return result;
  }

  const auto offsets = core::Utf8Offsets(scalars);
  std::size_t p = ScanRunWithUnderscore(scalars, start, IsDecDigit);
  if (p == start) {
    return result;
  }
  if (p >= n || scalars[p] != '.') {
    return result;
  }
  if (p + 1 < n && scalars[p + 1] == '.') {
    return result;
  }
  ++p;
  p = ScanRunWithUnderscore(scalars, p, IsDecDigit);
  if (!HasFloatCore(scalars, start, p)) {
    return result;
  }

  if (p < n && (scalars[p] == 'e' || scalars[p] == 'E')) {
    std::size_t exp_run = p + 1;
    if (exp_run < n && (scalars[exp_run] == '+' || scalars[exp_run] == '-')) {
      ++exp_run;
    }
    p = ScanRunWithUnderscore(scalars, exp_run, IsDecDigit);
  }

  const std::size_t int_suffix_len = MatchIntSuffix(scalars, p);
  const std::size_t float_suffix_len = MatchFloatSuffix(scalars, p);
  const std::size_t suffix_len =
      int_suffix_len >= float_suffix_len ? int_suffix_len : float_suffix_len;
  const std::size_t j = p + suffix_len;

  result.ok = true;
  result.next = j;

  const std::string lexeme = LexemeSlice(source, offsets, start, j);
  const bool underscore_ok = NumericUnderscoreOk(lexeme);
  const bool float_grammar_ok = MatchesFloatLiteralLexeme(lexeme);
  if (!underscore_ok || !float_grammar_ok) {
    EmitDiag(result.diags, "E-SRC-0304", SpanOfText(source, offsets, start, j));
  }

  return result;
}

LiteralScanResult ScanStringLiteral(const core::SourceFile& source,
                                    std::size_t start) {
  SPEC_RULE("Lex-String");
  SPEC_RULE("Lex-String-Unterminated");
  SPEC_RULE("Lex-String-BadEscape");
  LiteralScanResult result;
  result.ok = false;
  result.next = start;

  const auto& scalars = source.scalars;
  const std::size_t n = scalars.size();
  if (start >= n || scalars[start] != '"') {
    return result;
  }

  const auto offsets = core::Utf8Offsets(scalars);
  const TerminatorResult term = FindTerminator(scalars, start, '"');
  if (!term.closed) {
    EmitDiag(result.diags, "E-SRC-0301",
             SpanOfText(source, offsets, start, start + 1));
    result.next = term.index;
    return result;
  }

  const std::size_t j = term.index + 1;
  result.ok = true;
  result.next = j;
  result.range = ScalarRange{start, j};

  const auto bad = FirstBadEscape(scalars, start, term.index);
  if (bad.has_value()) {
    EmitDiag(result.diags, "E-SRC-0302",
             SpanOfText(source, offsets, *bad, *bad + 1));
  }

  return result;
}

LiteralScanResult ScanCharLiteral(const core::SourceFile& source,
                                  std::size_t start) {
  SPEC_RULE("Lex-Char");
  SPEC_RULE("Lex-Char-Unterminated");
  SPEC_RULE("Lex-Char-BadEscape");
  SPEC_RULE("Lex-Char-Invalid");
  LiteralScanResult result;
  result.ok = false;
  result.next = start;

  const auto& scalars = source.scalars;
  const std::size_t n = scalars.size();
  if (start >= n || scalars[start] != '\'') {
    return result;
  }

  const auto offsets = core::Utf8Offsets(scalars);
  const TerminatorResult term = FindTerminator(scalars, start, '\'');
  if (!term.closed) {
    EmitDiag(result.diags, "E-SRC-0303",
             SpanOfText(source, offsets, start, start + 1));
    result.next = term.index;
    return result;
  }

  const std::size_t j = term.index + 1;
  result.ok = true;
  result.next = j;
  result.range = ScalarRange{start, j};

  const auto bad = FirstBadEscape(scalars, start, term.index);
  if (bad.has_value()) {
    EmitDiag(result.diags, "E-SRC-0302",
             SpanOfText(source, offsets, *bad, *bad + 1));
  }

  const std::size_t count = CharScalarCount(scalars, start, term.index);
  if (count != 1) {
    EmitDiag(result.diags, "E-SRC-0303",
             SpanOfText(source, offsets, start, start + 1));
  }

  return result;
}

}  // namespace ultraviolet::lexer
