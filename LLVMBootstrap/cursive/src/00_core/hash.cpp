// =============================================================================
// MIGRATION MAPPING: hash.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.3.1 (referenced in source, covers symbol generation)
//     - FNV1a64: FNV-1a hash algorithm for 64-bit hashes
//     - FNVOffset64 = 14695981039346656037 (0xcbf29ce484222325)
//     - FNVPrime64 = 1099511628211 (0x100000001b3)
//     - Hex2: converts byte to 2-digit hex string
//     - Hex64: converts 64-bit hash to 16-digit hex string (MSB first)
//     - LEBytes: extracts bytes in little-endian order
//     - LiteralID(kind, contents) = mangle(kind) ++ "_" ++ Hex64(FNV1a64(contents))
//
// SOURCE FILE: cursive-bootstrap/src/00_core/hash.cpp
//   - Lines 1-73 (entire file)
//
// CONTENT TO MIGRATE:
//   - FNV1a64(bytes) -> uint64_t (lines 8-19)
//     Implements FNV-1a hash: hash = offset; for each byte: hash ^= byte; hash *= prime
//   - FNV1a64(string_view) -> uint64_t (lines 21-24)
//     Convenience overload converting string to byte span
//   - Hex2(byte) -> string (lines 26-36)
//     Converts single byte to 2-char uppercase hex string
//     Uses HexDigit lookup: "0123456789ABCDEF"
//   - Hex64(hash) -> string (lines 38-59)
//     Converts 64-bit hash to 16-char hex string
//     Per spec: rev(LEBytes(h, 8)) -> outputs MSB first (big-endian string)
//   - LiteralID(kind, contents) -> string (lines 61-71)
//     Generates unique ID for literals: mangle(kind) + "_" + Hex64(FNV1a64(contents))
//
// DEPENDENCIES:
//   - cursive/include/00_core/hash.h (header)
//     - kFNVOffset64 constant
//     - kFNVPrime64 constant
//   - cursive/include/00_core/assert_spec.h
//     - SPEC_DEF macro
//   - cursive/include/00_core/symbols.h
//     - Mangle() function for PathToPrefix encoding
//   - <span> for std::span<const uint8_t>
//   - <cstdint> for uint64_t, uint8_t
//
// REFACTORING NOTES:
//   1. FNV-1a algorithm: XOR then multiply (not multiply then XOR)
//   2. Hex64 outputs big-endian (MSB first) per spec's rev(LEBytes(...))
//   3. Hex2 uses uppercase hex digits - maintain consistency
//   4. SPEC_DEF traces to "6.3.1" for all functions
//   5. LiteralID used for string literal symbol generation
//   6. Consider constexpr for Hex2 if C++20 string is available
//   7. span<const uint8_t> matches spec's Bytes type
//
// =============================================================================

#include "00_core/hash.h"

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"

namespace cursive::core {

std::uint64_t FNV1a64(std::span<const std::uint8_t> bytes) {
  SPEC_DEF("FNV1a64", "6.3.1");
  SPEC_DEF("FNVOffset64", "6.3.1");
  SPEC_DEF("FNVPrime64", "6.3.1");

  std::uint64_t hash = kFNVOffset64;
  for (const std::uint8_t byte : bytes) {
    hash ^= byte;
    hash *= kFNVPrime64;
  }
  return hash;
}

std::uint64_t FNV1a64(std::string_view s) {
  return FNV1a64(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
}

std::string Hex2(std::uint8_t byte) {
  SPEC_DEF("Hex2", "6.3.1");
  SPEC_DEF("HexDigit", "6.3.1");

  static constexpr char kHexDigits[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(2);
  result.push_back(kHexDigits[(byte >> 4) & 0x0F]);
  result.push_back(kHexDigits[byte & 0x0F]);
  return result;
}

std::string Hex64(std::uint64_t hash) {
  SPEC_DEF("Hex64", "6.3.1");
  SPEC_DEF("LEBytes", "6.3.1");

  // Per §6.3.1:
  //   Hex64(h) = Join("", [Hex2(b1),...,Hex2(b8)]) where rev(LEBytes(h, 8)) = [b1,...,b8]
  //
  // LEBytes extracts bytes in little-endian order (LSB first).
  // rev() reverses that to get MSB first (big-endian order in the string).
  // So we print the most significant byte first.

  std::string result;
  result.reserve(16);

  // Extract bytes in big-endian order (MSB first)
  for (int i = 7; i >= 0; --i) {
    const std::uint8_t byte = static_cast<std::uint8_t>((hash >> (i * 8)) & 0xFF);
    result += Hex2(byte);
  }

  return result;
}

std::string LiteralID(std::string_view kind, std::span<const std::uint8_t> contents) {
  SPEC_DEF("LiteralID", "6.3.1");

  // Per §6.3.1:
  //   LiteralID(kind, contents) = mangle(kind) ++ "_" ++ Hex64(FNV1a64(contents))

  std::string result = Mangle(kind);
  result += '_';
  result += Hex64(FNV1a64(contents));
  return result;
}

}  // namespace cursive::core
