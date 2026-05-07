#include "00_core/host/services.h"

#include <mutex>

#include "host_internal.h"

namespace cursive::core {

namespace {

std::mutex& OverrideMutex() {
  static std::mutex mu;
  return mu;
}

const HostServices*& OverridePtr() {
  static const HostServices* override_services = nullptr;
  return override_services;
}

const HostServices* SetOverrideLocked(const HostServices* services) {
  const HostServices* const previous = OverridePtr();
  OverridePtr() = services;
  return previous;
}

}  // namespace

const HostServices& DefaultHostServices() {
  const std::lock_guard<std::mutex> lock(OverrideMutex());
  if (OverridePtr() != nullptr) {
    return *OverridePtr();
  }
  return NativeHostServices();
}

ScopedHostServicesOverride::ScopedHostServicesOverride(
    const HostServices* services) {
  const std::lock_guard<std::mutex> lock(OverrideMutex());
  previous_ = SetOverrideLocked(services);
}

ScopedHostServicesOverride::~ScopedHostServicesOverride() {
  const std::lock_guard<std::mutex> lock(OverrideMutex());
  SetOverrideLocked(previous_);
}

std::optional<std::string> HostGetEnvUtf8(std::string_view name) {
  return DefaultHostServices().getenv_utf8(name);
}

char HostPathListSeparator() {
  return DefaultHostServices().path_list_separator();
}

std::vector<std::string> HostToolNameCandidates(std::string_view tool) {
  return DefaultHostServices().tool_name_candidates(tool);
}

std::filesystem::path CurrentExecutablePath() {
  return DefaultHostServices().current_executable_path();
}

unsigned long CurrentHostProcessId() {
  return DefaultHostServices().current_process_id();
}

std::uint64_t CurrentHostThreadId() {
  return DefaultHostServices().current_thread_id();
}

HostProcessResult RunHostProcess(const HostProcessSpec& spec) {
  return DefaultHostServices().run_process(spec);
}

HostTerminalInfo QueryHostTerminal(FILE* stream) {
  return DefaultHostServices().terminal_info(stream);
}

std::filesystem::path ResolveHostCompilerSidecarBinDir(
    CompilerSupportLayoutKind layout,
    const std::filesystem::path& support_root) {
  return DefaultHostServices().compiler_sidecar_bin_dir(layout, support_root);
}

bool ConfigureHostCompilerSidecarBinDir(
    const std::filesystem::path& dir,
    std::string* error_message) {
  return DefaultHostServices().configure_compiler_sidecar_bin_dir(dir,
                                                                  error_message);
}

}  // namespace cursive::core
