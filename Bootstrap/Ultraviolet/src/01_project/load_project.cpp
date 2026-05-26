// =============================================================================
// MIGRATION MAPPING: load_project.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 2.1 (lines 1030-1159)
//   - Project Load (Small-Step): Step-Parse, Step-Validate, Step-Asm-*
//   - Assembly Selection: Select-Only, Select-By-Name, Select-Err
//   - Assembly Build (Big-Step): BuildAssembly-Ok/Err-*
//   - Project Load (Big-Step): LoadProject-Ok/Err
//
// SOURCE FILE: ultraviolet-bootstrap/src/01_project/load_project.cpp
//   - Lines 1-211 (entire file)
//
// =============================================================================
// CONTENT TO MIGRATE
// =============================================================================
//
// 1. MakeExternalDiag() helper (lines 17-23)
//    - PURPOSE: Create diagnostic without source span
//    - REFACTORING: Consolidate into core diagnostics module
//
// 2. EmitExternal() helper (lines 25-29)
//    - PURPOSE: Emit external diagnostic to stream
//
// 3. IsDirDefault() helper (lines 31-38)
//    - PURPOSE: Check if path is a directory (default implementation)
//    - DEPENDENCIES: std::filesystem::is_directory
//
// 4. BuildAssembly() (lines 40-96)
//    - PURPOSE: Build Assembly record from validated spec
//    - SPEC RULES:
//      * BuildAssembly-Ok (spec lines 1122-1125)
//      * BuildAssembly-Err-Resolve (spec lines 1127-1130)
//      * BuildAssembly-Err-Root (spec lines 1132-1135)
//      * BuildAssembly-Err-Modules (spec lines 1137-1140)
//      * WF-Source-Root / WF-Source-Root-Err (spec lines 1215-1223)
//      * ModuleList-Ok / ModuleList-Err (spec lines 1376-1384)
//    - DIAGNOSTICS:
//      * E-PRJ-0302: Source root not a directory
//      * E-PRJ-0304: Path resolution error
//    - DEPENDENCIES: deps.resolve, deps.is_dir, deps.modules, deps.outputs
//
// 5. SelectAssembly() (lines 98-120)
//    - PURPOSE: Select assembly from list by target name
//    - SPEC RULES:
//      * Select-Only (spec lines 1105-1108)
//      * Select-By-Name (spec lines 1110-1113)
//      * Select-Err (spec lines 1115-1118)
//    - DIAGNOSTICS: E-PRJ-0205
//
// 6. LoadProjectImpl() (lines 122-183)
//    - PURPOSE: Main project loading implementation
//    - SPEC RULES:
//      * Step-Parse / Step-Parse-Err (spec lines 1035-1043)
//      * Step-Validate / Step-Validate-Err (spec lines 1045-1053)
//      * Step-Asm-Init (spec lines 1078-1081)
//      * Step-Asm-Cons (spec lines 1083-1086)
//      * Step-Asm-Err (spec lines 1088-1091)
//      * Step-Asm-Done / Step-Asm-Done-Err (spec lines 1093-1101)
//      * LoadProject-Ok (spec lines 1144-1147)
//      * LoadProject-Err (spec lines 1149-1152)
//    - DEPENDENCIES: deps.parse, deps.validate, BuildAssembly, SelectAssembly
//
// 7. LoadProjectWithDeps() (lines 187-191)
//    - PURPOSE: Public entry point with dependency injection
//
// 8. LoadProject() overloads (lines 193-208)
//    - PURPOSE: Public entry points with default dependencies
//    - DEPENDENCIES: ParseManifest, ValidateManifest, core::Resolve,
//                    IsDirDefault, Modules, ComputeOutputPaths
//
// =============================================================================
// DEPENDENCIES
// =============================================================================
//
// Headers required:
//   - "uv/01_project/project.h" (types: Project, Assembly, LoadProjectResult, LoadProjectDeps)
//   - "uv/00_core/assert_spec.h" (SPEC_RULE macro)
//   - "uv/00_core/diagnostic_messages.h" (MakeDiagnostic)
//   - "uv/00_core/diagnostics.h" (DiagnosticStream, Emit, HasError)
//   - "uv/01_project/deterministic_order.h" (Fold, Utf8LexLess)
//   - "uv/01_project/module_discovery.h" (Modules, ModulesResult)
//   - "uv/01_project/outputs.h" (ComputeOutputPaths)
//   - <algorithm>
//   - <string_view>
//   - <filesystem>
//
// Types from header (project.h):
//   - Project { root, assemblies, assembly, source_root, outputs, modules }
//   - Assembly { name, kind, root, out_dir, emit_ir, source_root, outputs, modules }
//   - LoadProjectResult { std::optional<Project> project; DiagnosticStream diags; }
//   - LoadProjectDeps { parse, validate, resolve, is_dir, modules, outputs }
//
// =============================================================================
// REFACTORING NOTES
// =============================================================================
//
// 1. The LoadProjectDeps struct enables dependency injection.
//    RECOMMENDATION: Maintain this pattern for controlled project-loading seams.
//
// 2. Module sorting logic (lines 73-81) duplicates ordering logic
//    RECOMMENDATION: Extract to deterministic_order module
//
// 3. Consider adding progress callbacks for large projects
//
// 4. The spec defines small-step and big-step semantics for project loading
//    The implementation follows the big-step pattern with early returns on error
//
// =============================================================================
// SPEC RULE ANNOTATIONS (use SPEC_RULE macro)
// =============================================================================
//
// Line 48: SPEC_RULE("BuildAssembly-Err-Resolve");
// Line 55-56: SPEC_RULE("WF-Source-Root-Err"); SPEC_RULE("BuildAssembly-Err-Root");
// Line 60: SPEC_RULE("WF-Source-Root");
// Line 67-68: SPEC_RULE("ModuleList-Err"); SPEC_RULE("BuildAssembly-Err-Modules");
// Line 82: SPEC_RULE("ModuleList-Ok");
// Line 94: SPEC_RULE("BuildAssembly-Ok");
// Line 104: SPEC_RULE("Select-Only");
// Line 107: SPEC_RULE("Select-Err");
// Line 113: SPEC_RULE("Select-By-Name");
// Line 117: SPEC_RULE("Select-Err");
// Line 132-133: SPEC_RULE("Step-Parse-Err"); SPEC_RULE("LoadProject-Err");
// Line 136: SPEC_RULE("Step-Parse");
// Line 144-145: SPEC_RULE("Step-Validate-Err"); SPEC_RULE("LoadProject-Err");
// Line 148: SPEC_RULE("Step-Validate");
// Line 150: SPEC_RULE("Step-Asm-Init");
// Line 154: SPEC_RULE("Step-Asm-Cons");
// Line 157-158: SPEC_RULE("Step-Asm-Err"); SPEC_RULE("LoadProject-Err");
// Line 163-164: SPEC_RULE("Step-Asm-Own-Err"); SPEC_RULE("LoadProject-Err");
// Line 166-167: SPEC_RULE("Step-Asm-Done-Err"); SPEC_RULE("LoadProject-Err");
// Line 180-181: SPEC_RULE("Step-Asm-Done"); SPEC_RULE("LoadProject-Ok");
//
// =============================================================================

