#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "00_core/diagnostics.h"
#include "01_project/link.h"
#include "01_project/project.h"
#include "02_source/ast/ast.h"
#include "06_driver/cli.h"
#include "06_driver/incremental_state.h"

namespace ultraviolet::driver {

struct IncrementalBuildDataResult {
  bool ok = false;
  std::string build_key;
  std::unordered_map<std::string, IncrementalModuleInfo> modules;
};

using ModuleInterfaceHashFn =
    std::function<std::string(const ast::ASTModule& module)>;

bool IncrementalEnabled();

std::filesystem::path IncrementalDirPath(const project::Project& project);
std::filesystem::path IncrementalManifestPath(const project::Project& project);

std::optional<IncrementalManifestState> LoadIncrementalManifest(
    const std::filesystem::path& path);

bool SaveIncrementalManifest(
    const project::Project& project,
    const std::function<bool(const std::filesystem::path& path)>& ensure_dir,
    const std::function<bool(const std::filesystem::path& path,
                             std::string_view bytes)>& write_file,
    const IncrementalManifestState& state);

std::string BuildIncrementalBuildKey(
    const project::Project& project,
    project::TargetProfile target_profile,
    const CliOptions& opts,
    const std::filesystem::path& compiler_executable_path,
    std::string_view runtime_log_file_path);

IncrementalBuildDataResult BuildIncrementalBuildData(
    const project::Project& project,
    const std::vector<ast::ASTModule>& resolved_modules,
    const std::string& build_key,
    const ModuleInterfaceHashFn& module_interface_hash,
    core::DiagnosticStream& diags);

std::string ComputeLinkFingerprint(
    const project::Project& proj,
    project::TargetProfile target_profile,
    const std::string& build_key,
    const std::unordered_map<std::string, IncrementalManifestModuleState>& modules,
    const std::vector<std::filesystem::path>& link_inputs,
    const std::optional<std::filesystem::path>& runtime_lib,
    const project::LinkPlan& plan,
    std::string_view emit_ir);

}  // namespace ultraviolet::driver
