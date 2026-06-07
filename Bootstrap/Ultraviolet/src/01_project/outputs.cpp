#include "01_project/outputs.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
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

std::string RenderBool(bool value) {
  return value ? "true" : "false";
}

std::string RenderOptString(const std::optional<std::string>& value) {
  return value.has_value() ? *value : "<bottom>";
}

std::string RenderOptPath(
    const std::optional<std::filesystem::path>& value) {
  return value.has_value() ? value->generic_string() : "<bottom>";
}

std::string LinkOutputKindName(LinkOutputKind kind) {
  switch (kind) {
    case LinkOutputKind::Executable:
      return "exe";
    case LinkOutputKind::SharedLibrary:
      return "shared";
  }
  return "<bottom>";
}

std::string ArtifactLibraryName(const Project& project,
                                TargetProfile target_profile) {
  return std::string(LibraryPrefix(target_profile)) + project.assembly.name;
}

std::string RenderPath(const std::filesystem::path& path) {
  return path.generic_string();
}

std::string RenderFirstPath(const std::vector<std::filesystem::path>& paths) {
  return paths.empty() ? "<bottom>" : paths.front().generic_string();
}

std::string_view OutputRootSource(
    const std::optional<std::string>& assembly_out_dir) {
  if (core::OutDirOverride().has_value()) {
    return "cli_override";
  }
  if (assembly_out_dir.has_value()) {
    return "assembly_out_dir";
  }
  return "default";
}

void RecordOutputPathLayout(std::string_view assembly_name,
                            const OutputPaths& paths,
                            std::string_view root_source) {
  if (!core::Conformance::Enabled()) {
    return;
  }

  SPEC_RULE("def.OutputRoot");
  core::Conformance::Record(
      "def.OutputRoot",
      std::nullopt,
      "assembly=" + std::string(assembly_name) + ";root_source=" +
          std::string(root_source) + ";value=" + RenderPath(paths.root));

  SPEC_RULE("def.OutputPathsRoot");
  core::Conformance::Record(
      "def.OutputPathsRoot",
      std::nullopt,
      "assembly=" + std::string(assembly_name) + ";root_source=" +
          std::string(root_source) + ";value=" + RenderPath(paths.root));

  SPEC_RULE("def.OutputPathsDirectories");
  core::Conformance::Record(
      "def.OutputPathsDirectories",
      std::nullopt,
      "assembly=" + std::string(assembly_name) +
          ";intermediate=" + RenderPath(paths.intermediate_dir) +
          ";obj=" + RenderPath(paths.obj_dir) +
          ";ir=" + RenderPath(paths.ir_dir) +
          ";bin=" + RenderPath(paths.bin_dir) +
          ";lib=" + RenderPath(paths.lib_dir) +
          ";logs=" + RenderPath(paths.logs_dir) +
          ";incremental=" + RenderPath(paths.incremental_dir));
}

void RecordFinalArtifactNames(const Project& project,
                              TargetProfile target_profile) {
  if (!core::Conformance::Enabled()) {
    return;
  }

  const std::string libname = ArtifactLibraryName(project, target_profile);
  SPEC_RULE("def.FinalArtifactLibraryName");
  core::Conformance::Record(
      "def.FinalArtifactLibraryName",
      std::nullopt,
      "assembly=" + project.assembly.name + ";libname=" + libname);

  SPEC_RULE("def.FinalArtifactNames");
  core::Conformance::Record(
      "def.FinalArtifactNames",
      std::nullopt,
      "assembly=" + project.assembly.name +
          ";exe=" +
          RenderPath(project.outputs.bin_dir /
                     (project.assembly.name +
                      std::string(ExeSuffix(target_profile)))) +
          ";shared=" +
          RenderPath(project.outputs.bin_dir /
                     (libname + std::string(SharedLibSuffix(target_profile)))) +
          ";static=" +
          RenderPath(project.outputs.lib_dir /
                     (libname + std::string(StaticLibSuffix(target_profile)))) +
          ";import=" +
          RenderPath(project.outputs.lib_dir /
                     (libname + std::string(ImportLibSuffix(target_profile)))));
}

