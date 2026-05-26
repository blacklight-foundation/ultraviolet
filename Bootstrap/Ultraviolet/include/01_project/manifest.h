#pragma once

#include <filesystem>
#include <optional>

#include <toml++/toml.hpp>

#include "00_core/diagnostics.h"
#include "01_project/language_profile.h"

namespace ultraviolet::project {

using TOMLTable = toml::table;

struct ManifestParseResult {
  std::optional<TOMLTable> table;
  core::DiagnosticStream diags;
};

std::filesystem::path FindProjectRoot(const std::filesystem::path& input_path);
std::filesystem::path FindProjectRoot(const std::filesystem::path& input_path,
                                      const LanguageProfile& language);

ManifestParseResult ParseManifest(const std::filesystem::path& project_root);
ManifestParseResult ParseManifest(const std::filesystem::path& project_root,
                                  const LanguageProfile& language);

}  // namespace ultraviolet::project
