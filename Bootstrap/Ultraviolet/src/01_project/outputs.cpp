#include "01_project/outputs.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/path.h"
#include "00_core/process_config.h"
#include "00_core/symbols.h"
#include "01_project/assemblies.h"
#include "01_project/module_discovery.h"
#include "01_project/project.h"
#include "01_project/target_profile.h"

namespace ultraviolet::project {

namespace {

constexpr const char* DEFAULT_OUTPUT_ROOT = "Build";
constexpr const char* INTERMEDIATE_DIR = "Intermediate";
constexpr const char* OBJ_DIR = "Obj";
constexpr const char* IR_DIR = "IR";
constexpr const char* BINARY_DIR = "Binary";
constexpr const char* LIBRARY_DIR = "Library";
constexpr const char* LOGS_DIR = "Logs";
constexpr const char* INCREMENTAL_DIR = "Incremental";

std::string_view EmitIrMode(const Project& project) {
  if (project.assembly.emit_ir.has_value()) {
    return *project.assembly.emit_ir;
  }
  return "none";
}

bool UnderPath(const std::filesystem::path& path,
               const std::filesystem::path& root) {
  const std::string path_norm = core::Normalize(path.generic_string());
  const std::string root_norm = core::Normalize(root.generic_string());
  return core::Prefix(root_norm, path_norm);
}

std::string JoinList(const std::vector<std::string>& items,
                     std::string_view sep) {
  std::ostringstream oss;
  bool first = true;
  for (const auto& item : items) {
    if (!first) {
      oss << sep;
    }
    first = false;
    oss << item;
  }
  return oss.str();
}

std::string RenderList(const std::vector<std::string>& items) {
  std::ostringstream oss;
  oss << "[";
  oss << JoinList(items, ", ");
  oss << "]";
  return oss.str();
}

std::filesystem::path ModuleOutputRelativeDir(const Project& project,
                                              const ModuleInfo& module) {
  const std::optional<std::string> rel =
      core::Relative(module.dir.generic_string(), project.root.generic_string());
  if (!rel.has_value() || rel->empty()) {
    return {};
  }
  return std::filesystem::path(*rel);
}

}  // namespace

OutputPaths OutputPathsForRoot(const std::filesystem::path& root) {
  OutputPaths paths;
  paths.root = root;
  paths.intermediate_dir = root / INTERMEDIATE_DIR;
  paths.obj_dir = paths.intermediate_dir / OBJ_DIR;
  paths.ir_dir = paths.intermediate_dir / IR_DIR;
  paths.bin_dir = root / BINARY_DIR;
  paths.lib_dir = root / LIBRARY_DIR;
  paths.logs_dir = root / LOGS_DIR;
  paths.incremental_dir = paths.intermediate_dir / INCREMENTAL_DIR;
  return paths;
}

OutputPaths ComputeOutputPaths(const std::filesystem::path& project_root,
                               const ValidatedAssembly& assembly) {
  const auto cli_out_dir = core::OutDirOverride();
  const std::filesystem::path root =
      cli_out_dir.has_value()        ? (project_root / *cli_out_dir)
      : assembly.out_dir.has_value() ? (project_root / *assembly.out_dir)
                                     : (project_root / DEFAULT_OUTPUT_ROOT);
  return OutputPathsForRoot(root);
}

Project AssemblyProject(const Project& base_project, const Assembly& assembly) {
  Project project = base_project;
  project.assembly = assembly;
  project.source_root = assembly.source_root;
  project.outputs = assembly.outputs;
  project.modules = assembly.modules;
  project.lifecycle_modules = assembly.modules;
  return project;
}

std::filesystem::path ObjPath(const Project& project,
                              TargetProfile target_profile,
                              const ModuleInfo& module) {
  const std::string mangled = core::MangleModulePath(module.path);
  return project.outputs.obj_dir / ModuleOutputRelativeDir(project, module) /
         (mangled + std::string(ObjExt(target_profile)));
}

std::filesystem::path IRPath(const Project& project,
                             TargetProfile,
                             const ModuleInfo& module,
                             std::string_view emit_ir) {
  std::string ext = ".ll";
  if (emit_ir == "bc") {
    ext = ".bc";
  }
  const std::string mangled = core::MangleModulePath(module.path);
  return project.outputs.ir_dir / ModuleOutputRelativeDir(project, module) /
         (mangled + ext);
}

std::filesystem::path ExePath(const Project& project,
                              TargetProfile target_profile) {
  return project.outputs.bin_dir /
         (project.assembly.name + std::string(ExeSuffix(target_profile)));
}

std::filesystem::path SharedLibPath(const Project& project,
                                    TargetProfile target_profile) {
  return project.outputs.bin_dir /
         (std::string(LibraryPrefix(target_profile)) + project.assembly.name +
          std::string(SharedLibSuffix(target_profile)));
}

std::filesystem::path StaticLibPath(const Project& project,
                                    TargetProfile target_profile) {
  return project.outputs.lib_dir /
         (std::string(LibraryPrefix(target_profile)) + project.assembly.name +
          std::string(StaticLibSuffix(target_profile)));
}

std::optional<std::filesystem::path> ImportLibPath(
    const Project& project,
    TargetProfile target_profile) {
  if (!IsSharedLibrary(project) || !EmitsImportLib(target_profile)) {
    return std::nullopt;
  }
  return project.outputs.lib_dir /
         (std::string(LibraryPrefix(target_profile)) + project.assembly.name +
          std::string(ImportLibSuffix(target_profile)));
}

std::optional<std::filesystem::path> MapPath(const Project& project,
                                             TargetProfile target_profile) {
  if (ObjectFormatOf(target_profile) != ObjectFormat::Coff) {
    return std::nullopt;
  }
  if (!(IsExecutable(project) || IsSharedLibrary(project))) {
    return std::nullopt;
  }
  const auto primary = PrimaryArtifactPath(project, target_profile);
  if (!primary.has_value()) {
    return std::nullopt;
  }
  auto map_path = *primary;
  map_path.replace_extension(".map");
  return map_path;
}

std::optional<std::filesystem::path> PrimaryArtifactPath(
    const Project& project,
    TargetProfile target_profile) {
  if (IsExecutable(project)) {
    return ExePath(project, target_profile);
  }
  if (IsSharedLibrary(project)) {
    return SharedLibPath(project, target_profile);
  }
  if (IsStaticLibrary(project)) {
    return StaticLibPath(project, target_profile);
  }
  return std::nullopt;
}

std::vector<std::filesystem::path> LibraryArtifactInputs(
    const std::vector<std::filesystem::path>& inputs) {
  return inputs;
}

std::optional<LinkOutputKind> LinkMode(const Project& project) {
  if (IsExecutable(project)) {
    return LinkOutputKind::Executable;
  }
  if (IsSharedLibrary(project)) {
    return LinkOutputKind::SharedLibrary;
  }
  return std::nullopt;
}

std::optional<std::filesystem::path> LinkOutputPath(
    const Project& project,
    TargetProfile target_profile) {
  if (IsExecutable(project)) {
    return ExePath(project, target_profile);
  }
  if (IsSharedLibrary(project)) {
    return SharedLibPath(project, target_profile);
  }
  return std::nullopt;
}

bool UsesBinDir(const Project& project, TargetProfile) {
  return IsExecutable(project) || IsSharedLibrary(project);
}

bool UsesLibDir(const Project& project, TargetProfile target_profile) {
  return IsStaticLibrary(project) ||
         ImportLibPath(project, target_profile).has_value();
}

std::vector<std::filesystem::path> ObjPaths(
    const Project& project,
    TargetProfile target_profile,
    const std::vector<ModuleInfo>& modules) {
  std::vector<std::filesystem::path> out;
  out.reserve(modules.size());
  for (const auto& module : modules) {
    out.push_back(ObjPath(project, target_profile, module));
  }
  return out;
}

std::vector<std::filesystem::path> IRPaths(
    const Project& project,
    TargetProfile target_profile,
    const std::vector<ModuleInfo>& modules,
    std::string_view emit_ir) {
  std::vector<std::filesystem::path> out;
  if (!(emit_ir == "ll" || emit_ir == "bc")) {
    return out;
  }
  out.reserve(modules.size());
  for (const auto& module : modules) {
    out.push_back(IRPath(project, target_profile, module, emit_ir));
  }
  return out;
}

std::vector<std::filesystem::path> RequiredOutputs(
    const Project& project,
    TargetProfile target_profile) {
  std::vector<std::filesystem::path> out;
  const auto objs = ObjPaths(project, target_profile, project.modules);
  out.insert(out.end(), objs.begin(), objs.end());
  const std::string_view emit_ir = EmitIrMode(project);
  if (emit_ir == "ll" || emit_ir == "bc") {
    const auto irs = IRPaths(project, target_profile, project.modules, emit_ir);
    out.insert(out.end(), irs.begin(), irs.end());
  }
  if (const auto primary = PrimaryArtifactPath(project, target_profile);
      primary.has_value()) {
    out.push_back(*primary);
  }
  if (const auto import_lib = ImportLibPath(project, target_profile);
      import_lib.has_value()) {
    out.push_back(*import_lib);
  }
  return out;
}

bool OutputHygiene(const Project& project, TargetProfile target_profile) {
  const auto required = RequiredOutputs(project, target_profile);
  for (const auto& path : required) {
    if (!UnderPath(path, project.outputs.root)) {
      return false;
    }
  }
  return true;
}

std::vector<std::string> DumpProject(const Project& project,
                                     TargetProfile target_profile,
                                     bool dump_files) {
  std::vector<std::string> out;
  out.reserve(9 + project.modules.size());

  auto render_opt = [](const std::optional<std::string>& value) -> std::string {
    return value.has_value() ? *value : "<bottom>";
  };

  auto render_opt_path =
      [](const std::optional<std::filesystem::path>& value) -> std::string {
    return value.has_value() ? value->generic_string() : "<bottom>";
  };

  std::vector<std::string> assembly_names;
  assembly_names.reserve(project.assemblies.size());
  for (const auto& assembly : project.assemblies) {
    assembly_names.push_back(assembly.name);
  }

  std::vector<std::string> module_names;
  module_names.reserve(project.modules.size());
  for (const auto& module : project.modules) {
    module_names.push_back(module.path);
  }

  out.push_back("<project_root, " + project.root.generic_string() + ">");
  out.push_back("<assemblies, " + RenderList(assembly_names) + ">");
  out.push_back("<assembly_name, " + project.assembly.name + ">");
  out.push_back("<assembly_kind, " + project.assembly.kind + ">");
  out.push_back("<link_kind, " + render_opt(project.assembly.link_kind) + ">");
  out.push_back("<source_root, " + project.source_root.generic_string() + ">");
  out.push_back("<output_root, " + project.outputs.root.generic_string() + ">");
  out.push_back("<module_list, " + RenderList(module_names) + ">");

  if (IsExecutable(project) || IsLibrary(project)) {
    const auto primary = PrimaryArtifactPath(project, target_profile);
    const auto import_lib = ImportLibPath(project, target_profile);
    out.push_back("<artifact, " + render_opt_path(primary) +
                  ", import_lib, " + render_opt_path(import_lib) + ">");
  }

  const bool emit_ir_enabled =
      project.assembly.emit_ir.has_value() && *project.assembly.emit_ir != "none";
  const std::string_view emit_ir = emit_ir_enabled ? *project.assembly.emit_ir : "";
  for (const auto& module : project.modules) {
    const std::string obj_path =
        ObjPath(project, target_profile, module).generic_string();
    const std::string ir_value =
        emit_ir_enabled
            ? IRPath(project, target_profile, module, emit_ir).generic_string()
            : "<bottom>";
    out.push_back("<module, " + module.path + ", obj, " + obj_path +
                  ", ir, " + ir_value + ">");
  }

  if (dump_files) {
    for (const auto& module : project.modules) {
      const auto unit = CompilationUnit(module.dir);
      for (const auto& file : unit.files) {
        std::string line = "file:";
        line.append(file.generic_string());
        out.push_back(line);
      }
    }
  }

  return out;
}

}  // namespace ultraviolet::project
