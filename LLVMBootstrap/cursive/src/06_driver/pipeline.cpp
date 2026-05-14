// =============================================================================
// pipeline.cpp - Compilation pipeline execution
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md Section 1.1 - Conformance
//   CursiveSpecification.md Section 3.6 - Output Artifacts and Linking
//   CursiveSpecification.md Chapter 24 - Common Lowering, Program Lifecycle, and Backend
//
// =============================================================================

#include "06_driver/pipeline.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/build_log_policy.h"
#include "00_core/diagnostics.h"
#include "00_core/host/services.h"
#include "00_core/host_primitives.h"
#include "00_core/process_config.h"
#include "00_core/source_text.h"
#include "00_core/symbols.h"
#include "01_project/assemblies.h"
#include "01_project/ir_assembly.h"
#include "01_project/language_profile.h"
#include "01_project/project.h"
#include "01_project/tool_resolution.h"
#include "02_source/lexer.h"
#include "04_analysis/conformance/conformance.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/globals/globals.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_module.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/llvm/llvm_emit.h"
#include "05_codegen/llvm/llvm_passes.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

namespace cursive::driver {

using core::Diagnostic;
using core::DiagnosticStream;
using core::Severity;
using analysis::ConformanceInput;
using analysis::PhaseOrderResult;

// ============================================================================
// Source Inspection
// ============================================================================

core::DiagnosticStream InspectSource(const core::SourceFile& source) {
  (void)source;
  return {};
}

// ============================================================================
// LLVM Initialization
// ============================================================================

void EnsureLLVMInit() {
  static std::once_flag once;
  std::call_once(once, []() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
  });
}

// ============================================================================
// File System Helpers
// ============================================================================

bool EnsureDir(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    if (ec) {
      return false;
    }
    return std::filesystem::is_directory(path, ec) && !ec;
  }
  std::filesystem::create_directories(path, ec);
  if (ec) {
    return false;
  }
  return std::filesystem::is_directory(path, ec) && !ec;
}

bool WriteFile(const std::filesystem::path& path, std::string_view bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    core::HostPrimFail(core::HostPrim::WriteFile, true);
    return false;
  }
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!out) {
    core::HostPrimFail(core::HostPrim::WriteFile, true);
    return false;
  }
  return true;
}

// ============================================================================
// LLVM Module Bundle (internal)
// ============================================================================

