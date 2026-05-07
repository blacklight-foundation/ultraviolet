// =============================================================================
// MIGRATION MAPPING: unicode.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 3.1.6 "NFC Normalization for Identifiers and Module Paths" (lines 1838-1851)
//     - NFC(s) = UnicodeNFC_{15.0.0}(s)
//     - CaseFold(s) = UnicodeCaseFold_{15.0.0}(s)
//     - Totality: NFC and CaseFold are total on Unicode scalar sequences
//     - IdKey(s) = NFC(s)
//     - IdEq(s1, s2) iff IdKey(s1) = IdKey(s2)
//   - Section 3.2.2 "Identifier Lexing" (referenced in SpecDefsIdentChars)
//     - IdentStart: '_' or XID_Start
//     - IdentContinue: '_' or XID_Continue
//     - XID_Start, XID_Continue: Unicode properties
//     - NonCharacter: U+FDD0-U+FDEF, U+nFFFE, U+nFFFF
//     - Sensitive: bidirectional controls and zero-width joiners
//   - Section 3.1.5 "Prohibited Code Points" (lines 1827-1836)
//     - Prohibited(c): Cc category except {U+0009, U+000A, U+000C, U+000D}
//     - WF-Prohibited rule for source files
//   - Section 3.1.2 "Lexically Sensitive Unicode" (lines 1743-1746)
//     - Sensitive characters: U+200E-U+200F, U+202A-U+202E, U+2066-U+2069, U+200C-U+200D
//
// SOURCE FILE: cursive-bootstrap/src/00_core/unicode.cpp
//   - Lines 1-335 (entire file)
//
// CONTENT TO MIGRATE:
//   - SpecDefsUnicode() (lines 21-24) [static inline]
//     Traces NFC and CaseFold to spec section 3.1.6
//   - NFC(s) -> string (lines 26-45)
//     Unicode NFC normalization via ICU, targeting Unicode 15.0
//   - CaseFold(s) -> string (lines 47-59)
//     Unicode case folding via ICU, targeting Unicode 15.0
//   - SpecDefsIdentChars() (lines 61-68) [static inline]
//     Traces identifier character definitions to spec section 3.2.2
//   - IsXidStart(c) -> bool (lines 70-76)
//     Checks XID_Start Unicode property
//   - IsXidContinue(c) -> bool (lines 78-84)
//     Checks XID_Continue Unicode property
//   - IsNonCharacter(c) -> bool (lines 86-96)
//     Checks for Unicode noncharacters (FDD0-FDEF, xFFFE, xFFFF)
//   - IsIdentStart(c) -> bool (lines 98-101)
//     Returns '_' || IsXidStart(c)
//   - IsIdentContinue(c) -> bool (lines 103-106)
//     Returns '_' || IsXidContinue(c)
//   - IsSensitive(c) -> bool (lines 108-112)
//     Checks for bidirectional controls and zero-width joiners
//   - [Internal helpers] (lines 114-302)
//     - ByteSpan struct, IsHexDigit, HexValue, IsUnicodeScalarValue
//     - IsStringChar, IsCharContent
//     - ScanEscape, ScanStringLiteral, ScanCharLiteral
//     - LiteralSpan: identifies string/char literal ranges
//     - ByteInLiteralSpan: checks if offset is inside a literal
//   - IsProhibited(c) -> bool (lines 305-311)
//     Checks for prohibited control characters
//   - FirstProhibitedOutsideLiteral(scalars) -> optional<size_t> (lines 313-328)
//     Finds first prohibited character outside string/char literals
//   - NoProhibited(scalars) -> bool (lines 330-333)
//     Returns true if no prohibited characters outside literals
//
// DEPENDENCIES:
//   - cursive/include/00_core/unicode.h (header)
//     - UnicodeScalar type
//     - kBOM, kCR, kLF constants
//     - Utf8Offsets() function
//   - cursive/include/00_core/assert_spec.h
//     - SPEC_DEF macro
//     - SPEC_RULE macro
//   - ICU headers:
//     - <unicode/normalizer2.h> for NFC
//     - <unicode/stringpiece.h> for StringPiece
//     - <unicode/uchar.h> for u_hasBinaryProperty, UCHAR_XID_START/CONTINUE
//     - <unicode/unistr.h> for UnicodeString
//     - <unicode/utypes.h> for UErrorCode
//     - <unicode/uversion.h> for U_UNICODE_VERSION
//
// REFACTORING NOTES:
//   1. ICU dependency: requires ICU library with Unicode 15.0 support
//   2. static_assert verifies U_UNICODE_VERSION == "15.0" for spec compliance
//   3. SPEC_DEF traces:
//      - "NFC", "CaseFold" -> "3.1.6"
//      - "IdentStart", "IdentContinue", "XID_Start", "XID_Continue" -> "3.2.2"
//      - "NonCharacter", "Sensitive" -> "3.2.2"
//   4. SPEC_RULE traces:
//      - "WF-Prohibited" in NoProhibited()
//   5. Literal scanning handles escape sequences for accurate span detection
//   6. Prohibited check excludes content inside string/char literals
//   7. std::abort() on ICU failure - consider error handling improvement
//   8. UnicodeScalar range check (> 0x10FFFF) in XID functions
//
// =============================================================================

