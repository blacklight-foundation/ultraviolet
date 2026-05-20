// =============================================================================
// cli.cpp - Command-line interface parsing and output formatting
// =============================================================================
//
// SPEC REFERENCE:
//   Docs/SPECIFICATION.md §0.3 (lines 186-204) - Observable Compiler Behavior
//   Status(C, P) = ok ⇔ ∀ d ∈ DiagStream(C, P). d.severity ≠ Error
//   Status(C, P) = fail ⇔ ∃ d ∈ DiagStream(C, P). d.severity = Error
//   ExitCode(C, P) = 0 ⇔ Status(C, P) = ok
//   ExitCode(C, P) = 1 ⇔ Status(C, P) = fail
//
//   Docs/SPECIFICATION.md §2 (lines 766-777) - Command-Line Output
//
// =============================================================================

#include "06_driver/cli.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/diagnostics.h"
#include "00_core/diagnostic_render.h"
#include "00_core/process_config.h"
#include "01_project/target_profile.h"

namespace ultraviolet::driver {

// ============================================================================
// Internal Helpers
// ============================================================================

namespace {

struct DebugSubsystemInfo {
  std::string_view name;
  std::string_view description;
};

constexpr DebugSubsystemInfo kDebugSubsystems[] = {
    {"lex", "Lexer diagnostics and tokenization traces"},
    {"parse", "Parser item-level traces"},
    {"phases", "Frontend phase sequencing"},
    {"pipeline", "Top-level driver pipeline and build coordination"},
    {"output", "Output pipeline, artifacts, and incremental decisions"},
    {"link", "Linker and archiver debugging"},
    {"sema", "Semantic-analysis progress and diagnostics"},
    {"codegen", "IR lowering and code generation traces"},
    {"typeperf", "Semantic-analysis performance counters"},
    {"obj", "Object emission and object-model details"},
    {"binop", "Binary operator lowering/debugging"},
    {"call", "Call resolution and lowering"},
    {"loop", "Loop lowering/debugging"},
    {"parallel", "Parallel lowering/debugging"},
    {"return", "Return-path lowering/debugging"},
    {"union", "Union lowering/debugging"},
    {"wait", "Wait lowering/debugging"},
    {"propagate", "Propagate lowering/debugging"},
    {"spawn", "Spawn lowering/debugging"},
    {"method", "Method resolution/debugging"},
    {"shadow", "Shadowing and pattern-shadow diagnostics"},
    {"all", "Enable all debug subsystems"},
};

bool InternalFlagsEnabled() {
  return core::HasDebugSubsystems();
}

bool StartsWith(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

std::optional<bool> ParseToggleMode(std::string_view value) {
  if (value == "on" || value == "true" || value == "TRUE" ||
      value == "1" || value == "yes" || value == "YES") {
    return true;
  }
  if (value == "off" || value == "false" || value == "FALSE" ||
      value == "0" || value == "no" || value == "NO") {
    return false;
  }
  return std::nullopt;
}

std::optional<std::string> NormalizeOptLevel(std::string_view value) {
  if (value == "O0" || value == "0") return std::string("O0");
  if (value == "O1" || value == "1") return std::string("O1");
  if (value == "O2" || value == "2" || value == "release") {
    return std::string("O2");
  }
  if (value == "O3" || value == "3") return std::string("O3");
  if (value == "Os" || value == "s") return std::string("Os");
  if (value == "Oz" || value == "z") return std::string("Oz");
  return std::nullopt;
}

// Parse a comma-separated list of debug subsystem names.
std::vector<std::string> ParseDebugSubsystems(std::string_view value) {
  std::vector<std::string> result;
  std::size_t start = 0;
  while (start < value.size()) {
    std::size_t end = value.find(',', start);
    if (end == std::string_view::npos) {
      end = value.size();
    }
    auto token = value.substr(start, end - start);
    // Trim whitespace
    while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
    while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
    if (!token.empty()) {
      result.emplace_back(token);
    }
    start = end + 1;
  }
  return result;
}

bool IsKnownDebugSubsystem(std::string_view value) {
  for (const auto& info : kDebugSubsystems) {
    if (info.name == value) {
      return true;
    }
  }
  return false;
}

std::string DebugSubsystemList() {
  std::ostringstream oss;
  constexpr std::size_t kDebugSubsystemCount =
      sizeof(kDebugSubsystems) / sizeof(kDebugSubsystems[0]);
  for (std::size_t i = 0; i < kDebugSubsystemCount; ++i) {
    if (i != 0) {
      oss << ", ";
    }
    oss << kDebugSubsystems[i].name;
  }
  return oss.str();
}

std::optional<std::string> ValidateDebugSubsystems(
    const std::vector<std::string>& subsystems) {
  if (subsystems.empty()) {
    return std::string(
        "--debug requires a comma-separated list of subsystems or 'help'");
  }
  for (const auto& subsystem : subsystems) {
    if (!IsKnownDebugSubsystem(subsystem)) {
      return "unknown debug subsystem '" + subsystem +
             "'; expected one of: " + DebugSubsystemList();
    }
  }
  return std::nullopt;
}

std::vector<std::string> ParseCsvTokens(std::string_view value) {
  std::vector<std::string> result;
  std::size_t start = 0;
  while (start < value.size()) {
    std::size_t end = value.find(',', start);
    if (end == std::string_view::npos) {
      end = value.size();
    }
    auto token = value.substr(start, end - start);
    while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
    while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
    if (!token.empty()) {
      result.emplace_back(token);
    }
    start = end + 1;
  }
  return result;
}

std::optional<std::uint8_t> ParseTraceFilterMask(std::string_view value) {
  const auto classes = ParseCsvTokens(value);
  if (classes.empty()) {
    return std::nullopt;
  }
  std::uint8_t mask = 0;
  for (const auto& cls : classes) {
    if (cls == "all") {
      mask = static_cast<std::uint8_t>(0x7u);
      continue;
    }
    if (cls == "log") {
      mask = static_cast<std::uint8_t>(mask | 0x1u);
      continue;
    }
    if (cls == "diagnostic" || cls == "diag") {
      mask = static_cast<std::uint8_t>(mask | 0x2u);
      continue;
    }
    if (cls == "runtime") {
      mask = static_cast<std::uint8_t>(mask | 0x4u);
      continue;
    }
    return std::nullopt;
  }
  return mask;
}

std::optional<std::uint8_t> ParseTraceLevel(std::string_view value) {
  if (value == "trace") {
    return static_cast<std::uint8_t>(0u);
  }
  if (value == "info") {
    return static_cast<std::uint8_t>(1u);
  }
  if (value == "warning" || value == "warn") {
    return static_cast<std::uint8_t>(2u);
  }
  if (value == "error") {
    return static_cast<std::uint8_t>(3u);
  }
  return std::nullopt;
}

std::optional<core::ErrorRecoveryPolicy> ParseMaxErrorsPolicy(
    std::string_view value) {
  if (value == "inf" || value == "infinite" || value == "unlimited") {
    return core::ErrorRecoveryPolicy{std::nullopt};
  }
  if (value.empty()) {
    return std::nullopt;
  }
  std::size_t parsed = 0;
  for (const unsigned char ch : value) {
    if (!std::isdigit(ch)) {
      return std::nullopt;
    }
    const std::size_t digit = static_cast<std::size_t>(ch - '0');
    if (parsed > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
      return std::nullopt;
    }
    parsed = parsed * 10 + digit;
  }
  return core::ErrorRecoveryPolicy{parsed};
}

CliParseResult Fail(std::string message) {
  return CliParseResult{{}, std::move(message)};
}

std::size_t LevenshteinDistance(std::string_view a, std::string_view b) {
  const std::size_t m = a.size();
  const std::size_t n = b.size();
  std::vector<std::size_t> prev(n + 1);
  std::vector<std::size_t> curr(n + 1);
  for (std::size_t j = 0; j <= n; ++j) {
    prev[j] = j;
  }
  for (std::size_t i = 1; i <= m; ++i) {
    curr[0] = i;
    for (std::size_t j = 1; j <= n; ++j) {
      const std::size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
    }
    std::swap(prev, curr);
  }
  return prev[n];
}

std::string SuggestFlag(std::string_view unknown) {
  static const std::string_view known_flags[] = {
      "--help", "-h", "--version", "-V",
      "--color", "--check", "--diag-json", "--dump", "--dump-ast",
      "--assembly", "--out-dir",
      "--target-profile",
      "--opt-level",
      "--test", "--coverage",
      "--build-progress", "--incremental",
      "--max-errors", "--no-crash-report",
      "--runtime-lib",
      "--link-debug", "--no-link-debug",
      "--log", "--log-file", "--trace", "--trace-filter", "--trace-level",
      "--debug", "--conformance", "--emit-ir",
      "--phase1-only", "--no-output",
      "--verbose", "-v",
  };
  std::string_view best;
  std::size_t best_dist = unknown.size() <= 2 ? 1 : 4;
  for (const auto& flag : known_flags) {
    const auto dist = LevenshteinDistance(unknown, flag);
    if (dist < best_dist) {
      best_dist = dist;
      best = flag;
    }
  }
  if (!best.empty()) {
    return std::string(best);
  }
  return {};
}

}  // namespace

// ============================================================================
// Argument Parsing
// ============================================================================

CliParseResult ParseArgs(int argc, char** argv) {
  CliOptions opts;

  // First pass: parse --debug early so InternalFlagsEnabled() works for
  // subsequent flags. We scan ahead without consuming to find --debug.
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--debug") {
      if (i + 1 < argc) {
        const auto parsed = ParseDebugSubsystems(argv[i + 1]);
        if (!(parsed.size() == 1 && parsed.front() == "help")) {
          opts.debug_subsystems = parsed;
          core::SetDebugSubsystems(opts.debug_subsystems);
        }
      }
      break;
    }
    if (StartsWith(arg, "--debug=")) {
      const auto parsed = ParseDebugSubsystems(
          arg.substr(std::string_view("--debug=").size()));
      if (!(parsed.size() == 1 && parsed.front() == "help")) {
        opts.debug_subsystems = parsed;
        core::SetDebugSubsystems(opts.debug_subsystems);
      }
      break;
    }
  }

  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      opts.show_help = true;
      return CliParseResult{opts, {}};
    }
    if (arg == "--version" || arg == "-V") {
      opts.show_version = true;
      return CliParseResult{opts, {}};
    }
    if (arg == "--diag-json") {
      opts.diag_json = true;
      continue;
    }
    if (arg == "--target-profile") {
      if (i + 1 >= argc) {
        return Fail("--target-profile requires a profile argument");
      }
      const auto profile =
          project::ParseTargetProfile(std::string_view(argv[++i]));
      if (!profile.has_value()) {
        return Fail("invalid target profile; expected one of: x86_64-sysv, "
                    "x86_64-win64, aarch64-aapcs64");
      }
      opts.target_profile_override = *profile;
      continue;
    }
    if (StartsWith(arg, "--target-profile=")) {
      const auto profile = project::ParseTargetProfile(
          arg.substr(std::string_view("--target-profile=").size()));
      if (!profile.has_value()) {
        return Fail("invalid target profile; expected one of: x86_64-sysv, "
                    "x86_64-win64, aarch64-aapcs64");
      }
      opts.target_profile_override = *profile;
      continue;
    }
    if (arg == "--opt-level") {
      if (i + 1 >= argc) {
        return Fail("--opt-level requires a value (O0|O1|O2|O3|Os|Oz)");
      }
      const auto parsed = NormalizeOptLevel(std::string_view(argv[++i]));
      if (!parsed.has_value()) {
        return Fail("invalid --opt-level value; expected one of: O0, O1, O2, O3, Os, Oz");
      }
      opts.opt_level = *parsed;
      continue;
    }
    if (StartsWith(arg, "--opt-level=")) {
      const auto parsed = NormalizeOptLevel(
          arg.substr(std::string_view("--opt-level=").size()));
      if (!parsed.has_value()) {
        return Fail("invalid --opt-level value; expected one of: O0, O1, O2, O3, Os, Oz");
      }
      opts.opt_level = *parsed;
      continue;
    }
    if (arg == "--color") {
      if (i + 1 >= argc) {
        return Fail("--color requires an argument (auto|always|never)");
      }
      std::string_view val = argv[++i];
      if (val == "auto") {
        opts.color_mode = ColorMode::Auto;
      } else if (val == "always") {
        opts.color_mode = ColorMode::Always;
      } else if (val == "never") {
        opts.color_mode = ColorMode::Never;
      } else {
        return Fail("invalid --color value; expected 'auto', 'always', or 'never'");
      }
      continue;
    }
    if (StartsWith(arg, "--color=")) {
      std::string_view val = arg.substr(std::string_view("--color=").size());
      if (val == "auto") {
        opts.color_mode = ColorMode::Auto;
      } else if (val == "always") {
        opts.color_mode = ColorMode::Always;
      } else if (val == "never") {
        opts.color_mode = ColorMode::Never;
      } else {
        return Fail("invalid --color value; expected 'auto', 'always', or 'never'");
      }
      continue;
    }
    if (arg == "--verbose" || arg == "-v") {
      opts.verbosity = Verbosity::Verbose;
      continue;
    }
    if (arg == "--check") {
      opts.check_only = true;
      continue;
    }
    if (arg == "--no-crash-report") {
      opts.no_crash_report = true;
      continue;
    }
    if (arg == "--conformance") {
      if (i + 1 >= argc) {
        return Fail("--conformance requires a path argument");
      }
      opts.conformance_path = std::string(argv[++i]);
      continue;
    }
    if (StartsWith(arg, "--conformance=")) {
      opts.conformance_path =
          std::string(arg.substr(std::string_view("--conformance=").size()));
      continue;
    }
    if (arg == "--dump") {
      opts.dump_project = true;
      continue;
    }
    if (arg == "--log") {
      opts.log_enabled = true;
      opts.log_to_console = true;
      continue;
    }
    if (arg == "--log-file") {
      if (i + 1 >= argc) {
        return Fail("--log-file requires a path argument");
      }
      const std::string path = std::string(argv[++i]);
      if (path.empty()) {
        return Fail("--log-file path must not be empty");
      }
      opts.log_enabled = true;
      opts.log_to_file = true;
      opts.log_file_path = path;
      continue;
    }
    if (StartsWith(arg, "--log-file=")) {
      const std::string path =
          std::string(arg.substr(std::string_view("--log-file=").size()));
      if (path.empty()) {
        return Fail("--log-file path must not be empty");
      }
      opts.log_enabled = true;
      opts.log_to_file = true;
      opts.log_file_path = path;
      continue;
    }
    if (arg == "--trace") {
      opts.log_enabled = true;
      opts.log_to_console = true;
      opts.trace = true;
      continue;
    }
    if (arg == "--trace-filter") {
      if (i + 1 >= argc) {
        return Fail("--trace-filter requires classes (log,diagnostic,runtime,all)");
      }
      const auto parsed = ParseTraceFilterMask(std::string_view(argv[++i]));
      if (!parsed.has_value()) {
        return Fail(
            "invalid --trace-filter value; expected comma-separated classes from "
            "log,diagnostic,runtime,all");
      }
      opts.log_enabled = true;
      opts.log_to_console = true;
      opts.trace = true;
      opts.trace_filter_mask = *parsed;
      continue;
    }
    if (StartsWith(arg, "--trace-filter=")) {
      const std::string_view value =
          arg.substr(std::string_view("--trace-filter=").size());
      const auto parsed = ParseTraceFilterMask(value);
      if (!parsed.has_value()) {
        return Fail(
            "invalid --trace-filter value; expected comma-separated classes from "
            "log,diagnostic,runtime,all");
      }
      opts.log_enabled = true;
      opts.log_to_console = true;
      opts.trace = true;
      opts.trace_filter_mask = *parsed;
      continue;
    }
    if (arg == "--trace-level") {
      if (i + 1 >= argc) {
        return Fail("--trace-level requires a level (trace|info|warning|error)");
      }
      const auto parsed = ParseTraceLevel(std::string_view(argv[++i]));
      if (!parsed.has_value()) {
        return Fail(
            "invalid --trace-level value; expected trace, info, warning, or error");
      }
      opts.log_enabled = true;
      opts.log_to_console = true;
      opts.trace = true;
      opts.trace_min_level = *parsed;
      continue;
    }
    if (StartsWith(arg, "--trace-level=")) {
      const std::string_view value =
          arg.substr(std::string_view("--trace-level=").size());
      const auto parsed = ParseTraceLevel(value);
      if (!parsed.has_value()) {
        return Fail(
            "invalid --trace-level value; expected trace, info, warning, or error");
      }
      opts.log_enabled = true;
      opts.log_to_console = true;
      opts.trace = true;
      opts.trace_min_level = *parsed;
      continue;
    }
    if (arg == "--build-progress") {
      if (i + 1 >= argc) {
        return Fail("--build-progress requires an argument (on|off)");
      }
      const auto parsed = ParseToggleMode(std::string_view(argv[++i]));
      if (!parsed.has_value()) {
        return Fail("invalid --build-progress value; expected 'on' or 'off'");
      }
      opts.build_progress = *parsed;
      continue;
    }
    if (StartsWith(arg, "--build-progress=")) {
      const std::string_view value =
          arg.substr(std::string_view("--build-progress=").size());
      const auto parsed = ParseToggleMode(value);
      if (!parsed.has_value()) {
        return Fail("invalid --build-progress value; expected 'on' or 'off'");
      }
      opts.build_progress = *parsed;
      continue;
    }
    if (arg == "--incremental") {
      if (i + 1 >= argc) {
        return Fail("--incremental requires an argument (on|off)");
      }
      const auto parsed = ParseToggleMode(std::string_view(argv[++i]));
      if (!parsed.has_value()) {
        return Fail("invalid --incremental value; expected 'on' or 'off'");
      }
      opts.incremental = *parsed;
      continue;
    }
    if (StartsWith(arg, "--incremental=")) {
      const std::string_view value =
          arg.substr(std::string_view("--incremental=").size());
      const auto parsed = ParseToggleMode(value);
      if (!parsed.has_value()) {
        return Fail("invalid --incremental value; expected 'on' or 'off'");
      }
      opts.incremental = *parsed;
      continue;
    }
    if (arg == "--max-errors") {
      if (i + 1 >= argc) {
        return Fail("--max-errors requires a value (N|inf)");
      }
      const auto parsed = ParseMaxErrorsPolicy(std::string_view(argv[++i]));
      if (!parsed.has_value()) {
        return Fail("invalid --max-errors value; expected non-negative integer or inf");
      }
      opts.max_errors_override = *parsed;
      continue;
    }
    if (StartsWith(arg, "--max-errors=")) {
      const std::string_view value =
          arg.substr(std::string_view("--max-errors=").size());
      const auto parsed = ParseMaxErrorsPolicy(value);
      if (!parsed.has_value()) {
        return Fail("invalid --max-errors value; expected non-negative integer or inf");
      }
      opts.max_errors_override = *parsed;
      continue;
    }
    if (arg == "--runtime-lib") {
      if (i + 1 >= argc) {
        return Fail("--runtime-lib requires a path argument");
      }
      const std::string path = std::string(argv[++i]);
      if (path.empty()) {
        return Fail("--runtime-lib path must not be empty");
      }
      opts.runtime_lib_path = path;
      continue;
    }
    if (StartsWith(arg, "--runtime-lib=")) {
      const std::string path =
          std::string(arg.substr(std::string_view("--runtime-lib=").size()));
      if (path.empty()) {
        return Fail("--runtime-lib path must not be empty");
      }
      opts.runtime_lib_path = path;
      continue;
    }
    if (arg == "--link-debug") {
      opts.link_debug = true;
      continue;
    }
    if (arg == "--no-link-debug") {
      opts.link_debug = false;
      continue;
    }
    if (StartsWith(arg, "--link-debug=")) {
      const std::string_view value =
          arg.substr(std::string_view("--link-debug=").size());
      const auto parsed = ParseToggleMode(value);
      if (!parsed.has_value()) {
        return Fail("invalid --link-debug value; expected 'on' or 'off'");
      }
      opts.link_debug = *parsed;
      continue;
    }
    if (arg == "--out-dir") {
      if (i + 1 >= argc) {
        return Fail("--out-dir requires a path argument");
      }
      const std::string path = std::string(argv[++i]);
      if (path.empty()) {
        return Fail("--out-dir path must not be empty");
      }
      opts.out_dir = path;
      continue;
    }
    if (StartsWith(arg, "--out-dir=")) {
      const std::string path =
          std::string(arg.substr(std::string_view("--out-dir=").size()));
      if (path.empty()) {
        return Fail("--out-dir path must not be empty");
      }
      opts.out_dir = path;
      continue;
    }
    if (arg == "--debug") {
      if (i + 1 >= argc) {
        return Fail("--debug requires a comma-separated list of subsystems or 'help'");
      }
      const auto parsed = ParseDebugSubsystems(argv[++i]);
      if (parsed.size() == 1 && parsed.front() == "help") {
        opts.show_debug_help = true;
        return CliParseResult{opts, {}};
      }
      if (const auto error = ValidateDebugSubsystems(parsed); error.has_value()) {
        return Fail(*error);
      }
      opts.debug_subsystems = parsed;
      core::SetDebugSubsystems(opts.debug_subsystems);
      continue;
    }
    if (StartsWith(arg, "--debug=")) {
      const auto parsed = ParseDebugSubsystems(
          arg.substr(std::string_view("--debug=").size()));
      if (parsed.size() == 1 && parsed.front() == "help") {
        opts.show_debug_help = true;
        return CliParseResult{opts, {}};
      }
      if (const auto error = ValidateDebugSubsystems(parsed); error.has_value()) {
        return Fail(*error);
      }
      opts.debug_subsystems = parsed;
      core::SetDebugSubsystems(opts.debug_subsystems);
      continue;
    }
    if (arg == "--dump-ast") {
      opts.dump_ast = true;
      continue;
    }
    if (arg == "--phase1-only") {
      opts.phase1_only = true;
      continue;
    }
    if (arg == "--no-output") {
      if (!InternalFlagsEnabled()) {
        return Fail("--no-output requires --debug to be set");
      }
      opts.no_output = true;
      continue;
    }
    if (arg == "--assembly") {
      if (i + 1 >= argc) {
        return Fail("--assembly requires a name argument");
      }
      opts.assembly_target = std::string(argv[++i]);
      continue;
    }
    if (StartsWith(arg, "--assembly=")) {
      opts.assembly_target =
          std::string(arg.substr(std::string_view("--assembly=").size()));
      continue;
    }
    if (arg == "--test-harness-assembly") {
      if (i + 1 >= argc) {
        return Fail("--test-harness-assembly requires a name argument");
      }
      opts.test_harness_assembly = std::string(argv[++i]);
      continue;
    }
    if (StartsWith(arg, "--test-harness-assembly=")) {
      opts.test_harness_assembly = std::string(
          arg.substr(std::string_view("--test-harness-assembly=").size()));
      continue;
    }
    if (arg == "--test-harness-module") {
      if (i + 1 >= argc) {
        return Fail("--test-harness-module requires a module path argument");
      }
      opts.test_harness_module = std::string(argv[++i]);
      continue;
    }
    if (StartsWith(arg, "--test-harness-module=")) {
      opts.test_harness_module = std::string(
          arg.substr(std::string_view("--test-harness-module=").size()));
      continue;
    }
    if (arg == "--test-harness-dir") {
      if (i + 1 >= argc) {
        return Fail("--test-harness-dir requires a path argument");
      }
      opts.test_harness_dir = std::string(argv[++i]);
      continue;
    }
    if (StartsWith(arg, "--test-harness-dir=")) {
      opts.test_harness_dir = std::string(
          arg.substr(std::string_view("--test-harness-dir=").size()));
      continue;
    }
    if (arg == "--test") {
      if (i + 1 >= argc) {
        return Fail("--test requires a name argument");
      }
      opts.test_name_filter = std::string(argv[++i]);
      continue;
    }
    if (StartsWith(arg, "--test=")) {
      opts.test_name_filter =
          std::string(arg.substr(std::string_view("--test=").size()));
      continue;
    }
    if (arg == "--coverage") {
      if (i + 1 >= argc) {
        return Fail("--coverage requires an obligation anchor argument");
      }
      opts.test_coverage_filter = std::string(argv[++i]);
      continue;
    }
    if (StartsWith(arg, "--coverage=")) {
      opts.test_coverage_filter =
          std::string(arg.substr(std::string_view("--coverage=").size()));
      continue;
    }
    if (arg == "--emit-ir") {
      opts.emit_ir = true;
      continue;
    }
    if (arg == "build") {
      // Subcommand - currently ignored
      continue;
    }
    if (arg == "test") {
      opts.do_test = true;
      continue;
    }
    if (arg == "init") {
      opts.do_init = true;
      continue;
    }
    if (arg == "clean") {
      opts.do_clean = true;
      continue;
    }
    if (StartsWith(arg, "-")) {
      std::string msg = "unknown option: " + std::string(arg);
      const auto suggestion = SuggestFlag(arg);
      if (!suggestion.empty()) {
        msg += "; did you mean '";
        msg += suggestion;
        msg += "'?";
      }
      return Fail(std::move(msg));
    }
    if (opts.do_test) {
      if (opts.test_target.has_value()) {
        opts.test_target_rejected = true;
        continue;
      }
      opts.test_target = std::string(arg);
      continue;
    }
    if (!opts.input_path.empty()) {
      return Fail("multiple input paths not supported; got '" +
                   opts.input_path + "' and '" + std::string(arg) + "'");
    }
    opts.input_path = std::string(arg);
  }
  if (opts.show_help || opts.show_version) {
    return CliParseResult{opts, {}};
  }
  if (opts.do_init) {
    // init subcommand: input_path is optional (defaults to ".")
    if (opts.input_path.empty()) {
      opts.input_path = ".";
    }
    return CliParseResult{opts, {}};
  }
  if (opts.do_clean) {
    // clean subcommand: still needs input path to find project root
    if (opts.input_path.empty()) {
      opts.input_path = ".";
    }
    return CliParseResult{opts, {}};
  }
  if (opts.do_test) {
    opts.input_path = ".";
    return CliParseResult{opts, {}};
  }
  if (opts.input_path.empty()) {
    return Fail("no input file specified");
  }
  return CliParseResult{opts, {}};
}

