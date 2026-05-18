// =============================================================================
// MIGRATION MAPPING: tool_resolution.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 2.6 (lines 1651-1671)
//   - 2.6. Tool Resolution and IR Assembly Inputs
//   - SearchDirs(P) definition
//   - ResolveTool-Ok / ResolveTool-Err-Linker / ResolveTool-Err-IR rules
//
// SOURCE FILE: ultraviolet-bootstrap/src/01_project/tool_resolution.cpp
//   - Lines 1-347 (entire file)
//
// =============================================================================
// CONTENT TO MIGRATE
// =============================================================================
//
// 1. Helper functions (lines 13-92)
//    - GetEnv() (lines 15-21)
//      PURPOSE: Get environment variable value
//
//    - PathSeparator() (lines 23-29)
//      PURPOSE: Get path list separator (';' on Windows, ':' on POSIX)
//
//    - SplitPathList() (lines 31-45)
//      PURPOSE: Split PATH-style list into directory paths
//
//    - EndsWithExe() (lines 53-62)
//      PURPOSE: Check if filename ends with ".exe"
//
//    - ToolCandidates() (lines 64-75)
//      PURPOSE: Generate tool name variants (with/without .exe on Windows)
//
//    - AppendUniquePaths() (lines 77-91)
//      PURPOSE: Append paths avoiding duplicates
//
// 2. SearchDirs() (lines 288-303)
//    - PURPOSE: Get search directories for tool resolution
//    - SPEC RULE:
//      SearchDirs(P) =
//        [ToolchainConfig(P).llvm_bin] if ToolchainConfig(P).llvm_bin != empty
//        [CompilerSidecarToolsDir(P)]  if sidecar tools are present
//        PATHDirs  otherwise
//    - SPEC REF: Lines 1653-1656
//
// 3. ResolveTool() (lines 305-344)
//    - PURPOSE: Resolve tool path from search directories
//    - SPEC RULES:
//      * ResolveTool-Ok (spec lines 1658-1661)
//      * ResolveTool-Err-Linker (spec lines 1663-1666)
//      * ResolveTool-Err-IR (spec lines 1668-1671)
//
// =============================================================================
// DEPENDENCIES
// =============================================================================
//
// Headers required:
//   - "uv/01_project/tool_resolution.h"
//   - "uv/00_core/assert_spec.h" (SPEC_RULE macro)
//   - "uv/00_core/host_primitives.h" (HostPrimFail)
//   - "uv/01_project/project.h" (Project)
//   - <algorithm>
//   - <cstdlib>
//   - <string>
//   - <vector>
//   - <optional>
//   - <filesystem>
//   - <system_error>
//
// Types from header (tool_resolution.h):
//   - No custom types, just function declarations
//
// =============================================================================
// REFACTORING NOTES
// =============================================================================
//
// 1. The search order (toolchain llvm_bin > compiler sidecars > PATH) is intentional:
//    - Uses manifest configuration as explicit source of truth
//    - Supports staged compiler-sidecar tools in the compiler distribution
//    - Falls back to system PATH when no explicit/toolchain LLVM is present
//
// 2. Tool candidates include both with and without .exe extension on Windows
//    This handles cases where the extension may or may not be present
//
// =============================================================================
// SPEC RULE ANNOTATIONS (use SPEC_RULE macro)
// =============================================================================
//
// Line 322: SPEC_RULE("ResolveTool-Ok");
// Line 331: SPEC_RULE("ResolveTool-Ok");
// Line 337: HostPrimFail call
// Line 339: SPEC_RULE("ResolveTool-Err-Linker");
// Line 341: SPEC_RULE("ResolveTool-Err-IR");
//
// =============================================================================

#include "01_project/tool_resolution.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/host/services.h"
#include "00_core/host_primitives.h"
#include "00_core/path.h"
#include "01_project/compiler_support_paths.h"
#include "01_project/llvm_toolchain.h"
#include "01_project/project.h"
#include "01_project/target_profile.h"

