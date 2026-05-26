#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "01_project/link.h"
#include "01_project/module_discovery.h"

namespace ultraviolet::project {

struct Assembly;
struct ValidatedAssembly;
struct Project;

struct OutputPaths {
  std::filesystem::path root;
  std::filesystem::path intermediate_dir;
  std::filesystem::path obj_dir;
  std::filesystem::path ir_dir;
  std::filesystem::path bin_dir;
  std::filesystem::path lib_dir;
  std::filesystem::path logs_dir;
  std::filesystem::path incremental_dir;
};

OutputPaths OutputPathsForRoot(const std::filesystem::path& root);
OutputPaths ComputeOutputPaths(const std::filesystem::path& project_root,
                               const ValidatedAssembly& assembly);
Project AssemblyProject(const Project& base_project, const Assembly& assembly);

std::filesystem::path ObjPath(const Project& project,
                              TargetProfile target_profile,
                              const ModuleInfo& module);
std::filesystem::path IRPath(const Project& project,
                             TargetProfile target_profile,
                             const ModuleInfo& module,
                             std::string_view emit_ir);
std::filesystem::path ExePath(const Project& project,
                              TargetProfile target_profile);
std::filesystem::path SharedLibPath(const Project& project,
                                    TargetProfile target_profile);
std::filesystem::path StaticLibPath(const Project& project,
                                    TargetProfile target_profile);
std::optional<std::filesystem::path> ImportLibPath(const Project& project,
                                                   TargetProfile target_profile);
std::optional<std::filesystem::path> MapPath(const Project& project,
                                             TargetProfile target_profile);
std::optional<std::filesystem::path> PrimaryArtifactPath(
    const Project& project,
    TargetProfile target_profile);
std::vector<std::filesystem::path> LibraryArtifactInputs(
    const std::vector<std::filesystem::path>& inputs);
std::optional<LinkOutputKind> LinkMode(const Project& project);
std::optional<std::filesystem::path> LinkOutputPath(
    const Project& project,
    TargetProfile target_profile);
bool UsesBinDir(const Project& project, TargetProfile target_profile);
bool UsesLibDir(const Project& project, TargetProfile target_profile);

std::vector<std::filesystem::path> ObjPaths(
    const Project& project,
    TargetProfile target_profile,
    const std::vector<ModuleInfo>& modules);
std::vector<std::filesystem::path> IRPaths(
    const Project& project,
    TargetProfile target_profile,
    const std::vector<ModuleInfo>& modules,
    std::string_view emit_ir);

std::vector<std::filesystem::path> RequiredOutputs(const Project& project,
                                                   TargetProfile target_profile);
bool OutputHygiene(const Project& project, TargetProfile target_profile);
std::vector<std::string> DumpProject(const Project& project,
                                     TargetProfile target_profile,
                                     bool dump_files);

}  // namespace ultraviolet::project