// ============================================================================
// JSON Output Formatting
// ============================================================================

std::string EscapeJson(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          std::ostringstream oss;
          oss << "\\u";
          oss << std::hex << std::uppercase;
          oss.width(4);
          oss.fill('0');
          oss << static_cast<int>(static_cast<unsigned char>(c));
          out += oss.str();
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  return out;
}

std::string SeverityString(core::Severity severity) {
  switch (severity) {
    case core::Severity::Error:
      return "error";
    case core::Severity::Warning:
      return "warning";
    case core::Severity::Info:
      return "info";
    case core::Severity::Panic:
      return "panic";
    case core::Severity::Note:
      return "note";
  }
  return "error";
}

std::string SubDiagKindString(core::SubDiagnosticKind kind) {
  switch (kind) {
    case core::SubDiagnosticKind::Note:
      return "note";
    case core::SubDiagnosticKind::Help:
      return "help";
    case core::SubDiagnosticKind::FixIt:
      return "fix";
  }
  return "note";
}

namespace {

void EmitSpanJson(std::ostringstream& oss, const core::Span& sp) {
  oss << "{";
  oss << "\"file\":\"" << EscapeJson(sp.file) << "\",";
  oss << "\"start_line\":" << sp.start_line << ",";
  oss << "\"start_col\":" << sp.start_col << ",";
  oss << "\"end_line\":" << sp.end_line << ",";
  oss << "\"end_col\":" << sp.end_col;
  oss << "}";
}

void EmitChildrenJson(std::ostringstream& oss,
                      const std::vector<core::SubDiagnostic>& children) {
  oss << "\"children\":[";
  for (std::size_t j = 0; j < children.size(); ++j) {
    if (j > 0) oss << ",";
    const auto& child = children[j];
    oss << "{";
    oss << "\"kind\":\"" << SubDiagKindString(child.kind) << "\",";
    oss << "\"message\":\"" << EscapeJson(child.message) << "\",";
    if (child.span.has_value()) {
      oss << "\"span\":";
      EmitSpanJson(oss, *child.span);
    } else {
      oss << "\"span\":null";
    }
    if (child.fix_text.has_value()) {
      oss << ",\"fix_text\":\"" << EscapeJson(*child.fix_text) << "\"";
    }
    oss << "}";
  }
  oss << "]";
}

// Extract a single source line (1-based) from source text.
std::string_view GetSourceLineJson(std::string_view source,
                                   std::size_t line_number) {
  if (line_number == 0) return {};
  std::size_t current_line = 1;
  std::size_t pos = 0;
  while (pos < source.size()) {
    if (current_line == line_number) {
      std::size_t end = source.find('\n', pos);
      if (end == std::string_view::npos) end = source.size();
      std::size_t line_end = end;
      if (line_end > pos && source[line_end - 1] == '\r') --line_end;
      return source.substr(pos, line_end - pos);
    }
    std::size_t nl = source.find('\n', pos);
    if (nl == std::string_view::npos) break;
    pos = nl + 1;
    ++current_line;
  }
  return {};
}

}  // namespace

