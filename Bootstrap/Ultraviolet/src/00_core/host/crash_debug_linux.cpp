#include "crash_debug_internal.h"

#ifndef _WIN32

namespace ultraviolet::core::crash_debug_detail {

bool CrashCaptureSupportedBackend() {
  return false;
}

void InstallCrashHandlersBackend() {}

void MaybeTriggerCrashFixtureFromEnvBackend() {}

DebugRunResult DebugRunProcessBackend(const DebugRunOptions& options) {
  return RunProcessWithoutDebugger(options);
}

}  // namespace ultraviolet::core::crash_debug_detail

#endif
