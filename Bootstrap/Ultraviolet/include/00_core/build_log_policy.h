#pragma once

#include <optional>
#include <string_view>

namespace ultraviolet::core {

enum class BuildLogChannel {
  Phase,
  Output,
  Codegen,
};

enum class BuildLogMode {
  None,
  Summary,
  Detailed,
};

struct BuildLogResolveOptions {
  bool channel_enabled = true;
  bool debug_enabled = false;
  std::optional<bool> cli_progress;
  std::optional<bool> manifest_progress;
  bool default_enabled = true;
};

BuildLogMode ResolveBuildLogMode(const BuildLogResolveOptions& options);

bool ShouldEmitSummaryBuildLog(BuildLogChannel channel, std::string_view message);

}  // namespace ultraviolet::core
