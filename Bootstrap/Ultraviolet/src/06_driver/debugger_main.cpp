// =============================================================================
// debugger_main.cpp - Crash debugger entry point
// =============================================================================

#include "00_core/crash_debug.h"
#include "06_driver/version.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

void PrintUsage() {
  std::cout
      << "Usage:\n"
      << "  uvc-debug run [options] <program> [-- arg...]\n"
      << "\n"
      << "Options:\n"
      << "  --report-dir <path>    Override crash report directory\n"
      << "  --max-frames <N>       Limit captured stack frames (default: 128)\n"
      << "  --no-minidump          Do not write crash.dmp\n"
      << "  --no-crash-report      Disable debugger crash capture and run normally\n"
      << "  --help                 Show this help\n"
      << "  --version              Show version information\n";
}

std::optional<std::size_t> ParseFrameCount(std::string_view value) {
  if (value.empty()) {
    return std::nullopt;
  }
  std::size_t parsed = 0;
  for (const unsigned char ch : value) {
    if (ch < '0' || ch > '9') {
      return std::nullopt;
    }
    parsed = parsed * 10 + static_cast<std::size_t>(ch - '0');
  }
  if (parsed == 0) {
    return std::nullopt;
  }
  return parsed;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace ultraviolet;

  if (argc <= 1) {
    PrintUsage();
    return 2;
  }

  std::string_view command = argv[1];
  if (command == "--help" || command == "-h") {
    PrintUsage();
    return 0;
  }
  if (command == "--version" || command == "-V") {
    std::cout << driver::GetVersionString() << "\n";
    return 0;
  }
  if (command != "run") {
    std::cerr << "error: unsupported command '" << command << "'\n";
    PrintUsage();
    return 2;
  }
  if (!core::CrashCaptureSupported()) {
    std::cerr << "error: crash capture is not supported on this host.\n";
    return 1;
  }

  core::DebugRunOptions options;
  options.tool_name = "uvc-debug";
  options.tool_version = driver::GetVersionString();

  bool passthrough_args = false;
  for (int i = 2; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (!passthrough_args && (arg == "--help" || arg == "-h")) {
      PrintUsage();
      return 0;
    }
    if (!passthrough_args && (arg == "--version" || arg == "-V")) {
      std::cout << driver::GetVersionString() << "\n";
      return 0;
    }
    if (!passthrough_args && arg == "--") {
      passthrough_args = true;
      continue;
    }
    if (!passthrough_args && arg == "--report-dir") {
      if (i + 1 >= argc) {
        std::cerr << "error: --report-dir requires a path argument\n";
        return 2;
      }
      options.report_root = argv[++i];
      continue;
    }
    if (!passthrough_args && arg == "--max-frames") {
      if (i + 1 >= argc) {
        std::cerr << "error: --max-frames requires a numeric argument\n";
        return 2;
      }
      const auto parsed = ParseFrameCount(argv[++i]);
      if (!parsed.has_value()) {
        std::cerr << "error: invalid --max-frames value\n";
        return 2;
      }
      options.max_frames = *parsed;
      continue;
    }
    if (!passthrough_args && arg == "--no-minidump") {
      options.write_minidump = false;
      continue;
    }
    if (!passthrough_args && arg == "--no-crash-report") {
      options.enabled = false;
      continue;
    }
    if (options.program.empty()) {
      options.program = std::filesystem::path(std::string(arg));
      continue;
    }
    options.arguments.push_back(std::string(arg));
  }

  if (options.program.empty()) {
    std::cerr << "error: missing program path\n";
    PrintUsage();
    return 2;
  }

  std::error_code ec;
  options.working_directory = std::filesystem::current_path(ec);
  if (options.report_root.empty()) {
    options.report_root = core::DefaultTargetCrashReportRoot(options.program);
  }

  const auto result = core::DebugRunProcess(options);
  if (!result.stdout_text.empty()) {
    std::cout << result.stdout_text;
    std::cout.flush();
  }
  if (!result.stderr_text.empty()) {
    std::cerr << result.stderr_text;
    std::cerr.flush();
  }

  if (!result.launched) {
    std::cerr << "error: failed to launch program '" << options.program.string()
              << "'";
    if (!result.launch_error.empty()) {
      std::cerr << ": " << result.launch_error;
    }
    std::cerr << "\n";
    return 1;
  }

  return result.exit_code >= 0 ? result.exit_code : 1;
}