std::string DiagnosticToJson(const core::Diagnostic& diag) {
  std::ostringstream oss;
  oss << "{";
  if (diag.code.empty()) {
    oss << "\"code\":null,";
  } else {
    oss << "\"code\":\"" << EscapeJson(diag.code) << "\",";
  }
  oss << "\"severity\":\"" << SeverityString(diag.severity) << "\",";
  oss << "\"message\":\"" << EscapeJson(diag.message) << "\",";
  if (!diag.span.has_value()) {
    oss << "\"span\":null,";
    oss << "\"source_line\":null,";
  } else {
    oss << "\"span\":";
    EmitSpanJson(oss, *diag.span);
    oss << ",\"source_line\":null,";
  }
  EmitChildrenJson(oss, diag.children);
  oss << "}";
  return oss.str();
}

std::string DiagnosticToJson(const core::Diagnostic& diag,
                             const core::SourceRegistry& sources) {
  std::ostringstream oss;
  oss << "{";
  if (diag.code.empty()) {
    oss << "\"code\":null,";
  } else {
    oss << "\"code\":\"" << EscapeJson(diag.code) << "\",";
  }
  oss << "\"severity\":\"" << SeverityString(diag.severity) << "\",";
  oss << "\"message\":\"" << EscapeJson(diag.message) << "\",";
  if (!diag.span.has_value()) {
    oss << "\"span\":null,";
    oss << "\"source_line\":null,";
  } else {
    const auto& sp = *diag.span;
    oss << "\"span\":";
    EmitSpanJson(oss, sp);
    oss << ",";
    if (sources) {
      auto content = sources(sp.file);
      if (content.has_value()) {
        auto line = GetSourceLineJson(*content, sp.start_line);
        oss << "\"source_line\":\"" << EscapeJson(line) << "\",";
      } else {
        oss << "\"source_line\":null,";
      }
    } else {
      oss << "\"source_line\":null,";
    }
  }
  EmitChildrenJson(oss, diag.children);
  oss << "}";
  return oss.str();
}

