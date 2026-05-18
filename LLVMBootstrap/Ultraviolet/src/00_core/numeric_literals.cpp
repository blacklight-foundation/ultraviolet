// =============================================================================
// MIGRATION MAPPING: numeric_literals.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 5.2.10 "Literal Expressions" (IntValueCore, StripIntSuffix)
//   - Section 5.10 "Enum Discriminant Defaults" (DiscValue via IntValue)
//
// SOURCE FILES:
//   - ultraviolet-bootstrap/src/03_analysis/types/literals.cpp
//     - DigitValue, ParseIntCore (lines 93-170)
//   - ultraviolet-bootstrap/src/03_analysis/composite/enums.cpp
//     - ParseIntCore, StripIntSuffix, ParseUnsignedIntLiteral (lines 23-109)
//
// CONTENT TO MIGRATE:
//   - ParseIntCore() implementation with base prefixes and '_' skipping
//   - StripIntSuffix() integer suffix stripping
//   - ParseUnsignedIntLiteral() lexeme parsing into uint64_t
//
// DEPENDENCIES:
//   - ultraviolet/include/00_core/numeric_literals.h
//   - ultraviolet/include/00_core/int128.h
//
// REFACTORING NOTES:
//   1. Keep parsing logic identical to bootstrap to avoid semantic drift.
//   2. These helpers do not accept sign; callers enforce sign constraints.
//   3. Underscore separators are ignored in digit parsing.
//
// =============================================================================

#include "00_core/numeric_literals.h"

namespace ultraviolet::core {

std::string_view StripIntSuffix(std::string_view lexeme) {
  static constexpr std::string_view kIntSuffixes[] = {
      "i128", "u128", "isize", "usize", "i64", "u64",
      "i32",  "u32",  "i16",  "u16",  "i8",  "u8"};
  for (auto suffix : kIntSuffixes) {
    if (lexeme.size() <= suffix.size()) {
      continue;
    }
    if (lexeme.substr(lexeme.size() - suffix.size()) == suffix) {
      return lexeme.substr(0, lexeme.size() - suffix.size());
    }
  }
  return lexeme;
}

std::optional<UInt128> ParseIntCore(std::string_view core) {
  if (core.empty()) {
    return std::nullopt;
  }
  unsigned base = 10;
  std::string_view digits = core;
  if (core.rfind("0x", 0) == 0 || core.rfind("0X", 0) == 0) {
    base = 16;
    digits = core.substr(2);
  } else if (core.rfind("0o", 0) == 0 || core.rfind("0O", 0) == 0) {
    base = 8;
    digits = core.substr(2);
  } else if (core.rfind("0b", 0) == 0 || core.rfind("0B", 0) == 0) {
    base = 2;
    digits = core.substr(2);
  }

  if (digits.empty()) {
    return std::nullopt;
  }

  UInt128 value = UInt128FromU64(0);
  const UInt128 max = UInt128Max();
  const UInt128 base128 = UInt128FromU64(base);
  bool saw_digit = false;
  for (char c : digits) {
    if (c == '_') {
      continue;
    }
    unsigned digit = 0;
    if (c >= '0' && c <= '9') {
      digit = static_cast<unsigned>(c - '0');
    } else if (base > 10 && c >= 'a' && c <= 'f') {
      digit = static_cast<unsigned>(10 + (c - 'a'));
    } else if (base > 10 && c >= 'A' && c <= 'F') {
      digit = static_cast<unsigned>(10 + (c - 'A'));
    } else {
      return std::nullopt;
    }
    if (digit >= base) {
      return std::nullopt;
    }
    saw_digit = true;
    const UInt128 digit128 = UInt128FromU64(digit);
    const UInt128 max_minus_digit = UInt128Sub(max, digit128);
    const UInt128 threshold = UInt128Div(max_minus_digit, base128);
    if (UInt128Greater(value, threshold)) {
      return std::nullopt;
    }
    value = UInt128Add(UInt128Mul(value, base128), digit128);
  }
  if (!saw_digit) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::uint64_t> ParseUnsignedIntLiteral(std::string_view lexeme) {
  if (lexeme.empty() || lexeme[0] == '-') {
    return std::nullopt;
  }
  const std::string_view core = StripIntSuffix(lexeme);
  const auto value = ParseIntCore(core);
  if (!value.has_value()) {
    return std::nullopt;
  }
  if (!UInt128FitsU64(*value)) {
    return std::nullopt;
  }
  return UInt128ToU64(*value);
}

}  // namespace ultraviolet::core