namespace {

unsigned long CurrentProcessId() {
  return core::CurrentHostProcessId();
}

void LogCodegenProgress(const std::string& message) {
  const bool debug_codegen =
      core::IsDebugEnabled("pipeline") || core::IsDebugEnabled("codegen");
  core::BuildLogResolveOptions options;
  options.debug_enabled = debug_codegen;
  options.cli_progress = core::BuildProgressOverride();
  options.manifest_progress = core::ManifestBuildProgress();
  // Codegen progress is opt-in outside debug/verbose modes.
  options.default_enabled = false;
  const core::BuildLogMode mode = core::ResolveBuildLogMode(options);
  if (mode == core::BuildLogMode::None) {
    return;
  }
  if (mode == core::BuildLogMode::Summary &&
      !core::ShouldEmitSummaryBuildLog(core::BuildLogChannel::Codegen,
                                       message)) {
    return;
  }

  static std::mutex output_mu;
  std::lock_guard<std::mutex> lock(output_mu);
  if (!debug_codegen) {
    std::cerr << "[cursive] codegen " << message << "\n";
  } else {
    std::cerr << "[cursive] codegen pid=" << CurrentProcessId() << " "
              << message << "\n";
  }
  std::cerr.flush();
}

std::string ModuleNameForLog(const ModuleCodegen& module) {
  if (!module.path_key.empty()) {
    return module.path_key;
  }
  return "<root>";
}

std::string ModuleNameForPath(std::string_view module_path) {
  if (module_path.empty()) {
    return "<root>";
  }
  return std::string(module_path);
}

struct RenderedLLVMArtifact {
  std::string text;
  std::string bitcode;
};

std::optional<RenderedLLVMArtifact> RenderLLVMText(
    llvm::Module& module,
    const std::filesystem::path& assembler,
    std::string_view module_name) {
  std::string text;
  llvm::raw_string_ostream os(text);
  module.print(os, nullptr);

  // raw_string_ostream itself is infallible. The render boundary is therefore
  // defined by whether the pinned LLVM textual IR oracle accepts the bytes.
  auto bitcode = project::AssembleIR(assembler, text);
  if (!bitcode.has_value()) {
    LogCodegenProgress("emit-ir-error module=" + std::string(module_name) +
                       " stage=validate-llvm-text");
    SPEC_RULE("EmitLLVM-Err");
    return std::nullopt;
  }

  return RenderedLLVMArtifact{std::move(text), std::move(*bitcode)};
}

void CollectHostedStateTemplates(CodegenCache& cache,
                                 const ModuleCodegen& module_entry) {
  analysis::ScopeContext scope;
  if (cache.ctx.sigma) {
    scope.sigma = *cache.ctx.sigma;
    scope.sigma_source = cache.ctx.sigma;
    scope.current_module = module_entry.path;
    scope.target_profile = cache.ctx.target_profile;
  }

  auto hosted_layout_for = [&](const std::string& symbol)
      -> std::pair<std::uint64_t, std::uint64_t> {
    const analysis::TypeRef static_type = cache.ctx.LookupStaticType(symbol);
    const std::optional<std::uint64_t> size =
        static_type ? analysis::layout::SizeOf(scope, static_type)
                    : std::nullopt;
    const std::optional<std::uint64_t> align =
        static_type ? analysis::layout::AlignOf(scope, static_type)
                    : std::nullopt;
    return {size.value_or(0u), std::max<std::uint64_t>(1u, align.value_or(1u))};
  };

  for (const auto& decl : module_entry.decls) {
    if (const auto* global = std::get_if<codegen::GlobalConst>(&decl)) {
      codegen::LowerCtx::HostedStateTemplate tmpl;
      tmpl.symbol = global->symbol;
      tmpl.bytes = global->bytes;
      const auto [size, align] = hosted_layout_for(tmpl.symbol);
      tmpl.size = size != 0u ? size
                             : static_cast<std::uint64_t>(global->bytes.size());
      tmpl.align = align;
      tmpl.zero_init = false;
      cache.hosted_state_templates.emplace(tmpl.symbol, std::move(tmpl));
      continue;
    }
    if (const auto* global = std::get_if<codegen::GlobalZero>(&decl)) {
      codegen::LowerCtx::HostedStateTemplate tmpl;
      tmpl.symbol = global->symbol;
      const auto [size, align] = hosted_layout_for(tmpl.symbol);
      tmpl.size = size != 0u ? size : global->size;
      tmpl.align = align;
      tmpl.zero_init = true;
      cache.hosted_state_templates.emplace(tmpl.symbol, std::move(tmpl));
    }
  }
}

void ConfigureResolveCallbacks(codegen::LowerCtx& ctx,
                               const analysis::NameMapBuildResult& name_maps) {
  ctx.resolve_name = [&ctx, &name_maps](const std::string& name)
                         -> std::optional<std::vector<std::string>> {
    const auto map_it = name_maps.name_maps.find(analysis::PathKeyOf(ctx.module_path));
    if (map_it == name_maps.name_maps.end()) {
      if (!codegen::BuiltinSym(name).empty()) {
        return std::vector<std::string>{name};
      }
      return std::nullopt;
    }
    const auto& map = map_it->second;
    const auto ent_it = map.find(analysis::IdKeyOf(name));
    if (ent_it == map.end()) {
      if (!codegen::BuiltinSym(name).empty()) {
        return std::vector<std::string>{name};
      }
      return std::nullopt;
    }
    const auto& ent = ent_it->second;
    if (ent.kind != analysis::EntityKind::Value) {
      if (!codegen::BuiltinSym(name).empty()) {
        return std::vector<std::string>{name};
      }
      return std::nullopt;
    }
    const std::string resolved_name = ent.target_opt.value_or(name);
    if (!ent.origin_opt.has_value()) {
      if (codegen::BuiltinSym(resolved_name).empty()) {
        return std::nullopt;
      }
      return std::vector<std::string>{resolved_name};
    }
    std::vector<std::string> full = *ent.origin_opt;
    full.push_back(resolved_name);
    return full;
  };

  ctx.resolve_type_name = [&ctx, &name_maps](const std::string& name)
                              -> std::optional<std::vector<std::string>> {
    const auto map_it = name_maps.name_maps.find(analysis::PathKeyOf(ctx.module_path));
    if (map_it == name_maps.name_maps.end()) {
      return std::nullopt;
    }
    const auto& map = map_it->second;
    const auto ent_it = map.find(analysis::IdKeyOf(name));
    if (ent_it == map.end()) {
      return std::nullopt;
    }
    const auto& ent = ent_it->second;
    if (ent.kind != analysis::EntityKind::Type || !ent.origin_opt.has_value()) {
      return std::nullopt;
    }
    std::vector<std::string> full = *ent.origin_opt;
    const std::string resolved_name = ent.target_opt.value_or(name);
    full.push_back(resolved_name);
    return full;
  };

  ctx.resolve_type_name_in_module =
      [&name_maps](const std::vector<std::string>& module_path,
                   const std::string& name)
          -> std::optional<std::vector<std::string>> {
    const auto map_it = name_maps.name_maps.find(analysis::PathKeyOf(module_path));
    if (map_it == name_maps.name_maps.end()) {
      return std::nullopt;
    }
    const auto ent_it = map_it->second.find(analysis::IdKeyOf(name));
    if (ent_it == map_it->second.end()) {
      return std::nullopt;
    }
    const auto& ent = ent_it->second;
    if (ent.kind != analysis::EntityKind::Type || !ent.origin_opt.has_value()) {
      return std::nullopt;
    }
    std::vector<std::string> full = *ent.origin_opt;
    const std::string resolved_name = ent.target_opt.value_or(name);
    full.push_back(resolved_name);
    return full;
  };
}

void ResetLowerContextForModule(codegen::LowerCtx& ctx,
                                const ast::ModulePath& module_path) {
  ctx.module_path = module_path;
  ctx.resolve_failed = false;
  ctx.codegen_failed = false;
  ctx.resolve_failures.clear();
  ctx.proc_ret_type = nullptr;
  ctx.main_symbol.reset();
  ctx.expr_prov.reset();
  ctx.expr_region.reset();
  ctx.expr_region_tags.reset();
  ctx.dynamic_checks = false;
  ctx.current_access_order.reset();
  ctx.log_enabled = false;
  ctx.log_to_console = false;
  ctx.log_to_file = false;
  ctx.trace = false;
  ctx.trace_filter_mask.reset();
  ctx.trace_min_level.reset();
  ctx.trace_root.clear();
  ctx.log_file_path.clear();
  ctx.active_contract_postcondition = nullptr;
  ctx.contract_result_value.reset();
  ctx.contract_entry_values.clear();
  ctx.contract_param_entry_values.clear();
  ctx.lowering_contract_postcondition = false;
  ctx.values.static_types.clear();
  ctx.values.drop_glue_types.clear();
  ctx.values.parent = nullptr;
  ctx.values.value_types.clear();
  ctx.values.value_type_insert_sink = nullptr;
  ctx.values.derived_values.clear();
  ctx.values.required_vtables.clear();
  ctx.temp_counter = std::make_shared<std::uint64_t>(0);
  ctx.scope_stack.clear();
  ctx.next_runtime_scope_id = std::make_shared<std::uint64_t>(1);
  ctx.binding_states.clear();
  ctx.local_addr_aliases.clear();
  ctx.next_binding_id = 1;
  ctx.temp_sink = nullptr;
  ctx.temp_depth = 0;
  ctx.suppress_temp_at_depth.reset();
  ctx.parallel_collect = nullptr;
  ctx.parallel_collect_depth = 0;
  ctx.capture_env.reset();
  ctx.active_key_scopes.clear();
  ctx.implicit_key_scope_names.clear();
  ctx.extra_procs.clear();
  ctx.current_proc_symbol.reset();
  ctx.current_closure_counter = 0;
  ctx.generic_instantiation_stack.clear();
  ctx.generic_instantiation_in_progress.clear();
  ctx.active_region_aliases.clear();
  ctx.region_alias_counter = 0;
}

std::unordered_set<std::string> BuildProjectModuleKeySet(
    const project::Project& project) {
  std::unordered_set<std::string> keys;
  keys.reserve(project.modules.size());
  for (const auto& module : project.modules) {
    keys.insert(module.path);
  }
  return keys;
}

std::unordered_set<std::string> BuildProjectLifecycleModuleKeySet(
    const project::Project& project) {
  const auto& modules = project.lifecycle_modules.empty()
                            ? project.modules
                            : project.lifecycle_modules;
  std::unordered_set<std::string> keys;
  keys.reserve(modules.size());
  for (const auto& module : modules) {
    keys.insert(module.path);
  }
  return keys;
}

bool ProjectContainsModule(
    const std::unordered_set<std::string>& project_modules,
    const ast::ModulePath& module_path) {
  return project_modules.find(core::StringOfPath(module_path)) !=
         project_modules.end();
}

bool IsRootModule(const project::Project& project,
                  std::string_view module_path_key) {
  return module_path_key == project.assembly.name;
}

bool IsRootModule(const project::Project& project,
                  const ModuleCodegen& module) {
  if (!module.path_key.empty()) {
    return IsRootModule(project, module.path_key);
  }
  return IsRootModule(project, core::StringOfPath(module.path));
}

bool WithEntry(const project::Project& project, const ModuleCodegen& module) {
  return project::IsExecutable(project) &&
         IsRootModule(project, module) &&
         module.main_symbol.has_value();
}

std::optional<std::string> SelectProjectEntryModule(
    const project::Project& project) {
  if (project.modules.empty()) {
    return std::nullopt;
  }

  for (const auto& module : project.modules) {
    if (IsRootModule(project, module.path)) {
      return module.path;
    }
  }

  return std::nullopt;
}

void FilterHostedExportsForProject(
    codegen::LowerCtx& ctx,
    const std::unordered_set<std::string>& project_modules);
void FilterInitPlanForProject(
    codegen::LowerCtx& ctx,
    const analysis::InitPlan& init_plan,
    const std::unordered_set<std::string>& project_modules);

void ConfigureCodegenContextForProjectImpl(CodegenCache& cache,
                                           const project::Project& project) {
  const std::string context_key =
      project.assembly.name + "|" + project.assembly.kind + "|" +
      project.assembly.link_kind.value_or("none");
  const bool context_changed = cache.active_project_context_key != context_key;
  if (context_changed) {
    cache.active_project_context_key = context_key;
    cache.emit_context_epoch += 1;
    cache.modules.clear();
    cache.index.clear();
    cache.module_entries.clear();
    cache.module_states.clear();
    cache.lowered_proc_symbols.clear();
    cache.hosted_state_templates.clear();
    cache.ctx.shared_library_export_symbols.clear();
  }
  const auto project_modules = BuildProjectModuleKeySet(project);
  const auto lifecycle_modules = BuildProjectLifecycleModuleKeySet(project);

  cache.ctx.executable_project = project::IsExecutable(project);
  cache.ctx.shared_library_project = project::IsSharedLibrary(project);
  cache.ctx.hosted_library = project::IsLibrary(project);
  cache.ctx.project_entry_module = SelectProjectEntryModule(project);

  cache.ctx.dependency_assembly_names.clear();
  cache.ctx.shared_library_assembly_names.clear();
  for (const auto& assembly : project.assemblies) {
    if (project::IsDependency(assembly)) {
      cache.ctx.dependency_assembly_names.insert(assembly.name);
    } else if (project::IsSharedLibrary(assembly)) {
      cache.ctx.shared_library_assembly_names.insert(assembly.name);
    }
  }

  cache.hosted_project_modules.clear();
  cache.hosted_project_modules.reserve(project.modules.size());
  for (const auto& module : project.modules) {
    cache.hosted_project_modules.push_back(module.path);
  }
  cache.ctx.hosted_project_modules = cache.hosted_project_modules;

  cache.ctx.hosted_exports = cache.all_hosted_exports;
  FilterHostedExportsForProject(cache.ctx, project_modules);
  cache.ctx.hosted_library =
      cache.ctx.hosted_library && !cache.ctx.hosted_exports.empty();

  if (cache.full_init_plan.has_value()) {
    FilterInitPlanForProject(cache.ctx, *cache.full_init_plan, lifecycle_modules);
  } else {
    cache.ctx.init_order.clear();
    cache.ctx.init_modules.clear();
    cache.ctx.init_eager_edges.clear();
  }

  if (!cache.ctx.shared_library_project) {
    cache.ctx.shared_library_export_symbols.clear();
  }
}

bool ValidateCodegenCacheCoverage(CodegenCache& cache,
                                  const project::Project& project) {
  for (const auto& module : project.modules) {
    if (cache.ast_modules.find(module.path) == cache.ast_modules.end()) {
      LogCodegenProgress("cache-build-error stage=module-coverage missing=" +
                         module.path);
      std::cerr << "[cursive] BuildCodegenCache: missing AST module entry for path '"
                << module.path << "'\n";
      cache.ok.store(false);
      return false;
    }
  }
  return true;
}

void FilterHostedExportsForProject(
    codegen::LowerCtx& ctx,
    const std::unordered_set<std::string>& project_modules) {
  if (ctx.hosted_exports.empty()) {
    return;
  }

  std::vector<codegen::LowerCtx::HostedExportInfo> filtered;
  filtered.reserve(ctx.hosted_exports.size());
  for (auto& info : ctx.hosted_exports) {
    const auto* owner = ctx.LookupProcModule(info.internal_symbol);
    if (!owner || owner->empty()) {
      continue;
    }
    if (project_modules.find(core::StringOfPath(*owner)) !=
        project_modules.end()) {
      filtered.push_back(std::move(info));
    }
  }
  ctx.hosted_exports = std::move(filtered);
}

void FilterInitPlanForProject(
    codegen::LowerCtx& ctx,
    const analysis::InitPlan& init_plan,
    const std::unordered_set<std::string>& project_modules) {
  ctx.init_order.clear();
  ctx.init_modules.clear();
  ctx.init_eager_edges.clear();

  std::unordered_map<std::string, std::size_t> remapped_indices;
  remapped_indices.reserve(init_plan.graph.modules.size());

  for (const auto& module_path : init_plan.graph.modules) {
    if (!ProjectContainsModule(project_modules, module_path)) {
      continue;
    }
    remapped_indices.emplace(core::StringOfPath(module_path),
                             ctx.init_modules.size());
    ctx.init_modules.push_back(module_path);
  }

  for (const auto& edge : init_plan.graph.eager_edges) {
    if (edge.first >= init_plan.graph.modules.size() ||
        edge.second >= init_plan.graph.modules.size()) {
      continue;
    }
    const auto& from_path = init_plan.graph.modules[edge.first];
    const auto& to_path = init_plan.graph.modules[edge.second];
    const auto from_it = remapped_indices.find(core::StringOfPath(from_path));
    const auto to_it = remapped_indices.find(core::StringOfPath(to_path));
    if (from_it == remapped_indices.end() || to_it == remapped_indices.end()) {
      continue;
    }
    ctx.init_eager_edges.emplace_back(from_it->second, to_it->second);
  }

  for (const auto& module_path : init_plan.init_order) {
    if (ProjectContainsModule(project_modules, module_path)) {
      ctx.init_order.push_back(module_path);
    }
  }
}

std::optional<ModuleCodegen> LowerCodegenModule(codegen::LowerCtx& ctx,
                                                const ast::ASTModule& module,
                                                std::size_t index) {
  const std::string module_path_key = core::StringOfPath(module.path);
  const std::string module_log_name = ModuleNameForPath(module_path_key);
  const auto lower_start = std::chrono::steady_clock::now();
  LogCodegenProgress("cache-lower-start index=" + std::to_string(index) +
                     " module=" + module_log_name);

  ResetLowerContextForModule(ctx, module.path);

  ModuleCodegen entry;
  entry.path = module.path;
  entry.path_key = module_path_key;
  entry.decls = codegen::LowerModule(module, ctx);
  LogCodegenProgress("cache-lower-mid index=" + std::to_string(index) +
                     " module=" + module_log_name +
                     " stage=lower-module-returned");

  if (ctx.resolve_failed || ctx.codegen_failed) {
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - lower_start)
                                 .count();
    LogCodegenProgress("cache-lower-error index=" + std::to_string(index) +
                       " module=" + module_log_name +
                       " elapsed_ms=" + std::to_string(elapsed_ms));
    std::cerr << "[cursive] EnsureCodegenModule: lowering failed for module '"
              << entry.path_key << "'"
              << " (resolve_failed="
              << (ctx.resolve_failed ? "true" : "false")
              << ", codegen_failed="
              << (ctx.codegen_failed ? "true" : "false") << ")";
    if (!ctx.resolve_failures.empty()) {
      std::cerr << " unresolved=[";
      for (std::size_t i = 0; i < ctx.resolve_failures.size(); ++i) {
        if (i > 0) {
          std::cerr << ", ";
        }
        std::cerr << ctx.resolve_failures[i];
      }
      std::cerr << "]";
    }
    std::cerr << "\n";
    return std::nullopt;
  }

