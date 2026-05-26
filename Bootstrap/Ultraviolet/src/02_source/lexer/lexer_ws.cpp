// =============================================================================
// MIGRATION MAPPING: lexer_ws.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 3.2.2 - Character Classes (Whitespace, LineFeed) (lines 2052-2058)
//   Section 3.2.5 - Comment and Whitespace Scanning (lines 2119-2169)
//
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/lexer_ws.cpp
//   Lines 1-171 (entire file)
//
// =============================================================================

#include "02_source/lexer/lexer.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/source_text.h"
#include "00_core/span.h"

namespace ultraviolet::lexer {

namespace {

// Check if two consecutive scalars match given characters.
// Used for "//" and "/*" detection.
bool Match2(const std::vector<core::UnicodeScalar>& scalars,
            std::size_t i,
            core::UnicodeScalar a,
            core::UnicodeScalar b) {
  return i + 1 < scalars.size() && scalars[i] == a && scalars[i + 1] == b;
}

// Implements spec DocMarker from lines 2026-2029:
// DocMarker(T, i) =
//   LineDoc   if T[i..i+3] = "///"
//   ModuleDoc if T[i..i+3] = "//!"
//   bottom    otherwise
std::optional<DocKind> DocMarker(const std::vector<core::UnicodeScalar>& scalars,
                                 std::size_t i) {
  if (i + 2 >= scalars.size()) {
    return std::nullopt;
  }
  if (scalars[i] == '/' && scalars[i + 1] == '/' && scalars[i + 2] == '/') {
    return DocKind::LineDoc;
  }
  if (scalars[i] == '/' && scalars[i + 1] == '/' && scalars[i + 2] == '!') {
    return DocKind::ModuleDoc;
  }
  return std::nullopt;
}

// Implements spec DocBody from line 2025:
// DocBody(T, i, j) = StripLeadingSpace(T[i+3..j))
// Where StripLeadingSpace removes leading U+0020 if present.
std::string DocBody(const std::vector<core::UnicodeScalar>& scalars,
                    std::size_t i,
                    std::size_t j) {
  if (j <= i + 3 || i + 3 > scalars.size()) {
    return std::string();
  }
  const std::size_t end = std::min(j, scalars.size());
  std::vector<core::UnicodeScalar> body;
  body.reserve(end - (i + 3));
  body.insert(body.end(), scalars.begin() + i + 3, scalars.begin() + end);
  // StripLeadingSpace: remove single leading space (U+0020) if present.
  if (!body.empty() && body.front() == 0x20) {
    body.erase(body.begin());
  }
  return core::EncodeUtf8(body);
}

// Implements spec SpanOfText(S, i, j) from line 2044.
// Convert scalar indices to Span using Utf8Offsets.
core::Span SpanOfText(const core::SourceFile& source,
                      const std::vector<core::UnicodeScalar>& scalars,
                      std::size_t i,
                      std::size_t j) {
  const auto offsets = core::Utf8Offsets(scalars);
  const std::size_t start = offsets[i];
  const std::size_t end = offsets[j];
  return core::SpanOf(source, start, end);
}

}  // namespace

// Implements spec Whitespace from line 2054:
// Whitespace(c) = c in {U+0020, U+0009, U+000C}
// (space, tab, form feed)
bool IsWhitespace(core::UnicodeScalar c) {
  return c == 0x20 || c == 0x09 || c == 0x0C;
}

// Implements spec LineFeed from line 2058:
// LineFeed(c) = c = U+000A
bool IsLineFeed(core::UnicodeScalar c) {
  return c == core::kLF;
}

// Implements spec (Scan-Line-Comment) from lines 2125-2128:
//   T[i] = '/' AND T[i+1] = '/'
//   j = min{p | i <= p AND (p = |T| OR T[p] = LF)}
//   => ScanLineComment(T, i) => j
//
// Scans from "//" to end of line (before LF) or end of file.
// Returns: {ok, next, range}
CommentScanResult ScanLineComment(const core::SourceFile& source,
                                  std::size_t start) {
  SPEC_RULE("Scan-Line-Comment");
  CommentScanResult result;
  const auto& scalars = source.scalars;
  const std::size_t n = scalars.size();
  if (!Match2(scalars, start, '/', '/')) {
    result.ok = false;
    result.next = start;
    result.range = ScalarRange{start, start};
    return result;
  }
  std::size_t j = n;
  for (std::size_t p = start; p < n; ++p) {
    if (scalars[p] == core::kLF) {
      j = p;
      break;
    }
  }
  result.ok = true;
  result.next = j;
  result.range = ScalarRange{start, j};
  return result;
}

// Implements spec (Doc-Comment) from lines 2134-2137:
//   ScanLineComment(T, i) => j
//   kind = DocMarker(T, i) != bottom
//   body = DocBody(T, i, j)
//   => DocComment(T, i) => <kind, body, span>
//
// Extends ScanLineComment to extract doc comment metadata.
// Returns: CommentScanResult with optional DocComment.
CommentScanResult ScanDocComment(const core::SourceFile& source,
                                 std::size_t start) {
  SPEC_RULE("Doc-Comment");
  CommentScanResult result = ScanLineComment(source, start);
  const auto& scalars = source.scalars;
  const auto kind = DocMarker(scalars, start);
  if (!kind.has_value()) {
    result.ok = false;
    return result;
  }
  DocComment doc;
  doc.kind = *kind;
  doc.text = DocBody(scalars, start, result.next);
  doc.span = SpanOfText(source, scalars, start, result.next);
  result.doc = doc;
  return result;
}

// Implements spec block comment rules from lines 2143-2168:
//
// (Block-Start) line 2145-2148:
//   T[i..i+2] = "/*" => depth += 1, i += 2
//
// (Block-End) line 2150-2153:
//   T[i..i+2] = "*/" AND depth > 1 => depth -= 1, i += 2
//
// (Block-Done) line 2155-2158:
//   T[i..i+2] = "*/" AND depth = 1 => return i+2
//
// (Block-Step) line 2160-2163:
//   T[i..i+2] != "/*" AND T[i..i+2] != "*/" => i += 1
//
// (Block-Comment-Unterminated) line 2165-2168:
//   Reached end of file with depth > 0 => E-SRC-0306
//
// Key: Block comments NEST in Ultraviolet (unlike C).
// /* outer /* inner */ still open */
CommentScanResult ScanBlockComment(const core::SourceFile& source,
                                   std::size_t start) {
  SPEC_RULE("Block-Start");
  SPEC_RULE("Block-End");
  SPEC_RULE("Block-Done");
  SPEC_RULE("Block-Step");
  CommentScanResult result;
  const auto& scalars = source.scalars;
  const std::size_t n = scalars.size();
  BlockState state = BlockScanState{start, 0, start};

  if (!Match2(scalars, start, '/', '*')) {
    result.ok = false;
    return result;
  }

  auto* scan = std::get_if<BlockScanState>(&state);
  scan->depth = 1;
  scan->index = start + 2;

  while ((scan = std::get_if<BlockScanState>(&state)) != nullptr &&
         scan->index < n) {
    if (Match2(scalars, scan->index, '/', '*')) {
      ++scan->depth;
      scan->index += 2;
      continue;
    }
    if (Match2(scalars, scan->index, '*', '/')) {
      if (scan->depth == 1) {
        state = BlockDoneState{scan->index + 2};
        break;
      }
      --scan->depth;
      scan->index += 2;
      continue;
    }
    ++scan->index;
  }

  if (const auto* done = std::get_if<BlockDoneState>(&state)) {
    result.ok = true;
    result.next = done->next;
    result.range = ScalarRange{start, done->next};
    return result;
  }

  SPEC_RULE("Block-Comment-Unterminated");
  const auto* unterminated = std::get_if<BlockScanState>(&state);
  const std::size_t start_index =
      unterminated != nullptr ? unterminated->start_index : start;
  const auto span = SpanOfText(source, scalars, start_index, start_index + 2);
  const auto diag = core::MakeDiagnosticById("E-SRC-0306", span);
  if (diag.has_value()) {
    core::Emit(result.diags, *diag);
  }
  result.ok = false;
  result.next = n;
  result.range = ScalarRange{start_index, n};
  return result;
}

bool TokenInComment(const core::SourceFile& source, const Token& token) {
  const auto range = TokenRange(source, token);
  if (!range.has_value()) {
    return false;
  }

  const auto [i, j] = *range;
  const auto& scalars = source.scalars;
  std::size_t p = 0;
  while (p < scalars.size()) {
    if (Match2(scalars, p, '/', '/')) {
      CommentScanResult line = ScanLineComment(source, p);
      if (line.ok && line.next > p) {
        if (p <= i && j <= line.next) {
          return true;
        }
        p = line.next;
        continue;
      }
    }

    if (Match2(scalars, p, '/', '*')) {
      CommentScanResult block = ScanBlockComment(source, p);
      if (block.next > p) {
        if (p <= i && j <= block.next) {
          return true;
        }
        p = block.next;
        continue;
      }
    }

    ++p;
  }

  return false;
}

}  // namespace ultraviolet::lexer
