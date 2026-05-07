// =============================================================================
// MIGRATION MAPPING: ident.cpp
// =============================================================================
//
// NOTE: This file is being RELOCATED from 01_project to 00_core as part of the
// refactor. The functionality is fundamental to lexing and should be in core.
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 3.2.2 "Identifier Lexing" (referenced in unicode.cpp)
//     - Identifier = IdentStart IdentContinue*
//     - IdentStart: '_' or XID_Start
//     - IdentContinue: '_' or XID_Continue
//   - Section 3.2.3 "Reserved Keywords" (referenced but not inline here)
//     - List of all reserved keywords that cannot be identifiers
//     - Keyword(s) predicate: true if s is a reserved keyword
//   - Section 2.1 "Manifest Schema" (lines 814-822)
//     - WF-Assembly-Name: n : Identifier AND n : NotKeyword => n : Name
//     - WF-Assembly-Name-Err: fails if not identifier or is keyword
//   - Section 2.4 "Module Discovery" (lines 1272-1286)
//     - WF-Module-Path rules: components must be identifiers, not keywords
//     - WF-Module-Path-Reserved: keyword in path component
//     - WF-Module-Path-Ident-Err: non-identifier in path component
//
// SOURCE FILE: cursive-bootstrap/src/01_project/ident.cpp
//   - Lines 1-47 (entire file)
//
// CONTENT TO MIGRATE:
//   - ToBytes(ident) -> vector<uint8_t> (lines 14-16) [internal]
//     Converts string_view to byte vector for UTF-8 decoding
//   - IsKeyword(ident) -> bool (lines 20-22)
//     Delegates to core::IsKeyword (from keywords.h)
//   - IsIdentifier(ident) -> bool (lines 24-41)
//     Validates identifier syntax:
//     1. Non-empty
//     2. Valid UTF-8 (via Decode)
//     3. First scalar is IdentStart
//     4. Remaining scalars are IdentContinue
//   - IsName(ident) -> bool (lines 43-45)
//     Returns IsIdentifier(ident) && !IsKeyword(ident)
//     This is the WF-Assembly-Name predicate from spec
//
// DEPENDENCIES:
//   - cursive/include/00_core/ident.h (NEW header location)
//     - Function declarations (move from 01_project)
//   - cursive/include/00_core/keywords.h
//     - IsKeyword() function
//   - cursive/include/00_core/source_text.h
//     - DecodeResult struct (for Decode function)
//   - cursive/include/00_core/source_load.h
//     - Decode() function for UTF-8 decoding
//   - cursive/include/00_core/unicode.h
//     - IsIdentStart(), IsIdentContinue() functions
//     - UnicodeScalar type
//
// REFACTORING NOTES:
//   1. NAMESPACE CHANGE: cursive0::project -> cursive::core
//      The file is moving from project layer to core layer
//   2. INCLUDE PATH CHANGES:
//      - "cursive0/01_project/ident.h" -> "cursive/00_core/ident.h"
//      - "cursive0/00_core/keywords.h" -> "cursive/00_core/keywords.h"
//      - etc.
//   3. IsKeyword delegates to core::IsKeyword - after move, this becomes
//      a same-namespace call (no core:: prefix needed)
//   4. Consider adding SPEC_DEF traces:
//      - "Identifier" -> "3.2.2"
//      - "Name" -> "2.1" (WF-Assembly-Name)
//   5. Decode() from source_text.h converts bytes to UnicodeScalars
//   6. ToBytes helper is trivial - consider inline or remove
//   7. IsName is the key validation for assembly names and module path
//      components - used throughout project loading
//   8. No current SPEC_RULE traces - consider adding for:
//      - "WF-Assembly-Name" predicate implementation
//
// =============================================================================

#include "00_core/ident.h"

#include <cstdint>
#include <vector>

#include "00_core/keywords.h"
#include "00_core/source_text.h"
#include "00_core/unicode.h"

namespace cursive::core {

namespace {

std::vector<std::uint8_t> ToBytes(std::string_view ident) {
  return std::vector<std::uint8_t>(ident.begin(), ident.end());
}

}  // namespace

bool IsIdentifier(std::string_view ident) {
  if (ident.empty()) {
    return false;
  }
  const DecodeResult decoded = Decode(ToBytes(ident));
  if (!decoded.ok || decoded.scalars.empty()) {
    return false;
  }
  if (!IsIdentStart(decoded.scalars[0])) {
    return false;
  }
  for (std::size_t i = 1; i < decoded.scalars.size(); ++i) {
    if (!IsIdentContinue(decoded.scalars[i])) {
      return false;
    }
  }
  return true;
}

bool IsName(std::string_view ident) {
  return IsIdentifier(ident) && !IsKeyword(ident);
}

}  // namespace cursive::core
