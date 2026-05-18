#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace ultraviolet::core {

// FNV-1a 64-bit hash constants per §6.3.1.
constexpr std::uint64_t kFNVOffset64 = 14695981039346656037ULL;
constexpr std::uint64_t kFNVPrime64 = 1099511628211ULL;

// FNV1a64(bytes): FNV-1a hash algorithm returning uint64.
// Per §6.3.1:
//   FNV1a64([]) = FNVOffset64
//   FNV1a64([b1,...,bn]) = hn where h0 = FNVOffset64 and
//     for i in 0..n-1: h(i+1) = ((hi XOR b(i+1)) * FNVPrime64) mod 2^64
std::uint64_t FNV1a64(std::span<const std::uint8_t> bytes);
std::uint64_t FNV1a64(std::string_view s);

// Hex2(byte): Two-character uppercase hex encoding of a byte.
// Per §6.3.1: HexDigit(0)="0"...HexDigit(9)="9"...HexDigit(15)="F"
//   Hex2(b) = HexDigit(b/16) ++ HexDigit(b mod 16)
std::string Hex2(std::uint8_t byte);

// LEBytes(value, n): Extract n bytes from value in little-endian order.
// Returns bytes [b1,...,bn] where value = sum(bi * 256^(i-1)).

// Hex64(hash): 16-character hex string from rev(LEBytes(hash, 8)).
// Per §6.3.1:
//   Hex64(h) = Join("", [Hex2(b1),...,Hex2(b8)]) where rev(LEBytes(h, 8)) = [b1,...,b8]
// This means the most significant byte is printed first (big-endian order in the string).
std::string Hex64(std::uint64_t hash);

// LiteralID(kind, contents): Generate literal identity string.
// Per §6.3.1:
//   LiteralID(kind, contents) = mangle(kind) ++ "_" ++ Hex64(FNV1a64(contents))
std::string LiteralID(std::string_view kind, std::span<const std::uint8_t> contents);

}  // namespace ultraviolet::core
