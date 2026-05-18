#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/diagnostics.h"
#include "01_project/target_profile.h"

namespace ultraviolet::project {

struct Project;

enum class LinkStatus {
  Ok,
  NotFound,
  RuntimeMissing,
  RuntimeIncompatible,
  Fail,
};

enum class LinkOutputKind {
  Executable,
  SharedLibrary,
};

enum class SharedLibraryLifecycleMode {
  None,
  WindowsEntry,
  PosixCtorDtor,
};

struct LinkPlan {
  TargetProfile target_profile;
  LinkOutputKind output_kind = LinkOutputKind::Executable;
  SharedLibraryLifecycleMode shared_library_lifecycle_mode =
      SharedLibraryLifecycleMode::None;
  std::optional<std::string> entry_symbol;
  std::vector<std::string> export_symbols;
  std::vector<std::string> data_export_symbols;
};

struct LinkInvocationResult {
  bool launched = false;
  bool ok = false;
  bool crashed = false;
  int exit_code = -1;
  std::filesystem::path tool_path;
  std::filesystem::path working_directory;
  std::vector<std::string> argv;
  std::string launch_error;
  std::string stdout_text;
  std::string stderr_text;
  std::filesystem::path crash_report_json_path;
  std::filesystem::path transcript_path;
  std::string crash_kind;
  std::string output;
};

struct LinkDeps {
  std::function<std::optional<std::filesystem::path>(const Project& project,
                                                     TargetProfile target_profile,
                                                     std::string_view tool)>
      resolve_tool;
  std::function<std::optional<std::filesystem::path>(const Project& project,
                                                     TargetProfile target_profile)>
      resolve_runtime_lib;
  std::function<std::optional<std::vector<std::string>>(
      const std::filesystem::path& tool,
      const std::vector<std::filesystem::path>& inputs,
      const std::filesystem::path& output)>
      linker_syms;
  std::function<std::optional<std::vector<std::filesystem::path>>(
      const std::filesystem::path& archive)>
      archive_members;
  std::function<LinkInvocationResult(
      const std::filesystem::path& tool,
      const std::vector<std::filesystem::path>& inputs,
      const std::filesystem::path& output,
      const std::optional<std::filesystem::path>& import_lib,
      const LinkPlan& plan)>
      invoke_linker;
  std::function<bool(const std::filesystem::path& tool,
                     const std::vector<std::filesystem::path>& inputs,
                     const std::filesystem::path& output)>
      invoke_archiver;
};

struct LinkResult {
  LinkStatus status = LinkStatus::Fail;
  core::DiagnosticStream diags;
};

std::filesystem::path RuntimeLibPath(const Project& project,
                                     TargetProfile target_profile);
std::vector<std::string> RuntimeRequiredSyms();
bool IsHiddenSharedLibraryExportSymbol(std::string_view symbol);
std::vector<std::filesystem::path> MaterializeLinkInputsForTool(
    const Project& project,
    TargetProfile target_profile,
    const std::vector<std::filesystem::path>& inputs);
std::vector<std::filesystem::path> LinkInputs(
    const std::vector<std::filesystem::path>& objs,
    const std::vector<std::filesystem::path>& library_artifact_inputs,
    const std::filesystem::path& runtime_lib);

std::optional<std::filesystem::path> ResolveRuntimeLib(
    const Project& project,
    TargetProfile target_profile);
std::optional<std::vector<std::string>> LinkerSyms(
    const std::filesystem::path& tool,
    const std::vector<std::filesystem::path>& inputs,
    const std::filesystem::path& output);
std::optional<std::vector<std::filesystem::path>> ArchiveMembers(
    const std::filesystem::path& archive);
LinkInvocationResult InvokeLinker(
    const std::filesystem::path& tool,
    const std::vector<std::filesystem::path>& inputs,
    const std::filesystem::path& output,
    const std::optional<std::filesystem::path>& import_lib,
    const LinkPlan& plan);
bool InvokeArchiver(const std::filesystem::path& tool,
                    const std::vector<std::filesystem::path>& inputs,
                    const std::filesystem::path& output);

LinkResult Link(const std::vector<std::filesystem::path>& objs,
                const std::vector<std::filesystem::path>& extra_inputs,
                const Project& project,
                const LinkPlan& plan,
                const LinkDeps& deps);

LinkResult Archive(const std::vector<std::filesystem::path>& objs,
                   const Project& project,
                   TargetProfile target_profile,
                   const LinkDeps& deps);

}  // namespace ultraviolet::project