  entry.values = std::move(ctx.values);
  entry.values.parent = nullptr;
  entry.values.value_type_insert_sink = nullptr;
  entry.proc_sigs = ctx.proc_sigs;
  entry.proc_linkages = ctx.proc_linkages;
  entry.async_procs = ctx.async_procs;
  entry.temp_counter = ctx.temp_counter ? *ctx.temp_counter : 0;
  entry.main_symbol = ctx.main_symbol;

  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - lower_start)
                               .count();
  LogCodegenProgress("cache-lower-finish index=" + std::to_string(index) +
                     " module=" + module_log_name + " value_types=" +
                     std::to_string(entry.values.value_types.size()) +
                     " derived_values=" +
                     std::to_string(entry.values.derived_values.size()) +
                     " elapsed_ms=" + std::to_string(elapsed_ms));
  return entry;
}

void DeduplicateProcDecls(
    codegen::IRDecls& decls,
    const std::unordered_map<std::string, codegen::LinkageKind>& proc_linkages,
    std::unordered_set<std::string>& seen_proc_symbols) {
  if (decls.empty()) {
    return;
  }

  codegen::IRDecls filtered;
  filtered.reserve(decls.size());
  for (auto& decl : decls) {
    if (auto* proc = std::get_if<codegen::ProcIR>(&decl)) {
      const auto linkage_it = proc_linkages.find(proc->symbol);
      const auto linkage =
          linkage_it == proc_linkages.end()
              ? codegen::LinkageKind::Internal
              : linkage_it->second;
      if (linkage != codegen::LinkageKind::Internal &&
          !seen_proc_symbols.emplace(proc->symbol).second) {
        continue;
      }
    }
    filtered.push_back(std::move(decl));
  }
  decls = std::move(filtered);
}

