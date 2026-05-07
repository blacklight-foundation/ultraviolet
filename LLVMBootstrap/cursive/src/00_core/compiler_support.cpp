#include "00_core/compiler_support.h"
#include "00_core/host/services.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace cursive::core {

namespace {

struct BundleState {
  bool initialized = false;
  bool success = false;
  CompilerSupportLayoutKind layout = CompilerSupportLayoutKind::None;
  std::filesystem::path support_root;
  std::filesystem::path host_sidecar_bin_dir;
  std::string error_message;
};

std::once_flag g_bundle_once;
BundleState g_bundle_state;

std::filesystem::path CurrentExecutableDir() {
  const auto executable = CurrentExecutablePath();
  return executable.empty() ? std::filesystem::path()
                            : executable.parent_path();
}

bool FileExists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec) && !ec;
}

bool DirExists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_directory(path, ec) && !ec;
}

bool IsBuildTreeSupportRootCandidate(const std::filesystem::path& candidate) {
  if (candidate.empty()) {
    return false;
  }
  for (const char* rel : {"runtime", "tools", "bin", "lib"}) {
    if (DirExists(candidate / rel)) {
      return true;
    }
  }
  return false;
}

bool IsPackagedSupportRootCandidate(const std::filesystem::path& candidate) {
  if (candidate.empty()) {
    return false;
  }
  return DirExists(candidate / "windows") || DirExists(candidate / "linux");
}

struct SupportLayout {
  CompilerSupportLayoutKind kind = CompilerSupportLayoutKind::None;
  std::filesystem::path root;
};

SupportLayout ResolveSupportLayout() {
  const auto executable_dir = CurrentExecutableDir();
  const auto executable = CurrentExecutablePath();
  const auto executable_name = executable.filename().generic_string();
  auto is_packaged_compiler_name = [](std::string_view name) {
    return name == "cursive.exe" || name == "Cursive.exe" ||
           name == "cursive-debug.exe" || name == "Cursive-debug.exe";
  };
  if (IsPackagedSupportRootCandidate(executable_dir)) {
    return {CompilerSupportLayoutKind::PackagedOut, executable_dir};
  }
  if (IsBuildTreeSupportRootCandidate(executable_dir)) {
    return {CompilerSupportLayoutKind::BuildTree, executable_dir};
  }
  const auto parent = executable_dir.parent_path();
  if (IsBuildTreeSupportRootCandidate(parent)) {
    return {CompilerSupportLayoutKind::BuildTree, parent};
  }
  if (is_packaged_compiler_name(executable_name)) {
    return {CompilerSupportLayoutKind::PackagedOut, executable_dir};
  }
  return {CompilerSupportLayoutKind::None, std::filesystem::path()};
}

std::filesystem::path HostPlatformSidecarBinDir(const SupportLayout& layout) {
  if (layout.kind == CompilerSupportLayoutKind::None || layout.root.empty()) {
    return {};
  }
  return ResolveHostCompilerSidecarBinDir(layout.kind, layout.root);
}

bool ConfigureHostSupport(BundleState& state, std::string* error_message) {
  const SupportLayout layout = ResolveSupportLayout();
  state.layout = layout.kind;
  state.support_root = layout.root;
  state.host_sidecar_bin_dir = HostPlatformSidecarBinDir(layout);

  if (layout.kind == CompilerSupportLayoutKind::None) {
    if (error_message != nullptr) {
      error_message->clear();
    }
    return true;
  }

  if (!DirExists(state.host_sidecar_bin_dir)) {
    if (error_message != nullptr) {
      *error_message = "Missing compiler sidecar directory: " +
                       state.host_sidecar_bin_dir.string();
    }
    return false;
  }

  if (!ConfigureHostCompilerSidecarBinDir(state.host_sidecar_bin_dir,
                                          error_message)) {
    return false;
  }

  if (error_message != nullptr) {
    error_message->clear();
  }
  return true;
}

BundleState& BundleStateInstance() {
  std::call_once(g_bundle_once, []() {
    g_bundle_state.initialized = true;
    std::string error_message;
    g_bundle_state.success = ConfigureHostSupport(g_bundle_state, &error_message);
    if (!g_bundle_state.success) {
      g_bundle_state.error_message = std::move(error_message);
    }
  });
  return g_bundle_state;
}

}  // namespace

bool EnsureBundledHostCompilerSupport(std::string* error_message) {
  BundleState& state = BundleStateInstance();
  if (!state.success && error_message != nullptr) {
    *error_message = state.error_message;
  } else if (error_message != nullptr) {
    error_message->clear();
  }
  return state.success;
}

CompilerSupportLayoutKind CompilerSupportLayout() {
  return BundleStateInstance().layout;
}

std::optional<std::filesystem::path> CompilerSupportRootPath() {
  BundleState& state = BundleStateInstance();
  if (state.layout == CompilerSupportLayoutKind::None ||
      state.support_root.empty()) {
    return std::nullopt;
  }
  return state.support_root;
}

std::optional<std::filesystem::path> HostCompilerSidecarBinDirPath() {
  BundleState& state = BundleStateInstance();
  if (state.host_sidecar_bin_dir.empty() ||
      !DirExists(state.host_sidecar_bin_dir)) {
    return std::nullopt;
  }
  return state.host_sidecar_bin_dir;
}

std::optional<std::filesystem::path> CompilerSupportAssetPath(
    const std::filesystem::path& packaged_relative_path,
    const std::filesystem::path& build_tree_relative_path) {
  const auto support_root = CompilerSupportRootPath();
  if (!support_root.has_value()) {
    return std::nullopt;
  }

  auto try_relative_path =
      [&](const std::filesystem::path& relative_path)
      -> std::optional<std::filesystem::path> {
    if (relative_path.empty()) {
      return std::nullopt;
    }
    const auto candidate = *support_root / relative_path;
    if (FileExists(candidate)) {
      return candidate;
    }
    return std::nullopt;
  };

  const auto layout = CompilerSupportLayout();
  if (layout == CompilerSupportLayoutKind::PackagedOut) {
    if (const auto packaged = try_relative_path(packaged_relative_path);
        packaged.has_value()) {
      return packaged;
    }
    return std::nullopt;
  }

  if (layout == CompilerSupportLayoutKind::BuildTree) {
    if (const auto build_tree = try_relative_path(build_tree_relative_path);
        build_tree.has_value()) {
      return build_tree;
    }
    return std::nullopt;
  }

  if (const auto packaged = try_relative_path(packaged_relative_path);
      packaged.has_value()) {
    return packaged;
  }
  return try_relative_path(build_tree_relative_path);
}

}  // namespace cursive::core
