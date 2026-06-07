#include "01_project/compiler_support_paths.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/compiler_support.h"
#include "00_core/host/services.h"
#include "01_project/project.h"
#include "01_project/target_platform.h"
#include "01_project/target_profile.h"

namespace ultraviolet::project {

namespace {

bool DirExists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_directory(path, ec) && !ec;
}

std::string_view BoolRecord(bool value) {
  return value ? "true" : "false";
}

std::string_view CompilerSupportLayoutRecordName(
    core::CompilerSupportLayoutKind layout) {
  switch (layout) {
    case core::CompilerSupportLayoutKind::None:
      return "none";
    case core::CompilerSupportLayoutKind::PackagedOut:
      return "packaged";
    case core::CompilerSupportLayoutKind::BuildTree:
      return "legacy";
  }
  return "unknown";
}

bool LegacySidecarsBeside(const std::filesystem::path& dir) {
  if (dir.empty()) {
    return false;
  }
  for (const char* rel : {"runtime", "tools", "bin", "lib"}) {
    if (DirExists(dir / rel)) {
      return true;
    }
  }
  return false;
}

bool PackagedHostSidecarsBeside(const std::filesystem::path& dir) {
  if (dir.empty()) {
    return false;
  }
  for (const char* platform : {"windows", "macos", "linux"}) {
    for (const char* subdir : {"tools", "bin", "lib"}) {
      if (DirExists(dir / platform / subdir)) {
        return true;
      }
    }
  }
  return false;
}

bool FileExists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec) && !ec;
}

std::optional<std::filesystem::path> CompilerSupportSubdir(
    TargetProfile profile,
    std::string_view subdir) {
  const auto support_root = core::CompilerSupportRootPath();
  if (!support_root.has_value()) {
    return std::nullopt;
  }

  std::filesystem::path dir;
  switch (core::CompilerSupportLayout()) {
    case core::CompilerSupportLayoutKind::PackagedOut:
      dir = *support_root / std::string(PackagedSupportPlatformDir(profile)) /
            std::string(subdir);
      break;
    case core::CompilerSupportLayoutKind::BuildTree:
      dir = *support_root / std::string(subdir);
      break;
    case core::CompilerSupportLayoutKind::None:
      return std::nullopt;
  }

  if (!DirExists(dir)) {
    return std::nullopt;
  }
  return dir;
}

void AppendSidecarFiles(TargetProfile profile,
                        const std::optional<std::filesystem::path>& dir,
                        std::vector<std::filesystem::path>& out) {
  if (!dir.has_value()) {
    return;
  }

  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(*dir, ec)) {
    if (ec) {
      return;
    }
    if (!FileExists(entry.path()) ||
        !TargetIsCompilerSupportSidecarFile(profile, entry.path())) {
      continue;
    }
    out.push_back(entry.path());
  }
}

void RecordCompilerSidecarLayoutPredicates(const Project& project) {
  if (!core::Conformance::Enabled()) {
    return;
  }

  const std::filesystem::path executable_dir = CompilerExecutableDir(project);
  const std::filesystem::path parent_dir = executable_dir.parent_path();
  const auto support_root = core::CompilerSupportRootPath();
  const auto layout = core::CompilerSupportLayout();
  const bool executable_legacy = LegacySidecarsBeside(executable_dir);
  const bool executable_packaged = PackagedHostSidecarsBeside(executable_dir);
  const bool parent_legacy = LegacySidecarsBeside(parent_dir);
  const std::filesystem::path selected_root =
      support_root.has_value() ? *support_root : executable_dir;
  std::string root_branch = "fallback_compiler_dir";
  if (executable_packaged) {
    root_branch = "packaged_compiler_dir";
  } else if (executable_legacy) {
    root_branch = "legacy_compiler_dir";
  } else if (parent_legacy) {
    root_branch = "legacy_parent_dir";
  }

  SPEC_RULE("def.CompilerSidecarLayoutPredicates");
  core::Conformance::Record(
      "def.CompilerSidecarLayoutPredicates",
      std::nullopt,
      "compiler_dir=" + executable_dir.generic_string() +
          ";legacy_beside=" +
          std::string(BoolRecord(executable_legacy)) +
          ";packaged_host_beside=" +
          std::string(BoolRecord(executable_packaged)) +
          ";parent=" + parent_dir.generic_string() +
          ";parent_legacy_beside=" +
          std::string(BoolRecord(parent_legacy)) +
          ";support_root=" +
          (support_root.has_value() ? support_root->generic_string()
                                    : std::string("<bottom>")) +
          ";layout=" + std::string(CompilerSupportLayoutRecordName(layout)));
  SPEC_RULE("def.CompilerSupportRoot");
  core::Conformance::Record(
      "def.CompilerSupportRoot",
      std::nullopt,
      "branch=" + root_branch + ";value=" + selected_root.generic_string());
}

}  // namespace

