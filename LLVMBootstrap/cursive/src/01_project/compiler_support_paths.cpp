#include "01_project/compiler_support_paths.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "00_core/compiler_support.h"
#include "00_core/host/services.h"
#include "01_project/project.h"
#include "01_project/target_profile.h"

namespace cursive::project {

namespace {

bool DirExists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_directory(path, ec) && !ec;
}

bool FileExists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec) && !ec;
}

bool IsTargetSidecarFile(TargetProfile profile,
                         const std::filesystem::path& path) {
  if (!FileExists(path)) {
    return false;
  }
  const std::string name = path.filename().generic_string();
  if (ObjectFormatOf(profile) == ObjectFormat::Coff) {
    return path.extension() == ".dll";
  }
  return name.find(".so") != std::string::npos || name == "icudt72l.dat";
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
    if (!IsTargetSidecarFile(profile, entry.path())) {
      continue;
    }
    out.push_back(entry.path());
  }
}

}  // namespace

std::string_view PackagedSupportPlatformDir(TargetProfile profile) {
  return ObjectFormatOf(profile) == ObjectFormat::Coff ? "windows" : "linux";
}

std::filesystem::path CompilerExecutableDir(const Project&) {
  const auto executable = core::CurrentExecutablePath();
  return executable.empty() ? std::filesystem::path()
                            : executable.parent_path();
}

std::filesystem::path CompilerSupportRoot(const Project& project) {
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
  const std::filesystem::path runtime_name(RuntimeLibNameFor(profile));
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

}  // namespace cursive::project