void RecordAssemblyAndLinkKinds(const Project& project) {
  const bool executable = IsExecutable(project);
  const bool library = IsLibrary(project);
  const bool dependency = IsDependency(project);
  const bool linkable = IsLinkable(project);
  const bool shared_library = IsSharedLibrary(project);
  const bool static_library = IsStaticLibrary(project);

  SPEC_RULE("def.AssemblyAndLinkKinds");
  core::Conformance::Record(
      "def.AssemblyAndLinkKinds",
      std::nullopt,
      "assembly=" + project.assembly.name +
          ";assembly_kind=" + project.assembly.kind +
          ";link_kind=" + RenderOptString(project.assembly.link_kind) +
          ";executable=" + RenderBool(executable) +
          ";library=" + RenderBool(library) +
          ";dependency=" + RenderBool(dependency) +
          ";linkable=" + RenderBool(linkable) +
          ";shared_library=" + RenderBool(shared_library) +
          ";static_library=" + RenderBool(static_library));
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
  const std::string_view root_source = OutputRootSource(assembly.out_dir);
  const std::filesystem::path root =
      cli_out_dir.has_value()        ? (project_root / *cli_out_dir)
      : assembly.out_dir.has_value() ? (project_root / *assembly.out_dir)
                                     : (project_root / DEFAULT_OUTPUT_ROOT);
  OutputPaths paths = OutputPathsForRoot(root);
  RecordOutputPathLayout(assembly.name, paths, root_source);
  return paths;
}

Project AssemblyProject(const Project& base_project, const Assembly& assembly) {
  Project project = base_project;
  project.assembly = assembly;
  project.source_root = assembly.source_root;
  project.outputs = assembly.outputs;
  project.modules = assembly.modules;
  project.lifecycle_modules = assembly.modules;
  if (core::Conformance::Enabled()) {
    SPEC_RULE("def.ProjectOutputBinding");
    core::Conformance::Record(
        "def.ProjectOutputBinding",
        std::nullopt,
        "assembly=" + assembly.name +
            ";outputs_bound=true;root=" + RenderPath(project.outputs.root));
  }
  return project;
}

std::filesystem::path ObjPath(const Project& project,
                              TargetProfile target_profile,
                              const ModuleInfo& module) {
  const std::string mangled = core::MangleModulePath(module.path);
  const std::filesystem::path rel_dir = ModuleOutputRelativeDir(project, module);
  const std::filesystem::path path =
      project.outputs.obj_dir / rel_dir /
      (mangled + std::string(ObjExt(target_profile)));
  if (core::Conformance::Enabled()) {
    const std::string payload =
        "assembly=" + project.assembly.name + ";module=" + module.path +
        ";relative_dir=" + RenderPath(rel_dir) +
        ";mangled=" + mangled +
        ";target=" + std::string(TargetProfileName(target_profile)) +
        ";value=" + RenderPath(path);
    SPEC_RULE("def.ObjectPath");
    core::Conformance::Record("def.ObjectPath", std::nullopt, payload);
    SPEC_RULE("def.ObjPath");
    core::Conformance::Record("def.ObjPath", std::nullopt, payload);
  }
  return path;
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
  const std::filesystem::path rel_dir = ModuleOutputRelativeDir(project, module);
  const std::filesystem::path path =
      project.outputs.ir_dir / rel_dir / (mangled + ext);
  if (core::Conformance::Enabled()) {
    SPEC_RULE("def.EmitIRExtension");
    core::Conformance::Record(
        "def.EmitIRExtension",
        std::nullopt,
        "emit_ir=" + std::string(emit_ir) + ";extension=" + ext);
    SPEC_RULE("def.IRPath");
    core::Conformance::Record(
        "def.IRPath",
        std::nullopt,
        "assembly=" + project.assembly.name + ";module=" + module.path +
            ";relative_dir=" + RenderPath(rel_dir) +
            ";mangled=" + mangled + ";emit_ir=" + std::string(emit_ir) +
            ";value=" + RenderPath(path));
  }
  return path;
}

std::filesystem::path ExePath(const Project& project,
                              TargetProfile target_profile) {
  const std::filesystem::path path =
      project.outputs.bin_dir /
      (project.assembly.name + std::string(ExeSuffix(target_profile)));
  if (core::Conformance::Enabled()) {
    SPEC_RULE("def.ExePath");
    core::Conformance::Record(
        "def.ExePath",
        std::nullopt,
        "assembly=" + project.assembly.name + ";value=" + RenderPath(path));
  }
  return path;
}

