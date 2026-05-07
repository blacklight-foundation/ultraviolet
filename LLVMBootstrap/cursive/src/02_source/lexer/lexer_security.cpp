// =============================================================================
// MIGRATION MAPPING: lexer_security.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 3.2.10 - Lexical Security (lines 2446-2498)
//
// SOURCE FILE: cursive-bootstrap/src/02_syntax/lexer_security.cpp
//   Lines 1-142 (entire file)
//
// DEPENDENCIES:
//   - cursive/include/02_source/lexer.h (LexSecureResult, Token)
//   - cursive/src/00_core/unicode.cpp (Utf8Offsets)
//   - cursive/src/00_core/span.cpp (SpanOf, SpanRange)
//   - cursive/src/00_core/diagnostic_messages.cpp (MakeDiagnostic, Emit)
//
// =============================================================================
// CONTENT TO MIGRATE:
// =============================================================================
//
// INTERNAL HELPERS (anonymous namespace, source lines 14-98):
//
// 1. SpanOfText() (lines 16-23)
//    Convert scalar indices to Span
//
// 2. IsLBrace(), IsRBrace() (lines 25-31)
//    Implements spec predicates from lines 2464-2465:
//    IsLBrace(t) = t.kind = Punctuator("{")
//    IsRBrace(t) = t.kind = Punctuator("}")
//
// 3. NextNonNewline() (lines 33-41)
//    Implements spec NextNonNewline from lines 2467-2468:
//    NextNonNewline(K, i) = min{j | j >= i AND K[j].kind != Newline}
//
// 4. MatchBrace() (lines 43-57)
//    Implements spec MatchBrace from lines 2470-2473:
//    Find matching closing brace using depth counting
//    Balance(K, j, m) counts open minus close braces
//
// 5. SpanFrom() (lines 59-65)
//    Construct span from two tokens (start to end)
//    Implements spec SpanFrom from line 2475
//
// 6. ComputeUnsafeSpans() (lines 67-85)
//    Implements spec UnsafeSpans from line 2477:
//    UnsafeSpans(K) = {SpanFrom(K[j], K[k]) |
//      K[i].kind = Keyword("unsafe"),
//      j = NextNonNewline(K, i+1),
//      K[j].kind = Punctuator("{"),
//      k = MatchBrace(K, j),
//      k != bottom}
//
//    Algorithm:
//    - Scan for "unsafe" keyword
//    - Find next non-newline token (must be "{")
//    - Find matching "}"
//    - Record span from "{" to "}"
//
// 7. UnsafeAtByte() (lines 87-96)
//    Implements spec UnsafeAtByte from line 2479:
//    UnsafeAtByte(b) = exists sp in UnsafeSpans(K). b in SpanRange(sp)
//
// MAIN FUNCTIONS:
//
// 8. UnsafeSpans() (lines 100-102)
//    Public wrapper for ComputeUnsafeSpans
//
// 9. LexSecure() (lines 104-138)
//    Implements spec rules from 3.2.10:
//
//    (LexSecure-Err) - spec lines 2486-2489:
//      If any sensitive position is NOT inside unsafe block:
//        Emit E-SRC-0308 at that position
//        Return error
//
//    (LexSecure-Warn) - spec lines 2491-2494:
//      If all sensitive positions ARE inside unsafe blocks:
//        Emit W-SRC-0308 for each (warning only)
//        Return ok
//
//    Algorithm:
//    a. If no sensitive positions, return ok
//    b. Compute unsafe block spans from token stream
//    c. For each sensitive position:
//       - If NOT in unsafe span: error E-SRC-0308, return fail
//    d. For all sensitive positions (all in unsafe):
//       - Emit warning W-SRC-0308 for each
//    e. Return ok
//
// =============================================================================
// SPEC DEFINITIONS:
// =============================================================================
//
// Sensitive characters (line 2084):
//   Sensitive(c) = c in {U+202A...U+202E, U+2066...U+2069, U+200C, U+200D}
//   These are bidirectional control characters and zero-width joiners
//   (Trojan Source attack vectors)
//
// Unsafe span mode (line 2481):
//   UnsafeSpanMode = TokenOnly
//   Only token-level detection, not AST-level
//
// LexSecureErrSpan (line 2497):
//   Error span is single character at sensitive position
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
//
// - Current implementation uses TokenOnly mode as spec requires
// - Consider caching UnsafeSpans computation if called multiple times
// - SpanRange returns (start_offset, end_offset) pair for range check
//
// - The spec allows sensitive chars ONLY inside unsafe {} blocks
// - Even inside unsafe, a warning is emitted (security audit trail)
//
// - Sensitive character list is security-critical:
//   U+202A-U+202E: Bidirectional embedding controls
//   U+2066-U+2069: Bidirectional isolate controls
//   U+200C: Zero-width non-joiner
//   U+200D: Zero-width joiner
//
// - These characters can cause "Trojan Source" attacks where code
//   visually appears different from its actual semantics
//
// =============================================================================

