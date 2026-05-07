#pragma once

#include "00_core/compiler_support.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cursive::core {

enum class HostProcessOutputMode {
  Inherit,
  CaptureMerged,
  CaptureSeparate,
};

struct HostProcessSpec {
  std::filesystem::path program;
  std::vector<std::string> arguments;
  std::optional<std::filesystem::path> working_directory;
  HostProcessOutputMode output_mode = HostProcessOutputMode::Inherit;
  bool hide_window = true;
};

struct HostProcessResult {
  bool launched = false;
  int exit_code = -1;
  std::string output;
  std::string stdout_text;
  std::string stderr_text;
  std::string error_message;
};

struct HostTerminalInfo {
  bool is_tty = false;
  bool ansi_enabled = false;
  int width = 0;
};

struct HostServices {
  std::function<std::optional<std::string>(std::string_view name)> getenv_utf8;
  std::function<char()> path_list_separator;
  std::function<std::vector<std::string>(std::string_view tool)>
      tool_name_candidates;
  std::function<std::filesystem::path()> current_executable_path;
  std::function<unsigned long()> current_process_id;
  std::function<std::uint64_t()> current_thread_id;
  std::function<HostProcessResult(const HostProcessSpec& spec)> run_process;
  std::function<HostTerminalInfo(FILE* stream)> terminal_info;
  std::function<std::filesystem::path(
      CompilerSupportLayoutKind layout,
      const std::filesystem::path& support_root)>
      compiler_sidecar_bin_dir;
  std::function<bool(const std::filesystem::path& dir, std::string* error_message)>
      configure_compiler_sidecar_bin_dir;
};

const HostServices& DefaultHostServices();

class ScopedHostServicesOverride {
 public:
  explicit ScopedHostServicesOverride(const HostServices* services);
  ScopedHostServicesOverride(const ScopedHostServicesOverride&) = delete;
  ScopedHostServicesOverride& operator=(const ScopedHostServicesOverride&) =
      delete;
  ~ScopedHostServicesOverride();

 private:
  const HostServices* previous_ = nullptr;
};

std::optional<std::string> HostGetEnvUtf8(std::string_view name);
char HostPathListSeparator();
std::vector<std::string> HostToolNameCandidates(std::string_view tool);
std::filesystem::path CurrentExecutablePath();
unsigned long CurrentHostProcessId();
std::uint64_t CurrentHostThreadId();
HostProcessResult RunHostProcess(const HostProcessSpec& spec);
HostTerminalInfo QueryHostTerminal(FILE* stream);
std::filesystem::path ResolveHostCompilerSidecarBinDir(
    CompilerSupportLayoutKind layout,
    const std::filesystem::path& support_root);
bool ConfigureHostCompilerSidecarBinDir(
    const std::filesystem::path& dir,
    std::string* error_message = nullptr);

}  // namespace cursive::core
