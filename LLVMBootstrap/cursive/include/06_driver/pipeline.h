#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "00_core/diagnostics.h"
#include "00_core/source_text.h"
#include "01_project/project.h"
#include "06_driver/codegen_cache.h"
#include "06_driver/output_pipeline.h"
#include "04_analysis/resolve/resolve_items.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/typecheck.h"
#include "05_codegen/llvm/llvm_passes.h"

namespace cursive::driver {

// Pipeline Result
struct PipelineResult {
  bool phase1_ok = false;
  bool resolve_ok = false;
  bool typecheck_ok = false;
  bool phase4_ok = false;
  core::DiagnosticStream diags;
};

// Inspect source before parsing and return any front-end diagnostics.
core::DiagnosticStream InspectSource(const core::SourceFile& source);

// LLVM initialization
void EnsureLLVMInit();

// File system helpers
bool EnsureDir(const std::filesystem::path& path);
bool WriteFile(const std::filesystem::path& path, std::string_view bytes);

// LLVM module emission
std::optional<std::string> EmitIRForModule(
    const CodegenCache& cache,
    const ModuleCodegen& module,
    const project::Project& project,
    project::TargetProfile target_profile,
    std::string_view emit_ir);

std::optional<std::string> EmitObjForModule(
    const CodegenCache& cache,
    const ModuleCodegen& module,
    const project::Project& project,
    project::TargetProfile target_profile,
    codegen::OptLevel opt_level);

std::optional<std::string> CodegenObj(
    CodegenCache& cache,
    const project::ModuleInfo& module,
    const project::Project& project,
    project::TargetProfile target_profile,
    codegen::OptLevel opt_level);

std::optional<CodegenObjectAndIR> CodegenObjAndIR(
    CodegenCache& cache,
    const project::ModuleInfo& module,
    const project::Project& project,
    project::TargetProfile target_profile,
    codegen::OptLevel opt_level,
    std::string_view emit_ir);

std::optional<std::string> CodegenIR(
    CodegenCache& cache,
    const project::ModuleInfo& module,
    const project::Project& project,
    project::TargetProfile target_profile,
    std::string_view emit_ir);

// Codegen cache building
std::shared_ptr<CodegenCache> BuildCodegenCache(
    const project::Project& project,
    const analysis::ScopeContext& sema_ctx,
    const analysis::NameMapBuildResult& name_maps,
    const analysis::TypecheckResult& typechecked);

std::optional<std::size_t> EnsureCodegenModule(CodegenCache& cache,
                                               std::string_view module_path);
std::shared_ptr<const ModuleCodegen> FindCodegenModuleEntry(
    const CodegenCache& cache, std::string_view module_path);
bool PopulateCodegenModules(CodegenCache& cache, const project::Project& project);
void ConfigureCodegenContextForProject(CodegenCache& cache,
                                       const project::Project& project);

// Diagnostic helpers
void AppendDiags(core::DiagnosticStream& out, const core::DiagnosticStream& add);
bool HasDiagCode(const core::DiagnosticStream& diags, std::string_view code);

}  // namespace cursive::driver
