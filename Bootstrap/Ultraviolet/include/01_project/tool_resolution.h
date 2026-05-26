#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "01_project/target_profile.h"

namespace ultraviolet::project {

struct Project;

std::vector<std::filesystem::path> SearchDirs(const Project& project,
                                             TargetProfile target_profile);

std::optional<std::filesystem::path> ResolveTool(const Project& project,
                                                 TargetProfile target_profile,
                                                 std::string_view tool);

// Returns a formatted string listing the directories that were searched
// for the given tool. Used for error reporting when tool resolution fails.
std::string FormatSearchedPaths(const Project& project,
                                TargetProfile target_profile,
                                std::string_view tool);

}  // namespace ultraviolet::project
