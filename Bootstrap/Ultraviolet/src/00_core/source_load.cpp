// =============================================================================
// MIGRATION MAPPING: source_load.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 3.1 "Source Loading and Normalization" (lines 1703-1999)
//   - Section 3.1.1 UTF-8 encoding/decoding (lines 1715-1736)
//     - UnicodeScalar: 0 <= u <= 0x10FFFF, excluding surrogates [0xD800, 0xDFFF]
//     - Utf8Len, EncodeUTF8, DecodeUTF8 algorithms
//   - Section 3.1.3 UTF-8 Decoding and BOM Handling (lines 1747-1781)
//     - Decode-Ok, Decode-Err rules
//     - StripBOM rules: Empty, None, Start, Embedded
//     - BOM = U+FEFF
//   - Section 3.1.4 Line Ending Normalization (lines 1783-1818)
//     - CR = U+000D, LF = U+000A
//     - Norm rules: Empty, CRLF, CR, LF, Other
//     - LineStarts, Utf8Offsets computation
//   - Section 3.1.5 Prohibited Code Points (lines 1827-1836)
//     - Prohibited(c): Cc category except {U+0009, U+000A, U+000C, U+000D}
//     - WF-Prohibited rule
//   - Section 3.1.8 Source Loading Pipeline (lines 1893-1956)
//     - Small-step: Start -> Sized -> Decoded -> BomStripped -> Normalized -> LineMapped -> Validated
//     - Big-step: LoadSource-Ok, LoadSource-Err
//   - Section 3.1.9 Diagnostic Spans (lines 1958-1993)
//     - SpanAtIndex, SpanAtLineStart for error locations
//     - Diagnostic codes: E-SRC-0101 (decode), E-SRC-0103 (embedded BOM), E-SRC-0104 (prohibited)
//     - Warning: W-SRC-0101 (leading BOM)
//
// SOURCE FILE: ultraviolet-bootstrap/src/00_core/source_load.cpp
//   - Lines 1-295 (entire file)
//
// CONTENT TO MIGRATE:
//   - IsContinuation(byte) -> bool (lines 21-23) [internal]
//     Checks UTF-8 continuation byte pattern (10xxxxxx)
//   - IsSurrogate(u) -> bool (lines 25-27) [internal]
//     Checks if codepoint is surrogate (D800-DFFF)
//   - DecodeUtf8Internal(bytes) -> optional<vector<UnicodeScalar>> (lines 29-106)
//     Full UTF-8 decoder with validation
//   - BuildSpanSource helper (lines 109-121) [internal]
//     Constructs SourceFile from decoded scalars
//   - FirstBomIndex(scalars) -> optional<size_t> (lines 123-131) [internal]
//     Finds first BOM occurrence in scalar sequence
//   - SpanAtIndex helper (lines 133-139) [internal]
//     Creates span for scalar at given index
//   - Decode(bytes) -> DecodeResult (lines 143-154)
//     Public decoder: returns scalars and ok flag
//   - StripBOM(scalars) -> StripBOMResult (lines 156-185)
//     Removes leading BOM, detects embedded BOMs
//   - NormalizeLF(scalars) -> vector<UnicodeScalar> (lines 187-223)
//     Converts CR, CRLF to LF
//   - LoadSource(path, bytes) -> SourceLoadResult (lines 225-293)
//     Complete pipeline: decode -> strip BOM -> normalize -> validate
//
// DEPENDENCIES:
//   - ultraviolet/include/00_core/source_load.h (header)
//     - DecodeResult, StripBOMResult, SourceLoadResult structs
//   - ultraviolet/include/00_core/source_text.h
//     - SourceFile struct, LineStarts(), EncodeUtf8(), Utf8Offsets()
//   - ultraviolet/include/00_core/span.h
//     - Span struct, SpanOf()
//   - ultraviolet/include/00_core/unicode.h
//     - UnicodeScalar type, kBOM, kCR, kLF constants
//     - NoProhibited(), FirstProhibitedOutsideLiteral()
//   - ultraviolet/include/00_core/diagnostics.h
//     - DiagnosticStream, Emit()
//   - ultraviolet/include/00_core/diagnostic_messages.h
//     - MakeDiagnosticById()
//   - ultraviolet/include/00_core/assert_spec.h
//     - SPEC_RULE macro
//
// REFACTORING NOTES:
//   1. SPEC_RULE traces map to spec sections:
//      - "Decode-Ok", "Decode-Err" -> 3.1.3
//      - "StripBOM-*" rules -> 3.1.3
//      - "Norm-*" rules -> 3.1.4
//      - "Step-*" rules -> 3.1.8
//      - "Span-*" rules -> 3.1.9
//      - "LoadSource-Ok", "LoadSource-Err" -> 3.1.8
//   2. UTF-8 decoder validates:
//      - Proper continuation bytes
//      - No overlong encodings
//      - No surrogates
//      - Valid codepoint range (0-0x10FFFF)
//   3. LoadSource emits diagnostics for all error conditions
//   4. W-SRC-0101 emitted even if LoadSource ultimately fails
//   5. NoProhibited check excludes literals (see unicode.cpp)
//
// =============================================================================

