#pragma once

#include <string_view>

#include "00_core/keywords.h"

namespace cursive::core {

bool IsIdentifier(std::string_view ident);
bool IsName(std::string_view ident);

}  // namespace cursive::core
