#include "01_project/language_profile.h"

#include <system_error>
#include <vector>

#include "00_core/symbols.h"

namespace ultraviolet::project {

namespace {

SourceLanguage& ActiveSourceLanguage() {
  static SourceLanguage language = SourceLanguage::Ultraviolet;
  return language;
}

std::filesystem::path StartDirForInput(const std::filesystem::path& input_path) {
  std::filesystem::path dir = input_path;
  if (dir.empty()) {
    dir = ".";
  }

  std::error_code ec;
  if (dir.is_relative()) {
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec && !cwd.empty()) {
      dir = cwd / dir;
    }
  }

  dir = dir.lexically_normal();
  ec.clear();
  const bool exists = std::filesystem::exists(dir, ec);
  if (!ec && exists && !std::filesystem::is_directory(dir, ec) &&
      dir.has_filename()) {
    dir = dir.parent_path();
  }

  if (dir.empty()) {
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec) {
      dir = cwd;
    }
  }

  if (dir.empty()) {
    dir = ".";
  }
  return dir;
}

bool ExistsNoError(const std::filesystem::path& path) {
  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec);
  return !ec && exists;
}

std::optional<SourceLanguage> SourceLanguageForExtension(
    const std::filesystem::path& path) {
  const std::string extension = path.extension().string();
  if (extension == UltravioletLanguageProfile().source_extension) {
    return SourceLanguage::Ultraviolet;
  }
  return std::nullopt;
}

}  // namespace

const LanguageProfile& UltravioletLanguageProfile() {
  static const LanguageProfile profile{
      SourceLanguage::Ultraviolet,
      "Ultraviolet",
      "Ultraviolet.toml",
      ".uv",
      "ultraviolet",
      "ultraviolet",
      "UltravioletRT.a",
      "UltravioletRT.lib",
      "uv_start_x86_64_sysv.o",
      "libUltravioletRTSupport.so",
      "__uv_library_entry",
      "__uv_library_ctor",
      "__uv_library_dtor",
      "__uv_library_attached",
      "__uv_image_panic_record",
      "__uv_host_abi_version",
      "__uv_host_session_create",
      "__uv_host_session_destroy",
      "__uv_host_session_owner_token",
      "uv_host_alloc",
      "uv_host_free",
      "uv_host_session_register",
      "uv_host_session_try_enter",
      "uv_host_session_leave",
      "uv_host_session_try_retire",
      "uv_host_session_abort_live",
      "uv_host_session_current_env",
      "uv_host_session_enter_retired",
      "uv_host_session_leave_retired",
      "uv_host_session_abort_retired",
      "uv_raw_dylib_resolve",
      "ultraviolet_x3a_x3aruntime_x3a_x3ainit_x3a_x3a",
      "ultraviolet_x3a_x3aruntime_x3a_x3adeinit_x3a_x3a",
      "__uv_",
      "uv_",
      "__uv_region_active",
      "__uv_session",
      ".uv-incremental",
  };
  return profile;
}

const LanguageProfile& LanguageProfileFor(SourceLanguage language) {
  switch (language) {
    case SourceLanguage::Ultraviolet:
      return UltravioletLanguageProfile();
  }
  return UltravioletLanguageProfile();
}

const LanguageProfile& ActiveLanguageProfile() {
  return LanguageProfileFor(ActiveSourceLanguage());
}

void SetActiveLanguageProfile(SourceLanguage language) {
  ActiveSourceLanguage() = language;
}

std::optional<SourceLanguage> DetectSourceLanguageForInput(
    const std::filesystem::path& input_path) {
  if (const auto language = SourceLanguageForExtension(input_path)) {
    return language;
  }

  std::filesystem::path current = StartDirForInput(input_path);
  for (;;) {
    const bool has_ultraviolet_manifest = ExistsNoError(
        current / std::string(UltravioletLanguageProfile().manifest_name));

    if (has_ultraviolet_manifest) {
      return SourceLanguage::Ultraviolet;
    }

    const auto parent = current.parent_path();
    if (parent.empty() || parent == current) {
      break;
    }
    current = parent;
  }

  return std::nullopt;
}

std::string LanguagePathSig(std::initializer_list<std::string_view> tail) {
  std::vector<std::string> path;
  path.reserve(tail.size() + 1);
  path.emplace_back(ActiveLanguageProfile().runtime_root);
  for (const std::string_view segment : tail) {
    path.emplace_back(segment);
  }
  return core::Mangle(core::StringOfPath(path));
}

std::string RuntimePathSig(std::initializer_list<std::string_view> tail) {
  std::vector<std::string> path;
  path.reserve(tail.size() + 2);
  path.emplace_back(ActiveLanguageProfile().runtime_root);
  path.emplace_back("runtime");
  for (const std::string_view segment : tail) {
    path.emplace_back(segment);
  }
  return core::Mangle(core::StringOfPath(path));
}

}  // namespace ultraviolet::project
