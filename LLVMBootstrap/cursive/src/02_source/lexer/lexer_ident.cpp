// =============================================================================
// MIGRATION MAPPING: lexer_ident.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 3.2.2 - Character Classes (lines 2048-2085)
//   Section 3.2.3 - Reserved Lexemes (lines 2086-2106)
//   Section 3.2.7 - Identifier and Keyword Lexing (lines 2365-2393)
//
// SOURCE FILE: cursive-bootstrap/src/02_syntax/lexer_ident.cpp
//   Lines 1-75 (entire file)
//
// DEPENDENCIES:
//   - cursive/include/02_source/lexer.h (IdentScanResult)
//   - cursive/src/00_core/keywords.cpp (IsKeyword)
//   - cursive/src/00_core/unicode.cpp (IsIdentStart, IsIdentContinue, IsNonCharacter)
//   - cursive/src/00_core/diagnostic_messages.cpp (MakeDiagnostic, Emit)
//   - cursive/src/00_core/span.cpp (SpanOf)
//
// =============================================================================
// CONTENT TO MIGRATE:
// =============================================================================
//
// INTERNAL HELPERS (anonymous namespace, source lines 16-22):
//
// 1. IsKeyword() - Delegate to core::IsKeyword (lines 18-20)
//
// MAIN FUNCTION:
//
// 2. ScanIdentToken() - Identifier/keyword scanning (lines 25-73)
//    Implements spec rules from 3.2.7:
//
//    (Lex-Identifier) - spec lines 2372-2375:
//      IdentStart(T[i])
//      j = IdentScanEnd(T, i)
//      s = Lexeme(T, i, j)
//      => Ident(T, i) => (s, j)
//
//    Where IdentScanEnd is defined at line 2370:
//      IdentScanEnd(T, i) = min{j | j > i AND (NOT IdentContinue(T[j]) OR j = |T|)}
//
//    (Lex-Ident-InvalidUnicode) - spec lines 2377-2380:
//      If any scalar in [i, j) is NonCharacter:
//        Emit E-SRC-0307 at that position
//
//    (Lex-Ident-Token) - spec lines 2382-2385:
//      ClassifyIdent(s) determines final TokenKind
//
//    ClassifyIdent logic (spec lines 2388-2392):
//      - "true" or "false" => BoolLiteral
//      - "null" => NullLiteral
//      - Keyword(s) => Keyword
//      - otherwise => Identifier
//
//    Implementation steps:
//      a. Check IdentStart(scalars[start])
//      b. Scan while IdentContinue
//      c. Extract lexeme from byte offsets
//      d. Check for NonCharacter in range (emit E-SRC-0307)
//      e. Classify: bool literal / null literal / keyword / identifier
//
// =============================================================================
// SPEC CROSS-REFERENCES:
// =============================================================================
//
// Character class definitions (3.2.2):
//   - IdentStart(c) = c = '_' OR XID_Start(c)  [line 2064]
//   - IdentContinue(c) = c = '_' OR XID_Continue(c)  [line 2065]
//   - XIDVersion = "15.0.0"  [line 2067]
//   - XID_Start/XID_Continue from UAX31  [lines 2068-2069]
//   - NonCharacter(c) = c in [U+FDD0, U+FDEF] OR (c & 0xFFFF) in {0xFFFE, 0xFFFF}  [line 2073]
//
// Reserved lexemes (3.2.3):
//   - Reserved = {all, as, break, class, continue, dispatch, else, enum, false,
//                 defer, frame, from, if, imm, import, internal, let, loop,
//                 modal, move, mut, null, parallel, private, procedure, protected,
//                 public, race, record, region, return, shadow, shared, spawn,
//                 sync, transition, transmute, true, type, unique, unsafe, var,
//                 widen, where, using, yield, const, override}  [line 2089]
//   - FutureReserved = empty  [line 2091]
//   - Keyword(s) = s in Reserved  [line 2094]
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
//
// - Unicode validation happens inline; could be factored out
// - The source checks IsNonCharacter; spec uses NonCharacter predicate
// - Only first NonCharacter emits diagnostic (break after first)
// - Consider caching Utf8Offsets to avoid recomputation
//
// - IMPORTANT: "true", "false", "null" are classified as literals, NOT keywords
//   This matches spec ClassifyIdent (line 2388-2392)
//
// - Reserved namespace prefixes (cursive., gen_) are NOT checked here
//   They are checked at Phase 3 per spec line 2099
//
// - UniverseProtected names checked at Phase 3 per spec line 2103
//
// =============================================================================

#include "02_source/lexer/lexer.h"

#include <cstddef>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/keywords.h"
#include "00_core/source_text.h"
#include "00_core/span.h"
#include "00_core/unicode.h"

namespace cursive::lexer {

namespace {

bool IsKeyword(std::string_view ident) {
  return core::IsKeyword(ident);
}

}  // namespace

IdentScanResult ScanIdentToken(const core::SourceFile& source,
                               std::size_t start) {
  SPEC_RULE("Lex-Identifier");
  SPEC_RULE("Lex-Ident-Token");
  IdentScanResult result;
  const auto& scalars = source.scalars;
  if (start >= scalars.size() || !core::IsIdentStart(scalars[start])) {
    result.ok = false;
    result.next = start;
    return result;
  }

  std::size_t end = start + 1;
  while (end < scalars.size() && core::IsIdentContinue(scalars[end])) {
    ++end;
  }

  const auto offsets = core::Utf8Offsets(scalars);
  result.lexeme = core::EncodeUtf8(LexemeSliceScalars(scalars, start, end));
  result.ok = true;
  result.next = end;

  for (std::size_t i = start; i < end; ++i) {
    if (!core::IsNonCharacter(scalars[i])) {
      continue;
    }
    SPEC_RULE("Lex-Ident-InvalidUnicode");
    const auto span = core::SpanOf(source, offsets[i], offsets[i + 1]);
    if (auto diag = core::MakeDiagnosticById("E-SRC-0307", span)) {
      core::Emit(result.diags, *diag);
    }
    break;
  }

  if (result.lexeme == "true" || result.lexeme == "false") {
    result.kind = TokenKind::BoolLiteral;
  } else if (result.lexeme == "null") {
    result.kind = TokenKind::NullLiteral;
  } else if (IsKeyword(result.lexeme)) {
    result.kind = TokenKind::Keyword;
  } else {
    result.kind = TokenKind::Identifier;
  }

  return result;
}

}  // namespace cursive::lexer