struct LLVMModuleBundle {
  std::unique_ptr<llvm::LLVMContext> ctx;
  std::unique_ptr<llvm::Module> module;
  bool codegen_failed = false;
};

struct CachedLLVMArtifacts {
  std::optional<LLVMModuleBundle> bundle;
  std::optional<std::string> ir_text;
  std::optional<std::string> bitcode;
};

codegen::LowerCtx& AcquireThreadLocalLowerCtx(const CodegenCache& cache) {
  thread_local codegen::LowerCtx ctx;
  ctx = cache.ctx;
  if (cache.name_maps != nullptr) {
    ConfigureResolveCallbacks(ctx, *cache.name_maps);
  }
  return ctx;
}

struct ThreadLocalEmitCtxState {
  const CodegenCache* cache = nullptr;
  std::uint64_t epoch = 0;
  codegen::LowerCtx ctx;
};

codegen::LowerCtx& AcquireThreadLocalEmitCtx(const CodegenCache& cache) {
  thread_local ThreadLocalEmitCtxState state;
  if (state.cache != &cache || state.epoch != cache.emit_context_epoch) {
    // Seed once per thread/cache. This avoids copying large shared maps for
    // every module emission in full non-incremental builds. The epoch changes
    // when later-lowered modules publish static metadata needed by emission.
    state.ctx = cache.ctx;
    if (cache.name_maps != nullptr) {
      ConfigureResolveCallbacks(state.ctx, *cache.name_maps);
    }
    state.cache = &cache;
    state.epoch = cache.emit_context_epoch;
  }
  return state.ctx;
}

struct ThreadLocalEmitArtifactState {
  const CodegenCache* cache = nullptr;
  std::uint64_t epoch = 0;
  std::unordered_map<std::string, CachedLLVMArtifacts> artifacts;
};

ThreadLocalEmitArtifactState& AcquireThreadLocalEmitArtifacts(
    const CodegenCache& cache) {
  thread_local ThreadLocalEmitArtifactState state;
  if (state.cache != &cache || state.epoch != cache.emit_context_epoch) {
    state.cache = &cache;
    state.epoch = cache.emit_context_epoch;
    state.artifacts.clear();
  }
  return state;
}

std::optional<LLVMModuleBundle> EmitLLVMModule(
    const CodegenCache& cache,
    const ModuleCodegen& module,
    const project::Project& project,
    project::TargetProfile target_profile) {
  codegen::LowerCtx& emit_ctx = AcquireThreadLocalEmitCtx(cache);
  ResetLowerContextForModule(emit_ctx, module.path);
  emit_ctx.values.value_types = module.values.value_types;
  emit_ctx.values.derived_values = module.values.derived_values;
  emit_ctx.values.required_vtables = module.values.required_vtables;
  emit_ctx.proc_sigs = module.proc_sigs;
  emit_ctx.proc_linkages = module.proc_linkages;
  emit_ctx.async_procs = module.async_procs;
  emit_ctx.temp_counter = std::make_shared<std::uint64_t>(module.temp_counter);
  emit_ctx.values.drop_glue_types = module.values.drop_glue_types;
  emit_ctx.shared_library_project = cache.ctx.shared_library_project;
  emit_ctx.hosted_library = cache.ctx.hosted_library;
  emit_ctx.hosted_exports = cache.ctx.hosted_exports;
  emit_ctx.hosted_state_templates = cache.hosted_state_templates;
  emit_ctx.hosted_project_modules = cache.hosted_project_modules;
  emit_ctx.shared_library_export_symbols =
      cache.ctx.shared_library_export_symbols;
  emit_ctx.main_symbol.reset();
  if (WithEntry(project, module)) {
    emit_ctx.main_symbol = module.main_symbol;
  }
  emit_ctx.resolve_failed = false;
  emit_ctx.codegen_failed = false;

  LLVMModuleBundle bundle;
  bundle.ctx = std::make_unique<llvm::LLVMContext>();
	  codegen::LLVMEmitter emitter(
	      *bundle.ctx,
	      module.path_key.empty()
	          ? std::string(project::ActiveLanguageProfile().lower_name) + "_module"
	          : module.path_key,
	      target_profile);
  llvm::Module* raw = emitter.EmitModule(module.decls, emit_ctx);
  bundle.module = emitter.ReleaseModule();
  bundle.codegen_failed = emit_ctx.codegen_failed;
  if (!raw || !bundle.module || emit_ctx.codegen_failed) {
    SPEC_RULE("LowerIR-Err");
    return std::nullopt;
  }
  return bundle;
}