std::string DiagnosticStreamToJson(const core::DiagnosticStream& stream) {
  std::ostringstream oss;
  oss << "{";
  oss << "\"diagnostics\":[";
  for (std::size_t i = 0; i < stream.size(); ++i) {
    if (i > 0) oss << ",";
    oss << DiagnosticToJson(stream[i]);
  }
  oss << "]";
  oss << "}";
  return oss.str();
}

std::string DiagnosticStreamToJson(const core::DiagnosticStream& stream,
                                   const core::SourceRegistry& sources) {
  std::ostringstream oss;
  oss << "{";
  oss << "\"diagnostics\":[";
  for (std::size_t i = 0; i < stream.size(); ++i) {
    if (i > 0) oss << ",";
    oss << DiagnosticToJson(stream[i], sources);
  }
  oss << "]";
  oss << "}";
  return oss.str();
}

// ============================================================================
// Help and Usage (C7)
// ============================================================================

void PrintUsage(std::string_view command_name) {
  std::cerr << "usage: " << command_name << " <command> [options]\n"
            << "       " << command_name << " build <file> [options]\n"
            << "       " << command_name << " test [target] [options]\n"
            << "       " << command_name << " init [directory]\n"
            << "       " << command_name << " clean [file]\n"
            << "Try '" << command_name << " --help' for more information.\n";
}

