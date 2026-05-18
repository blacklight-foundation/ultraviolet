#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Note: assert_spec.h is NOT included here to avoid circular dependency with span.h

namespace ultraviolet::core {

class UnicodeScalar {
 public:
  constexpr UnicodeScalar() = default;

  constexpr UnicodeScalar(std::uint32_t value) : value_(Validate(value)) {}

  [[nodiscard]] static constexpr bool IsValue(std::uint32_t value) {
    return value <= 0x10FFFFu &&
           !(value >= 0xD800u && value <= 0xDFFFu);
  }

  [[nodiscard]] constexpr std::uint32_t value() const {
    return value_;
  }

  constexpr operator std::uint32_t() const {
    return value_;
  }

 private:
  [[nodiscard]] static constexpr std::uint32_t Validate(
      std::uint32_t value) {
    if (!IsValue(value)) {
      throw "invalid Unicode scalar";
    }
    return value;
  }

  std::uint32_t value_ = 0;
};

using Scalars = std::vector<UnicodeScalar>;
using String = Scalars;

inline Scalars NormalizeOutsideIdentifiers(const Scalars& scalars) {
  return scalars;
}

constexpr UnicodeScalar kLF = 0x0A;
constexpr UnicodeScalar kCR = 0x0D;

constexpr UnicodeScalar kBOM = 0xFEFF;

inline std::size_t Utf8Len(UnicodeScalar u) {
  if (u <= 0x7F) {
    return 1;
  }
  if (u <= 0x7FF) {
    return 2;
  }
  if (u <= 0xFFFF) {
    return 3;
  }
  return 4;
}

inline std::vector<std::size_t> Utf8Offsets(const std::vector<UnicodeScalar>& scalars) {
  std::vector<std::size_t> offsets;
  offsets.reserve(scalars.size() + 1);
  std::size_t acc = 0;
  offsets.push_back(0);
  for (UnicodeScalar u : scalars) {
    acc += Utf8Len(u);
    offsets.push_back(acc);
  }
  return offsets;
}

inline std::vector<std::size_t> LineStarts(const std::vector<UnicodeScalar>& scalars) {
  std::vector<std::size_t> starts;
  starts.reserve(1 + scalars.size());
  starts.push_back(0);
  if (scalars.empty()) {
    return starts;
  }
  const auto offsets = Utf8Offsets(scalars);
  for (std::size_t i = 0; i < scalars.size(); ++i) {
    if (scalars[i] == kLF) {
      starts.push_back(offsets[i] + 1);
    }
  }
  return starts;
}

inline std::size_t ByteLenFromScalars(const std::vector<UnicodeScalar>& scalars) {
  const auto offsets = Utf8Offsets(scalars);
  return offsets.empty() ? 0 : offsets.back();
}

inline void AppendUtf8(std::string& out, UnicodeScalar u) {
  if (u <= 0x7F) {
    out.push_back(static_cast<char>(u));
  } else if (u <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (u >> 6)));
    out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
  } else if (u <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (u >> 12)));
    out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (u >> 18)));
    out.push_back(static_cast<char>(0x80 | ((u >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
  }
}

inline std::string EncodeUtf8(const std::vector<UnicodeScalar>& scalars) {
  std::string out;
  out.reserve(ByteLenFromScalars(scalars));
  for (UnicodeScalar u : scalars) {
    AppendUtf8(out, u);
  }
  return out;
}

struct DecodeResult {
  std::vector<UnicodeScalar> scalars;
  bool ok = false;
};

DecodeResult Decode(const std::vector<std::uint8_t>& bytes);

struct StripBOMResult {
  std::vector<UnicodeScalar> scalars;
  bool had_bom = false;
  std::optional<std::size_t> embedded_index;
};

StripBOMResult StripBOM(const std::vector<UnicodeScalar>& scalars);

std::vector<UnicodeScalar> NormalizeLF(const std::vector<UnicodeScalar>& scalars);

struct SourceFile {
  std::string path;
  std::vector<std::uint8_t> bytes;
  std::vector<UnicodeScalar> scalars;
  std::string text;
  std::size_t byte_len = 0;
  std::vector<std::size_t> line_starts;
  std::size_t line_count = 0;
};

}  // namespace ultraviolet::core