std::optional<CachedLLVMArtifacts*> MaterializeCachedLLVMArtifacts(
    const CodegenCache& cache,
    const ModuleCodegen& module,
    const project::Project& project,
    project::TargetProfile target_profile,
    bool need_llvm_module,
    bool need_ir_artifact) {
  auto& artifact_state = AcquireThreadLocalEmitArtifacts(cache);
  auto& entry = artifact_state.artifacts[module.path_key];
  if ((need_llvm_module || need_ir_artifact) && !entry.bundle.has_value()) {
    auto bundle = EmitLLVMModule(cache, module, project, target_profile);
    if (!bundle) {
      return std::nullopt;
    }
    entry.bundle = std::move(*bundle);
  }

  if (need_ir_artifact && !entry.ir_text.has_value()) {
    const auto assembler = project::ResolveTool(project, target_profile, "llvm-as");
    if (!assembler.has_value()) {
      return std::nullopt;
    }
    const std::string module_name = ModuleNameForLog(module);
    auto rendered = RenderLLVMText(*entry.bundle->module, *assembler, module_name);
    if (!rendered.has_value()) {
      return std::nullopt;
    }
    entry.ir_text = std::move(rendered->text);
    entry.bitcode = std::move(rendered->bitcode);
  }

  return &entry;
}

const llvm::Target* GetCachedTarget(const llvm::Triple& triple,
                                    std::string& err) {
  static std::mutex cache_mu;
  static std::unordered_map<std::string, const llvm::Target*> cache;

  const std::string triple_key = triple.str();
  {
    std::lock_guard<std::mutex> lock(cache_mu);
    if (const auto it = cache.find(triple_key); it != cache.end()) {
      return it->second;
    }
  }

  const llvm::Target* target =
      llvm::TargetRegistry::lookupTarget(triple_key, err);
  if (!target) {
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(cache_mu);
  const auto [it, _inserted] = cache.emplace(triple_key, target);
  return it->second;
}

llvm::CodeGenOptLevel ToLLVMCodeGenOptLevel(codegen::OptLevel opt_level) {
  switch (opt_level) {
    case codegen::OptLevel::O0:
      return llvm::CodeGenOptLevel::None;
    case codegen::OptLevel::O1:
      return llvm::CodeGenOptLevel::Less;
    case codegen::OptLevel::O2:
    case codegen::OptLevel::Os:
    case codegen::OptLevel::Oz:
      return llvm::CodeGenOptLevel::Default;
    case codegen::OptLevel::O3:
      return llvm::CodeGenOptLevel::Aggressive;
  }
  return llvm::CodeGenOptLevel::None;
}

std::string_view OptLevelName(codegen::OptLevel opt_level) {
  switch (opt_level) {
    case codegen::OptLevel::O0:
      return "O0";
    case codegen::OptLevel::O1:
      return "O1";
    case codegen::OptLevel::O2:
      return "O2";
    case codegen::OptLevel::O3:
      return "O3";
    case codegen::OptLevel::Os:
      return "Os";
    case codegen::OptLevel::Oz:
      return "Oz";
  }
  return "O0";
}

llvm::TargetMachine* GetThreadLocalTargetMachine(const llvm::Triple& triple,
                                                 codegen::OptLevel opt_level,
                                                 std::string& err) {
  const llvm::Target* target = GetCachedTarget(triple, err);
  if (!target) {
    return nullptr;
  }

  thread_local std::unordered_map<std::string, std::unique_ptr<llvm::TargetMachine>>
      machine_cache;
  const std::string triple_key =
      triple.str() + "|" + std::string(OptLevelName(opt_level));
  if (const auto it = machine_cache.find(triple_key); it != machine_cache.end()) {
    return it->second.get();
  }

  llvm::TargetOptions options;
  std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
      triple.str(),
      "generic",
      "",
      options,
      std::nullopt,
      std::nullopt,
      ToLLVMCodeGenOptLevel(opt_level)));
  if (!machine) {
    err = "target machine creation failed";
    return nullptr;
  }

  llvm::TargetMachine* out = machine.get();
  machine_cache.emplace(triple_key, std::move(machine));
  return out;
}

}  // namespace

// ============================================================================
// LLVM Module Emission
// ============================================================================

std::optional<std::string> EmitIRForModule(
    const CodegenCache& cache,
    const ModuleCodegen& module,
    const project::Project& project,
    project::TargetProfile target_profile,
    std::string_view emit_ir) {
  const std::string module_name = ModuleNameForLog(module);
  LogCodegenProgress("emit-ir-start module=" + module_name);
  auto cached = MaterializeCachedLLVMArtifacts(cache,
                                               module,
                                               project,
                                               target_profile,
                                               true,
                                               true);
  if (!cached.has_value()) {
    LogCodegenProgress("emit-ir-error module=" + module_name +
                       " stage=lower-ir");
    return std::nullopt;
  }
  auto& entry = **cached;
  std::optional<std::string> out;
  if (emit_ir == "ll") {
    if (entry.ir_text.has_value()) {
      out = *entry.ir_text;
    }
  } else if (emit_ir == "bc") {
    if (entry.bitcode.has_value()) {
      out = *entry.bitcode;
    }
  }
  if (!out.has_value()) {
    LogCodegenProgress("emit-ir-error module=" + module_name +
                       " stage=render-ir mode=" + std::string(emit_ir));
    return std::nullopt;
  }
  entry.bundle.reset();
  entry.ir_text.reset();
  entry.bitcode.reset();

  LogCodegenProgress("emit-ir-finish module=" + module_name +
                     " mode=" + std::string(emit_ir) +
                     " bytes=" + std::to_string(out->size()));
  return out;
}

std::optional<CodegenObjectAndIR> EmitObjAndOptionalIRForModule(
    const CodegenCache& cache,
    const ModuleCodegen& module,
    const project::Project& project,
    project::TargetProfile target_profile,
    codegen::OptLevel opt_level,
    std::string_view emit_ir);

std::optional<std::string> EmitObjForModule(
    const CodegenCache& cache,
    const ModuleCodegen& module,
    const project::Project& project,
    project::TargetProfile target_profile,
    codegen::OptLevel opt_level) {
  auto emitted = EmitObjAndOptionalIRForModule(cache,
                                               module,
                                               project,
                                               target_profile,
                                               opt_level,
                                               "none");
  if (!emitted.has_value()) {
    return std::nullopt;
  }
  return std::move(emitted->object);
}