namespace ultraviolet::project {

namespace {

std::vector<std::filesystem::path> SplitPathList(std::string_view path_list) {
  std::vector<std::filesystem::path> out;
  const char sep = core::HostPathListSeparator();
  std::size_t start = 0;
  for (std::size_t i = 0; i <= path_list.size(); ++i) {
    if (i == path_list.size() || path_list[i] == sep) {
      const std::string_view segment = path_list.substr(start, i - start);
      if (!segment.empty()) {
        out.emplace_back(std::string(segment));
      }
      start = i + 1;
    }
  }
  return out;
}

std::optional<std::string> RunToolVersionCommand(
    const std::filesystem::path& tool) {
  core::HostProcessSpec spec;
  spec.program = tool;
  spec.arguments.push_back("--version");
  spec.output_mode = core::HostProcessOutputMode::CaptureMerged;
  spec.hide_window = true;
  const auto result = core::RunHostProcess(spec);
  if (!result.launched || result.exit_code != 0) {
    return std::nullopt;
  }
  return result.output;
}

bool IsPinnedLlvmAssembler(std::string_view tool) {
  return tool == "llvm-as";
}

bool CandidateMatchesLLVMToolchain(const std::filesystem::path& candidate) {
  static std::mutex cache_mu;
  static std::unordered_map<std::string, bool> cache;

  const std::string cache_key = candidate.lexically_normal().generic_string();
  {
    const std::lock_guard<std::mutex> lock(cache_mu);
    const auto it = cache.find(cache_key);
    if (it != cache.end()) {
      return it->second;
    }
  }

  const auto version_output = RunToolVersionCommand(candidate);
  const bool matches =
      version_output.has_value() &&
      version_output->find(std::string(kLLVMToolchainVersion)) !=
          std::string::npos;

  const std::lock_guard<std::mutex> lock(cache_mu);
  cache.insert_or_assign(cache_key, matches);
  return matches;
}



std::vector<std::string> ToolCandidates(std::string_view tool) {
  auto out = core::HostToolNameCandidates(tool);
  if (out.empty()) {
    out.emplace_back(tool);
  }
  return out;
}

void AddUniquePath(std::vector<std::filesystem::path>& out,
                   const std::filesystem::path& candidate) {
  if (candidate.empty()) {
    return;
  }
  if (std::find(out.begin(), out.end(), candidate) == out.end()) {
    out.push_back(candidate);
  }
}

std::filesystem::path ResolveManifestToolchainPath(
    const Project& project,
    std::string_view raw_path) {
  const std::filesystem::path path(raw_path);
  if (path.is_absolute() || !core::IsRelative(raw_path)) {
    return path;
  }
  return (project.root / path).lexically_normal();
}

}  // namespace

std::vector<std::filesystem::path> SearchDirs(const Project& project,
                                             TargetProfile target_profile) {
  if (project.toolchain.llvm_bin.has_value() &&
      !project.toolchain.llvm_bin->empty()) {
    return {ResolveManifestToolchainPath(project, *project.toolchain.llvm_bin)};
  }

  const auto tool_bin_dir = CompilerToolBinDir(project, target_profile);
  std::error_code ec;
  if (!tool_bin_dir.empty() &&
      std::filesystem::is_directory(tool_bin_dir, ec) && !ec) {
    return {tool_bin_dir};
  }

  std::vector<std::filesystem::path> dirs;
  const auto path_env = core::HostGetEnvUtf8("PATH");
  if (path_env.has_value() && !path_env->empty()) {
    for (const auto& path : SplitPathList(*path_env)) {
      AddUniquePath(dirs, path);
    }
  }
  return dirs;
}

std::optional<std::filesystem::path> ResolveTool(const Project& project,
                                                 TargetProfile target_profile,
                                                 std::string_view tool) {
  const auto dirs = SearchDirs(project, target_profile);
  const auto candidates = ToolCandidates(tool);
  for (const auto& dir : dirs) {
    for (const auto& name : candidates) {
      const auto candidate = dir / name;
      std::error_code ec;
      if (std::filesystem::exists(candidate, ec) && !ec) {
        if (IsPinnedLlvmAssembler(tool) &&
            !CandidateMatchesLLVMToolchain(candidate)) {
          continue;
        }
        SPEC_RULE("ResolveTool-Ok");
        return candidate;
      }
    }
  }

  core::HostPrimFail(core::HostPrim::ResolveTool, true);
  if (tool == LinkerToolName(target_profile)) {
    SPEC_RULE("ResolveTool-Err-Linker");
  } else if (tool == ArchiverToolName(target_profile)) {
    SPEC_RULE("ResolveTool-Err-Archiver");
  } else if (tool == "llvm-as") {
    SPEC_RULE("ResolveTool-Err-IR");
  }
  return std::nullopt;
}

std::string FormatSearchedPaths(const Project& project,
                                TargetProfile target_profile,
                                std::string_view tool) {
  const auto dirs = SearchDirs(project, target_profile);
  const auto candidates = ToolCandidates(tool);
  std::string result = "searched for '" + std::string(tool) + "' in:";
  if (dirs.empty()) {
    result += " (no search directories found)";
    return result;
  }
  for (const auto& dir : dirs) {
    result += "\n  ";
    result += dir.string();
    for (const auto& name : candidates) {
      const auto candidate = dir / name;
      std::error_code ec;
      const bool exists = std::filesystem::exists(candidate, ec) && !ec;
      result += "\n    ";
      result += name;
      if (!exists) {
        result += " (not found)";
      }
    }
  }
  return result;
}

}  // namespace ultraviolet::project