#include "02_source/lexer/lexer.h"

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/process_config.h"
#include "00_core/source_text.h"
#include "00_core/span.h"
#include "00_core/unicode.h"

namespace cursive::lexer {

namespace {

core::Span SpanOfText(const core::SourceFile& source,
                      const std::vector<std::size_t>& offsets,
                      std::size_t i,
                      std::size_t j) {
  const std::size_t start = offsets[i];
  const std::size_t end = offsets[j];
  return core::SpanOf(source, start, end);
}

bool IsLBrace(const Token& tok) {
  return tok.kind == TokenKind::Punctuator && tok.lexeme == "{";
}

bool IsRBrace(const Token& tok) {
  return tok.kind == TokenKind::Punctuator && tok.lexeme == "}";
}

std::optional<std::size_t> NextNonNewline(const std::vector<Token>& tokens,
                                          std::size_t index) {
  for (std::size_t i = index; i < tokens.size(); ++i) {
    if (tokens[i].kind != TokenKind::Newline) {
      return i;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> MatchBrace(const std::vector<Token>& tokens,
                                      std::size_t open_index) {
  int depth = 0;
  for (std::size_t i = open_index; i < tokens.size(); ++i) {
    if (IsLBrace(tokens[i])) {
      ++depth;
    } else if (IsRBrace(tokens[i])) {
      --depth;
      if (depth == 0 && i > open_index) {
        return i;
      }
    }
  }
  return std::nullopt;
}

core::Span SpanFrom(const Token& start, const Token& end) {
  core::Span span = start.span;
  span.end_offset = end.span.end_offset;
  span.end_line = end.span.end_line;
  span.end_col = end.span.end_col;
  return span;
}

std::vector<core::Span> ComputeUnsafeSpans(const std::vector<Token>& tokens) {
  std::vector<core::Span> spans;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const Token& tok = tokens[i];
    if (tok.kind != TokenKind::Keyword || tok.lexeme != "unsafe") {
      continue;
    }
    const auto next = NextNonNewline(tokens, i + 1);
    if (!next.has_value() || !IsLBrace(tokens[*next])) {
      continue;
    }
    const auto match = MatchBrace(tokens, *next);
    if (!match.has_value()) {
      continue;
    }
    spans.push_back(SpanFrom(tokens[*next], tokens[*match]));
  }
  return spans;
}

bool UnsafeAtByte(std::size_t offset,
                  const std::vector<core::Span>& spans) {
  for (const auto& sp : spans) {
    const auto range = core::SpanRange(sp);
    if (offset >= range.first && offset < range.second) {
      return true;
    }
  }
  return false;
}

struct SeenIdentifier {
  std::string normalized;
};

bool MatchCommentPrefix(const std::vector<core::UnicodeScalar>& scalars,
                        std::size_t start,
                        core::UnicodeScalar a,
                        core::UnicodeScalar b) {
  return start + 1 < scalars.size() && scalars[start] == a &&
         scalars[start + 1] == b;
}

}  // namespace

std::vector<core::Span> UnsafeSpans(const std::vector<Token>& tokens) {
  return ComputeUnsafeSpans(tokens);
}

std::vector<std::size_t> LexSensitivePos(const core::SourceFile& source) {
  const auto& scalars = source.scalars;
  std::vector<std::size_t> sensitive;
  sensitive.reserve(scalars.size());

  std::size_t i = 0;
  while (i < scalars.size()) {
    if (MatchCommentPrefix(scalars, i, '/', '/')) {
      CommentScanResult line = ScanLineComment(source, i);
      if (line.ok && line.next > i) {
        i = line.next;
        continue;
      }
    }

    if (MatchCommentPrefix(scalars, i, '/', '*')) {
      CommentScanResult block = ScanBlockComment(source, i);
      if (block.next > i) {
        i = block.next;
        continue;
      }
    }

    if (scalars[i] == '"') {
      LiteralScanResult lit = ScanStringLiteral(source, i);
      if (lit.ok || lit.next > i) {
        i = lit.next;
        continue;
      }
    }

    if (scalars[i] == '\'') {
      LiteralScanResult lit = ScanCharLiteral(source, i);
      if (lit.ok || lit.next > i) {
        i = lit.next;
        continue;
      }
    }

    if (core::IsSensitive(scalars[i])) {
      sensitive.push_back(i);
    }
    ++i;
  }

  return sensitive;
}

LexSecureResult LexSecure(const core::SourceFile& source,
                          const std::vector<Token>& tokens,
                          const std::vector<std::size_t>& sensitive) {
  SPEC_RULE("LexSecure-Err");
  SPEC_RULE("LexSecure-Warn");

  LexSecureResult result;
  if (sensitive.empty()) {
    return result;
  }

  const auto offsets = core::Utf8Offsets(source.scalars);
  const std::vector<core::Span> unsafe_spans = UnsafeSpans(tokens);
  for (std::size_t p : sensitive) {
    if (!UnsafeAtByte(offsets[p], unsafe_spans)) {
      const core::Span span = SpanOfText(source, offsets, p, p + 1);
      const auto diag = core::MakeDiagnosticById("E-SRC-0308", span);
      if (diag.has_value()) {
        core::Emit(result.diags, *diag);
      }
      result.ok = false;
      return result;
    }
  }

  for (std::size_t p : sensitive) {
    const core::Span span = SpanOfText(source, offsets, p, p + 1);
    const auto diag = core::MakeDiagnosticById("W-SRC-0308", span);
    if (diag.has_value()) {
      core::Emit(result.diags, *diag);
    }
  }

  return result;
}

LexSecureResult ConfusableCheck(const core::SourceFile& source,
                                const std::vector<Token>& tokens) {
  (void)source;

  LexSecureResult result;
  std::unordered_map<std::string, SeenIdentifier> seen_by_skeleton;
  seen_by_skeleton.reserve(tokens.size());

  for (const Token& tok : tokens) {
    if (tok.kind != TokenKind::Identifier) {
      continue;
    }

    if (core::IsDebugEnabled("lex") || core::IsDebugEnabled("parse")) {
      std::cerr << "[cursive] unicode: ConfusableCheck token=\"" << tok.lexeme
                << "\"\n";
    }

    const core::IdentifierSecurityInfo security =
        core::AnalyzeIdentifierSecurity(tok.lexeme);
    if (security.mixed_script) {
      SPEC_RULE("MixedScript-Err");
      const auto diag = core::MakeDiagnosticById("E-SRC-0311", tok.span);
      if (diag.has_value()) {
        core::Emit(result.diags, *diag);
      }
      result.ok = false;
      return result;
    }

    const auto seen = seen_by_skeleton.find(security.skeleton);
    if (seen != seen_by_skeleton.end() &&
        seen->second.normalized != security.normalized) {
      SPEC_RULE("Confusable-Err");
      const auto diag = core::MakeDiagnosticById("E-SRC-0310", tok.span);
      if (diag.has_value()) {
        core::Emit(result.diags, *diag);
      }
      result.ok = false;
      return result;
    }

    seen_by_skeleton.emplace(security.skeleton,
                             SeenIdentifier{security.normalized});
  }

  return result;
}

}  // namespace cursive::lexer
