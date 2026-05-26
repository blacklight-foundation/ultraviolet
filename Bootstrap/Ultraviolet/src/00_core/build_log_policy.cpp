#include "00_core/build_log_policy.h"

#include "00_core/process_config.h"

namespace ultraviolet::core {

namespace {

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() &&
         text.substr(0, prefix.size()) == prefix;
}

bool ShouldEmitSummaryPhaseMessage(std::string_view message) {
  if (StartsWith(message, "event=start") ||
      StartsWith(message, "event=finish") ||
      StartsWith(message, "phase=project-load") ||
      StartsWith(message, "phase=parse-modules") ||
      StartsWith(message, "phase=incremental-fastpath") ||
      message == "phase=sema" ||
      message == "phase=codegen") {
    return true;
  }
  if (StartsWith(message, "phase=sema step=name-map-collect-finish") ||
      StartsWith(message, "phase=sema step=resolve-finish") ||
      StartsWith(message, "phase=sema step=typecheck-finish")) {
    return true;
  }
  if (StartsWith(message, "phase=codegen cache=build-start") ||
      StartsWith(message, "phase=codegen cache=build-finish") ||
      StartsWith(message, "phase=codegen incremental=fingerprint-start") ||
      StartsWith(message, "phase=codegen incremental=fingerprint-finish") ||
      StartsWith(message, "phase=codegen incremental=disabled")) {
    return true;
  }
  return false;
}

bool ShouldEmitSummaryOutputMessage(std::string_view message) {
  if (StartsWith(message, "start assembly=") ||
      StartsWith(message, "finish kind=") ||
      StartsWith(message, "dirs-ok") ||
      StartsWith(message, "dirs-error") ||
      StartsWith(message, "incremental mode=") ||
      StartsWith(message, "incremental-disabled") ||
      StartsWith(message, "incremental-warning") ||
      StartsWith(message, "codegen-context-start") ||
      StartsWith(message, "codegen-context-finish") ||
      StartsWith(message, "codegen-context-error") ||
      StartsWith(message, "obj-phase-start") ||
      StartsWith(message, "obj-phase-finish") ||
      StartsWith(message, "obj-codegen-batch") ||
      StartsWith(message, "obj-codegen-start") ||
      StartsWith(message, "obj-codegen-finish") ||
      StartsWith(message, "obj-progress") ||
      StartsWith(message, "obj-none") ||
      StartsWith(message, "obj-collision") ||
      StartsWith(message, "obj-error") ||
      StartsWith(message, "obj-write-error") ||
      StartsWith(message, "ir-phase-start") ||
      StartsWith(message, "ir-phase-finish") ||
      StartsWith(message, "ir-codegen-start") ||
      StartsWith(message, "ir-codegen-finish") ||
      StartsWith(message, "ir-progress") ||
      StartsWith(message, "ir-skip") ||
      StartsWith(message, "ir-collision") ||
      StartsWith(message, "ir-error") ||
      StartsWith(message, "ir-write-error") ||
      StartsWith(message, "link-start") ||
      StartsWith(message, "link-ok") ||
      StartsWith(message, "link-reuse") ||
      StartsWith(message, "link-error")) {
    return true;
  }
  return false;
}

bool ShouldEmitSummaryCodegenMessage(std::string_view message) {
  return StartsWith(message, "cache-build-start") ||
         StartsWith(message, "cache-build-finish") ||
         StartsWith(message, "cache-build-error") ||
         StartsWith(message, "cache-register-start") ||
         StartsWith(message, "cache-register-finish") ||
         StartsWith(message, "cache-lower-start") ||
         StartsWith(message, "cache-lower-mid") ||
         StartsWith(message, "cache-lower-finish") ||
         StartsWith(message, "cache-lower-skip") ||
         StartsWith(message, "cache-lower-error") ||
         StartsWith(message, "emit-ir-error") ||
         StartsWith(message, "emit-obj-error");
}

}  // namespace

BuildLogMode ResolveBuildLogMode(const BuildLogResolveOptions& options) {
  if (!options.channel_enabled) {
    return BuildLogMode::None;
  }
  if (options.debug_enabled || GetVerbosity() == Verbosity::Verbose) {
    return BuildLogMode::Detailed;
  }
  if (options.cli_progress.has_value()) {
    return *options.cli_progress ? BuildLogMode::Summary : BuildLogMode::None;
  }
  if (options.manifest_progress.has_value()) {
    return *options.manifest_progress ? BuildLogMode::Summary
                                      : BuildLogMode::None;
  }
  return options.default_enabled ? BuildLogMode::Summary : BuildLogMode::None;
}

bool ShouldEmitSummaryBuildLog(BuildLogChannel channel,
                               std::string_view message) {
  switch (channel) {
    case BuildLogChannel::Phase:
      return ShouldEmitSummaryPhaseMessage(message);
    case BuildLogChannel::Output:
      return ShouldEmitSummaryOutputMessage(message);
    case BuildLogChannel::Codegen:
      return ShouldEmitSummaryCodegenMessage(message);
  }
  return false;
}

}  // namespace ultraviolet::core
