// =============================================================================
// MIGRATION MAPPING: numeric_literals.h
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 5.2.10 "Literal Expressions" (IntValue, StripIntSuffix)
//   - Section 5.10 "Enum Discriminant Defaults" (DiscValue via IntValue)
//
// SOURCE FILES:
//   - ultraviolet-bootstrap/src/03_analysis/types/literals.cpp
//     - ParseIntCore, StripIntSuffix, IntValue (lines 93-183)
//   - ultraviolet-bootstrap/src/03_analysis/composite/enums.cpp
//     - ParseIntCore, StripIntSuffix, ParseUnsignedIntLiteral (lines 23-109)
//
// CONTENT TO MIGRATE:
//   - StripIntSuffix(lexeme) -> string_view
//   - ParseIntCore(core) -> optional<UInt128>
//   - ParseUnsignedIntLiteral(lexeme) -> optional<uint64_t>
//
// DEPENDENCIES:
//   - ultraviolet/include/00_core/int128.h (UInt128 operations)
//   - <optional>, <string_view>, <cstdint>
//
// REFACTORING NOTES:
//   1. Shared integer parsing used by enums, tuples, const_len, and literal typing.
//   2. Parsing supports 0x/0o/0b prefixes and '_' digit separators.
//   3. Caller handles sign and range policy; helpers parse unsigned core only.
//
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "00_core/int128.h"

namespace ultraviolet::core {

std::string_view StripIntSuffix(std::string_view lexeme);

std::optional<UInt128> ParseIntCore(std::string_view core);

std::optional<std::uint64_t> ParseUnsignedIntLiteral(
    std::string_view lexeme);

}  // namespace ultraviolet::core
