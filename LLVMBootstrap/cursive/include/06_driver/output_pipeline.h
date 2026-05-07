#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/diagnostics.h"
#include "01_project/link.h"
#include "01_project/module_discovery.h"
#include "01_project/project.h"
#include "02_source/ast/ast.h"
#include "02_source/parser/parse_modules.h"
#include "06_driver/incremental_state.h"

namespace cursive::driver {

struct OutputArtifacts {
  std::vector<std::filesystem::path> objs;
  std::vector<std::filesystem::path> irs;
  std::optional<std::filesystem::path> primary_artifact;
  std::optional<std::filesystem::path> import_lib;
  std::optional<std::filesystem::path> map_file;
};

struct OutputPipelineResult {
  std::optional<OutputArtifacts> artifacts;
  core::DiagnosticStream diags;
};

struct CodegenObjectAndIR {
  std::string object;
  std::optional<std::string> ir;
};

struct SharedLibraryExports {
  std::vector<std::string> export_symbols;
  std::vector<std::string> data_export_symbols;
};

struct OutputPipelineDeps {
  std::function<bool(const std::filesystem::path& path)> ensure_dir;
  std::function<std::optional<std::string>(const project::ModuleInfo& module,
                                           const project::Project& project)>
      codegen_obj;
  std::function<std::optional<CodegenObjectAndIR>(
      const project::ModuleInfo& module,
      const project::Project& project,
      std::string_view emit_ir)>
      codegen_obj_and_ir;
  std::function<std::optional<std::string>(const project::ModuleInfo& module,
                                           const project::Project& project,
                                           std::string_view emit_ir)>
      codegen_ir;
  std::function<bool(const std::filesystem::path& path, std::string_view bytes)>
      write_file;
  std::function<std::optional<std::filesystem::path>(
      const project::Project& project,
      project::TargetProfile target_profile,
      std::string_view tool)>
      resolve_tool;
  std::function<std::optional<std::string>(const std::filesystem::path& tool,
                                           std::string_view ir_text)>
      assemble_ir;
  std::function<std::optional<std::filesystem::path>(
      const project::Project& project,
      project::TargetProfile target_profile)>
      resolve_runtime_lib;
  std::function<project::LinkInvocationResult(
      const std::filesystem::path& tool,
      const std::vector<std::filesystem::path>& inputs,
      const std::filesystem::path& output,
      const std::optional<std::filesystem::path>& import_lib,
      const project::LinkPlan& plan)>
      invoke_linker;
  std::function<std::optional<std::vector<std::string>>(
      const std::filesystem::path& tool,
      const std::vector<std::filesystem::path>& inputs,
      const std::filesystem::path& output)>
      linker_syms;
  std::function<std::optional<std::vector<std::filesystem::path>>(
      const std::filesystem::path& archive)>
      archive_members;
  std::function<bool(const std::filesystem::path& tool,
                     const std::vector<std::filesystem::path>& inputs,
                     const std::filesystem::path& output)>
      invoke_archiver;
  std::function<std::optional<
      std::reference_wrapper<const std::vector<ast::ASTModule>>>(
      const project::Project& project)>
      resolve_project_ast_modules;
  std::function<std::optional<SharedLibraryExports>(
      const project::Project& project)>
      resolve_shared_library_exports;
  std::function<bool(const project::Project& project,
                     const SharedLibraryExports& exports)>
      prepare_codegen_context;
  std::function<std::optional<IncrementalModuleInfo>(
      const project::ModuleInfo& module,
      const project::Project& project)>
      incremental_module;
  std::function<std::optional<std::string>(const project::Project& project)>
      incremental_build_key;
  bool codegen_obj_thread_safe = false;
};

OutputPipelineResult OutputPipeline(const project::Project& project,
                                    project::TargetProfile target_profile,
                                    const OutputPipelineDeps& deps);

}  // namespace cursive::driver
