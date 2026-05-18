#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/source_text.h"

namespace ultraviolet::core {

// Unicode 15.0.0 NFC + case folding.
std::string NFC(std::string_view s);
std::string CaseFold(std::string_view s);

struct IdentifierSecurityInfo {
  std::string normalized;
  std::string skeleton;
  bool mixed_script = false;
};

IdentifierSecurityInfo AnalyzeIdentifierSecurity(std::string_view ident);

bool IsXidStart(UnicodeScalar c);
bool IsXidContinue(UnicodeScalar c);
bool IsNonCharacter(UnicodeScalar c);
bool IsIdentStart(UnicodeScalar c);
bool IsIdentContinue(UnicodeScalar c);

bool IsSensitive(UnicodeScalar c);

bool IsProhibited(UnicodeScalar c);

std::optional<std::size_t> FirstProhibitedOutsideLiteral(
    const std::vector<UnicodeScalar>& scalars);

bool NoProhibited(const std::vector<UnicodeScalar>& scalars);

}  // namespace ultraviolet::core
