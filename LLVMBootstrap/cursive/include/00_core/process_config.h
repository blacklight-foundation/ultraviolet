#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/behavior_model.h"

namespace cursive::core {

void SetBuildProgressOverride(std::optional<bool> enabled);
std::optional<bool> BuildProgressOverride();

void SetIncrementalOverride(std::optional<bool> enabled);
std::optional<bool> IncrementalOverride();

void SetRuntimeLibOverride(std::optional<std::string> path);
std::optional<std::string> RuntimeLibOverride();

void SetLinkDebugOverride(std::optional<bool> enabled);
std::optional<bool> LinkDebugOverride();

void SetOutDirOverride(std::optional<std::string> path);
std::optional<std::string> OutDirOverride();

void SetMaxErrorsOverride(std::optional<ErrorRecoveryPolicy> policy);
std::optional<ErrorRecoveryPolicy> MaxErrorsOverride();

// Debug subsystem support.
// Call SetDebugSubsystems with a list like {"lex", "parse"} or {"all"}.
void SetDebugSubsystems(const std::vector<std::string>& subsystems);

// Check if a debug subsystem is enabled. Returns true if the subsystem was
// specified in SetDebugSubsystems, or if "all" was specified.
bool IsDebugEnabled(std::string_view subsystem);

// Returns true if any debug subsystems have been configured.
bool HasDebugSubsystems();

// Manifest-level build/toolchain config setters (from [build] and [toolchain]).
void SetManifestBuildProgress(std::optional<bool> enabled);
std::optional<bool> ManifestBuildProgress();

void SetManifestIncremental(std::optional<bool> enabled);
std::optional<bool> ManifestIncremental();

void SetManifestRuntimeLib(std::optional<std::string> path);
std::optional<std::string> ManifestRuntimeLib();

// Verbosity level for build output.
enum class Verbosity { Normal, Verbose };

void SetVerbosity(Verbosity level);
Verbosity GetVerbosity();

}  // namespace cursive::core
