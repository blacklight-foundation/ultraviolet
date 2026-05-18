// =============================================================================
// MIGRATION MAPPING: enums.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
// - Section 5.10 "Enum Discriminant Defaults" (not shown in excerpts, referenced in source)
//   - DiscOf function
//   - DiscSeq function
//   - EnumDiscriminants function
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/composite/enums.cpp
// - Lines 1-181 (entire file)
//
// Key source functions to migrate:
// - EnumDiscriminants (lines 113-178): Compute discriminant values for enum variants
//
// Supporting helpers:
// - core::ParseUnsignedIntLiteral: Parse unsigned integer literal
//
// DEPENDENCIES:
// - ultraviolet/src/00_core/int128.h (UInt128 operations)
// - ultraviolet/src/02_syntax/ast.h (EnumDecl, EnumVariant, Token)
// - ultraviolet/src/00_core/assert_spec.h (SPEC_DEF, SPEC_RULE)
//
// Diagnostic rules implemented:
// - Enum-Disc-NotInt (line 129): Discriminant must be an integer literal
// - Enum-Disc-Negative (line 136): Discriminant must not be negative
// - Enum-Disc-Invalid (lines 143, 149, 157, 160): Invalid discriminant value
// - Enum-Disc-Dup (line 166): Duplicate discriminant value
//
// REFACTORING NOTES:
// 1. Discriminant values are u64, computed incrementally from 0
// 2. Explicit discriminants set the next implicit value
// 3. Duplicate discriminant values are an error
// 4. Overflow at max_u64 is an error if not the last variant
// 5. The result includes both the discriminant list and max discriminant
// =============================================================================

#include "04_analysis/composite/enums.h"

#include <algorithm>
#include <cstddef>
#include <unordered_set>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/numeric_literals.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsEnumDisc() {
  SPEC_DEF("DiscOf", "5.10");
  SPEC_DEF("DiscSeq", "5.10");
  SPEC_DEF("EnumDiscriminants", "5.10");
}

}  // namespace

EnumDiscResult EnumDiscriminants(const ast::EnumDecl& decl) {
  SpecDefsEnumDisc();
  EnumDiscResult result;
  std::vector<std::uint64_t> discs;
  discs.reserve(decl.variants.size());
  std::unordered_set<std::uint64_t> seen;
  std::uint64_t next = 0;
  const std::uint64_t max_u64 =
      static_cast<std::uint64_t>(~static_cast<std::uint64_t>(0));

  for (std::size_t i = 0; i < decl.variants.size(); ++i) {
    const auto& variant = decl.variants[i];
    std::uint64_t disc = 0;
    if (variant.discriminant_opt.has_value()) {
      const auto& tok = *variant.discriminant_opt;
      if (tok.kind != lexer::TokenKind::IntLiteral) {
        SPEC_RULE("Enum-Disc-NotInt");
        result.diag_id = "Enum-Disc-NotInt";
        result.span = tok.span;
        return result;
      }
      if (!tok.lexeme.empty() && tok.lexeme[0] == '-') {
        SPEC_RULE("Enum-Disc-Negative");
        result.diag_id = "Enum-Disc-Negative";
        result.span = tok.span;
        return result;
      }
      const auto parsed = core::ParseUnsignedIntLiteral(tok.lexeme);
      if (!parsed.has_value()) {
        SPEC_RULE("Enum-Disc-Invalid");
        result.diag_id = "E-TYP-1921";
        result.span = tok.span;
        return result;
      }
      disc = *parsed;
      if (disc == max_u64 && i + 1 < decl.variants.size()) {
        SPEC_RULE("Enum-Disc-Invalid");
        result.diag_id = "E-TYP-1921";
        result.span = tok.span;
        return result;
      }
      next = disc + 1;
    } else {
      disc = next;
      if (disc == max_u64 && i + 1 < decl.variants.size()) {
        SPEC_RULE("Enum-Disc-Invalid");
        result.diag_id = "E-TYP-1921";
        result.span = decl.span;
        return result;
      }
      next = disc + 1;
    }
    if (!seen.insert(disc).second) {
      SPEC_RULE("Enum-Disc-Dup");
      result.diag_id = "E-TYP-1923";
      result.span = variant.span;
      return result;
    }
    discs.push_back(disc);
    result.max_disc = std::max(result.max_disc, disc);
  }

  result.ok = true;
  result.discs = std::move(discs);
  return result;
}

}  // namespace ultraviolet::analysis
