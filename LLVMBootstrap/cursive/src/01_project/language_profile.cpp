#include "01_project/language_profile.h"

#include <system_error>
#include <vector>

#include "00_core/symbols.h"

namespace cursive::project {

namespace {

SourceLanguage& ActiveSourceLanguage() {
  static SourceLanguage language = SourceLanguage::Cursive;
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
  if (extension == CursiveLanguageProfile().source_extension) {
    return SourceLanguage::Cursive;
  }
  if (extension == UltravioletLanguageProfile().source_extension) {
    return SourceLanguage::Ultraviolet;
  }
  return std::nullopt;
}

}  // namespace

const LanguageProfile& CursiveLanguageProfile() {
  static const LanguageProfile profile{
      SourceLanguage::Cursive,
      "Cursive",
      "Cursive.toml",
      ".cursive",
      "cursive",
      "cursive",
      "CursiveRT.a",
      "CursiveRT.lib",
      "cursive0_start_x86_64_sysv.o",
      "libCursiveRTSupport.so",
      "__cursive_library_entry",
      "__cursive_library_ctor",
      "__cursive_library_dtor",
      "__cursive_library_attached",
      "__cursive_image_panic_record",
      "__cursive_host_abi_version",
      "__cursive_host_session_create",
      "__cursive_host_session_destroy",
      "__cursive_host_session_owner_token",
      "cursive_host_alloc",
      "cursive_host_free",
      "cursive_host_session_register",
      "cursive_host_session_try_enter",
      "cursive_host_session_leave",
      "cursive_host_session_try_retire",
      "cursive_host_session_abort_live",
      "cursive_host_session_current_env",
      "cursive_host_session_enter_retired",
      "cursive_host_session_leave_retired",
      "cursive_host_session_abort_retired",
      "cursive_raw_dylib_resolve",
      "cursive_x3a_x3aruntime_x3a_x3ainit_x3a_x3a",
      "cursive_x3a_x3aruntime_x3a_x3adeinit_x3a_x3a",
      "__cursive_",
      "cursive_",
      "__cursive_region_active",
      "__cursive_session",
      ".cursive-incremental",
  };
  return profile;
}

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
      "ultraviolet_start_x86_64_sysv.o",
      "libUltravioletRTSupport.so",
      "__ultraviolet_library_entry",
      "__ultraviolet_library_ctor",
      "__ultraviolet_library_dtor",
      "__ultraviolet_library_attached",
      "__ultraviolet_image_panic_record",
      "__ultraviolet_host_abi_version",
      "__ultraviolet_host_session_create",
      "__ultraviolet_host_session_destroy",
      "__ultraviolet_host_session_owner_token",
      "ultraviolet_host_alloc",
      "ultraviolet_host_free",
      "ultraviolet_host_session_register",
      "ultraviolet_host_session_try_enter",
      "ultraviolet_host_session_leave",
      "ultraviolet_host_session_try_retire",
      "ultraviolet_host_session_abort_live",
      "ultraviolet_host_session_current_env",
      "ultraviolet_host_session_enter_retired",
      "ultraviolet_host_session_leave_retired",
      "ultraviolet_host_session_abort_retired",
      "ultraviolet_raw_dylib_resolve",
      "ultraviolet_x3a_x3aruntime_x3a_x3ainit_x3a_x3a",
      "ultraviolet_x3a_x3aruntime_x3a_x3adeinit_x3a_x3a",
      "__ultraviolet_",
      "ultraviolet_",
      "__ultraviolet_region_active",
      "__ultraviolet_session",
      ".ultraviolet-incremental",
  };
  return profile;
}

const LanguageProfile& LanguageProfileFor(SourceLanguage language) {
  switch (language) {
    case SourceLanguage::Cursive:
      return CursiveLanguageProfile();
    case SourceLanguage::Ultraviolet:
      return UltravioletLanguageProfile();
  }
  return CursiveLanguageProfile();
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
    const bool has_cursive_manifest = ExistsNoError(
        current / std::string(CursiveLanguageProfile().manifest_name));
    const bool has_ultraviolet_manifest = ExistsNoError(
        current / std::string(UltravioletLanguageProfile().manifest_name));

    if (has_cursive_manifest != has_ultraviolet_manifest) {
      return has_ultraviolet_manifest ? SourceLanguage::Ultraviolet
                                      : SourceLanguage::Cursive;
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

}  // namespace cursive::project