std::string_view PackagedSupportPlatformDir(TargetProfile profile) {
  return TargetPackagedSupportPlatformDir(profile);
}

std::filesystem::path CompilerExecutableDir(const Project&) {
  const auto executable = core::CurrentExecutablePath();
  return executable.empty() ? std::filesystem::path()
                            : executable.parent_path();
}

std::filesystem::path CompilerSupportRoot(const Project& project) {
  RecordCompilerSidecarLayoutPredicates(project);
  if (const auto support_root = core::CompilerSupportRootPath();
      support_root.has_value()) {
    return *support_root;
  }
  return CompilerExecutableDir(project);
}

std::filesystem::path CompilerToolBinDir(const Project& project,
                                         TargetProfile profile) {
  const auto support_root = CompilerSupportRoot(project);
  if (support_root.empty()) {
    return {};
  }
  if (core::CompilerSupportLayout() == core::CompilerSupportLayoutKind::PackagedOut) {
    return support_root / std::string(PackagedSupportPlatformDir(profile)) /
           "tools";
  }
  return support_root / "tools";
}

std::filesystem::path CompilerRuntimeLibPath(const Project& project,
                                             TargetProfile profile) {
  const std::filesystem::path executable_dir = CompilerExecutableDir(project);
  const std::filesystem::path runtime_name(TargetRuntimeLibName(profile));
  const auto support_root = CompilerSupportRoot(project);
  if (!support_root.empty()) {
    const auto layout = core::CompilerSupportLayout();
    if (layout == core::CompilerSupportLayoutKind::BuildTree) {
      const std::filesystem::path staged_runtime = support_root / "runtime" / runtime_name;
      if (FileExists(staged_runtime)) {
        return staged_runtime;
      }
    }
    if (layout == core::CompilerSupportLayoutKind::PackagedOut) {
      const std::filesystem::path beside_exe = support_root / runtime_name;
      if (FileExists(beside_exe)) {
        return beside_exe;
      }
    }
    const std::filesystem::path support_runtime = support_root / "runtime" / runtime_name;
    if (FileExists(support_runtime)) {
      return support_runtime;
    }
  }
  if (!executable_dir.empty()) {
    const std::filesystem::path beside_exe = executable_dir / runtime_name;
    if (FileExists(beside_exe)) {
      return beside_exe;
    }
  }
  if (support_root.empty()) {
    return executable_dir / runtime_name;
  }
  return support_root / "runtime" / runtime_name;
}

std::optional<std::filesystem::path> CompilerSupportToolBinDir(
    TargetProfile profile) {
  return CompilerSupportSubdir(profile, "tools");
}

std::optional<std::filesystem::path> CompilerSupportBinDir(
    TargetProfile profile) {
  return CompilerSupportSubdir(profile, "bin");
}

std::optional<std::filesystem::path> CompilerSupportLibDir(
    TargetProfile profile) {
  return CompilerSupportSubdir(profile, "lib");
}

std::vector<std::filesystem::path> CompilerExecutableSidecarPaths(
    TargetProfile profile) {
  std::vector<std::filesystem::path> out;
  if (ObjectFormatOf(profile) == ObjectFormat::Coff) {
    AppendSidecarFiles(profile, CompilerSupportBinDir(profile), out);
  } else {
    AppendSidecarFiles(profile, CompilerSupportLibDir(profile), out);
    AppendSidecarFiles(profile, CompilerSupportBinDir(profile), out);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

}  // namespace ultraviolet::project