#include "00_core/unicode.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include <unicode/normalizer2.h>
#include <unicode/stringpiece.h>
#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/uspoof.h>
#include <unicode/unistr.h>
#include <unicode/utypes.h>
#include <unicode/uversion.h>

#include "00_core/assert_spec.h"
#include "00_core/process_config.h"

namespace cursive::core {

static inline void SpecDefsUnicode() {
  SPEC_DEF("NFC", "3.1.6");
  SPEC_DEF("CaseFold", "3.1.6");
}

static inline void SpecDefsIdentifierSecurity() {
  SPEC_DEF("Skeleton", "4.2.10");
  SPEC_DEF("IdentifierScripts", "4.2.10");
}

namespace {

void LogUnicodeDebug(std::string_view message) {
  if (!IsDebugEnabled("lex") && !IsDebugEnabled("parse")) {
    return;
  }
  std::cerr << "[cursive] unicode: " << message << "\n";
}

bool IsAsciiText(std::string_view s) {
  for (const unsigned char ch : s) {
    if (ch >= 0x80u) {
      return false;
    }
  }
  return true;
}

std::string AsciiCaseFold(std::string_view s) {
  std::string out(s);
  for (char& ch : out) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return out;
}

const USpoofChecker* SpoofChecker() {
  static const USpoofChecker* checker = []() -> const USpoofChecker* {
    UErrorCode status = U_ZERO_ERROR;
    USpoofChecker* value = uspoof_open(&status);
    if (U_FAILURE(status) || value == nullptr) {
      LogUnicodeDebug("uspoof_open failed status=" + std::to_string(status));
      std::abort();
    }
    uspoof_setChecks(value, USPOOF_CONFUSABLE, &status);
    if (U_FAILURE(status)) {
      LogUnicodeDebug("uspoof_setChecks failed status=" +
                      std::to_string(status));
      std::abort();
    }
    return value;
  }();
  return checker;
}

std::string SkeletonForUtf8(std::string_view normalized) {
  UErrorCode status = U_ZERO_ERROR;
  const int32_t length = uspoof_getSkeletonUTF8(
      SpoofChecker(),
      0,
      normalized.data(),
      static_cast<int32_t>(normalized.size()),
      nullptr,
      0,
      &status);
  if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
    LogUnicodeDebug("uspoof_getSkeletonUTF8 length query failed status=" +
                    std::to_string(status) + " normalized=\"" +
                    std::string(normalized) + "\"");
    std::abort();
  }