std::optional<CodegenObjectAndIR> EmitObjAndIRForModule(
    const CodegenCache& cache,
    const ModuleCodegen& module,
    const project::Project& project,
    project::TargetProfile target_profile,
    codegen::OptLevel opt_level,
    std::string_view emit_ir) {
  if (!(emit_ir == "ll" || emit_ir == "bc")) {
    return std::nullopt;
  }
  return EmitObjAndOptionalIRForModule(cache,
                                       module,
                                       project,
                                       target_profile,
                                       opt_level,
                                       emit_ir);
}

std::optional<CodegenObjectAndIR> EmitObjAndOptionalIRForModule(
    const CodegenCache& cache,
    const ModuleCodegen& module,
    const project::Project& project,
    project::TargetProfile target_profile,
    codegen::OptLevel opt_level,
    std::string_view emit_ir) {
  EnsureLLVMInit();
  const std::string module_name = ModuleNameForLog(module);
  LogCodegenProgress("emit-obj-start module=" + module_name);
  const bool debug_obj = core::IsDebugEnabled("obj");
  const bool verify_llvm_module =
      debug_obj || core::IsDebugEnabled("pipeline") ||
      core::IsDebugEnabled("codegen");
  const bool wants_ir_artifact = emit_ir == "ll" || emit_ir == "bc";
  auto cached = MaterializeCachedLLVMArtifacts(cache,
                                               module,
                                               project,
                                               target_profile,
                                               true,
                                               false);
  if (!cached.has_value()) {
    LogCodegenProgress("emit-obj-error module=" + module_name +
                       " stage=lower-ir");
    std::cerr << "[cursive] EmitObjForModule: module="
              << (module.path_key.empty() ? "<root>" : module.path_key)
              << " LLVM module emission failed before object generation\n";
    return std::nullopt;
  }
  auto& entry = **cached;
  auto& bundle = *entry.bundle;
  std::optional<std::string> ir_bytes;
  if (wants_ir_artifact) {
    const auto assembler = project::ResolveTool(project, target_profile, "llvm-as");
    if (assembler.has_value()) {
      auto rendered = RenderLLVMText(*bundle.module, *assembler, module_name);
      if (rendered.has_value()) {
        if (emit_ir == "ll") {
          ir_bytes = std::move(rendered->text);
        } else {
          ir_bytes = std::move(rendered->bitcode);
        }
      }
    }
  }
  if (opt_level != codegen::OptLevel::O0) {
    codegen::PassConfig pass_config;
    pass_config.opt_level = opt_level;
    codegen::RunOptimizationPipeline(*bundle.module, pass_config);
  }
  if (verify_llvm_module) {
    std::string verify_err;
    llvm::raw_string_ostream verify_os(verify_err);
    if (llvm::verifyModule(*bundle.module, &verify_os)) {
      LogCodegenProgress("emit-obj-error module=" + module_name +
                         " stage=verify");
      std::cerr << "[cursive] EmitObjForModule: LLVM module verification failed:\n"
                << verify_os.str() << "\n";
      if (debug_obj) {
        bundle.module->print(llvm::errs(), nullptr);
        std::cerr << "\n";
      }
      SPEC_RULE("EmitObj-Err");
      return std::nullopt;
    }
  }
  llvm::Triple triple = bundle.module->getTargetTriple();
  if (triple.getTriple().empty()) {
    triple = llvm::Triple(
        std::string(project::LLVMTripleOf(target_profile)));
    bundle.module->setTargetTriple(triple);
  }

  std::string err;
  llvm::TargetMachine* machine = GetThreadLocalTargetMachine(triple, opt_level, err);
  if (!machine) {
    LogCodegenProgress("emit-obj-error module=" + module_name +
                       " stage=target-machine");
    std::cerr << "[cursive] EmitObjForModule: target machine unavailable for "
                 "triple '"
              << triple.str() << "': " << err << "\n";
    SPEC_RULE("EmitObj-Err");
    return std::nullopt;
  }

  if (bundle.module->getDataLayout().isDefault()) {
    bundle.module->setDataLayout(machine->createDataLayout());
  }

  llvm::SmallVector<char, 0> buffer;
  llvm::raw_svector_ostream dest(buffer);
  llvm::legacy::PassManager pass;
  if (machine->addPassesToEmitFile(pass, dest, nullptr,
                                   llvm::CodeGenFileType::ObjectFile)) {
    LogCodegenProgress("emit-obj-error module=" + module_name +
                       " stage=emit-pass-setup");
    std::cerr << "[cursive] EmitObjForModule: addPassesToEmitFile failed\n";
    SPEC_RULE("EmitObj-Err");
    return std::nullopt;
  }
  LogCodegenProgress("emit-obj-run module=" + module_name +
                     " stage=pass-run");
  pass.run(*bundle.module);
  SPEC_RULE("EmitObj-Ok");
  CodegenObjectAndIR emitted;
  emitted.object.assign(buffer.begin(), buffer.end());
  emitted.ir = std::move(ir_bytes);
  entry.bundle.reset();
  entry.ir_text.reset();
  entry.bitcode.reset();
  LogCodegenProgress("emit-obj-finish module=" + module_name +
                     " bytes=" + std::to_string(emitted.object.size()));
  return emitted;
}

std::optional<std::string> CodegenObj(CodegenCache& cache,
                                      const project::ModuleInfo& module,
                                      const project::Project& project,
                                      project::TargetProfile target_profile,
                                      codegen::OptLevel opt_level) {
  const auto lowered = FindCodegenModuleEntry(cache, module.path);
  if (!lowered) {
    cache.ok.store(false);
    return std::nullopt;
  }
  auto bytes = EmitObjForModule(cache, *lowered, project, target_profile, opt_level);
  if (bytes.has_value()) {
    SPEC_RULE("CodegenObj-LLVM");
  }
  return bytes;
}

std::optional<CodegenObjectAndIR> CodegenObjAndIR(
    CodegenCache& cache,
    const project::ModuleInfo& module,
    const project::Project& project,
    project::TargetProfile target_profile,
    codegen::OptLevel opt_level,
    std::string_view emit_ir) {
  if (!(emit_ir == "ll" || emit_ir == "bc")) {
    return std::nullopt;
  }
  const auto lowered = FindCodegenModuleEntry(cache, module.path);
  if (!lowered) {
    cache.ok.store(false);
    return std::nullopt;
  }
  auto emitted =
      EmitObjAndIRForModule(cache, *lowered, project, target_profile, opt_level, emit_ir);
  if (emitted.has_value()) {
    SPEC_RULE("CodegenObj-LLVM");
    SPEC_RULE("CodegenIR-LLVM");
  }
  return emitted;
}