#include "01_project/project.h"

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "00_core/ident.h"
#include "00_core/process_config.h"
#include "01_project/deterministic_order.h"
#include "01_project/module_discovery.h"
#include "01_project/outputs.h"
#include "01_project/target_profile.h"

namespace ultraviolet::project {

namespace {

void EmitExternal(core::DiagnosticStream& diags, std::string_view code) {
  core::EmitExternalDiagnostic(diags, code);
}

bool IsDirDefault(const std::filesystem::path& path) {
  std::error_code ec;
  const bool ok = std::filesystem::is_directory(path, ec);
  if (ec) {
    return false;
  }
  return ok;
}

std::optional<Assembly> BuildAssembly(const std::filesystem::path& project_root,
                                      const ValidatedAssembly& spec,
                                      const LoadProjectDeps& deps,
                                      core::DiagnosticStream& diags) {
  const std::string root_str = project_root.generic_string();
  const std::optional<core::ResolveResult> resolved =
      deps.resolve(root_str, spec.root);
  if (!resolved.has_value()) {
    SPEC_RULE("BuildAssembly-Err-Resolve");
    EmitExternal(diags, "E-PRJ-0304");
    return std::nullopt;
  }

  const std::filesystem::path source_root = resolved->path;
  if (!deps.is_dir(source_root)) {
    SPEC_RULE("WF-Source-Root-Err");
    SPEC_RULE("BuildAssembly-Err-Root");
    EmitExternal(diags, "E-PRJ-0302");
    return std::nullopt;
  }
  SPEC_RULE("WF-Source-Root");

  const ModulesResult modules_result = deps.modules(source_root, spec.name);
  for (const auto& diag : modules_result.diags) {
    core::Emit(diags, diag);
  }
  if (core::HasError(modules_result.diags)) {
    SPEC_RULE("ModuleList-Err");
    SPEC_RULE("BuildAssembly-Err-Modules");
    return std::nullopt;
  }

  std::vector<ModuleInfo> modules = modules_result.modules;
  std::stable_sort(modules.begin(), modules.end(),
                   [](const ModuleInfo& lhs, const ModuleInfo& rhs) {
                     const std::string lhs_fold = Fold(lhs.path);
                     const std::string rhs_fold = Fold(rhs.path);
                     if (lhs_fold == rhs_fold) {
                       return Utf8LexLess(lhs.path, rhs.path);
                     }
                     return Utf8LexLess(lhs_fold, rhs_fold);
                   });
  SPEC_RULE("ModuleList-Ok");

  Assembly assembly;
  assembly.name = spec.name;
  assembly.kind = spec.kind;
  assembly.link_kind = spec.link_kind;
  assembly.root = spec.root;
  assembly.out_dir = spec.out_dir;
  assembly.emit_ir = spec.emit_ir;
  assembly.source_root = source_root;
  assembly.outputs = deps.outputs(project_root, spec);
  assembly.modules = std::move(modules);

  SPEC_RULE("BuildAssembly-Ok");
  return assembly;
}

static std::size_t RootDepth(const std::filesystem::path& source_root) {
  return core::PathComps(source_root.generic_string()).size();
}

std::optional<std::size_t> OwnerAssemblyForDir(
    const std::filesystem::path& dir,
    const std::vector<Assembly>& assemblies,
    core::DiagnosticStream& diags) {
  std::optional<std::size_t> owner;
  std::size_t owner_depth = 0;

  for (std::size_t i = 0; i < assemblies.size(); ++i) {
    const auto rel = core::Relative(
        dir.generic_string(), assemblies[i].source_root.generic_string());
    if (!rel.has_value()) {
      continue;
    }

    const std::size_t depth = RootDepth(assemblies[i].source_root);
    if (!owner.has_value() || depth > owner_depth) {
      owner = i;
      owner_depth = depth;
      continue;
    }

    if (depth == owner_depth && owner.value() != i) {
      SPEC_RULE("WF-Assembly-Root-Owner-Ambiguous");
      EmitExternal(diags, "E-PRJ-0206");
      return std::nullopt;
    }
  }

  if (!owner.has_value()) {
    SPEC_RULE("WF-Assembly-Root-Owner-Ambiguous");
    EmitExternal(diags, "E-PRJ-0206");
    return std::nullopt;
  }

  return owner;
}

bool ApplyAssemblyRootOwnership(std::vector<Assembly>& assemblies,
                                core::DiagnosticStream& diags) {
  for (std::size_t i = 0; i < assemblies.size(); ++i) {
    std::vector<ModuleInfo> owned_modules;
    owned_modules.reserve(assemblies[i].modules.size());

    for (const auto& module : assemblies[i].modules) {
      const auto owner = OwnerAssemblyForDir(module.dir, assemblies, diags);
      if (!owner.has_value()) {
        return false;
      }
      if (*owner == i) {
        owned_modules.push_back(module);
      }
    }

    assemblies[i].modules = std::move(owned_modules);
  }

  return true;
}

std::optional<Assembly> SelectAssembly(
    const std::vector<Assembly>& assemblies,
    const AssemblyTarget& target,
    core::DiagnosticStream& diags) {
  if (target.name.has_value() && !core::IsName(*target.name)) {
    SPEC_RULE("Select-Err");
    EmitExternal(diags, "E-PRJ-0205");
    return std::nullopt;
  }

  if (!target.name.has_value()) {
    if (assemblies.size() == 1) {
      SPEC_RULE("Select-Only");
      return assemblies.front();
    }

    // Auto-select if exactly one executable exists among multiple assemblies.
    const Assembly* only_exe = nullptr;
    int exe_count = 0;
    for (const auto& a : assemblies) {
      if (a.kind == "executable") {
        only_exe = &a;
        ++exe_count;
      }
    }
    if (exe_count == 1) {
      SPEC_RULE("Select-Only-Exe");
      return *only_exe;
    }

    SPEC_RULE("Select-Err");
    std::string names;
    for (std::size_t i = 0; i < assemblies.size(); ++i) {
      if (i > 0) names += ", ";
      names += assemblies[i].name;
    }
    if (auto diag = core::MakeExternalDiagnostic("E-PRJ-0205")) {
      core::SubDiagnostic note;
      note.kind = core::SubDiagnosticKind::Note;
      note.message = "available assemblies: " + names;
      diag->children.push_back(std::move(note));
      core::Emit(diags, *diag);
    }
    return std::nullopt;
  }
  for (const auto& assembly : assemblies) {
    if (assembly.name == *target.name) {
      SPEC_RULE("Select-By-Name");
      return assembly;
    }
  }
  SPEC_RULE("Select-Err");
  std::string names;
  for (std::size_t i = 0; i < assemblies.size(); ++i) {
    if (i > 0) names += ", ";
    names += assemblies[i].name;
  }
  if (auto diag = core::MakeExternalDiagnostic("E-PRJ-0205")) {
    core::SubDiagnostic note;
    note.kind = core::SubDiagnosticKind::Note;
    note.message = "available assemblies: " + names;
    diag->children.push_back(std::move(note));
    core::Emit(diags, *diag);
  }
  return std::nullopt;
}

LoadProjectResult LoadProjectImpl(const std::filesystem::path& project_root,
                                  const AssemblyTarget& target,
                                  const LoadProjectDeps& deps,
                                  bool require_selected_assembly) {
  LoadProjectResult result;

  const ManifestParseResult parsed = deps.parse(project_root);
  for (const auto& diag : parsed.diags) {
    core::Emit(result.diags, diag);
  }
  if (!parsed.table.has_value()) {
    SPEC_RULE("Step-Parse-Err");
    SPEC_RULE("LoadProject-Err");
    return result;
  }
  SPEC_RULE("Step-Parse");

  const ManifestValidationResult validated =
      deps.validate(project_root, *parsed.table);
  for (const auto& diag : validated.diags) {
    core::Emit(result.diags, diag);
  }
  if (core::HasError(validated.diags) || validated.assemblies.empty()) {
    SPEC_RULE("Step-Validate-Err");
    SPEC_RULE("LoadProject-Err");
    return result;
  }
  SPEC_RULE("Step-Validate");

  SPEC_RULE("Step-Asm-Init");
  std::vector<Assembly> assemblies;
  assemblies.reserve(validated.assemblies.size());
  for (const auto& spec : validated.assemblies) {
    SPEC_RULE("Step-Asm-Cons");
    const auto built = BuildAssembly(project_root, spec, deps, result.diags);
    if (!built.has_value()) {
      SPEC_RULE("Step-Asm-Err");
      SPEC_RULE("LoadProject-Err");
      return result;
    }
    assemblies.push_back(*built);
  }

  if (!ApplyAssemblyRootOwnership(assemblies, result.diags)) {
    SPEC_RULE("Step-Asm-Own-Err");
    SPEC_RULE("LoadProject-Err");
    return result;
  }

  std::optional<Assembly> selected;
  if (require_selected_assembly || target.name.has_value()) {
    selected = SelectAssembly(assemblies, target, result.diags);
  } else if (!assemblies.empty()) {
    selected = assemblies.front();
  }
  if (!selected.has_value()) {
    SPEC_RULE("Step-Asm-Done-Err");
    SPEC_RULE("LoadProject-Err");
    return result;
  }

  Project project;
  project.language = ActiveLanguageProfile().language;
  project.root = project_root;
  project.assemblies = std::move(assemblies);
  project.assembly = *selected;
  project.source_root = selected->source_root;
  project.outputs = selected->outputs;
  project.modules = selected->modules;
  project.lifecycle_modules = selected->modules;
  project.toolchain = validated.toolchain;
  project.build = validated.build;

  // Apply manifest-level config to process config so downstream code can
  // query it via the priority chain: CLI > manifest > default.
  core::SetManifestBuildProgress(project.build.progress);
  core::SetManifestIncremental(project.build.incremental);
  core::SetManifestRuntimeLib(project.toolchain.runtime_lib);

  result.project = std::move(project);
  SPEC_RULE("Step-Asm-Done");
  SPEC_RULE("LoadProject-Ok");
  return result;
}

}  // namespace

AssemblyTarget NoAssemblyTarget() {
  return AssemblyTarget{};
}

std::optional<AssemblyTarget> ParseAssemblyTarget(
    const std::optional<std::string>& target) {
  if (!target.has_value()) {
    return AssemblyTarget{};
  }
  if (!core::IsName(*target)) {
    return std::nullopt;
  }
  return AssemblyTarget{*target};
}

LoadProjectResult LoadProjectWithDeps(const std::filesystem::path& project_root,
                                      const AssemblyTarget& target,
                                      const LoadProjectDeps& deps) {
  return LoadProjectImpl(project_root, target, deps, true);
}

LoadProjectResult LoadProject(const std::filesystem::path& project_root,
                              const AssemblyTarget& target) {
  const LoadProjectDeps deps = {
      static_cast<ManifestParseResult (*)(const std::filesystem::path&)>(
          ParseManifest),
      ValidateManifest,
      core::Resolve,
      IsDirDefault,
      static_cast<ModulesResult (*)(
          const std::filesystem::path&, std::string_view)>(Modules),
      ComputeOutputPaths,
  };
  return LoadProjectImpl(project_root, target, deps, true);
}

LoadProjectResult LoadProject(const std::filesystem::path& project_root) {
  return LoadProject(project_root, NoAssemblyTarget());
}

LoadProjectResult LoadProjectAllAssemblies(
    const std::filesystem::path& project_root,
    const AssemblyTarget& target) {
  const LoadProjectDeps deps = {
      static_cast<ManifestParseResult (*)(const std::filesystem::path&)>(
          ParseManifest),
      ValidateManifest,
      core::Resolve,
      IsDirDefault,
      static_cast<ModulesResult (*)(
          const std::filesystem::path&, std::string_view)>(Modules),
      ComputeOutputPaths,
  };
  return LoadProjectImpl(project_root, target, deps, false);
}

LoadProjectResult LoadProjectAllAssemblies(
    const std::filesystem::path& project_root) {
  return LoadProjectAllAssemblies(project_root, NoAssemblyTarget());
}

}  // namespace ultraviolet::project
