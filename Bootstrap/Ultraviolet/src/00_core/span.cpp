// =============================================================================
// MIGRATION MAPPING: span.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 1.6.1 "Source Locations and Spans" (lines 548-579)
//     - SourceLocation = <file, offset, line, column>
//     - Span = <file, start_offset, end_offset, start_line, start_col, end_line, end_col>
//     - SpanRange(sp) = [sp.start_offset, sp.end_offset)
//     - WF-Location rule: validates offset and computes location
//     - WF-Span rule: validates span bounds and computes line/col
//     - ClampSpan(S, s, e): clamps to file bounds
//       s' = min(s, S.byte_len)
//       e' = min(max(e, s'), S.byte_len)
//     - Span-Of rule: constructs span with clamped bounds
//   - Section 3.1.4 "Locate" (lines 1820-1825)
//     - L = S.line_starts
//     - o' = min(o, S.byte_len)
//     - k = max{ j | L[j] <= o' }
//     - Result: <file=S.path, offset=o', line=k+1, col=o'-L[k]+1>
//
// SOURCE FILE: ultraviolet-bootstrap/src/00_core/span.cpp
//   - Lines 1-68 (entire file)
//
// CONTENT TO MIGRATE:
//   - SpecDefsSpanTypes() (lines 9-12) [static inline]
//     Traces SourceLocation and Span definitions to spec
//   - SpanRange(sp) -> pair<size_t, size_t> (lines 14-17)
//     Returns [start_offset, end_offset) pair
//   - Locate(source, offset) -> SourceLocation (lines 19-38)
//     Computes line/column from byte offset:
//     - Clamps offset to byte_len
//     - Binary searches line_starts for line number
//     - Column = offset - line_start + 1 (1-based)
//   - ClampSpan(source, start, end) -> pair<size_t, size_t> (lines 40-48)
//     Clamps span bounds to file size
//   - SpanOf(source, start, end) -> Span (lines 50-66)
//     Constructs complete Span with line/column info
//
// DEPENDENCIES:
//   - ultraviolet/include/00_core/span.h (header)
//     - Span struct
//     - SourceLocation struct
//   - ultraviolet/include/00_core/source_text.h
//     - SourceFile struct (path, byte_len, line_starts)
//   - ultraviolet/include/00_core/assert_spec.h
//     - SPEC_DEF macro
//     - SPEC_RULE macro
//   - <algorithm> for std::min, std::max, std::upper_bound
//
// REFACTORING NOTES:
//   1. SPEC_DEF traces:
//      - "SourceLocation" -> "1.6.1"
//      - "Span" -> "1.6.1"
//      - "SpanRange" -> "1.6.1"
//      - "ClampSpan" -> "1.6.1"
//   2. SPEC_RULE traces:
//      - "WF-Location" in Locate()
//      - "Span-Of" and "WF-Span" in SpanOf()
//   3. Line numbers are 1-based (k + 1)
//   4. Column numbers are 1-based (offset - line_start + 1)
//   5. Binary search via upper_bound finds insertion point, then back up one
//   6. Empty line_starts handled: defaults to line 1, column 1
//   7. ClampSpan ensures end >= start after clamping
//
// =============================================================================

#include "00_core/span.h"

#include <algorithm>

#include "00_core/assert_spec.h"

namespace ultraviolet::core {

static inline void SpecDefsSpanTypes() {
  SPEC_DEF("SourceLocation", "1.6.1");
  SPEC_DEF("Span", "1.6.1");
}

std::pair<std::size_t, std::size_t> SpanRange(const Span& sp) {
  SPEC_DEF("SpanRange", "1.6.1");
  return {sp.start_offset, sp.end_offset};
}

SourceLocation Locate(const SourceFile& source, std::size_t offset) {
  SpecDefsSpanTypes();
  SPEC_RULE("WF-Location");
  const std::size_t o_prime = std::min(offset, source.byte_len);
  const auto& line_starts = source.line_starts;

  std::size_t k = 0;
  if (!line_starts.empty()) {
    auto it = std::upper_bound(line_starts.begin(), line_starts.end(), o_prime);
    if (it != line_starts.begin()) {
      k = static_cast<std::size_t>(it - line_starts.begin() - 1);
    }
  }

  const std::size_t line = k + 1;
  const std::size_t line_start = line_starts.empty() ? 0 : line_starts[k];
  const std::size_t col = (o_prime - line_start) + 1;

  return SourceLocation{source.path, o_prime, line, col};
}

std::pair<std::size_t, std::size_t> ClampSpan(
    const SourceFile& source,
    std::size_t start,
    std::size_t end) {
  SPEC_DEF("ClampSpan", "1.6.1");
  const std::size_t s = std::min(start, source.byte_len);
  const std::size_t e = std::min(std::max(end, s), source.byte_len);
  return {s, e};
}

Span SpanOf(const SourceFile& source, std::size_t start, std::size_t end) {
  SPEC_RULE("Span-Of");
  SPEC_RULE("WF-Span");
  const auto clamped = ClampSpan(source, start, end);
  const SourceLocation start_loc = Locate(source, clamped.first);
  const SourceLocation end_loc = Locate(source, clamped.second);

  Span sp;
  sp.file = source.path;
  sp.start_offset = clamped.first;
  sp.end_offset = clamped.second;
  sp.start_line = start_loc.line;
  sp.start_col = start_loc.column;
  sp.end_line = end_loc.line;
  sp.end_col = end_loc.column;
  return sp;
}

}  // namespace ultraviolet::core