std::optional<std::string> CodegenIR(CodegenCache& cache,
                                     const project::ModuleInfo& module,
                                     const project::Project& project,
                                     project::TargetProfile target_profile,
                                     std::string_view emit_ir) {
  if (!(emit_ir == "ll" || emit_ir == "bc")) {
    return std::nullopt;
  }
  const auto lowered = FindCodegenModuleEntry(cache, module.path);
  if (!lowered) {
    cache.ok.store(false);
    return std::nullopt;
  }
  auto bytes = EmitIRForModule(cache, *lowered, project, target_profile, emit_ir);
  if (bytes.has_value()) {
    SPEC_RULE("CodegenIR-LLVM");
  }
  return bytes;
}

// ============================================================================
// Codegen Cache Building
// ============================================================================

std::shared_ptr<CodegenCache> BuildCodegenCache(
    const project::Project& project,
    const analysis::ScopeContext& sema_ctx,
    const analysis::NameMapBuildResult& name_maps,
    const analysis::TypecheckResult& typechecked) {
  auto cache = std::make_shared<CodegenCache>();
  cache->ctx.sigma = &sema_ctx.sigma;
  cache->ctx.target_profile = sema_ctx.target_profile;
  cache->name_maps = &name_maps;
  LogCodegenProgress("cache-build-start modules=" +
                     std::to_string(sema_ctx.sigma.mods.size()));

  const auto* expr_types = &typechecked.expr_types;
  const auto* dynamic_refine_checks = &typechecked.dynamic_refine_checks;
  const auto* generic_call_substs = &typechecked.generic_call_substs;
  cache->ctx.expr_types =
      const_cast<analysis::ExprTypeMap*>(expr_types);
  cache->ctx.dynamic_refine_checks =
      const_cast<analysis::DynamicRefineExprMap*>(dynamic_refine_checks);
  cache->ctx.generic_call_substs =
      const_cast<analysis::GenericCallSubstMap*>(generic_call_substs);
  cache->ctx.expr_type =
      [expr_types](const ast::Expr& expr) -> analysis::TypeRef {
        if (!expr_types) {
          return nullptr;
        }
        const auto it = expr_types->find(&expr);
        if (it == expr_types->end()) {
          return nullptr;
        }
        return it->second;
      };

  ConfigureResolveCallbacks(cache->ctx, name_maps);

  cache->full_init_plan = typechecked.init_plan;

  cache->modules.reserve(sema_ctx.sigma.mods.size());
  cache->index.reserve(sema_ctx.sigma.mods.size());
  cache->ast_modules.reserve(sema_ctx.sigma.mods.size());
  cache->module_order.reserve(sema_ctx.sigma.mods.size());
  cache->module_entries.reserve(sema_ctx.sigma.mods.size());
  cache->module_states.reserve(sema_ctx.sigma.mods.size());
  cache->lowered_proc_symbols.reserve(sema_ctx.sigma.mods.size() * 2);

  for (std::size_t i = 0; i < sema_ctx.sigma.mods.size(); ++i) {
    const auto& module = sema_ctx.sigma.mods[i];
    const std::string module_key = core::StringOfPath(module.path);
    const auto [_, inserted] =
        cache->ast_modules.emplace(module_key, &module);
    if (!inserted) {
      LogCodegenProgress("cache-build-error stage=duplicate-module path=" +
                         module_key);
      std::cerr << "[cursive] BuildCodegenCache: duplicate module path '"
                << module_key << "'\n";
      cache->ok.store(false);
      break;
    }
    cache->module_order.emplace(module_key, i + 1);
    cache->module_states.emplace(module_key, CodegenCache::ModuleState::Pending);
  }

  if (cache->ok.load()) {
    for (const auto& module : sema_ctx.sigma.mods) {
      const std::string module_key = core::StringOfPath(module.path);
      const std::string module_log_name = ModuleNameForPath(module_key);
      LogCodegenProgress("cache-register-start module=" + module_log_name);
      ResetLowerContextForModule(cache->ctx, module.path);
      if (!codegen::RegisterModuleSignatures(module, cache->ctx)) {
        LogCodegenProgress("cache-build-error stage=register-signatures module=" +
                           module_log_name);
        std::cerr << "[cursive] BuildCodegenCache: signature registration failed for module '"
                  << module_key << "'"
                  << " (resolve_failed="
                  << (cache->ctx.resolve_failed ? "true" : "false")
                  << ", codegen_failed="
                  << (cache->ctx.codegen_failed ? "true" : "false") << ")";
        if (!cache->ctx.resolve_failures.empty()) {
          std::cerr << " unresolved=[";
          for (std::size_t i = 0; i < cache->ctx.resolve_failures.size(); ++i) {
            if (i > 0) {
              std::cerr << ", ";
            }
            std::cerr << cache->ctx.resolve_failures[i];
          }
          std::cerr << "]";
        }
        std::cerr << "\n";
        cache->ok.store(false);
        break;
      }
      LogCodegenProgress("cache-register-finish module=" + module_log_name);
    }
    if (cache->ok.load()) {
      for (const auto& module : sema_ctx.sigma.mods) {
        const std::string module_key = core::StringOfPath(module.path);
        const std::string module_log_name = ModuleNameForPath(module_key);
        LogCodegenProgress("cache-register-statics-start module=" +
                           module_log_name);
        cache->ctx.module_path = module.path;
        cache->ctx.resolve_failed = false;
        cache->ctx.codegen_failed = false;
        cache->ctx.resolve_failures.clear();
        for (const auto& item : module.items) {
          if (const auto* static_decl =
                  std::get_if<ast::StaticDecl>(&item)) {
            codegen::RegisterStaticMetadata(*static_decl, module.path,
                                            cache->ctx);
          }
          if (cache->ctx.resolve_failed || cache->ctx.codegen_failed) {
            break;
          }
        }
        if (cache->ctx.resolve_failed || cache->ctx.codegen_failed) {
          LogCodegenProgress(
              "cache-build-error stage=register-statics module=" +
              module_log_name);
          std::cerr << "[cursive] BuildCodegenCache: static metadata registration failed for module '"
                    << module_key << "'"
                    << " (resolve_failed="
                    << (cache->ctx.resolve_failed ? "true" : "false")
                    << ", codegen_failed="
                    << (cache->ctx.codegen_failed ? "true" : "false")
                    << ")\n";
          cache->ok.store(false);
          break;
        }
        LogCodegenProgress("cache-register-statics-finish module=" +
                           module_log_name);
      }
    }
    if (cache->ok.load()) {
      cache->ctx.FreezeLookupTables();
    }
  }

  ValidateCodegenCacheCoverage(*cache, project);

  LogCodegenProgress("cache-build-finish ok=" +
                     std::string(cache->ok.load() ? "true" : "false") +
                     " lowered=" + std::to_string(cache->modules.size()) +
                     " known=" + std::to_string(cache->ast_modules.size()));

  cache->all_hosted_exports = cache->ctx.hosted_exports;
  ConfigureCodegenContextForProjectImpl(*cache, project);

  return cache;
}

