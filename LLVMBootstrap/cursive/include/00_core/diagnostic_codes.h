#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "00_core/diagnostics.h"

namespace cursive::core {

using DiagId = std::string;
using DiagCodeMap = std::unordered_map<DiagId, DiagCode>;

std::optional<DiagCode> SpecCode(const DiagCodeMap& spec_map, const DiagId& id);
std::optional<DiagCode> C0Code(const DiagCodeMap& c0_map, const DiagId& id);
std::optional<DiagCode> Code(const DiagCodeMap& spec_map,
                             const DiagCodeMap& c0_map,
                             const DiagId& id);
const DiagCodeMap& SpecDiagCodeMap();
const DiagCodeMap& C0DiagCodeMap();
std::optional<DiagCode> ResolveDiagCode(const DiagId& id);
bool IsDiagCategory(std::string_view category);
bool IsDiagDigits(std::string_view digits);
std::optional<std::string> DiagBucket(std::string_view digits);
std::optional<std::string> DiagSeq(std::string_view digits);

}  // namespace cursive::core
