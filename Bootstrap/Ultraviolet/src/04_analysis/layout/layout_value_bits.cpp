// =============================================================================
// MIGRATION MAPPING: layout_value_bits.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.1.4.1 ValueBits specifications (lines 14729-14770)
//   - LEBytes definition (line 14416)
//   - FloatBits_t for IEEE754 encoding (line 14417)
//   - StructBits for record encoding (line 14732)
//   - PadBytes for padding (line 14733)
//   - ValueBits for each type variant (lines 14735-14770)
//   - UnionBits, UnionNicheBits, UnionTaggedBits (lines 14771-14814)
//   - ModalBits, ModalNicheBits, ModalTaggedBits (lines 14816-14834)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/layout/layout_value_bits.cpp
//   - ValueBitsOf functions for constant encoding
//   - StructBits helper for record field packing
//   - Union/Modal encoding helpers
//
// DEPENDENCIES:
//   - ultraviolet/include/04_analysis/layout/layout.h (Layout structs)
//   - ultraviolet/include/04_analysis/types/types.h (TypeRef, value types)
//   - IEEE 754 bit manipulation for floats
//   - Little-endian byte encoding
//
// REFACTORING NOTES:
//   1. LEBytes(v, n) encodes value v as n little-endian bytes
//   2. BoolByte: false=0x00, true=0x01
//   3. Char: UTF-32 codepoint as 4 LE bytes
//   4. Integers: LE encoding at type width
//   5. Floats: IEEE 754 bit pattern, LE encoded
//   6. Unit: empty byte array []
//   7. Never: empty byte array (unreachable)
//   8. Pointers: address as PtrSize LE bytes
//   9. Records: StructBits packs fields at offsets
//   10. Unions: niche or tagged encoding
//   11. Modals: niche or tagged encoding per state
//
// ENCODING FUNCTIONS:
//   - LEBytes(value, size) -> bytes
//   - StructBits(types, values, offsets, size) -> bytes
//   - PadBytes(content, total_size) -> bytes
//   - ValueBits(type, value) -> bytes
// =============================================================================

#include "04_analysis/layout/layout.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "00_core/assert_spec.h"
#include "00_core/int128.h"
#include "04_analysis/composite/enums.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/modal/modal_widen.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_equiv.h"

namespace ultraviolet::analysis::layout
{

  bool ValidValue(const ultraviolet::analysis::ScopeContext &ctx,
                  const ultraviolet::analysis::TypeRef &type,
                  const std::vector<std::uint8_t> &bits);

  namespace
  {

    std::uint64_t AlignUp(std::uint64_t value, std::uint64_t align)
    {
      if (align == 0)
      {
        return value;
      }
      const std::uint64_t rem = value % align;
      if (rem == 0)
      {
        return value;
      }
      return value + (align - rem);
    }

    std::vector<std::uint8_t> LEBytes(core::UInt128 value, std::size_t n)
    {
      std::vector<std::uint8_t> out(n);
      core::UInt128 tmp = value;
      for (std::size_t i = 0; i < n; ++i)
      {
        const std::uint64_t low = core::UInt128ToU64(tmp);
        out[i] = static_cast<std::uint8_t>(low & 0xFFu);
        tmp = core::UInt128ShiftRight(tmp, 8);
      }
      return out;
    }

    std::vector<std::uint8_t> LEBytesU64(std::uint64_t value, std::size_t n)
    {
      return LEBytes(core::UInt128FromU64(value), n);
    }

    std::optional<std::uint64_t> BitsToUInt(const std::vector<std::uint8_t> &bits)
    {
      if (bits.size() > 8)
      {
        return std::nullopt;
      }
      std::uint64_t out = 0;
      for (std::size_t i = 0; i < bits.size(); ++i)
      {
        out |= (static_cast<std::uint64_t>(bits[i]) << (8 * i));
      }
      return out;
    }

    bool IsUnicodeScalar(std::uint32_t value)
    {
      if (value > 0x10FFFFu)
      {
        return false;
      }
      if (value >= 0xD800u && value <= 0xDFFFu)
      {
        return false;
      }
      return true;
    }

    static constexpr std::array<std::string_view, 12> kIntTypes = {
        "i8", "i16", "i32", "i64", "i128", "isize",
        "u8", "u16", "u32", "u64", "u128", "usize"};

    static constexpr std::array<std::string_view, 3> kFloatTypes = {
        "f16", "f32", "f64"};

    static bool IsIntTypeName(std::string_view name)
    {
      for (const auto &t : kIntTypes)
      {
        if (name == t)
        {
          return true;
        }
      }
      return false;
    }

    static bool IsFloatTypeName(std::string_view name)
    {
      for (const auto &t : kFloatTypes)
      {
        if (name == t)
        {
          return true;
        }
      }
      return false;
    }

    static constexpr std::array<std::string_view, 12> kIntSuffixes = {
        "i128", "u128", "isize", "usize", "i64", "u64",
        "i32", "u32", "i16", "u16", "i8", "u8"};

    static constexpr std::array<std::string_view, 4> kFloatSuffixes = {
        "f16", "f32", "f64", "f"};

    static bool EndsWith(std::string_view value, std::string_view suffix)
    {
      if (suffix.size() > value.size())
      {
        return false;
      }
      return value.substr(value.size() - suffix.size()) == suffix;
    }

    static std::string_view StripIntSuffix(std::string_view lexeme)
    {
      for (const auto suffix : kIntSuffixes)
      {
        if (EndsWith(lexeme, suffix))
        {
          const std::size_t core_len = lexeme.size() - suffix.size();
          if (core_len == 0)
          {
            continue;
          }
          return lexeme.substr(0, core_len);
        }
      }
      return lexeme;
    }

    static std::string_view StripFloatSuffix(std::string_view lexeme)
    {
      for (const auto suffix : kFloatSuffixes)
      {
        if (EndsWith(lexeme, suffix))
        {
          const std::size_t core_len = lexeme.size() - suffix.size();
          if (core_len == 0)
          {
            continue;
          }
          return lexeme.substr(0, core_len);
        }
      }
      return lexeme;
    }

    static bool DigitValue(char c, unsigned base, unsigned *out)
    {
      if (c >= '0' && c <= '9')
      {
        unsigned digit = static_cast<unsigned>(c - '0');
        if (digit < base)
        {
          *out = digit;
          return true;
        }
        return false;
      }
      if (base <= 10)
      {
        return false;
      }
      if (c >= 'a' && c <= 'f')
      {
        unsigned digit = static_cast<unsigned>(10 + (c - 'a'));
        if (digit < base)
        {
          *out = digit;
          return true;
        }
        return false;
      }
      if (c >= 'A' && c <= 'F')
      {
        unsigned digit = static_cast<unsigned>(10 + (c - 'A'));
        if (digit < base)
        {
          *out = digit;
          return true;
        }
        return false;
      }
      return false;
    }