std::filesystem::path SharedLibPath(const Project& project,
                                    TargetProfile target_profile) {
  const std::filesystem::path path =
      project.outputs.bin_dir /
      (ArtifactLibraryName(project, target_profile) +
       std::string(SharedLibSuffix(target_profile)));
  if (core::Conformance::Enabled()) {
    SPEC_RULE("def.SharedLibPath");
    core::Conformance::Record(
        "def.SharedLibPath",
        std::nullopt,
        "assembly=" + project.assembly.name + ";value=" + RenderPath(path));
  }
  return path;
}

std::filesystem::path StaticLibPath(const Project& project,
                                    TargetProfile target_profile) {
  const std::filesystem::path path =
      project.outputs.lib_dir /
      (ArtifactLibraryName(project, target_profile) +
       std::string(StaticLibSuffix(target_profile)));
  if (core::Conformance::Enabled()) {
    SPEC_RULE("def.StaticLibPath");
    core::Conformance::Record(
        "def.StaticLibPath",
        std::nullopt,
        "assembly=" + project.assembly.name + ";value=" + RenderPath(path));
  }
  return path;
}

std::optional<std::filesystem::path> ImportLibPath(
    const Project& project,
    TargetProfile target_profile) {
  std::optional<std::filesystem::path> import_lib;
  if (IsSharedLibrary(project) && EmitsImportLib(target_profile)) {
    import_lib =
        project.outputs.lib_dir /
        (ArtifactLibraryName(project, target_profile) +
         std::string(ImportLibSuffix(target_profile)));
  }

  SPEC_RULE("def.ImportLibPath");
  core::Conformance::Record(
      "def.ImportLibPath",
      std::nullopt,
      "assembly=" + project.assembly.name +
          ";shared_library=" + RenderBool(IsSharedLibrary(project)) +
          ";emits_import_lib=" + RenderBool(EmitsImportLib(target_profile)) +
          ";value=" + RenderOptPath(import_lib));

  SPEC_RULE("def.LinkImportLibOpt");
  core::Conformance::Record(
      "def.LinkImportLibOpt",
      std::nullopt,
      "assembly=" + project.assembly.name +
          ";shared_library=" + RenderBool(IsSharedLibrary(project)) +
          ";emits_import_lib=" + RenderBool(EmitsImportLib(target_profile)) +
          ";value=" + RenderOptPath(import_lib));

  return import_lib;
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
  std::optional<std::filesystem::path> artifact;
  std::string branch = "<bottom>";
  if (IsExecutable(project)) {
    artifact = ExePath(project, target_profile);
    branch = "executable";
  } else if (IsSharedLibrary(project)) {
    artifact = SharedLibPath(project, target_profile);
    branch = "shared";
  } else if (IsStaticLibrary(project)) {
    artifact = StaticLibPath(project, target_profile);
    branch = "static";
  }
  if (core::Conformance::Enabled()) {
    SPEC_RULE("def.PrimaryArtifact");
    core::Conformance::Record(
        "def.PrimaryArtifact",
        std::nullopt,
        "assembly=" + project.assembly.name + ";branch=" + branch +
            ";value=" + RenderOptPath(artifact));
  }
  return artifact;
}

std::vector<std::filesystem::path> LibraryArtifactInputs(
    const std::vector<std::filesystem::path>& inputs) {
  if (core::Conformance::Enabled()) {
    SPEC_RULE("def.LibraryArtifactInputs");
    core::Conformance::Record(
        "def.LibraryArtifactInputs",
        std::nullopt,
        "count=" + std::to_string(inputs.size()) +
            ";first=" + RenderFirstPath(inputs));
  }
  return inputs;
}

std::optional<LinkOutputKind> LinkMode(const Project& project) {
  std::optional<LinkOutputKind> mode;
  if (IsExecutable(project)) {
    mode = LinkOutputKind::Executable;
  } else if (IsSharedLibrary(project)) {
    mode = LinkOutputKind::SharedLibrary;
  }

  SPEC_RULE("def.LinkMode");
  core::Conformance::Record(
      "def.LinkMode",
      std::nullopt,
      "assembly=" + project.assembly.name +
          ";mode=" + (mode.has_value() ? LinkOutputKindName(*mode) : "<bottom>"));

  return mode;
}