  status = U_ZERO_ERROR;
  std::string skeleton(static_cast<std::size_t>(length), '\0');
  uspoof_getSkeletonUTF8(SpoofChecker(),
                         0,
                         normalized.data(),
                         static_cast<int32_t>(normalized.size()),
                         skeleton.data(),
                         length,
                         &status);
  if (U_FAILURE(status)) {
    LogUnicodeDebug("uspoof_getSkeletonUTF8 fill failed status=" +
                    std::to_string(status) + " normalized=\"" +
                    std::string(normalized) + "\"");
    std::abort();
  }
  return skeleton;
}

bool HasMixedIdentifierScripts(std::string_view normalized) {
  const icu::UnicodeString input =
      icu::UnicodeString::fromUTF8(icu::StringPiece(normalized.data(),
                                                    normalized.size()));
  std::optional<UScriptCode> first;
  for (int32_t index = 0; index < input.length();) {
    const UChar32 scalar = input.char32At(index);
    index = input.moveIndex32(index, 1);

    UErrorCode status = U_ZERO_ERROR;
    const UScriptCode script = uscript_getScript(scalar, &status);
    if (U_FAILURE(status)) {
      LogUnicodeDebug("uscript_getScript failed status=" +
                      std::to_string(status) + " scalar=" +
                      std::to_string(static_cast<std::uint32_t>(scalar)));
      std::abort();
    }
    if (script == USCRIPT_COMMON || script == USCRIPT_INHERITED) {
      continue;
    }
    if (!first.has_value()) {
      first = script;
      continue;
    }
    if (*first != script) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::string NFC(std::string_view s) {
  SpecDefsUnicode();
  // Fast path: ASCII is already in NFC form.
  if (IsAsciiText(s)) {
    return std::string(s);
  }

  constexpr std::string_view kUnicodeVersion = U_UNICODE_VERSION;
  static_assert(kUnicodeVersion == "15.0",
                "ICU must target Unicode 15.0.x to match the Cursive0 spec.");
  UErrorCode status = U_ZERO_ERROR;
  const icu::Normalizer2* nfc = icu::Normalizer2::getNFCInstance(status);
  if (U_FAILURE(status) || nfc == nullptr) {
    LogUnicodeDebug("Normalizer2::getNFCInstance failed status=" +
                    std::to_string(status));
    std::abort();
  }
  const icu::UnicodeString input =
      icu::UnicodeString::fromUTF8(icu::StringPiece(s.data(), s.size()));
  icu::UnicodeString normalized = nfc->normalize(input, status);
  if (U_FAILURE(status)) {
    LogUnicodeDebug("Normalizer2::normalize failed status=" +
                    std::to_string(status) + " input=\"" + std::string(s) +
                    "\"");
    std::abort();
  }
  std::string out;
  normalized.toUTF8String(out);
  return out;
}

std::string CaseFold(std::string_view s) {
  SpecDefsUnicode();
  // Fast path: ASCII case folding is a pure lowercase transform.
  if (IsAsciiText(s)) {
    return AsciiCaseFold(s);
  }

  constexpr std::string_view kUnicodeVersion = U_UNICODE_VERSION;
  static_assert(kUnicodeVersion == "15.0",
                "ICU must target Unicode 15.0.x to match the Cursive0 spec.");
  const icu::UnicodeString input =
      icu::UnicodeString::fromUTF8(icu::StringPiece(s.data(), s.size()));
  icu::UnicodeString folded = input;
  folded.foldCase(U_FOLD_CASE_DEFAULT);
  std::string out;
  folded.toUTF8String(out);
  return out;
}

IdentifierSecurityInfo AnalyzeIdentifierSecurity(std::string_view ident) {
  SpecDefsIdentifierSecurity();

  if (IsDebugEnabled("lex") || IsDebugEnabled("parse")) {
    LogUnicodeDebug("AnalyzeIdentifierSecurity ident=\"" + std::string(ident) +
                    "\"");
  }

  IdentifierSecurityInfo result;
  result.normalized = NFC(ident);
  result.skeleton = SkeletonForUtf8(result.normalized);
  result.mixed_script = HasMixedIdentifierScripts(result.normalized);
  return result;
}

static inline void SpecDefsIdentChars() {
  SPEC_DEF("IdentStart", "3.2.2");
  SPEC_DEF("IdentContinue", "3.2.2");
  SPEC_DEF("XID_Start", "3.2.2");
  SPEC_DEF("XID_Continue", "3.2.2");
  SPEC_DEF("NonCharacter", "3.2.2");
  SPEC_DEF("Sensitive", "3.2.2");
}

bool IsXidStart(UnicodeScalar c) {
  SpecDefsIdentChars();
  if (c > 0x10FFFF) {
    return false;
  }
  return u_hasBinaryProperty(static_cast<UChar32>(c), UCHAR_XID_START);
}

bool IsXidContinue(UnicodeScalar c) {
  SpecDefsIdentChars();
  if (c > 0x10FFFF) {
    return false;
  }
  return u_hasBinaryProperty(static_cast<UChar32>(c), UCHAR_XID_CONTINUE);
}

bool IsNonCharacter(UnicodeScalar c) {
  SpecDefsIdentChars();
  if (c > 0x10FFFF) {
    return false;
  }
  if (c >= 0xFDD0 && c <= 0xFDEF) {
    return true;
  }
  const std::uint32_t low = static_cast<std::uint32_t>(c) & 0xFFFFu;
  return low == 0xFFFEu || low == 0xFFFFu;
}

bool IsIdentStart(UnicodeScalar c) {
  SpecDefsIdentChars();
  return c == '_' || IsXidStart(c);
}

bool IsIdentContinue(UnicodeScalar c) {
  SpecDefsIdentChars();
  return c == '_' || IsXidContinue(c);
}

bool IsSensitive(UnicodeScalar c) {
  SpecDefsIdentChars();
  return (c >= 0x202Au && c <= 0x202Eu) || (c >= 0x2066u && c <= 0x2069u) ||
         c == 0x200Cu || c == 0x200Du;
}

namespace {

struct ByteSpan {
  std::size_t start = 0;
  std::size_t end = 0;
};

bool IsHexDigit(UnicodeScalar c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

unsigned int HexValue(UnicodeScalar c) {
  if (c >= '0' && c <= '9') {
    return static_cast<unsigned int>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return 10u + static_cast<unsigned int>(c - 'a');
  }
  return 10u + static_cast<unsigned int>(c - 'A');
}

bool IsUnicodeScalarValue(std::uint32_t value) {
  return UnicodeScalar::IsValue(value);
}

bool IsStringChar(UnicodeScalar c) {
  return c != '"' && c != '\\' && c != kLF;
}

bool IsCharContent(UnicodeScalar c) {
  return c != '\'' && c != '\\' && c != kLF;
}

std::optional<std::size_t> ScanEscape(const std::vector<UnicodeScalar>& scalars,
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
      std::size_t p = start + 3;
      std::uint32_t value = 0;
      std::size_t digits = 0;
      while (p < scalars.size() && IsHexDigit(scalars[p])) {
        if (digits == 6) {
          return std::nullopt;
        }
        value = (value << 4) | HexValue(scalars[p]);
        ++digits;
        ++p;
      }
      if (digits == 0) {
        return std::nullopt;
      }
      if (p >= scalars.size() || scalars[p] != '}') {
        return std::nullopt;
      }
      if (!IsUnicodeScalarValue(value)) {
        return std::nullopt;
      }
      return p + 1;
    }
    default:
      return std::nullopt;
  }
}

std::optional<std::size_t> ScanStringLiteral(
    const std::vector<UnicodeScalar>& scalars,
    std::size_t start) {
  if (start >= scalars.size() || scalars[start] != '"') {
    return std::nullopt;
  }
  std::size_t i = start + 1;
  while (i < scalars.size()) {
    const UnicodeScalar c = scalars[i];
    if (c == '"') {
      return i + 1;
    }
    if (c == kLF) {
      return std::nullopt;
    }
    if (c == '\\') {
      const auto escaped = ScanEscape(scalars, i);
      if (!escaped.has_value()) {
        return std::nullopt;
      }
      i = *escaped;
      continue;
    }
    if (!IsStringChar(c)) {
      return std::nullopt;
    }
    ++i;
  }
  return std::nullopt;
}

std::optional<std::size_t> ScanCharLiteral(
    const std::vector<UnicodeScalar>& scalars,
    std::size_t start) {
  if (start >= scalars.size() || scalars[start] != '\'') {
    return std::nullopt;
  }
  if (start + 1 >= scalars.size()) {
    return std::nullopt;
  }
  if (scalars[start + 1] == kLF) {
    return std::nullopt;
  }
  std::size_t i = start + 1;
  if (scalars[i] == '\\') {
    const auto escaped = ScanEscape(scalars, i);
    if (!escaped.has_value()) {
      return std::nullopt;
    }
    i = *escaped;
  } else {
    if (!IsCharContent(scalars[i])) {
      return std::nullopt;
    }
    i += 1;
  }
  if (i >= scalars.size() || scalars[i] != '\'') {
    return std::nullopt;
  }
  return i + 1;
}

std::vector<ByteSpan> LiteralSpan(
    const std::vector<UnicodeScalar>& scalars) {
  const auto offsets = Utf8Offsets(scalars);
  std::vector<ByteSpan> spans;
  std::size_t i = 0;
  while (i < scalars.size()) {
    std::optional<std::size_t> end;
    if (scalars[i] == '"') {
      end = ScanStringLiteral(scalars, i);
    } else if (scalars[i] == '\'') {
      end = ScanCharLiteral(scalars, i);
    }
    if (end.has_value()) {
      spans.push_back(ByteSpan{offsets[i], offsets[*end]});
      i = *end;
      continue;
    }
    ++i;
  }
  return spans;
}

bool ByteInLiteralSpan(std::size_t offset,
                       const std::vector<ByteSpan>& spans,
                       std::size_t* span_index) {
  while (*span_index < spans.size() && offset >= spans[*span_index].end) {
    ++(*span_index);
  }
  if (*span_index >= spans.size()) {
    return false;
  }
  return offset >= spans[*span_index].start &&
         offset < spans[*span_index].end;
}

}  // namespace

bool IsProhibited(UnicodeScalar c) {
  const bool is_cc = (c <= 0x1F) || (c >= 0x7F && c <= 0x9F);
  if (!is_cc) {
    return false;
  }
  return c != 0x09 && c != 0x0A && c != 0x0C && c != 0x0D;
}

std::optional<std::size_t> FirstProhibitedOutsideLiteral(
    const std::vector<UnicodeScalar>& scalars) {
  const auto offsets = Utf8Offsets(scalars);
  const auto spans = LiteralSpan(scalars);
  std::size_t span_index = 0;
  for (std::size_t i = 0; i < scalars.size(); ++i) {
    if (IsProhibited(scalars[i]) &&
        !ByteInLiteralSpan(offsets[i], spans, &span_index)) {
      return i;
    }
  }
  return std::nullopt;
}

bool NoProhibited(const std::vector<UnicodeScalar>& scalars) {
  SPEC_RULE("WF-Prohibited");
  return !FirstProhibitedOutsideLiteral(scalars).has_value();
}

}  // namespace cursive::core
