#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace cursive::project {

using InvokeFn = std::optional<std::string> (*)(const std::filesystem::path& tool,
                                                std::string_view input);

std::optional<std::string> AssembleIRWithDeps(const std::filesystem::path& tool,
                                              std::string_view ir_text,
                                              InvokeFn invoke);

std::optional<std::string> AssembleIR(const std::filesystem::path& tool,
                                      std::string_view ir_text);

}  // namespace cursive::project