std::optional<std::filesystem::path> LinkOutputPath(
    const Project& project,
    TargetProfile target_profile) {
  std::optional<std::filesystem::path> output_path;
  std::string mode = "<bottom>";
  if (IsExecutable(project)) {
    output_path = ExePath(project, target_profile);
    mode = "exe";
  } else if (IsSharedLibrary(project)) {
    output_path = SharedLibPath(project, target_profile);
    mode = "shared";
  }

  SPEC_RULE("def.LinkOutputPath");
  core::Conformance::Record(
      "def.LinkOutputPath",
      std::nullopt,
      "assembly=" + project.assembly.name + ";mode=" + mode +
          ";value=" + RenderOptPath(output_path));

  return output_path;
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
  if (core::Conformance::Enabled()) {
    SPEC_RULE("def.ObjPaths");
    core::Conformance::Record(
        "def.ObjPaths",
        std::nullopt,
        "assembly=" + project.assembly.name +
            ";modules=" + std::to_string(modules.size()) +
            ";count=" + std::to_string(out.size()) +
            ";first=" + RenderFirstPath(out));
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
  if (core::Conformance::Enabled()) {
    SPEC_RULE("def.IRPaths");
    core::Conformance::Record(
        "def.IRPaths",
        std::nullopt,
        "assembly=" + project.assembly.name +
            ";emit_ir=" + std::string(emit_ir) +
            ";modules=" + std::to_string(modules.size()) +
            ";count=" + std::to_string(out.size()) +
            ";first=" + RenderFirstPath(out));
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
  if (core::Conformance::Enabled()) {
    const auto primary = PrimaryArtifactPath(project, target_profile);
    const auto import_lib = ImportLibPath(project, target_profile);
    RecordFinalArtifactNames(project, target_profile);

    SPEC_RULE("def.ArtifactPathContext");
    core::Conformance::Record(
        "def.ArtifactPathContext",
        std::nullopt,
        "assembly=" + project.assembly.name +
            ";root=" + RenderPath(project.outputs.root));

    SPEC_RULE("def.IRSet");
    core::Conformance::Record(
        "def.IRSet",
        std::nullopt,
        "assembly=" + project.assembly.name +
            ";emit_ir=" + std::string(emit_ir) +
            ";count=" + std::to_string((emit_ir == "ll" || emit_ir == "bc")
                                           ? project.modules.size()
                                           : 0u));

    SPEC_RULE("def.PrimaryArtifactSet");
    core::Conformance::Record(
        "def.PrimaryArtifactSet",
        std::nullopt,
        "assembly=" + project.assembly.name +
            ";linkable=" + RenderBool(IsLinkable(project)) +
            ";count=" + std::to_string(primary.has_value() ? 1u : 0u) +
            ";value=" + RenderOptPath(primary));

    SPEC_RULE("def.ImportLibSet");
    core::Conformance::Record(
        "def.ImportLibSet",
        std::nullopt,
        "assembly=" + project.assembly.name +
            ";shared_library=" + RenderBool(IsSharedLibrary(project)) +
            ";emits_import_lib=" + RenderBool(EmitsImportLib(target_profile)) +
            ";count=" + std::to_string(import_lib.has_value() ? 1u : 0u) +
            ";value=" + RenderOptPath(import_lib));

    SPEC_RULE("def.ArtifactOutputDirectoryUse");
    core::Conformance::Record(
        "def.ArtifactOutputDirectoryUse",
        std::nullopt,
        "assembly=" + project.assembly.name +
            ";uses_bin=" + RenderBool(IsExecutable(project) ||
                                       IsSharedLibrary(project)) +
            ";uses_lib=" + RenderBool(IsStaticLibrary(project) ||
                                       import_lib.has_value()));

    SPEC_RULE("def.RequiredOutputs");
    core::Conformance::Record(
        "def.RequiredOutputs",
        std::nullopt,
        "assembly=" + project.assembly.name +
            ";objs=" + std::to_string(objs.size()) +
            ";irs=" + std::to_string((emit_ir == "ll" || emit_ir == "bc")
                                        ? project.modules.size()
                                        : 0u) +
            ";primary=" + std::to_string(primary.has_value() ? 1u : 0u) +
            ";import_lib=" +
            std::to_string(import_lib.has_value() ? 1u : 0u) +
            ";total=" + std::to_string(out.size()) +
            ";first=" + RenderFirstPath(out));
  }
  return out;
}

bool OutputHygiene(const Project& project, TargetProfile target_profile) {
  if (core::Conformance::Enabled()) {
    RecordOutputPathLayout(project.assembly.name,
                           project.outputs,
                           OutputRootSource(project.assembly.out_dir));
  }

  const auto required = RequiredOutputs(project, target_profile);
  for (const auto& path : required) {
    if (!UnderPath(path, project.outputs.root)) {
      if (core::Conformance::Enabled()) {
        SPEC_RULE("def.OutputHygiene");
        core::Conformance::Record(
            "def.OutputHygiene",
            std::nullopt,
            "assembly=" + project.assembly.name +
                ";all_under_root=false;root=" +
                RenderPath(project.outputs.root) +
                ";failed=" + RenderPath(path));
      }
      return false;
    }
  }
  if (core::Conformance::Enabled()) {
    SPEC_RULE("def.OutputHygiene");
    core::Conformance::Record(
        "def.OutputHygiene",
        std::nullopt,
        "assembly=" + project.assembly.name +
            ";all_under_root=true;root=" + RenderPath(project.outputs.root) +
            ";count=" + std::to_string(required.size()));
  }
  return true;
}

std::vector<std::string> DumpProject(const Project& project,
                                     TargetProfile target_profile,
                                     bool dump_files) {
  std::vector<std::string> out;
  out.reserve(9 + project.modules.size());

  SPEC_RULE("def.DumpProjectOutput");
  core::Conformance::Record(
      "def.DumpProjectOutput",
      std::nullopt,
      dump_files ? "sections=ProjectSummary,OutputSummary,LinkOutputSummary,Files"
                 : "sections=ProjectSummary,OutputSummary,LinkOutputSummary");

  RecordAssemblyAndLinkKinds(project);

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
  out.push_back("<link_kind, " +
                RenderOptString(project.assembly.link_kind) + ">");
  out.push_back("<source_root, " + project.source_root.generic_string() + ">");
  out.push_back("<output_root, " + project.outputs.root.generic_string() + ">");
  out.push_back("<module_list, " + RenderList(module_names) + ">");

  SPEC_RULE("def.ProjectSummaryOutput");
  core::Conformance::Record(
      "def.ProjectSummaryOutput",
      std::nullopt,
      "fields=project_root,assemblies,assembly_name,assembly_kind,link_kind,"
      "source_root,output_root,module_list");

  const bool emit_ir_enabled =
      project.assembly.emit_ir.has_value() && *project.assembly.emit_ir != "none";
  const std::string emit_ir_mode =
      emit_ir_enabled ? *project.assembly.emit_ir : std::string("none");
  SPEC_RULE("def.OutputSummary");
  core::Conformance::Record("def.OutputSummary",
                            std::nullopt,
                            "fields=module,obj,ir;rows=" +
                                std::to_string(project.modules.size()));
  for (const auto& module : project.modules) {
    const std::string obj_path =
        ObjPath(project, target_profile, module).generic_string();
    const std::string ir_value =
        emit_ir_enabled
            ? IRPath(project, target_profile, module, emit_ir_mode).generic_string()
            : "<bottom>";
    SPEC_RULE("def.IROpt");
    core::Conformance::Record("def.IROpt",
                              std::nullopt,
                              "module=" + module.path +
                                  ";emit_ir=" + emit_ir_mode + ";value=" +
                                  ir_value);
    out.push_back("<module, " + module.path + ", obj, " + obj_path +
                  ", ir, " + ir_value + ">");
  }

  if (IsExecutable(project) || IsLibrary(project)) {
    const auto primary = PrimaryArtifactPath(project, target_profile);
    const auto import_lib = ImportLibPath(project, target_profile);
    const std::string primary_value = RenderOptPath(primary);
    const std::string import_lib_value = RenderOptPath(import_lib);
    SPEC_RULE("def.LinkOutputSummary");
    core::Conformance::Record("def.LinkOutputSummary",
                              std::nullopt,
                              "fields=artifact,import_lib;artifact=" +
                                  primary_value + ";import_lib=" +
                                  import_lib_value);
    SPEC_RULE("def.ImportLibOpt");
    core::Conformance::Record("def.ImportLibOpt",
                              std::nullopt,
                              "value=" + import_lib_value);
    out.push_back("<artifact, " + primary_value + ", import_lib, " +
                  import_lib_value + ">");
  } else {
    SPEC_RULE("def.LinkOutputSummary");
    core::Conformance::Record("def.LinkOutputSummary",
                              std::nullopt,
                              "fields=artifact,import_lib;rows=0");
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
