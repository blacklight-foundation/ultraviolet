#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace cursive::core {

enum class CompilerSupportLayoutKind {
  None,
  PackagedOut,
  BuildTree,
};

bool EnsureBundledHostCompilerSupport(std::string* error_message = nullptr);
CompilerSupportLayoutKind CompilerSupportLayout();
std::optional<std::filesystem::path> CompilerSupportRootPath();
std::optional<std::filesystem::path> HostCompilerSidecarBinDirPath();
std::optional<std::filesystem::path> CompilerSupportAssetPath(
    const std::filesystem::path& packaged_relative_path,
    const std::filesystem::path& build_tree_relative_path);

}  // namespace cursive::core
