#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ultraviolet::driver::tooling {

std::filesystem::path NormalizePath(const std::filesystem::path& path);
std::string PathKey(const std::filesystem::path& path);

std::string PathToFileUri(const std::filesystem::path& path);
std::optional<std::filesystem::path> FileUriToPath(std::string_view uri);

}  // namespace ultraviolet::driver::tooling
