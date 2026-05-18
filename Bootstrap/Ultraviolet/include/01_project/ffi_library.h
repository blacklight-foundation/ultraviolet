#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "01_project/target_profile.h"

namespace ultraviolet::project {

struct FfiLibrarySpec {
  std::string name;
  std::string kind;
};

bool IsLibraryKindSupportedForCurrentTarget(std::string_view kind,
                                            TargetProfile profile);

std::optional<std::string> ResolveLibraryNameForCurrentTarget(
    std::string_view name,
    std::string_view kind,
    TargetProfile profile);

std::optional<std::filesystem::path> ResolveLibraryLinkInputForCurrentTarget(
    std::string_view name,
    std::string_view kind,
    TargetProfile profile);

std::vector<std::filesystem::path> ResolveExternLibraryInputs(
    const std::vector<FfiLibrarySpec>& specs,
    TargetProfile profile);

}  // namespace ultraviolet::project
