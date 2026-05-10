// =============================================================================
// options.cpp - Compiler configuration options
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md §2 (lines 746-760) - Phase 0: Build/Project Model
//   CursiveSpecification.md §2.1 (lines 854-862) - (WF-Assembly-EmitIR)
//   CursiveSpecification.md §6.12 (lines 1412-1414) - LLVM Target Constants
//
// =============================================================================

#include "06_driver/options.h"

#include "00_core/process_config.h"

namespace cursive::driver {

bool InternalFlagsEnabled() {
  return core::HasDebugSubsystems();
}

TargetOptions DefaultTargetOptions() {
  return TargetOptions{};
}

EmitIRMode ParseEmitIRMode(std::string_view mode) {
  if (mode.empty() || mode == "none") {
    return EmitIRMode::None;
  }
  if (mode == "ll") {
    return EmitIRMode::LL;
  }
  if (mode == "bc") {
    return EmitIRMode::BC;
  }
  return EmitIRMode::None;
}

}  // namespace cursive::driver
