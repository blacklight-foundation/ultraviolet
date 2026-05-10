#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "01_project/target_profile.h"

namespace cursive::project {

struct Project;

std::string_view PackagedSupportPlatformDir(TargetProfile profile);
std::filesystem::path CompilerExecutableDir(const Project& project);
std::filesystem::path CompilerSupportRoot(const Project& project);
std::filesystem::path CompilerToolBinDir(const Project& project,
                                         TargetProfile profile);
std::filesystem::path CompilerRuntimeLibPath(const Project& project,
                                             TargetProfile profile);
std::optional<std::filesystem::path> CompilerSupportToolBinDir(
    TargetProfile profile);
std::optional<std::filesystem::path> CompilerSupportBinDir(
    TargetProfile profile);
std::optional<std::filesystem::path> CompilerSupportLibDir(
    TargetProfile profile);
std::vector<std::filesystem::path> CompilerExecutableSidecarPaths(
    TargetProfile profile);

}  // namespace cursive::project
