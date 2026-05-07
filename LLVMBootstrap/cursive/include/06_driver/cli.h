#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "00_core/behavior_model.h"
#include "00_core/diagnostics.h"
#include "00_core/diagnostic_render.h"
#include "01_project/target_profile.h"

namespace cursive::driver {

enum class ColorMode {
  Auto,
  Always,
  Never,
};

enum class Verbosity {
  Normal,
  Verbose,
};

// CLI Options Structure
struct CliOptions {
  bool diag_json = false;            // --diag-json
  bool show_help = false;            // --help, -h
  bool show_debug_help = false;      // --debug=help
  bool show_version = false;         // --version, -V
  bool check_only = false;           // --check (type-check only, no codegen)
  bool no_crash_report = false;      // --no-crash-report
  bool phase1_only = false;          // --phase1-only (internal)
  bool no_output = false;            // --no-output (internal)
  bool dump_project = false;         // --dump
  bool dump_ast = false;             // --dump-ast
  std::optional<std::string> conformance_path;  // --conformance
  std::optional<std::string> assembly_target;   // --assembly
  bool log_enabled = false;                          // --log, --log-file, or --trace
  bool log_to_console = false;                       // --log or --trace
  bool log_to_file = false;                          // --log-file was used
  std::optional<std::string> log_file_path;          // --log-file <path> (exact output path)
  bool trace = false;                                // --trace (disable log-only filter)
  std::optional<std::uint8_t> trace_filter_mask;     // --trace-filter <classes>
  std::optional<std::uint8_t> trace_min_level;       // --trace-level <level>
  std::optional<bool> build_progress;                         // --build-progress
  std::optional<bool> incremental;                            // --incremental
  std::optional<std::string> runtime_lib_path;               // --runtime-lib
  std::optional<bool> link_debug;                            // --link-debug
  std::optional<core::ErrorRecoveryPolicy> max_errors_override;  // --max-errors <N|inf>
  std::optional<std::string> out_dir;                        // --out-dir
  std::optional<project::TargetProfile> target_profile_override; // --target-profile
  std::vector<std::string> debug_subsystems;                 // --debug
  ColorMode color_mode = ColorMode::Auto;  // --color
  Verbosity verbosity = Verbosity::Normal; // -v/--verbose
  std::string input_path;
  bool emit_ir = false;              // --emit-ir
  bool do_init = false;              // init subcommand
  bool do_clean = false;             // clean subcommand
};

// Result of CLI parsing. On failure, error_message explains what went wrong.
struct CliParseResult {
  std::optional<CliOptions> options;
  std::string error_message;
};

// Parse command-line arguments
CliParseResult ParseArgs(int argc, char** argv);

// JSON output formatting
std::string EscapeJson(std::string_view value);
std::string SeverityString(core::Severity severity);
std::string SubDiagKindString(core::SubDiagnosticKind kind);
std::string DiagnosticToJson(const core::Diagnostic& diag);
std::string DiagnosticToJson(const core::Diagnostic& diag,
                             const core::SourceRegistry& sources);
std::string DiagnosticStreamToJson(const core::DiagnosticStream& stream);
std::string DiagnosticStreamToJson(const core::DiagnosticStream& stream,
                                   const core::SourceRegistry& sources);

// Help and usage
void PrintUsage();
void PrintHelp();
void PrintDebugHelp();

}  // namespace cursive::driver
