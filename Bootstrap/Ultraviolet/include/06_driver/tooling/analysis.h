#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "01_project/project.h"
#include "06_driver/tooling/document_store.h"
#include "06_driver/tooling/snapshot.h"

namespace ultraviolet::driver::tooling {

struct ToolingAnalysisOptions {
  std::filesystem::path project_root;
  std::optional<std::string> assembly_target;
  std::optional<project::TargetProfile> target_profile;
  // Last-resort target profile used only when neither `target_profile` nor the
  // manifest's own `[toolchain] target_profile` resolves one. Tools such as the
  // language server set this so analysis can still proceed (and build a symbol
  // index) for projects that do not pin a target; codegen-oriented callers leave
  // it empty so a missing profile remains an error.
  std::optional<project::TargetProfile> fallback_target_profile;
  bool run_comptime = true;
  bool semantic = true;
  std::size_t max_errors = 100;
};

AnalysisSnapshot AnalyzeWorkspace(
    const ToolingAnalysisOptions& options,
    std::span<const DocumentOverlay> overlays);

}  // namespace ultraviolet::driver::tooling