bool PopulateCodegenModules(CodegenCache& cache, const project::Project& project) {
  if (!cache.ok.load()) {
    return false;
  }
  for (const auto& module : project.modules) {
    if (!EnsureCodegenModule(cache, module.path).has_value()) {
      return false;
    }
  }
  return cache.ok.load();
}

void ConfigureCodegenContextForProject(CodegenCache& cache,
                                       const project::Project& project) {
  ConfigureCodegenContextForProjectImpl(cache, project);
}

std::optional<std::size_t> EnsureCodegenModule(CodegenCache& cache,
                                               std::string_view module_path) {
  const std::string module_key(module_path);
  if (cache.ctx.sigma == nullptr) {
    LogCodegenProgress("cache-lower-error module=" + ModuleNameForPath(module_path) +
                       " stage=sigma-missing");
    cache.ok.store(false);
    return std::nullopt;
  }

  const ast::ASTModule* target_module = nullptr;
  std::size_t module_index_for_log = 0;
  bool should_lower = false;
  {
    std::unique_lock<std::mutex> lock(cache.module_mu);

    if (!cache.ok.load()) {
      return std::nullopt;
    }

    const auto target_it = cache.ast_modules.find(module_key);
    if (target_it == cache.ast_modules.end() || target_it->second == nullptr) {
      LogCodegenProgress("cache-lower-error module=" + ModuleNameForPath(module_path) +
                         " stage=module-missing");
      std::cerr << "[cursive] EnsureCodegenModule: module not found for path '"
                << module_key << "'\n";
      cache.ok.store(false);
      cache.module_states[module_key] = CodegenCache::ModuleState::Failed;
      lock.unlock();
      cache.module_cv.notify_all();
      return std::nullopt;
    }
    target_module = target_it->second;

    if (const auto pos_it = cache.module_order.find(module_key);
        pos_it != cache.module_order.end()) {
      module_index_for_log = pos_it->second;
    }

    for (;;) {
      auto state_it = cache.module_states.find(module_key);
      if (state_it == cache.module_states.end()) {
        state_it = cache.module_states
                       .emplace(module_key, CodegenCache::ModuleState::Pending)
                       .first;
      }

      switch (state_it->second) {
        case CodegenCache::ModuleState::Done: {
          const auto lowered_it = cache.index.find(module_key);
          if (lowered_it == cache.index.end()) {
            LogCodegenProgress("cache-lower-error module=" +
                               ModuleNameForPath(module_path) +
                               " stage=inconsistent-index");
            cache.ok.store(false);
            state_it->second = CodegenCache::ModuleState::Failed;
            lock.unlock();
            cache.module_cv.notify_all();
            return std::nullopt;
          }
          LogCodegenProgress("cache-lower-skip module=" +
                             ModuleNameForPath(module_path) +
                             " reason=already-lowered");
          return lowered_it->second;
        }
        case CodegenCache::ModuleState::Failed:
          return std::nullopt;
        case CodegenCache::ModuleState::Pending:
          state_it->second = CodegenCache::ModuleState::InProgress;
          should_lower = true;
          break;
        case CodegenCache::ModuleState::InProgress:
          cache.module_cv.wait(lock, [&]() {
            return !cache.ok.load() ||
                   cache.module_states[module_key] !=
                       CodegenCache::ModuleState::InProgress;
          });
          if (!cache.ok.load()) {
            return std::nullopt;
          }
          continue;
      }
      break;
    }
  }

  if (!should_lower || target_module == nullptr) {
    return std::nullopt;
  }

  if (module_index_for_log == 0) {
    module_index_for_log = 1;
  }

  codegen::LowerCtx& lower_ctx = AcquireThreadLocalLowerCtx(cache);
  auto lowered = LowerCodegenModule(lower_ctx, *target_module, module_index_for_log);
  if (!lowered.has_value()) {
    {
      std::lock_guard<std::mutex> lock(cache.module_mu);
      cache.ok.store(false);
      cache.module_states[module_key] = CodegenCache::ModuleState::Failed;
    }
    cache.module_cv.notify_all();
    return std::nullopt;
  }

  auto module_entry = std::make_shared<ModuleCodegen>(std::move(*lowered));
  std::size_t lowered_index = 0;
  {
    std::lock_guard<std::mutex> lock(cache.module_mu);

    if (!cache.ok.load()) {
      cache.module_states[module_key] = CodegenCache::ModuleState::Failed;
      cache.module_cv.notify_all();
      return std::nullopt;
    }

    bool emit_context_changed = false;
    for (const auto& [symbol, type] : module_entry->values.static_types) {
      cache.ctx.values.static_types[symbol] = type;
      emit_context_changed = true;
    }
    for (const auto& [symbol, owner_module] : lower_ctx.static_modules) {
      cache.ctx.static_modules[symbol] = owner_module;
      emit_context_changed = true;
    }
    if (emit_context_changed) {
      cache.emit_context_epoch += 1;
    }

    DeduplicateProcDecls(module_entry->decls,
                        module_entry->proc_linkages,
                        cache.lowered_proc_symbols);
    if (cache.ctx.hosted_library) {
      CollectHostedStateTemplates(cache, *module_entry);
    }

    lowered_index = cache.modules.size();
    cache.modules.push_back(module_entry);
    cache.index[module_key] = lowered_index;
    cache.module_entries[module_key] = module_entry;
    cache.module_states[module_key] = CodegenCache::ModuleState::Done;
  }
  cache.module_cv.notify_all();
  return lowered_index;
}

std::shared_ptr<const ModuleCodegen> FindCodegenModuleEntry(
    const CodegenCache& cache, std::string_view module_path) {
  std::lock_guard<std::mutex> lock(cache.module_mu);
  const std::string key(module_path);
  if (const auto it = cache.module_entries.find(key);
      it != cache.module_entries.end()) {
    return it->second;
  }
  if (const auto idx_it = cache.index.find(key);
      idx_it != cache.index.end() && idx_it->second < cache.modules.size()) {
    return cache.modules[idx_it->second];
  }
  return nullptr;
}

// ============================================================================
// Diagnostic Helpers
// ============================================================================

void AppendDiags(DiagnosticStream& out, const DiagnosticStream& add) {
  for (const auto& diag : add) {
    core::Emit(out, diag);
  }
}

bool HasDiagCode(const DiagnosticStream& diags, std::string_view code) {
  for (const auto& diag : diags) {
    if (diag.code == code) {
      return true;
    }
  }
  return false;
}

}  // namespace cursive::driver
