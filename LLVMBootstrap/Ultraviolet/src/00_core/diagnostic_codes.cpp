#include "00_core/diagnostic_codes.h"

#include "00_core/assert_spec.h"

namespace ultraviolet::core {

namespace {

#include "generated/diag_registry.inc"

bool IsUpperAscii(char c) {
  return c >= 'A' && c <= 'Z';
}

bool IsDigitAscii(char c) {
  return c >= '0' && c <= '9';
}

bool IsDiagCodeLike(std::string_view value) {
  return (value.size() == 10 || value.size() == 11) &&
         (value[0] == 'E' || value[0] == 'W' || value[0] == 'I' ||
          value[0] == 'P') &&
         value[1] == '-' &&
         IsUpperAscii(value[2]) &&
         IsUpperAscii(value[3]) &&
         IsUpperAscii(value[4]) &&
         (value.size() == 10 ? value[5] == '-' : IsUpperAscii(value[5])) &&
         value[value.size() - 5] == '-' &&
         IsDigitAscii(value[value.size() - 4]) &&
         IsDigitAscii(value[value.size() - 3]) &&
         IsDigitAscii(value[value.size() - 2]) &&
         IsDigitAscii(value[value.size() - 1]);
}

}  // namespace

static inline void SpecDefsDiagCodeTypes() {
  SPEC_DEF("DiagId", "1.2");
  SPEC_DEF("DiagCode", "0.4");
  SPEC_DEF("DiagCategory", "0.4");
  SPEC_DEF("DiagDigits", "0.4");
  SPEC_DEF("Bucket", "0.4");
  SPEC_DEF("Seq", "0.4");
}

std::optional<DiagCode> SpecCode(const DiagCodeMap& spec_map, const DiagId& id) {
  SpecDefsDiagCodeTypes();
  auto it = spec_map.find(id);
  if (it == spec_map.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<DiagCode> UVCode(const DiagCodeMap& uv_map, const DiagId& id) {
  SpecDefsDiagCodeTypes();
  auto it = uv_map.find(id);
  if (it == uv_map.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<DiagCode> Code(const DiagCodeMap& spec_map,
                             const DiagCodeMap& uv_map,
                             const DiagId& id) {
  SpecDefsDiagCodeTypes();
  (void)uv_map;
  if (const auto spec = SpecCode(spec_map, id)) {
    SPEC_RULE("Code");
    return spec;
  }
  return std::nullopt;
}

const DiagCodeMap& SpecDiagCodeMap() {
  static const DiagCodeMap map = []() {
    DiagCodeMap out;
    for (const auto& entry : kDiagIdCodeMapEntries) {
      if (!entry.diag_id || !entry.code) {
        continue;
      }
      if (IsDiagCodeLike(entry.diag_id)) {
        out.emplace(std::string(entry.diag_id), std::string(entry.code));
      }
    }
    return out;
  }();
  return map;
}

const DiagCodeMap& UVDiagCodeMap() {
  static const DiagCodeMap map = []() {
    DiagCodeMap out;
    for (const auto& entry : kDiagIdCodeMapEntries) {
      if (!entry.diag_id || !entry.code) {
        continue;
      }
      if (!IsDiagCodeLike(entry.diag_id)) {
        out.emplace(std::string(entry.diag_id), std::string(entry.code));
      }
    }
    return out;
  }();
  return map;
}

std::optional<DiagCode> ResolveDiagCode(const DiagId& id) {
  const auto resolved = Code(SpecDiagCodeMap(), UVDiagCodeMap(), id);
  if (resolved.has_value()) {
    return resolved;
  }
  if (const auto c0 = UVCode(UVDiagCodeMap(), id); c0.has_value()) {
    return c0;
  }
  if (IsDiagCodeLike(id)) {
    return id;
  }
  return std::nullopt;
}

bool IsDiagCategory(std::string_view category) {
  SpecDefsDiagCodeTypes();
  return category.size() == 3 && IsUpperAscii(category[0]) &&
         IsUpperAscii(category[1]) && IsUpperAscii(category[2]);
}

bool IsDiagDigits(std::string_view digits) {
  SpecDefsDiagCodeTypes();
  return digits.size() == 4 && IsDigitAscii(digits[0]) &&
         IsDigitAscii(digits[1]) && IsDigitAscii(digits[2]) &&
         IsDigitAscii(digits[3]);
}

std::optional<std::string> DiagBucket(std::string_view digits) {
  SpecDefsDiagCodeTypes();
  if (!IsDiagDigits(digits)) {
    return std::nullopt;
  }
  return std::string(digits.substr(0, 2));
}

std::optional<std::string> DiagSeq(std::string_view digits) {
  SpecDefsDiagCodeTypes();
  if (!IsDiagDigits(digits)) {
    return std::nullopt;
  }
  return std::string(digits.substr(2, 2));
}

}  // namespace ultraviolet::core
