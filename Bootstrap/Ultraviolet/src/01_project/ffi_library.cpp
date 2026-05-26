#include "01_project/ffi_library.h"

#include <string_view>
#include <unordered_set>
#include <vector>

#include "01_project/target_platform.h"

namespace ultraviolet::project {

bool IsLibraryKindSupportedForCurrentTarget(std::string_view kind,
                                            TargetProfile profile) {
  return LibraryKindSupported(kind, profile);
}

std::optional<std::string> ResolveLibraryNameForCurrentTarget(
    std::string_view name,
    std::string_view kind,
    TargetProfile profile) {
  return ResolveLibraryName(kind, name, profile);
}

std::optional<std::filesystem::path> ResolveLibraryLinkInputForCurrentTarget(
    std::string_view name,
    std::string_view kind,
    TargetProfile profile) {
  return TargetLibraryLinkInput(name, kind, profile);
}

std::vector<std::filesystem::path> ResolveExternLibraryInputs(
    const std::vector<FfiLibrarySpec>& specs,
    TargetProfile profile) {
  std::vector<std::filesystem::path> out;
  std::unordered_set<std::string> seen;
  out.reserve(specs.size());
  for (const auto& spec : specs) {
    const auto resolved =
        ResolveLibraryLinkInputForCurrentTarget(spec.name, spec.kind, profile);
    if (!resolved.has_value()) {
      continue;
    }
    const std::string key = resolved->generic_string();
    if (!seen.insert(key).second) {
      continue;
    }
    out.push_back(*resolved);
  }
  return out;
}

}  // namespace ultraviolet::project
