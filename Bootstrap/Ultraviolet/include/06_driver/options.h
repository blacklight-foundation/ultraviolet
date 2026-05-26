#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ultraviolet::driver {

// EmitIRMode - IR emission modes
enum class EmitIRMode {
  None,  // No IR emission (default)
  LL,    // LLVM IR text format (.ll)
  BC     // LLVM bitcode format (.bc)
};

// CompilerOptions - Resolved compiler configuration
struct CompilerOptions {
  // From project/manifest
  std::string assembly_name;
  std::string assembly_kind;
  std::filesystem::path source_root;
  std::filesystem::path output_root;
  EmitIRMode emit_ir = EmitIRMode::None;

  // From CLI
  bool phase1_only = false;
  bool no_output = false;
  bool dump_ast = false;
  bool dump_project = false;
  bool diag_json = false;

  // Conformance tracing
  std::optional<std::string> conformance_path;
};

// TargetOptions - LLVM target configuration
struct TargetOptions {
  std::string triple = "x86_64-pc-windows-msvc";
  std::string data_layout =
      "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128";
  std::string cpu = "generic";
  std::string features = "";
};

// Check if internal flags are enabled
bool InternalFlagsEnabled();

// Get default target options
TargetOptions DefaultTargetOptions();

// Parse emit IR mode from string
EmitIRMode ParseEmitIRMode(std::string_view mode);

}  // namespace ultraviolet::driver