    static std::optional<std::pair<std::uint32_t, std::size_t>> DecodeUtf8One(
        const unsigned char *data,
        std::size_t size,
        std::size_t offset)
    {
      if (offset >= size)
      {
        return std::nullopt;
      }
      const std::uint8_t b0 = data[offset];
      if (b0 < 0x80u)
      {
        return std::make_pair(static_cast<std::uint32_t>(b0), 1u);
      }
      if ((b0 & 0xE0u) == 0xC0u)
      {
        if (offset + 1 >= size)
        {
          return std::nullopt;
        }
        const std::uint8_t b1 = data[offset + 1];
        if ((b1 & 0xC0u) != 0x80u)
        {
          return std::nullopt;
        }
        const std::uint32_t cp =
            ((static_cast<std::uint32_t>(b0) & 0x1Fu) << 6) |
            (static_cast<std::uint32_t>(b1) & 0x3Fu);
        if (cp < 0x80u)
        {
          return std::nullopt;
        }
        if (!IsUnicodeScalar(cp))
        {
          return std::nullopt;
        }
        return std::make_pair(cp, 2u);
      }
      if ((b0 & 0xF0u) == 0xE0u)
      {
        if (offset + 2 >= size)
        {
          return std::nullopt;
        }
        const std::uint8_t b1 = data[offset + 1];
        const std::uint8_t b2 = data[offset + 2];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u)
        {
          return std::nullopt;
        }
        const std::uint32_t cp =
            ((static_cast<std::uint32_t>(b0) & 0x0Fu) << 12) |
            ((static_cast<std::uint32_t>(b1) & 0x3Fu) << 6) |
            (static_cast<std::uint32_t>(b2) & 0x3Fu);
        if (cp < 0x800u)
        {
          return std::nullopt;
        }
        if (!IsUnicodeScalar(cp))
        {
          return std::nullopt;
        }
        return std::make_pair(cp, 3u);
      }
      if ((b0 & 0xF8u) == 0xF0u)
      {
        if (offset + 3 >= size)
        {
          return std::nullopt;
        }
        const std::uint8_t b1 = data[offset + 1];
        const std::uint8_t b2 = data[offset + 2];
        const std::uint8_t b3 = data[offset + 3];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u ||
            (b3 & 0xC0u) != 0x80u)
        {
          return std::nullopt;
        }
        const std::uint32_t cp =
            ((static_cast<std::uint32_t>(b0) & 0x07u) << 18) |
            ((static_cast<std::uint32_t>(b1) & 0x3Fu) << 12) |
            ((static_cast<std::uint32_t>(b2) & 0x3Fu) << 6) |
            (static_cast<std::uint32_t>(b3) & 0x3Fu);
        if (cp < 0x10000u || cp > 0x10FFFFu)
        {
          return std::nullopt;
        }
        if (!IsUnicodeScalar(cp))
        {
          return std::nullopt;
        }
        return std::make_pair(cp, 4u);
      }
      return std::nullopt;
    }

    static std::vector<std::uint8_t> EncodeUtf8(std::uint32_t value)
    {
      std::vector<std::uint8_t> out;
      if (value <= 0x7Fu)
      {
        out.push_back(static_cast<std::uint8_t>(value));
      }
      else if (value <= 0x7FFu)
      {
        out.push_back(static_cast<std::uint8_t>(0xC0u | (value >> 6)));
        out.push_back(static_cast<std::uint8_t>(0x80u | (value & 0x3Fu)));
      }
      else if (value <= 0xFFFFu)
      {
        out.push_back(static_cast<std::uint8_t>(0xE0u | (value >> 12)));
        out.push_back(static_cast<std::uint8_t>(0x80u | ((value >> 6) & 0x3Fu)));
        out.push_back(static_cast<std::uint8_t>(0x80u | (value & 0x3Fu)));
      }
      else
      {
        out.push_back(static_cast<std::uint8_t>(0xF0u | (value >> 18)));
        out.push_back(static_cast<std::uint8_t>(0x80u | ((value >> 12) & 0x3Fu)));
        out.push_back(static_cast<std::uint8_t>(0x80u | ((value >> 6) & 0x3Fu)));
        out.push_back(static_cast<std::uint8_t>(0x80u | (value & 0x3Fu)));
      }
      return out;
    }

    static std::optional<std::uint32_t> ParseHexScalar(std::string_view digits)
    {
      if (digits.empty())
      {
        return std::nullopt;
      }
      std::uint32_t value = 0;
      for (char c : digits)
      {
        unsigned digit = 0;
        if (!DigitValue(c, 16, &digit))
        {
          return std::nullopt;
        }
        if (value > (0x10FFFFu - digit) / 16u)
        {
          return std::nullopt;
        }
        value = static_cast<std::uint32_t>(value * 16u + digit);
      }
      if (!IsUnicodeScalar(value))
      {
        return std::nullopt;
      }
      return value;
    }

    static std::optional<std::uint32_t> DecodeCharLiteral(std::string_view lexeme)
    {
      if (lexeme.size() < 2 || lexeme.front() != '\'' || lexeme.back() != '\'')
      {
        return std::nullopt;
      }
      const std::string_view inner = lexeme.substr(1, lexeme.size() - 2);
      std::vector<std::uint8_t> bytes;
      const auto *data = reinterpret_cast<const unsigned char *>(inner.data());
      std::size_t i = 0;
      while (i < inner.size())
      {
        const unsigned char c = data[i];
        if (c == static_cast<unsigned char>('\\'))
        {
          if (i + 1 >= inner.size())
          {
            return std::nullopt;
          }
          const char esc = inner[i + 1];
          switch (esc)
          {
          case '\\':
            bytes.push_back(0x5Cu);
            i += 2;
            break;
          case '\"':
            bytes.push_back(0x22u);
            i += 2;
            break;
          case '\'':
            bytes.push_back(0x27u);
            i += 2;
            break;
          case 'n':
            bytes.push_back(0x0Au);
            i += 2;
            break;
          case 'r':
            bytes.push_back(0x0Du);
            i += 2;
            break;
          case 't':
            bytes.push_back(0x09u);
            i += 2;
            break;
          case '0':
            bytes.push_back(0x00u);
            i += 2;
            break;
          case 'x':
          {
            if (i + 3 >= inner.size())
            {
              return std::nullopt;
            }
            unsigned d1 = 0;
            unsigned d2 = 0;
            if (!DigitValue(inner[i + 2], 16, &d1) ||
                !DigitValue(inner[i + 3], 16, &d2))
            {
              return std::nullopt;
            }
            bytes.push_back(static_cast<std::uint8_t>((d1 << 4) | d2));
            i += 4;
            break;
          }
          case 'u':
          {
            if (i + 2 >= inner.size() || inner[i + 2] != '{')
            {
              return std::nullopt;
            }
            const std::size_t start = i + 3;
            const std::size_t close = inner.find('}', start);
            if (close == std::string_view::npos)
            {
              return std::nullopt;
            }
            const auto digits = inner.substr(start, close - start);
            const auto uval = ParseHexScalar(digits);
            if (!uval.has_value())
            {
              return std::nullopt;
            }
            const auto utf8 = EncodeUtf8(*uval);
            bytes.insert(bytes.end(), utf8.begin(), utf8.end());
            i = close + 1;
            break;
          }
          default:
            return std::nullopt;
          }
          continue;
        }
        const auto decoded = DecodeUtf8One(data, inner.size(), i);
        if (!decoded.has_value())
        {
          return std::nullopt;
        }
        const std::size_t len = decoded->second;
        bytes.insert(bytes.end(), inner.begin() + i, inner.begin() + i + len);
        i += len;
      }
      if (bytes.empty())
      {
        return std::nullopt;
      }
      const auto decoded = DecodeUtf8One(bytes.data(), bytes.size(), 0);
      if (!decoded.has_value() || decoded->second != bytes.size())
      {
        return std::nullopt;
      }
      return decoded->first;
    }

    static bool ParseIntCore(std::string_view core, core::UInt128 &value_out)
    {
      unsigned base = 10;
      std::string_view digits = core;
      if (core.size() >= 2 && core[0] == '0')
      {
        const char prefix = core[1];
        if (prefix == 'x' || prefix == 'X')
        {
          base = 16;
          digits = core.substr(2);
        }
        else if (prefix == 'o' || prefix == 'O')
        {
          base = 8;
          digits = core.substr(2);
        }
        else if (prefix == 'b' || prefix == 'B')
        {
          base = 2;
          digits = core.substr(2);
        }
      }
      if (digits.empty())
      {
        return false;
      }
      core::UInt128 value = core::UInt128FromU64(0);
      const core::UInt128 max = core::UInt128Max();
      const core::UInt128 base128 = core::UInt128FromU64(base);
      bool saw_digit = false;
      for (char c : digits)
      {
        if (c == '_')
        {
          continue;
        }
        unsigned digit = 0;
        if (!DigitValue(c, base, &digit))
        {
          return false;
        }
        saw_digit = true;
        const core::UInt128 digit128 = core::UInt128FromU64(digit);
        const core::UInt128 max_minus_digit = core::UInt128Sub(max, digit128);
        const core::UInt128 threshold = core::UInt128Div(max_minus_digit, base128);
        if (core::UInt128Greater(value, threshold))
        {
          return false;
        }
        value = core::UInt128Add(core::UInt128Mul(value, base128), digit128);
      }
      if (!saw_digit)
      {
        return false;
      }
      value_out = value;
      return true;
    }

    static std::optional<core::UInt128> ParseIntLiteralValue(
        std::string_view lexeme)
    {
      if (lexeme.empty() || lexeme[0] == '-')
      {
        return std::nullopt;
      }
      const std::string_view core = StripIntSuffix(lexeme);
      if (core.empty())
      {
        return std::nullopt;
      }
      core::UInt128 value = core::UInt128FromU64(0);
      if (!ParseIntCore(core, value))
      {
        return std::nullopt;
      }
      return value;
    }

    static std::optional<double> ParseFloatLiteralValue(
        std::string_view lexeme)
    {
      if (lexeme.empty() || lexeme[0] == '-')
      {
        return std::nullopt;
      }
      const std::string_view core = StripFloatSuffix(lexeme);
      if (core.empty())
      {
        return std::nullopt;
      }
      std::string cleaned;
      cleaned.reserve(core.size());
      for (char c : core)
      {
        if (c != '_')
        {
          cleaned.push_back(c);
        }
      }
      if (cleaned.empty())
      {
        return std::nullopt;
      }
      char *end = nullptr;
      const double value = std::strtod(cleaned.c_str(), &end);
      if (!end || *end != '\0')
      {
        return std::nullopt;
      }
      return value;
    }

    std::uint16_t FloatToHalf(float value)
    {
      const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
      const std::uint32_t sign = (bits >> 16) & 0x8000u;
      const std::uint32_t exp = (bits >> 23) & 0xFFu;
      const std::uint32_t mantissa = bits & 0x7FFFFFu;

      if (exp == 0xFFu)
      {
        if (mantissa == 0)
        {
          return static_cast<std::uint16_t>(sign | 0x7C00u);
        }
        const std::uint16_t payload = static_cast<std::uint16_t>(mantissa >> 13);
        return static_cast<std::uint16_t>(sign | 0x7C00u | payload | 0x1u);
      }

      if (exp == 0)
      {
        return static_cast<std::uint16_t>(sign);
      }

      const int exp_adjusted = static_cast<int>(exp) - 127 + 15;
      if (exp_adjusted >= 31)
      {
        return static_cast<std::uint16_t>(sign | 0x7C00u);
      }
      if (exp_adjusted <= 0)
      {
        if (exp_adjusted < -10)
        {
          return static_cast<std::uint16_t>(sign);
        }
        std::uint32_t mant = mantissa | 0x800000u;
        const std::uint32_t shift = static_cast<std::uint32_t>(14 - exp_adjusted);
        const std::uint32_t rounded =
            (mant + (1u << (shift - 1)) + ((mant >> shift) & 1u)) >> shift;
        return static_cast<std::uint16_t>(sign | rounded);
      }

      const std::uint32_t rounded =
          (mantissa + 0xFFFu + ((mantissa >> 13) & 1u)) >> 13;
      if (rounded == 0x400u)
      {
        return static_cast<std::uint16_t>(sign | ((exp_adjusted + 1) << 10));
      }
      return static_cast<std::uint16_t>(sign | (exp_adjusted << 10) |
                                        (rounded & 0x3FFu));
    }

    std::optional<std::vector<std::uint8_t>> StructBits(
        const std::vector<std::vector<std::uint8_t>> &field_bits,
        const std::vector<std::uint64_t> &offsets,
        std::uint64_t size)
    {
      if (field_bits.size() != offsets.size())
      {
        return std::nullopt;
      }
      std::vector<std::uint8_t> bits(size, 0);
      for (std::size_t i = 0; i < field_bits.size(); ++i)
      {
        const auto &fb = field_bits[i];
        const std::uint64_t off = offsets[i];
        if (off + fb.size() > bits.size())
        {
          return std::nullopt;
        }
        std::copy(fb.begin(), fb.end(), bits.begin() + off);
      }
      return bits;
    }

    std::vector<std::uint8_t> PadBytes(const std::vector<std::uint8_t> &bytes,
                                       std::uint64_t size)
    {
      std::vector<std::uint8_t> out(size, 0);
      const std::size_t copy = std::min<std::size_t>(bytes.size(), out.size());
      std::copy(bytes.begin(), bytes.begin() + copy, out.begin());
      return out;
    }

    std::optional<std::vector<std::uint8_t>> TaggedBits(
        const std::vector<std::uint8_t> &disc_bits,
        const std::vector<std::uint8_t> &payload_bits,
        std::uint64_t disc_size,
        std::uint64_t payload_size,
        std::uint64_t payload_align,
        std::uint64_t size)
    {
      const std::uint64_t payload_off = AlignUp(disc_size, payload_align);
      if (size < payload_off + payload_size)
      {
        return std::nullopt;
      }
      std::vector<std::uint8_t> out(size, 0);
      if (disc_bits.size() != disc_size || payload_bits.size() != payload_size)
      {
        return std::nullopt;
      }
      std::copy(disc_bits.begin(), disc_bits.end(), out.begin());
      std::copy(payload_bits.begin(), payload_bits.end(), out.begin() + payload_off);
      return out;
    }

    bool IsUnitType(const ultraviolet::analysis::TypeRef &type)
    {
      if (!type)
      {
        return false;
      }
      if (const auto *prim = std::get_if<ultraviolet::analysis::TypePrim>(&type->node))
      {
        return prim->name == "()";
      }
      return false;
    }

    ultraviolet::analysis::TypeRef StripPermRefine(
        const ultraviolet::analysis::TypeRef &type)
    {
      ultraviolet::analysis::TypeRef cur = type;
      while (cur)
      {
        if (const auto *perm = std::get_if<ultraviolet::analysis::TypePerm>(&cur->node))
        {
          cur = perm->base;
          continue;
        }
        if (const auto *refine =
                std::get_if<ultraviolet::analysis::TypeRefine>(&cur->node))
        {
          cur = refine->base;
          continue;
        }
        break;
      }
      return cur;
    }

    std::optional<std::vector<ultraviolet::analysis::TypeRef>> RangeFieldTypes(
        const ultraviolet::analysis::TypeRef &type)
    {
      const auto stripped = StripPermRefine(type);
      if (!stripped)
      {
        return std::nullopt;
      }

      std::vector<ultraviolet::analysis::TypeRef> fields;
      if (const auto *range =
              std::get_if<ultraviolet::analysis::TypeRange>(&stripped->node))
      {
        fields.push_back(range->base);
        fields.push_back(range->base);
        return fields;
      }
      if (const auto *range =
              std::get_if<ultraviolet::analysis::TypeRangeInclusive>(&stripped->node))
      {
        fields.push_back(range->base);
        fields.push_back(range->base);
        return fields;
      }
      if (const auto *range =
              std::get_if<ultraviolet::analysis::TypeRangeFrom>(&stripped->node))
      {
        fields.push_back(range->base);
        return fields;
      }
      if (const auto *range =
              std::get_if<ultraviolet::analysis::TypeRangeTo>(&stripped->node))
      {
        fields.push_back(range->base);
        return fields;
      }
      if (const auto *range = std::get_if<
              ultraviolet::analysis::TypeRangeToInclusive>(&stripped->node))
      {
        fields.push_back(range->base);
        return fields;
      }
      if (std::holds_alternative<ultraviolet::analysis::TypeRangeFull>(stripped->node))
      {
        return fields;
      }
      return std::nullopt;
    }

    std::optional<ValueRangeKind> ExpectedValueRangeKind(
        const ultraviolet::analysis::TypeRef &type)
    {
      const auto stripped = StripPermRefine(type);
      if (!stripped)
      {
        return std::nullopt;
      }
      if (std::holds_alternative<ultraviolet::analysis::TypeRange>(stripped->node))
      {
        return ValueRangeKind::Exclusive;
      }
      if (std::holds_alternative<ultraviolet::analysis::TypeRangeInclusive>(
              stripped->node))
      {
        return ValueRangeKind::Inclusive;
      }
      if (std::holds_alternative<ultraviolet::analysis::TypeRangeFrom>(stripped->node))
      {
        return ValueRangeKind::From;
      }
      if (std::holds_alternative<ultraviolet::analysis::TypeRangeTo>(stripped->node))
      {
        return ValueRangeKind::To;
      }
      if (std::holds_alternative<ultraviolet::analysis::TypeRangeToInclusive>(
              stripped->node))
      {
        return ValueRangeKind::ToInclusive;
      }
      if (std::holds_alternative<ultraviolet::analysis::TypeRangeFull>(stripped->node))
      {
        return ValueRangeKind::Full;
      }
      return std::nullopt;
    }

    std::optional<Value> RangeBoundValueForType(
        const ultraviolet::analysis::TypeRef &bound_type,
        std::uint64_t raw)
    {
      const auto stripped = StripPermRefine(bound_type);
      if (!stripped)
      {
        return std::nullopt;
      }
      const auto *prim = std::get_if<ultraviolet::analysis::TypePrim>(&stripped->node);
      if (!prim)
      {
        return std::nullopt;
      }
      if (IsIntTypeName(prim->name))
      {
        return Value{IntVal{prim->name, core::UInt128FromU64(raw)}};
      }
      if (prim->name == "char")
      {
        if (!IsUnicodeScalar(raw))
        {
          return std::nullopt;
        }
        return Value{CharVal{static_cast<std::uint32_t>(raw)}};
      }
      if (prim->name == "bool")
      {
        if (raw > 1)
        {
          return std::nullopt;
        }
        return Value{BoolVal{raw != 0}};
      }
      return std::nullopt;
    }

    bool IsNicheType(const ultraviolet::analysis::ScopeContext &ctx,
                     const ultraviolet::analysis::TypeRef &type)
    {
      (void)ctx;
      if (!type)
      {
        return false;
      }
      if (const auto *ptr = std::get_if<ultraviolet::analysis::TypePtr>(&type->node))
      {
        return ptr->state == ultraviolet::analysis::PtrState::Valid;
      }
      return false;
    }

    std::uint64_t NicheCount(const ultraviolet::analysis::ScopeContext &ctx,
                             const ultraviolet::analysis::TypeRef &type)
    {
      return IsNicheType(ctx, type) ? 1 : 0;
    }

    std::optional<std::vector<std::uint8_t>> NicheBitsForIndex(
        const ultraviolet::analysis::ScopeContext &ctx,
        const ultraviolet::analysis::TypeRef &type,
        std::size_t index)
    {
      if (!IsNicheType(ctx, type))
      {
        return std::nullopt;
      }
      if (index != 0)
      {
        return std::nullopt;
      }
      const auto size = SizeOf(ctx, type);
      if (!size.has_value())
      {
        return std::nullopt;
      }
      return LEBytesU64(0, *size);
    }

    bool IsNicheBits(const ultraviolet::analysis::ScopeContext &ctx,
                     const ultraviolet::analysis::TypeRef &type,
                     const std::vector<std::uint8_t> &bits)
    {
      const auto niche = NicheBitsForIndex(ctx, type, 0);
      return niche.has_value() && *niche == bits;
    }

    const ultraviolet::ast::StateBlock *FindState(
        const ultraviolet::ast::ModalDecl &decl,
        std::string_view name)
    {
      for (const auto &state : decl.states)
      {
        if (ultraviolet::analysis::IdEq(state.name, name))
        {
          return &state;
        }
      }
      return nullptr;
    }

    bool IsEmptyState(const ultraviolet::ast::StateBlock &state)
    {
      for (const auto &member : state.members)
      {
        if (std::holds_alternative<ultraviolet::ast::StateFieldDecl>(member))
        {
          return false;
        }
      }
      return true;
    }

    std::optional<std::pair<std::string, ultraviolet::analysis::TypeRef>>
    SingleFieldPayloadType(const ultraviolet::analysis::ScopeContext &ctx,
                           const ultraviolet::ast::StateBlock &state)
    {
      const ultraviolet::ast::StateFieldDecl *field = nullptr;
      for (const auto &member : state.members)
      {
        const auto *payload =
            std::get_if<ultraviolet::ast::StateFieldDecl>(&member);
        if (!payload)
        {
          continue;
        }
        if (field)
        {
          return std::nullopt;
        }
        field = payload;
      }
      if (!field)
      {
        return std::nullopt;
      }
      const auto lowered = LowerTypeForLayout(ctx, field->type);
      if (!lowered.has_value())
      {
        return std::nullopt;
      }
      return std::make_pair(field->name, *lowered);
    }

    std::optional<std::vector<std::pair<std::string, ultraviolet::analysis::TypeRef>>> CollectStateFields(
        const ultraviolet::analysis::ScopeContext &ctx,
        const ultraviolet::ast::StateBlock &state)
    {
      std::vector<std::pair<std::string, ultraviolet::analysis::TypeRef>> fields;
      for (const auto &member : state.members)
      {
        if (const auto *field =
                std::get_if<ultraviolet::ast::StateFieldDecl>(&member))
        {
          const auto lowered = LowerTypeForLayout(ctx, field->type);
          if (!lowered.has_value())
          {
            return std::nullopt;
          }
          fields.emplace_back(field->name, *lowered);
        }
      }
      return fields;
    }

    std::optional<std::vector<std::uint8_t>> ValueBitsForRecord(
        const ultraviolet::analysis::ScopeContext &ctx,
        const std::vector<std::pair<std::string, ultraviolet::analysis::TypeRef>> &fields,
        const RecordVal &val);

    std::optional<std::vector<std::uint8_t>> SliceBytes(
        const std::vector<std::uint8_t> &bits,
        std::size_t offset,
        std::size_t size)
    {
      if (offset > bits.size() || size > bits.size() - offset)
      {
        return std::nullopt;
      }
      return std::vector<std::uint8_t>(bits.begin() + offset,
                                       bits.begin() + offset + size);
    }

    bool AllZero(const std::vector<std::uint8_t> &bits,
                 std::size_t offset,
                 std::size_t size)
    {
      if (offset > bits.size() || size > bits.size() - offset)
      {
        return false;
      }
      for (std::size_t i = offset; i < offset + size; ++i)
      {
        if (bits[i] != 0)
        {
          return false;
        }
      }
      return true;
    }

    bool CheckPaddingZero(
        const std::vector<std::uint8_t> &bits,
        const std::vector<std::pair<std::size_t, std::size_t>> &ranges)
    {
      std::vector<bool> covered(bits.size(), false);
      for (const auto &range : ranges)
      {
        if (range.first > range.second || range.second > bits.size())
        {
          return false;
        }
        for (std::size_t i = range.first; i < range.second; ++i)
        {
          covered[i] = true;
        }
      }
      for (std::size_t i = 0; i < bits.size(); ++i)
      {
        if (!covered[i] && bits[i] != 0)
        {
          return false;
        }
      }
      return true;
    }

    bool ValidStructBits(const ultraviolet::analysis::ScopeContext &ctx,
                         const std::vector<ultraviolet::analysis::TypeRef> &types,
                         const std::vector<std::uint64_t> &offsets,
                         std::uint64_t size,
                         const std::vector<std::uint8_t> &bits)
    {
      if (types.size() != offsets.size())
      {
        return false;
      }
      if (bits.size() != size)
      {
        return false;
      }
      std::vector<std::pair<std::size_t, std::size_t>> ranges;
      ranges.reserve(types.size());
      for (std::size_t i = 0; i < types.size(); ++i)
      {
        const auto elem_size = SizeOf(ctx, types[i]);
        if (!elem_size.has_value())
        {
          return false;
        }
        const std::size_t off = static_cast<std::size_t>(offsets[i]);
        const std::size_t sz = static_cast<std::size_t>(*elem_size);
        if (off > bits.size() || sz > bits.size() - off)
        {
          return false;
        }
        const auto slice = SliceBytes(bits, off, sz);
        if (!slice.has_value())
        {
          return false;
        }
        if (!ValidValue(ctx, types[i], *slice))
        {
          return false;
        }
        ranges.emplace_back(off, off + sz);
      }
      return CheckPaddingZero(bits, ranges);
    }

  } // namespace

  std::optional<std::vector<std::uint8_t>> DecodeStringLiteralBytes(
      std::string_view lexeme)
  {
    if (lexeme.size() < 2 || lexeme.front() != '"' || lexeme.back() != '"')
    {
      return std::nullopt;
    }
    const std::string_view inner = lexeme.substr(1, lexeme.size() - 2);
    std::vector<std::uint8_t> bytes;
    const auto *data = reinterpret_cast<const unsigned char *>(inner.data());
    std::size_t i = 0;
    while (i < inner.size())
    {
      const unsigned char c = data[i];
      if (c == static_cast<unsigned char>('\\'))
      {
        if (i + 1 >= inner.size())
        {
          return std::nullopt;
        }
        const char esc = inner[i + 1];
        switch (esc)
        {
        case '\\':
          bytes.push_back(0x5Cu);
          i += 2;
          break;
        case '"':
          bytes.push_back(0x22u);
          i += 2;
          break;
        case '\'':
          bytes.push_back(0x27u);
          i += 2;
          break;
        case 'n':
          bytes.push_back(0x0Au);
          i += 2;
          break;
        case 'r':
          bytes.push_back(0x0Du);
          i += 2;
          break;
        case 't':
          bytes.push_back(0x09u);
          i += 2;
          break;
        case '0':
          bytes.push_back(0x00u);
          i += 2;
          break;
        case 'x':
        {
          if (i + 3 >= inner.size())
          {
            return std::nullopt;
          }
          unsigned d1 = 0;
          unsigned d2 = 0;
          if (!DigitValue(inner[i + 2], 16, &d1) ||
              !DigitValue(inner[i + 3], 16, &d2))
          {
            return std::nullopt;
          }
          bytes.push_back(static_cast<std::uint8_t>((d1 << 4) | d2));
          i += 4;
          break;
        }
        case 'u':
        {
          if (i + 2 >= inner.size() || inner[i + 2] != '{')
          {
            return std::nullopt;
          }
          const std::size_t start = i + 3;
          const std::size_t close = inner.find('}', start);
          if (close == std::string_view::npos)
          {
            return std::nullopt;
          }
          const auto digits = inner.substr(start, close - start);
          const auto uval = ParseHexScalar(digits);
          if (!uval.has_value())
          {
            return std::nullopt;
          }
          const auto utf8 = EncodeUtf8(*uval);
          bytes.insert(bytes.end(), utf8.begin(), utf8.end());
          i = close + 1;
          break;
        }
        default:
          return std::nullopt;
        }
        continue;
      }
      const auto decoded = DecodeUtf8One(data, inner.size(), i);
      if (!decoded.has_value())
      {
        return std::nullopt;
      }
      const std::size_t len = decoded->second;
      bytes.insert(bytes.end(), inner.begin() + i, inner.begin() + i + len);
      i += len;
    }
    return bytes;
  }

  std::optional<std::vector<std::uint8_t>> EncodeConst(
      const ultraviolet::analysis::TypeRef &type,
      const ultraviolet::ast::Token &lit)
  {
    if (!type)
    {
      return std::nullopt;
    }
    const auto *prim = std::get_if<ultraviolet::analysis::TypePrim>(&type->node);
    if (!prim)
    {
      if (std::holds_alternative<ultraviolet::analysis::TypeRawPtr>(type->node) &&
          lit.kind == ultraviolet::ast::TokenKind::NullLiteral)
      {
        SPEC_RULE("Encode-RawPtr-Null");
        return LEBytesU64(0, kPtrSize);
      }
      return std::nullopt;
    }

    if (prim->name == "bool")
    {
      if (lit.kind != ultraviolet::ast::TokenKind::BoolLiteral)
      {
        return std::nullopt;
      }
      SPEC_RULE("Encode-Bool");
      const bool value = lit.lexeme == "true";
      return LEBytesU64(value ? 1 : 0, 1);
    }
    if (prim->name == "char")
    {
      if (lit.kind != ultraviolet::ast::TokenKind::CharLiteral)
      {
        return std::nullopt;
      }
      SPEC_RULE("Encode-Char");
      const auto codepoint = DecodeCharLiteral(lit.lexeme);
      if (!codepoint.has_value())
      {
        return std::nullopt;
      }
      return LEBytesU64(*codepoint, 4);
    }

    if (prim->name == "()")
    {
      SPEC_RULE("Encode-Unit");
      return std::vector<std::uint8_t>{};
    }
    if (prim->name == "!")
    {
      SPEC_RULE("Encode-Never");
      return std::vector<std::uint8_t>{};
    }

    if (lit.kind == ultraviolet::ast::TokenKind::IntLiteral)
    {
      if (!IsIntTypeName(prim->name))
      {
        return std::nullopt;
      }
      SPEC_RULE("Encode-Int");
      const auto value = ParseIntLiteralValue(lit.lexeme);
      if (!value.has_value())
      {
        return std::nullopt;
      }
      const auto size = PrimSize(prim->name);
      if (!size.has_value())
      {
        return std::nullopt;
      }
      return LEBytes(*value, *size);
    }

    if (lit.kind == ultraviolet::ast::TokenKind::FloatLiteral)
    {
      if (!IsFloatTypeName(prim->name))
      {
        return std::nullopt;
      }
      SPEC_RULE("Encode-Float");
      const auto value = ParseFloatLiteralValue(lit.lexeme);
      if (!value.has_value())
      {
        return std::nullopt;
      }
      if (prim->name == "f64")
      {
        const double v = static_cast<double>(*value);
        const std::uint64_t bits = std::bit_cast<std::uint64_t>(v);
        return LEBytesU64(bits, 8);
      }
      if (prim->name == "f32")
      {
        const float v = static_cast<float>(*value);
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(v);
        return LEBytesU64(bits, 4);
      }
      if (prim->name == "f16")
      {
        const float v = static_cast<float>(*value);
        const std::uint16_t bits = FloatToHalf(v);
        return LEBytesU64(bits, 2);
      }
    }

    return std::nullopt;
  }

  bool ValidValue(const ultraviolet::analysis::ScopeContext &ctx,
                  const ultraviolet::analysis::TypeRef &type,
                  const std::vector<std::uint8_t> &bits)
  {
    if (!type)
    {
      return false;
    }
    const std::uint64_t ptr_size = PtrSize(ctx);
    if (const auto *prim = std::get_if<ultraviolet::analysis::TypePrim>(&type->node))
    {
      if (prim->name == "bool")
      {
        SPEC_RULE("Valid-Bool");
        return bits == std::vector<std::uint8_t>{0x00} ||
               bits == std::vector<std::uint8_t>{0x01};
      }
      if (prim->name == "char")
      {
        SPEC_RULE("Valid-Char");
        if (bits.size() != 4)
        {
          return false;
        }
        const auto value = BitsToUInt(bits);
        if (!value.has_value())
        {
          return false;
        }
        return IsUnicodeScalar(static_cast<std::uint32_t>(*value));
      }
      if (prim->name == "()")
      {
        SPEC_RULE("Valid-Unit");
        return bits.empty();
      }
      if (prim->name == "!")
      {
        SPEC_RULE("Valid-Never");
        return false;
      }
      SPEC_RULE("Valid-Scalar");
      const auto size = PrimSize(ctx, prim->name);
      return size.has_value() && bits.size() == *size;
    }
    if (const auto *perm = std::get_if<ultraviolet::analysis::TypePerm>(&type->node))
    {
      return ValidValue(ctx, perm->base, bits);
    }
    if (const auto *ptr = std::get_if<ultraviolet::analysis::TypePtr>(&type->node))
    {
      const auto zero = LEBytesU64(0, ptr_size);
      if (ptr->state == ultraviolet::analysis::PtrState::Valid)
      {
        return bits.size() == ptr_size && bits != zero;
      }
      if (ptr->state == ultraviolet::analysis::PtrState::Null)
      {
        return bits == zero;
      }
      return bits.size() == ptr_size;
    }
    if (std::holds_alternative<ultraviolet::analysis::TypeRawPtr>(type->node))
    {
      return bits.size() == ptr_size;
    }
    if (const auto *tuple = std::get_if<ultraviolet::analysis::TypeTuple>(&type->node))
    {
      const auto layout = RecordLayoutOf(ctx, tuple->elements);
      if (!layout.has_value())
      {
        return false;
      }
      return ValidStructBits(ctx, tuple->elements, layout->offsets,
                             layout->layout.size, bits);
    }
    if (const auto *array = std::get_if<ultraviolet::analysis::TypeArray>(&type->node))
    {
      const auto elem_size = SizeOf(ctx, array->element);
      if (!elem_size.has_value())
      {
        return false;
      }
      const std::size_t elem_sz = static_cast<std::size_t>(*elem_size);
      if (bits.size() != elem_sz * array->length)
      {
        return false;
      }
      for (std::size_t i = 0; i < array->length; ++i)
      {
        const auto slice = SliceBytes(bits, i * elem_sz, elem_sz);
        if (!slice.has_value())
        {
          return false;
        }
        if (!ValidValue(ctx, array->element, *slice))
        {
          return false;
        }
      }
      return true;
    }
    if (std::holds_alternative<ultraviolet::analysis::TypeSlice>(type->node))
    {
      return bits.size() == 2 * ptr_size;
    }
    if (ultraviolet::analysis::IsRangeType(type))
    {
      const auto layout = RangeLayoutOf(ctx, type);
      if (!layout.has_value())
      {
        return false;
      }
      const auto fields = RangeFieldTypes(type);
      if (!fields.has_value())
      {
        return false;
      }
      return ValidStructBits(ctx, *fields, layout->offsets, layout->layout.size,
                             bits);
    }
    if (std::holds_alternative<ultraviolet::analysis::TypeDynamic>(type->node))
    {
      const auto dyn = DynLayoutOf(ctx);
      return bits.size() == dyn.layout.size;
    }
    if (std::holds_alternative<ultraviolet::analysis::TypeString>(type->node))
    {
      const auto size = SizeOf(ctx, type);
      return size.has_value() && bits.size() == *size;
    }
    if (std::holds_alternative<ultraviolet::analysis::TypeBytes>(type->node))
    {
      const auto size = SizeOf(ctx, type);
      return size.has_value() && bits.size() == *size;
    }
    // Additional type cases would be handled here for full ValidValue support
    return false;
  }

  namespace
  {

    std::optional<std::vector<std::uint8_t>> ValueBitsForRecord(
        const ultraviolet::analysis::ScopeContext &ctx,
        const std::vector<std::pair<std::string, ultraviolet::analysis::TypeRef>> &fields,
        const RecordVal &val)
    {
      std::vector<std::vector<std::uint8_t>> bits;
      std::vector<ultraviolet::analysis::TypeRef> types;
      bits.reserve(fields.size());
      types.reserve(fields.size());

      for (const auto &field : fields)
      {
        const auto it = std::find_if(val.fields.begin(), val.fields.end(),
                                     [&](const auto &entry)
                                     {
                                       return ultraviolet::analysis::IdEq(entry.first,
                                                                      field.first);
                                     });
        if (it == val.fields.end())
        {
          return std::nullopt;
        }
        const auto field_bits = ValueBits(ctx, field.second, it->second);
        if (!field_bits.has_value())
        {
          return std::nullopt;
        }
        bits.push_back(*field_bits);
        types.push_back(field.second);
      }

      const auto layout = RecordLayoutOf(ctx, types);
      if (!layout.has_value())
      {
        return std::nullopt;
      }

      return StructBits(bits, layout->offsets, layout->layout.size);
    }

  } // namespace

  std::optional<Value> TupleValue(const TupleVal& tuple, std::size_t index)
  {
    if (index >= tuple.elements.size())
    {
      return std::nullopt;
    }
    return tuple.elements[index];
  }

  std::optional<TupleVal> TupleUpdate(const TupleVal& tuple,
                                      std::size_t index,
                                      Value value)
  {
    if (index >= tuple.elements.size())
    {
      return std::nullopt;
    }
    TupleVal updated = tuple;
    updated.elements[index] = std::move(value);
    return updated;
  }

  const Value* FieldValue(const RecordVal& record, std::string_view name)
  {
    for (const auto& entry : record.fields)
    {
      if (ultraviolet::analysis::IdEq(entry.first, name))
      {
        return &entry.second;
      }
    }
    return nullptr;
  }

  std::optional<RecordVal> FieldUpdate(const RecordVal& record,
                                       std::string_view name,
                                       Value value)
  {
    RecordVal updated = record;
    for (auto& entry : updated.fields)
    {
      if (ultraviolet::analysis::IdEq(entry.first, name))
      {
        entry.second = std::move(value);
        return updated;
      }
    }
    return std::nullopt;
  }

  std::optional<ArrayVal> IndexUpdate(const ArrayVal& array,
                                      std::size_t index,
                                      Value value)
  {
    if (index >= array.elements.size())
    {
      return std::nullopt;
    }
    ArrayVal updated = array;
    updated.elements[index] = std::move(value);
    return updated;
  }

  std::size_t SliceLen(const ArrayVal& array)
  {
    return array.elements.size();
  }

  std::optional<std::size_t> SliceLen(const Value& value)
  {
    if (const auto* array = std::get_if<ArrayVal>(&value.node))
    {
      return SliceLen(*array);
    }
    if (const auto* slice = std::get_if<SliceVal>(&value.node))
    {
      return static_cast<std::size_t>(slice->length);
    }
    return std::nullopt;
  }

  std::optional<Value> SliceElem(const ArrayVal& array, std::size_t index)
  {
    if (index >= array.elements.size())
    {
      return std::nullopt;
    }
    return array.elements[index];
  }

  std::optional<std::vector<std::uint8_t>> ValueBits(
      const ultraviolet::analysis::ScopeContext &ctx,
      const ultraviolet::analysis::TypeRef &type,
      const Value &value)
  {
    if (!type)
    {
      return std::nullopt;
    }
    const std::uint64_t ptr_size = PtrSize(ctx);

    if (const auto *prim = std::get_if<ultraviolet::analysis::TypePrim>(&type->node))
    {
      if (prim->name == "bool")
      {
        if (const auto *v = std::get_if<BoolVal>(&value.node))
        {
          return LEBytesU64(v->value ? 1 : 0, 1);
        }
      }
      if (prim->name == "char")
      {
        if (const auto *v = std::get_if<CharVal>(&value.node))
        {
          return LEBytesU64(v->value, 4);
        }
      }
      if (prim->name == "()")
      {
        if (std::holds_alternative<UnitVal>(value.node))
        {
          return std::vector<std::uint8_t>{};
        }
      }
      if (prim->name == "!")
      {
        return std::nullopt;
      }
      if (const auto *v = std::get_if<IntVal>(&value.node))
      {
        if (v->type == prim->name)
        {
          const auto size = PrimSize(ctx, prim->name);
          if (!size.has_value())
          {
            return std::nullopt;
          }
          return LEBytes(v->value, *size);
        }
      }
      if (const auto *v = std::get_if<FloatVal>(&value.node))
      {
        if (v->type == prim->name)
        {
          const auto size = PrimSize(ctx, prim->name);
          if (!size.has_value())
          {
            return std::nullopt;
          }
          return LEBytesU64(v->bits, *size);
        }
      }
      return std::nullopt;
    }

    if (const auto *perm = std::get_if<ultraviolet::analysis::TypePerm>(&type->node))
    {
      return ValueBits(ctx, perm->base, value);
    }

    if (const auto *ptr = std::get_if<ultraviolet::analysis::TypePtr>(&type->node))
    {
      if (const auto *v = std::get_if<PtrVal>(&value.node))
      {
        if (ptr->state.has_value() && v->state != *ptr->state)
        {
          return std::nullopt;
        }
        if (v->state == ultraviolet::analysis::PtrState::Valid && v->addr == 0)
        {
          return std::nullopt;
        }
        if (v->state == ultraviolet::analysis::PtrState::Null && v->addr != 0)
        {
          return std::nullopt;
        }
        return LEBytesU64(v->addr, ptr_size);
      }
      return std::nullopt;
    }

    if (const auto *raw = std::get_if<ultraviolet::analysis::TypeRawPtr>(&type->node))
    {
      if (const auto *v = std::get_if<RawPtrVal>(&value.node))
      {
        if (v->qual != raw->qual)
        {
          return std::nullopt;
        }
        return LEBytesU64(v->addr, ptr_size);
      }
      return std::nullopt;
    }

    if (const auto *tuple = std::get_if<ultraviolet::analysis::TypeTuple>(&type->node))
    {
      const auto *v = std::get_if<TupleVal>(&value.node);
      if (!v || v->elements.size() != tuple->elements.size())
      {
        return std::nullopt;
      }
      std::vector<std::vector<std::uint8_t>> bits;
      bits.reserve(tuple->elements.size());
      for (std::size_t i = 0; i < tuple->elements.size(); ++i)
      {
        const auto elem_bits = ValueBits(ctx, tuple->elements[i], v->elements[i]);
        if (!elem_bits.has_value())
        {
          return std::nullopt;
        }
        bits.push_back(*elem_bits);
      }
      const auto layout = RecordLayoutOf(ctx, tuple->elements);
      if (!layout.has_value())
      {
        return std::nullopt;
      }
      return StructBits(bits, layout->offsets, layout->layout.size);
    }

    if (const auto *array = std::get_if<ultraviolet::analysis::TypeArray>(&type->node))
    {
      const auto *v = std::get_if<ArrayVal>(&value.node);
      if (!v || v->elements.size() != array->length)
      {
        return std::nullopt;
      }
      const auto elem_size = SizeOf(ctx, array->element);
      if (!elem_size.has_value())
      {
        return std::nullopt;
      }
      std::vector<std::uint8_t> out;
      out.reserve(static_cast<std::size_t>(*elem_size * array->length));
      for (const auto &elem : v->elements)
      {
        const auto elem_bits = ValueBits(ctx, array->element, elem);
        if (!elem_bits.has_value())
        {
          return std::nullopt;
        }
        out.insert(out.end(), elem_bits->begin(), elem_bits->end());
      }
      return out;
    }

    if (std::holds_alternative<ultraviolet::analysis::TypeSlice>(type->node))
    {
      const auto *v = std::get_if<SliceVal>(&value.node);
      if (!v)
      {
        return std::nullopt;
      }
      if (v->ptr.qual != ultraviolet::analysis::RawPtrQual::Imm)
      {
        return std::nullopt;
      }
      std::vector<std::uint8_t> bits;
      const auto ptr_bits = LEBytesU64(v->ptr.addr, ptr_size);
      const auto len_bits = LEBytesU64(v->length, ptr_size);
      bits.insert(bits.end(), ptr_bits.begin(), ptr_bits.end());
      bits.insert(bits.end(), len_bits.begin(), len_bits.end());
      return bits;
    }

    if (ultraviolet::analysis::IsRangeType(type))
    {
      const auto *v = std::get_if<ValueRangeVal>(&value.node);
      if (!v)
      {
        return std::nullopt;
      }

      const auto expected_kind = ExpectedValueRangeKind(type);
      if (!expected_kind.has_value() || *expected_kind != v->kind)
      {
        return std::nullopt;
      }

      const auto field_types_opt = RangeFieldTypes(type);
      const auto layout = RangeLayoutOf(ctx, type);
      if (!field_types_opt.has_value() || !layout.has_value())
      {
        return std::nullopt;
      }
      const auto &field_types = *field_types_opt;

      std::vector<Value> field_values;
      field_values.reserve(field_types.size());

      auto push_bound = [&](const std::optional<std::uint64_t> &bound,
                            std::size_t index) -> bool
      {
        if (!bound.has_value() || index >= field_types.size())
        {
          return false;
        }
        const auto converted = RangeBoundValueForType(field_types[index], *bound);
        if (!converted.has_value())
        {
          return false;
        }
        field_values.push_back(*converted);
        return true;
      };

      switch (v->kind)
      {
      case ValueRangeKind::Full:
        if (!field_types.empty())
        {
          return std::nullopt;
        }
        break;
      case ValueRangeKind::From:
        if (!push_bound(v->lo, 0))
        {
          return std::nullopt;
        }
        break;
      case ValueRangeKind::To:
      case ValueRangeKind::ToInclusive:
        if (!push_bound(v->hi, 0))
        {
          return std::nullopt;
        }
        break;
      case ValueRangeKind::Exclusive:
      case ValueRangeKind::Inclusive:
        if (!push_bound(v->lo, 0) || !push_bound(v->hi, 1))
        {
          return std::nullopt;
        }
        break;
      }

      std::vector<std::vector<std::uint8_t>> field_bits;
      field_bits.reserve(field_values.size());
      for (std::size_t i = 0; i < field_values.size(); ++i)
      {
        const auto encoded = ValueBits(ctx, field_types[i], field_values[i]);
        if (!encoded.has_value())
        {
          return std::nullopt;
        }
        field_bits.push_back(*encoded);
      }
      return StructBits(field_bits, layout->offsets, layout->layout.size);
    }

    if (std::holds_alternative<ultraviolet::analysis::TypeDynamic>(type->node))
    {
      const auto *v = std::get_if<DynamicVal>(&value.node);
      if (!v)
      {
        return std::nullopt;
      }
      const auto dyn = DynLayoutOf(ctx);
      std::vector<std::vector<std::uint8_t>> bits;
      bits.push_back(LEBytesU64(v->data, ptr_size));
      bits.push_back(LEBytesU64(v->vtable, ptr_size));
      std::vector<std::uint64_t> offsets = {0, ptr_size};
      return StructBits(bits, offsets, dyn.layout.size);
    }

    if (std::holds_alternative<ultraviolet::analysis::TypeString>(type->node))
    {
      if (!std::holds_alternative<StringVal>(value.node))
      {
        return std::nullopt;
      }
      const auto size = SizeOf(ctx, type);
      if (!size.has_value())
      {
        return std::nullopt;
      }
      return std::vector<std::uint8_t>(*size, 0);
    }

    if (std::holds_alternative<ultraviolet::analysis::TypeBytes>(type->node))
    {
      if (!std::holds_alternative<BytesVal>(value.node))
      {
        return std::nullopt;
      }
      const auto size = SizeOf(ctx, type);
      if (!size.has_value())
      {
        return std::nullopt;
      }
      return std::vector<std::uint8_t>(*size, 0);
    }

    return std::nullopt;
  }

} // namespace ultraviolet::analysis::layout
