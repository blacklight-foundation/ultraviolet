#include "06_driver/tooling/analysis.h"

#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "00_core/source_load.h"
#include "01_project/module_discovery.h"
#include "02_source/parser/parse_modules.h"
#include "03_comptime/comptime.h"
#include "04_analysis/resolve/resolve_items.h"
#include "04_analysis/resolve/resolver.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/resolve/visibility.h"
#include "04_analysis/typing/typecheck.h"
#include "06_driver/tooling/uri.h"

namespace ultraviolet::driver::tooling {

namespace {

thread_local const std::unordered_map<std::string, std::string>* g_overlays =
    nullptr;

void AppendDiags(core::DiagnosticStream& out,
                 const core::DiagnosticStream& add) {
  for (const auto& diag : add) {
    core::Emit(out, diag);
  }
}

void EmitInternal(core::DiagnosticStream& diags, std::string message) {
  core::Diagnostic diag;
  diag.severity = core::Severity::Error;
  diag.message = std::move(message);
  core::Emit(diags, diag);
}

frontend::ReadBytesResult ReadBytesOverlayAware(
    const std::filesystem::path& path) {
  if (g_overlays != nullptr) {
    const auto it = g_overlays->find(PathKey(path));
    if (it != g_overlays->end()) {
      frontend::ReadBytesResult result;
      result.bytes = std::vector<std::uint8_t>(
          it->second.begin(), it->second.end());
      return result;
    }
  }
  return frontend::ReadBytesDefault(path);
}

frontend::ComptimePassOptions BuildComptimeOptions(
    const project::Project& project) {
  frontend::ComptimePassOptions options;
  options.project_root = project.root;
  options.fallback_source_root = project.source_root;
  options.source_roots_by_assembly.reserve(project.assemblies.size());
  for (const auto& assembly : project.assemblies) {
    options.source_roots_by_assembly[assembly.name] = assembly.source_root;
  }
  return options;
}

std::vector<project::ModuleInfo> AllAssemblyModules(
    const project::Project& project) {
  std::vector<project::ModuleInfo> modules;
  for (const auto& assembly : project.assemblies) {
    modules.insert(modules.end(), assembly.modules.begin(),
                   assembly.modules.end());
  }
  return modules;
}

}  // namespace

AnalysisSnapshot AnalyzeWorkspace(
    const ToolingAnalysisOptions& options,
    std::span<const DocumentOverlay> overlays) {
  AnalysisSnapshot snapshot;

  const auto assembly_target =
      project::ParseAssemblyTarget(options.assembly_target);
  if (!assembly_target.has_value()) {
    EmitInternal(snapshot.diagnostics, "Invalid Ultraviolet assembly target.");
    return snapshot;
  }

  const std::filesystem::path root = options.project_root.empty()
                                         ? std::filesystem::current_path()
                                         : NormalizePath(options.project_root);
  const project::LoadProjectResult loaded =
      project::LoadProject(root, *assembly_target);
  AppendDiags(snapshot.diagnostics, loaded.diags);
  if (!loaded.project.has_value() || core::HasError(loaded.diags)) {
    snapshot.project_ok = false;
    return snapshot;
  }

  snapshot.project_ok = true;
  project::Project sema_project = *loaded.project;
  snapshot.project = sema_project;

  std::optional<project::TargetProfile> selected_target_profile =
      options.target_profile.has_value() ? options.target_profile
                                         : sema_project.toolchain.target_profile;
  if (!selected_target_profile.has_value()) {
    core::EmitExternalDiagnostic(snapshot.diagnostics, "E-PRJ-0112");
    return snapshot;
  }

  std::unordered_map<std::string, std::string> overlay_text_by_path;
  overlay_text_by_path.reserve(overlays.size());
  for (const auto& overlay : overlays) {
    overlay_text_by_path[PathKey(overlay.path)] = overlay.text_utf8;
  }

	  frontend::ParseModuleDeps deps;
	  deps.compilation_unit = static_cast<project::CompilationUnitResult (*)(
	      const std::filesystem::path&)>(project::CompilationUnit);
  deps.read_bytes = ReadBytesOverlayAware;
  deps.load_source = core::LoadSource;
  deps.parse_file = ast::ParseFile;
  deps.inspect_source = nullptr;

  const auto* previous_overlays = g_overlays;
  g_overlays = &overlay_text_by_path;

  std::vector<ast::ASTModule> parsed_modules;
  frontend::UnsafeSpanMap unsafe_spans_by_file;
  bool parse_ok = true;
  for (const auto& assembly : sema_project.assemblies) {
    frontend::ParseModulesResult parsed = frontend::ParseModulesWithDeps(
        assembly.modules, assembly.source_root, assembly.name, deps);
    AppendDiags(snapshot.diagnostics, parsed.diags);
    for (auto& [path, spans] : parsed.unsafe_spans_by_file) {
      unsafe_spans_by_file.insert_or_assign(std::move(path), std::move(spans));
    }
    if (!parsed.modules.has_value() || core::HasError(parsed.diags)) {
      parse_ok = false;
      break;
    }
    parsed_modules.insert(parsed_modules.end(),
                          std::make_move_iterator(parsed.modules->begin()),
                          std::make_move_iterator(parsed.modules->end()));
  }

  g_overlays = previous_overlays;

  snapshot.parse_ok = parse_ok;
  if (!parse_ok) {
    snapshot.modules = std::move(parsed_modules);
    snapshot.symbols = BuildSymbolIndex(snapshot.modules);
    snapshot.language_service =
        analysis::BuildLanguageServiceDeclarations(snapshot.modules);
    return snapshot;
  }

  std::vector<ast::ASTModule> analysis_modules = std::move(parsed_modules);
  snapshot.comptime_ok = true;
  if (options.run_comptime) {
    frontend::ComptimeResult comptime =
        frontend::ExecuteComptime(analysis_modules,
                                  BuildComptimeOptions(sema_project));
    AppendDiags(snapshot.diagnostics, comptime.diags);
    if (!comptime.modules.has_value() || core::HasError(comptime.diags)) {
      snapshot.comptime_ok = false;
      snapshot.modules = std::move(analysis_modules);
      snapshot.symbols = BuildSymbolIndex(snapshot.modules);
      snapshot.language_service =
          analysis::BuildLanguageServiceDeclarations(snapshot.modules);
      return snapshot;
    }
    analysis_modules = std::move(*comptime.modules);
  }

  if (!options.semantic) {
    snapshot.modules = std::move(analysis_modules);
    snapshot.symbols = BuildSymbolIndex(snapshot.modules);
    snapshot.language_service =
        analysis::BuildLanguageServiceDeclarations(snapshot.modules);
    return snapshot;
  }

  sema_project.modules = AllAssemblyModules(sema_project);

  analysis::ScopeContext ctx;
  ctx.project = &sema_project;
  ctx.target_profile = *selected_target_profile;
  ctx.sigma.mods = std::move(analysis_modules);
  ctx.sigma.unsafe_spans_by_file = std::move(unsafe_spans_by_file);
  snapshot.language_service =
      analysis::BuildLanguageServiceDeclarations(ctx.sigma.mods);

  for (const auto& module : ctx.sigma.mods) {
    AppendDiags(snapshot.diagnostics,
                analysis::CheckModuleVisibility(ctx, module));
  }

  analysis::NameMapBuildResult name_maps = analysis::CollectNameMaps(ctx);
  AppendDiags(snapshot.diagnostics, name_maps.diags);
  if (core::HasError(snapshot.diagnostics)) {
    snapshot.modules = std::move(ctx.sigma.mods);
    snapshot.symbols = BuildSymbolIndex(snapshot.modules);
    return snapshot;
  }

  analysis::PopulateSigma(ctx);
  const auto module_names = analysis::ModuleNamesOf(sema_project);

  analysis::ResolveContext res_ctx;
  res_ctx.ctx = &ctx;
  res_ctx.name_maps = &name_maps.name_maps;
  res_ctx.module_names = &module_names;
  res_ctx.can_access = analysis::CanAccess;
  res_ctx.parse_ok = snapshot.parse_ok;
  res_ctx.parse_diags = &snapshot.diagnostics;
  res_ctx.language_service = &snapshot.language_service;

  analysis::ResolveModulesResult resolved = analysis::ResolveModules(res_ctx);
  snapshot.resolve_ok = resolved.ok;
  AppendDiags(snapshot.diagnostics, resolved.diags);
  if (resolved.ok) {
    ctx.sigma.mods = std::move(resolved.modules);
    analysis::PopulateSigma(ctx);
  }

  if (!core::HasError(snapshot.diagnostics) && snapshot.resolve_ok) {
    analysis::TypecheckResult typechecked =
        analysis::TypecheckModules(ctx, ctx.sigma.mods, &name_maps.name_maps);
    snapshot.typecheck_ok = typechecked.ok;
    AppendDiags(snapshot.diagnostics, typechecked.diags);
    snapshot.expr_types = std::move(typechecked.expr_types);
  }

  snapshot.modules = std::move(ctx.sigma.mods);
  snapshot.symbols = BuildSymbolIndex(snapshot.modules);
  snapshot.project = sema_project;
  return snapshot;
}

}  // namespace ultraviolet::driver::tooling
