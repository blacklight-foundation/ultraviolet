#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ultraviolet::core {

std::vector<std::string> RuntimeLinkRequiredSyms(std::string_view runtime_root);
std::vector<std::string> RuntimeLinkRequiredSyms();

}  // namespace ultraviolet::core
