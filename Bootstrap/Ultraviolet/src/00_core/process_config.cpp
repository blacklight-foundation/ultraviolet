#include "00_core/process_config.h"

#include <algorithm>
#include <mutex>
#include <unordered_set>
#include <utility>

namespace ultraviolet::core {

namespace {

struct ProcessConfigState {
  std::optional<bool> build_progress;
  std::optional<bool> incremental;
  std::optional<std::string> runtime_lib;
  std::optional<bool> link_debug;
  std::optional<std::string> out_dir;
  std::optional<ErrorRecoveryPolicy> max_errors;
  std::unordered_set<std::string> debug_subsystems;
  bool has_debug = false;

  // Manifest-level config
  std::optional<bool> manifest_build_progress;
  std::optional<bool> manifest_incremental;
  std::optional<std::string> manifest_runtime_lib;

  // Verbosity
  Verbosity verbosity = Verbosity::Normal;
};

ProcessConfigState& State() {
  static ProcessConfigState state;
  return state;
}

std::mutex& StateMutex() {
  static std::mutex mu;
  return mu;
}

}  // namespace

void SetBuildProgressOverride(std::optional<bool> enabled) {
  std::lock_guard<std::mutex> lock(StateMutex());
  State().build_progress = enabled;
}

std::optional<bool> BuildProgressOverride() {
  std::lock_guard<std::mutex> lock(StateMutex());
  return State().build_progress;
}

void SetIncrementalOverride(std::optional<bool> enabled) {
  std::lock_guard<std::mutex> lock(StateMutex());
  State().incremental = enabled;
}

std::optional<bool> IncrementalOverride() {
  std::lock_guard<std::mutex> lock(StateMutex());
  return State().incremental;
}

void SetRuntimeLibOverride(std::optional<std::string> path) {
  std::lock_guard<std::mutex> lock(StateMutex());
  State().runtime_lib = std::move(path);
}

std::optional<std::string> RuntimeLibOverride() {
  std::lock_guard<std::mutex> lock(StateMutex());
  return State().runtime_lib;
}

void SetLinkDebugOverride(std::optional<bool> enabled) {
  std::lock_guard<std::mutex> lock(StateMutex());
  State().link_debug = enabled;
}

std::optional<bool> LinkDebugOverride() {
  std::lock_guard<std::mutex> lock(StateMutex());
  return State().link_debug;
}

void SetOutDirOverride(std::optional<std::string> path) {
  std::lock_guard<std::mutex> lock(StateMutex());
  State().out_dir = std::move(path);
}

std::optional<std::string> OutDirOverride() {
  std::lock_guard<std::mutex> lock(StateMutex());
  return State().out_dir;
}

void SetMaxErrorsOverride(std::optional<ErrorRecoveryPolicy> policy) {
  std::lock_guard<std::mutex> lock(StateMutex());
  State().max_errors = std::move(policy);
}

std::optional<ErrorRecoveryPolicy> MaxErrorsOverride() {
  std::lock_guard<std::mutex> lock(StateMutex());
  return State().max_errors;
}

void SetDebugSubsystems(const std::vector<std::string>& subsystems) {
  std::lock_guard<std::mutex> lock(StateMutex());
  auto& state = State();
  state.debug_subsystems.clear();
  state.has_debug = !subsystems.empty();
  for (const auto& s : subsystems) {
    state.debug_subsystems.insert(s);
  }
}

bool IsDebugEnabled(std::string_view subsystem) {
  std::lock_guard<std::mutex> lock(StateMutex());
  const auto& state = State();
  if (!state.has_debug) {
    return false;
  }
  if (state.debug_subsystems.count("all") > 0) {
    return true;
  }
  return state.debug_subsystems.count(std::string(subsystem)) > 0;
}

bool HasDebugSubsystems() {
  std::lock_guard<std::mutex> lock(StateMutex());
  return State().has_debug;
}

void SetManifestBuildProgress(std::optional<bool> enabled) {
  std::lock_guard<std::mutex> lock(StateMutex());
  State().manifest_build_progress = enabled;
}

std::optional<bool> ManifestBuildProgress() {
  std::lock_guard<std::mutex> lock(StateMutex());
  return State().manifest_build_progress;
}

void SetManifestIncremental(std::optional<bool> enabled) {
  std::lock_guard<std::mutex> lock(StateMutex());
  State().manifest_incremental = enabled;
}

std::optional<bool> ManifestIncremental() {
  std::lock_guard<std::mutex> lock(StateMutex());
  return State().manifest_incremental;
}

void SetManifestRuntimeLib(std::optional<std::string> path) {
  std::lock_guard<std::mutex> lock(StateMutex());
  State().manifest_runtime_lib = std::move(path);
}

std::optional<std::string> ManifestRuntimeLib() {
  std::lock_guard<std::mutex> lock(StateMutex());
  return State().manifest_runtime_lib;
}

void SetVerbosity(Verbosity level) {
  std::lock_guard<std::mutex> lock(StateMutex());
  State().verbosity = level;
}

Verbosity GetVerbosity() {
  std::lock_guard<std::mutex> lock(StateMutex());
  return State().verbosity;
}

}  // namespace ultraviolet::core