#include "00_core/source_load.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "00_core/span.h"
#include "00_core/source_text.h"
#include "00_core/unicode.h"

namespace ultraviolet::core {

namespace {

bool IsContinuation(std::uint8_t byte) {
  return (byte & 0xC0) == 0x80;
}

bool IsSurrogate(UnicodeScalar u) {
  return u >= 0xD800 && u <= 0xDFFF;
}

std::optional<std::vector<UnicodeScalar>> DecodeUtf8Internal(
    const std::vector<std::uint8_t>& bytes) {
  std::vector<UnicodeScalar> out;
  out.reserve(bytes.size());

  std::size_t i = 0;
  while (i < bytes.size()) {
    const std::uint8_t b0 = bytes[i];
    if (b0 <= 0x7F) {
      out.push_back(b0);
      ++i;
      continue;
    }

    if ((b0 & 0xE0) == 0xC0) {
      if (i + 1 >= bytes.size()) {
        return std::nullopt;
      }
      const std::uint8_t b1 = bytes[i + 1];
      if (!IsContinuation(b1)) {
        return std::nullopt;
      }
      const UnicodeScalar u =
          static_cast<UnicodeScalar>(((b0 & 0x1F) << 6) | (b1 & 0x3F));
      if (u < 0x80) {
        return std::nullopt;
      }
      out.push_back(u);
      i += 2;
      continue;
    }

    if ((b0 & 0xF0) == 0xE0) {
      if (i + 2 >= bytes.size()) {
        return std::nullopt;
      }
      const std::uint8_t b1 = bytes[i + 1];
      const std::uint8_t b2 = bytes[i + 2];
      if (!IsContinuation(b1) || !IsContinuation(b2)) {
        return std::nullopt;
      }
      const UnicodeScalar u =
          static_cast<UnicodeScalar>(((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F));
      if (u < 0x800 || IsSurrogate(u)) {
        return std::nullopt;
      }
      out.push_back(u);
      i += 3;
      continue;
    }

    if ((b0 & 0xF8) == 0xF0) {
      if (i + 3 >= bytes.size()) {
        return std::nullopt;
      }
      const std::uint8_t b1 = bytes[i + 1];
      const std::uint8_t b2 = bytes[i + 2];
      const std::uint8_t b3 = bytes[i + 3];
      if (!IsContinuation(b1) || !IsContinuation(b2) || !IsContinuation(b3)) {
        return std::nullopt;
      }
      const UnicodeScalar u = static_cast<UnicodeScalar>(((b0 & 0x07) << 18)
                                                         | ((b1 & 0x3F) << 12)
                                                         | ((b2 & 0x3F) << 6)
                                                         | (b3 & 0x3F));
      if (u < 0x10000 || u > 0x10FFFF) {
        return std::nullopt;
      }
      out.push_back(u);
      i += 4;
      continue;
    }

    return std::nullopt;
  }

  return out;
}


SourceFile BuildSpanSource(std::string_view path,
                           const std::vector<std::uint8_t>& bytes,
                           std::vector<UnicodeScalar> scalars) {
  SourceFile source;
  source.path = std::string(path);
  source.bytes = bytes;
  source.scalars = std::move(scalars);
  source.text = EncodeUtf8(source.scalars);
  source.byte_len = source.text.size();
  source.line_starts = LineStarts(source.scalars);
  source.line_count = source.line_starts.size();
  return source;
}

struct SourceLoadSpanTemp {
  std::string path;
  std::vector<std::uint8_t> bytes;
  std::string text;
  std::size_t byte_len = 0;
  std::vector<std::size_t> line_starts;
  std::size_t line_count = 0;
};

SourceLoadSpanTemp BuildSpanTemp(const SourceFile& source) {
  SourceLoadSpanTemp temp;
  temp.path = source.path;
  temp.bytes = source.bytes;
  temp.text = source.text;
  temp.byte_len = source.byte_len;
  temp.line_starts = source.line_starts;
  temp.line_count = source.line_count;
  return temp;
}

SourceFile ToSpanSource(const SourceLoadSpanTemp& source) {
  SourceFile span_source;
  span_source.path = source.path;
  span_source.bytes = source.bytes;
  span_source.text = source.text;
  span_source.byte_len = source.byte_len;
  span_source.line_starts = source.line_starts;
  span_source.line_count = source.line_count;
  return span_source;
}

Span SpanOfTemp(const SourceLoadSpanTemp& source,
                std::size_t start,
                std::size_t end) {
  return SpanOf(ToSpanSource(source), start, end);
}

std::optional<std::size_t> FirstBomIndex(
    const std::vector<UnicodeScalar>& scalars) {
  for (std::size_t i = 0; i < scalars.size(); ++i) {
    if (scalars[i] == kBOM) {
      return i;
    }
  }
  return std::nullopt;
}

Span SpanAtIndex(const SourceFile& source,
                 const std::vector<std::size_t>& offsets,
                 std::size_t index) {
  const std::size_t start = offsets[index];
  const std::size_t end = offsets[index + 1];
  return SpanOf(source, start, end);
}

Span SpanAtIndex(const SourceLoadSpanTemp& source,
                 const std::vector<std::size_t>& offsets,
                 std::size_t index) {
  const std::size_t start = offsets[index];
  const std::size_t end = offsets[index + 1];
  return SpanOfTemp(source, start, end);
}

Span SpanAtLineStart(const SourceLoadSpanTemp& source, std::size_t index) {
  const std::size_t start =
      index < source.line_starts.size() ? source.line_starts[index] : source.byte_len;
  const std::size_t end =
      start < source.byte_len ? std::min(start + 1, source.byte_len) : source.byte_len;
  return SpanOfTemp(source, start, end);
}

SourceLoadSizedState StepSize(SourceLoadStartState start) {
  SPEC_RULE("Step-Size");
  return SourceLoadSizedState{std::move(start.path), std::move(start.bytes)};
}

SourceLoadDecodedState StepDecode(SourceLoadSizedState sized, Scalars scalars) {
  SPEC_RULE("Step-Decode");
  return SourceLoadDecodedState{
      std::move(sized.path),
      std::move(sized.bytes),
      std::move(scalars)};
}

SourceLoadErrorState StepDecodeErr(SourceLoadSizedState sized) {
  static_cast<void>(sized);
  SPEC_RULE("Step-Decode-Err");
  return SourceLoadErrorState{"E-SRC-0101"};
}

SourceLoadBomStrippedState StepBOM(SourceLoadDecodedState decoded,
                                   StripBOMResult stripped) {
  SPEC_RULE("Step-BOM");
  return SourceLoadBomStrippedState{
      std::move(decoded.path),
      std::move(decoded.bytes),
      std::move(stripped.scalars),
      stripped.had_bom,
      stripped.embedded_index};
}

SourceLoadNormalizedState StepNorm(SourceLoadBomStrippedState stripped,
                                   Scalars scalars) {
  SPEC_RULE("Step-Norm");
  return SourceLoadNormalizedState{
      std::move(stripped.path),
      std::move(stripped.bytes),
      std::move(scalars),
      stripped.j};
}

SourceLoadErrorState StepEmbeddedBOMErr(SourceLoadNormalizedState normalized) {
  static_cast<void>(normalized);
  SPEC_RULE("Step-EmbeddedBOM-Err");
  return SourceLoadErrorState{"E-SRC-0103"};
}

SourceLoadLineMappedState StepLineMap(SourceLoadNormalizedState normalized) {
  std::vector<std::size_t> line_starts = LineStarts(normalized.scalars);
  SPEC_RULE("Step-LineMap");
  return SourceLoadLineMappedState{
      std::move(normalized.path),
      std::move(normalized.bytes),
      std::move(normalized.scalars),
      std::move(line_starts)};
}

SourceLoadValidatedState StepProhibited(SourceLoadLineMappedState line_mapped) {
  SPEC_RULE("Step-Prohibited");
  SourceFile source = BuildSpanSource(
      line_mapped.path,
      line_mapped.bytes,
      std::move(line_mapped.scalars));
  source.line_starts = std::move(line_mapped.line_starts);
  source.line_count = source.line_starts.size();
  return SourceLoadValidatedState{std::move(source)};
}

SourceLoadErrorState StepProhibitedErr(SourceLoadLineMappedState line_mapped) {
  static_cast<void>(line_mapped);
  SPEC_RULE("Step-Prohibited-Err");
  return SourceLoadErrorState{"E-SRC-0104"};
}

}  // namespace

DecodeResult Decode(const std::vector<std::uint8_t>& bytes) {
  DecodeResult result;
  auto scalars = DecodeUtf8Internal(bytes);
  if (!scalars) {
    SPEC_RULE("Decode-Err");
    return result;
  }
  SPEC_RULE("Decode-Ok");
  result.scalars = std::move(*scalars);
  result.ok = true;
  return result;
}

StripBOMResult StripBOM(const std::vector<UnicodeScalar>& scalars) {
  StripBOMResult result;
  if (scalars.empty()) {
    SPEC_RULE("StripBOM-Empty");
    return result;
  }

  const bool has_lead = scalars.front() == kBOM;
  result.had_bom = has_lead;
  if (has_lead) {
    result.scalars.assign(scalars.begin() + 1, scalars.end());
  } else {
    result.scalars = scalars;
  }

  for (std::size_t i = 0; i < result.scalars.size(); ++i) {
    if (result.scalars[i] == kBOM) {
      result.embedded_index = i;
      SPEC_RULE("StripBOM-Embedded");
      return result;
    }
  }

  if (has_lead) {
    SPEC_RULE("StripBOM-Start");
  } else {
    SPEC_RULE("StripBOM-None");
  }
  return result;
}

std::vector<UnicodeScalar> NormalizeLF(const std::vector<UnicodeScalar>& scalars) {
  if (scalars.empty()) {
    SPEC_RULE("Norm-Empty");
    return {};
  }

  std::vector<UnicodeScalar> out;
  out.reserve(scalars.size());

  std::size_t i = 0;
  while (i < scalars.size()) {
    const UnicodeScalar c = scalars[i];
    if (c == kCR) {
      if (i + 1 < scalars.size() && scalars[i + 1] == kLF) {
        SPEC_RULE("Norm-CRLF");
        out.push_back(kLF);
        i += 2;
      } else {
        SPEC_RULE("Norm-CR");
        out.push_back(kLF);
        ++i;
      }
      continue;
    }
    if (c == kLF) {
      SPEC_RULE("Norm-LF");
      out.push_back(kLF);
      ++i;
      continue;
    }
    SPEC_RULE("Norm-Other");
    out.push_back(c);
    ++i;
  }

  return out;
}

SourceLoadResult LoadSource(std::string_view path,
                            const std::vector<std::uint8_t>& bytes) {
  SourceLoadResult result;
  SourceLoadStartState start{std::string(path), bytes};
  SourceLoadSizedState sized = StepSize(std::move(start));

  const DecodeResult decode_result = Decode(sized.bytes);
  if (!decode_result.ok) {
    const SourceLoadErrorState error = StepDecodeErr(std::move(sized));
    SPEC_RULE("NoSpan-Decode");
    if (auto diag = MakeDiagnosticById(error.code)) {
      Emit(result.diags, *diag);
    }
    SPEC_RULE("LoadSource-Err");
    return result;
  }
  SourceLoadDecodedState decoded =
      StepDecode(std::move(sized), std::move(decode_result.scalars));

  StripBOMResult strip_result = StripBOM(decoded.scalars);
  SourceLoadBomStrippedState stripped =
      StepBOM(std::move(decoded), std::move(strip_result));

  const bool had_bom = stripped.had_bom;
  Scalars normalized_outside_identifiers =
      NormalizeOutsideIdentifiers(stripped.scalars);
  Scalars normalized_scalars = NormalizeLF(normalized_outside_identifiers);
  SourceLoadNormalizedState normalized =
      StepNorm(std::move(stripped), std::move(normalized_scalars));

  SourceFile source =
      BuildSpanSource(normalized.path, normalized.bytes, normalized.scalars);
  const SourceLoadSpanTemp span_source = BuildSpanTemp(source);
  std::vector<std::size_t> offsets = Utf8Offsets(source.scalars);

  if (had_bom) {
    SPEC_RULE("Span-BOM-Warn");
    const std::size_t end = std::min<std::size_t>(1, span_source.byte_len);
    if (auto diag = MakeDiagnosticById("W-SRC-0101", SpanOfTemp(span_source, 0, end))) {
      Emit(result.diags, *diag);
    }
  }

  if (normalized.j.has_value()) {
    const SourceLoadErrorState error = StepEmbeddedBOMErr(std::move(normalized));
    std::size_t bom_index = 0;
    if (auto index = FirstBomIndex(source.scalars)) {
      bom_index = *index;
    }
    SPEC_RULE("Span-BOM-Embedded");
    if (auto diag = MakeDiagnosticById(error.code, SpanAtIndex(span_source, offsets, bom_index))) {
      Emit(result.diags, *diag);
    }
    SPEC_RULE("LoadSource-Err");
    return result;
  }

  SourceLoadLineMappedState line_mapped = StepLineMap(std::move(normalized));
  source = BuildSpanSource(
      line_mapped.path,
      line_mapped.bytes,
      line_mapped.scalars);
  source.line_starts = line_mapped.line_starts;
  source.line_count = source.line_starts.size();
  offsets = Utf8Offsets(source.scalars);

  if (!NoProhibited(source.scalars)) {
    const SourceLoadErrorState error = StepProhibitedErr(std::move(line_mapped));
    std::size_t prohibited_index = 0;
    if (auto index = FirstProhibitedOutsideLiteral(source.scalars)) {
      prohibited_index = *index;
    }
    SPEC_RULE("Span-Prohibited");
    if (auto diag = MakeDiagnosticById(error.code, SpanAtIndex(span_source, offsets, prohibited_index))) {
      Emit(result.diags, *diag);
    }
    SPEC_RULE("LoadSource-Err");
    return result;
  }
  SourceLoadValidatedState validated = StepProhibited(std::move(line_mapped));
  result.source = std::move(validated.source);
  SPEC_RULE("LoadSource-Ok");
  return result;
}

}  // namespace ultraviolet::core