void PrintHelp(std::string_view command_name) {
  std::cout << "Ultraviolet - Ultraviolet language compiler\n"
R"(

USAGE
  )" << command_name << R"( build <file> [options]
  )" << command_name << R"( test [target] [options]
  )" << command_name << R"( init [directory]
  )" << command_name << R"( clean [file]

OPTIONS
  -h, --help                 Show this help message
  -V, --version              Show version information
  -v, --verbose              Show detailed build information
  --color <auto|always|never> Control colored output (default: auto)
  --check                    Type-check only (no code generation or linking)
  --assembly <name>          Select assembly to build (auto-selects if only one
                             executable exists)
  --target-profile <profile> Select target profile: x86_64-sysv,
                             x86_64-win64, aarch64-aapcs64
  --test <name>              With `test`, run tests matching a procedure,
                             display, or fully-qualified test name
  --coverage <anchor>        With `test`, run tests covering the obligation
                             anchor
  --opt-level <level>        Select codegen optimization: O0, O1, O2, O3, Os, Oz
  --out-dir <path>           Override output directory (default: Build/)
  --diag-json                Output diagnostics as JSON
  --dump                     Dump project structure
  --dump-ast                 Dump parsed AST for project modules
  --emit-ir                  Emit textual IR to stdout
  --build-progress <on|off>  Enable/disable build progress display
  --incremental <on|off>     Enable/disable incremental compilation
  --max-errors <N|inf>       Abort compilation after N errors (default: 100)
  --no-crash-report          Disable built-in crash report generation

  --runtime-lib <path>       Path to active language runtime library
  --link-debug               Enable linker debug output
  --no-link-debug            Disable linker debug output

  --log                      Show runtime log records on console
  --log-file <path>          Write runtime logs to the exact path provided
  --trace                    Show all runtime trace records (not just log category)
  --trace-filter <classes>   Filter trace categories: log,diagnostic,runtime,all (comma-separated)
  --trace-level <level>      Filter minimum trace level: trace, info, warning, error
  --conformance <path>       Write compiler conformance trace to <out-dir>/Logs/Conformance/<basename(path)>

  --debug <subsystems|help>  Enable debug output or print subsystem help

MANIFEST (Ultraviolet.toml)
  [toolchain]
    runtime_lib = "path"     Runtime library path (overridden by --runtime-lib)
    target_profile = "x86_64-sysv"  Default target profile (overridden by --target-profile)

  [build]
    incremental = true       Enable incremental compilation (overridden by --incremental)
    progress = true          Enable build progress (overridden by --build-progress)

ENVIRONMENT
  NO_COLOR                   Disable colored output (see https://no-color.org)
  CLICOLOR_FORCE             Force colored output even when not a TTY
  CLICOLOR                   Set to 0 to disable colored output
  UV_RUNTIME_SINK       Runtime sink override: console|file|both
  UV_RUNTIME_PATH       Runtime file sink path (used with sink=file|both)
  UV_RUNTIME_ROOT       Runtime source-root prefix to strip from file paths
  UV_RUNTIME_FILTER     Runtime category filter: log,diagnostic,runtime,all
  UV_RUNTIME_LEVEL      Runtime min level: trace|info|warning|error
  UV_RUNTIME_RULE       Runtime rule-id substring filter
  UV_RUNTIME_FILE       Runtime file substring filter
  UV_RUNTIME_LABEL      Runtime label substring filter
  UV_RUNTIME_FAIL_ONLY  Runtime filter to cmp=fail records only (1/0)
  UV_RUNTIME_BREAK_ON_FAIL Runtime break on cmp=fail (1/0)

EXIT CODES
  0  Compilation succeeded
  1  Compilation failed (errors emitted)
  2  Invalid command-line arguments
)";
}

void PrintDebugHelp() {
  std::cout << "ultraviolet debug subsystems\n\n";
  std::cout << "Use --debug <name[,name...]> to enable subsystem traces.\n\n";
  for (const auto& info : kDebugSubsystems) {
    std::cout << "  " << info.name;
    if (info.name.size() < 9) {
      std::cout << std::string(9 - info.name.size(), ' ');
    }
    std::cout << "  " << info.description << "\n";
  }
}

}  // namespace ultraviolet::driver
