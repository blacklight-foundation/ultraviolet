#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/diagnostics.h"
#include "00_core/path.h"
#include "01_project/language_profile.h"
#include "01_project/manifest.h"
#include "01_project/module_discovery.h"
#include "01_project/outputs.h"
#include "01_project/target_profile.h"

namespace ultraviolet::project {

struct ToolchainConfig {
  std::optional<std::string> llvm_bin;
  std::optional<std::string> runtime_lib;
  std::optional<TargetProfile> target_profile;
};

struct BuildConfig {
  bool incremental = false;
  bool progress = true;
};

struct ValidatedAssembly {
  std::string name;
  std::string kind;
  std::optional<std::string> link_kind;
  std::string root;
  std::optional<std::string> out_dir;
  std::optional<std::string> emit_ir;
};

struct Assembly {
  std::string name;
  std::string kind;
  std::optional<std::string> link_kind;
  std::string root;
  std::optional<std::string> out_dir;
  std::optional<std::string> emit_ir;
  std::filesystem::path source_root;
  OutputPaths outputs;
  std::vector<ModuleInfo> modules;
};

struct ManifestValidationResult {
  std::vector<ValidatedAssembly> assemblies;
  ToolchainConfig toolchain;
  BuildConfig build;
  core::DiagnosticStream diags;
};

ManifestValidationResult ValidateManifest(
    const std::filesystem::path& project_root,
    const TOMLTable& table);

bool IsProjectRoot(const std::filesystem::path& root);

struct Project {
  SourceLanguage language = SourceLanguage::Ultraviolet;
  std::filesystem::path root;
  std::vector<Assembly> assemblies;
  Assembly assembly;
  std::filesystem::path source_root;
  OutputPaths outputs;
  std::vector<ModuleInfo> modules;
  std::vector<ModuleInfo> lifecycle_modules;
  ToolchainConfig toolchain;
  BuildConfig build;
};

struct LoadProjectResult {
  std::optional<Project> project;
  core::DiagnosticStream diags;
};

struct LoadProjectDeps {
  ManifestParseResult (*parse)(const std::filesystem::path& project_root);
  ManifestValidationResult (*validate)(const std::filesystem::path& project_root,
                                       const TOMLTable& table);
  std::optional<core::ResolveResult> (*resolve)(std::string_view root,
                                                std::string_view path);
  bool (*is_dir)(const std::filesystem::path& path);
  ModulesResult (*modules)(const std::filesystem::path& source_root,
                           std::string_view assembly_name);
  OutputPaths (*outputs)(const std::filesystem::path& project_root,
                         const ValidatedAssembly& assembly);
};

struct AssemblyTarget {
  std::optional<std::string> name;
};

AssemblyTarget NoAssemblyTarget();
std::optional<AssemblyTarget> ParseAssemblyTarget(
    const std::optional<std::string>& target);

LoadProjectResult LoadProjectWithDeps(const std::filesystem::path& project_root,
                                      const AssemblyTarget& target,
                                      const LoadProjectDeps& deps);

LoadProjectResult LoadProject(const std::filesystem::path& project_root,
                              const AssemblyTarget& target);

LoadProjectResult LoadProject(const std::filesystem::path& project_root);

}  // namespace ultraviolet::project
