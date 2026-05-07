#include "00_core/crash_debug.h"
#include "00_core/host/services.h"
#include "crash_debug_internal.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace cursive::core::crash_debug_detail {

RuntimeState& State() {
  static RuntimeState state;
  return state;
}

std::mutex& StateMutex() {
  static std::mutex mu;
  return mu;
}

std::atomic<bool>& HandlingCrash() {
  static std::atomic<bool> handling{false};
  return handling;
}

CrashRuntimeOptions CrashOptionsSnapshot() {
  std::lock_guard<std::mutex> lock(StateMutex());
  return State().options;
}

std::string EscapeJson(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char ch : value) {
    switch (ch) {
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
        if (static_cast<unsigned char>(ch) < 0x20) {
          char buffer[7];
          std::snprintf(buffer, sizeof(buffer), "\\u%04X",
                        static_cast<unsigned>(static_cast<unsigned char>(ch)));
          out += buffer;
        } else {
          out.push_back(ch);
        }
        break;
    }
  }
  return out;
}

std::tm CopyUtcTime(std::time_t value) {
  static std::mutex mu;
  std::lock_guard<std::mutex> lock(mu);
  const std::tm* tm_utc = std::gmtime(&value);
  return tm_utc != nullptr ? *tm_utc : std::tm{};
}

std::tm CopyLocalTime(std::time_t value) {
  static std::mutex mu;
  std::lock_guard<std::mutex> lock(mu);
  const std::tm* tm_local = std::localtime(&value);
  return tm_local != nullptr ? *tm_local : std::tm{};
}

std::string NowUtcString() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  const std::tm tm_utc = CopyUtcTime(now_time);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return buffer;
}

std::string TimestampFileStem() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  const std::tm tm_local = CopyLocalTime(now_time);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &tm_local);
  return buffer;
}

std::string PathString(const std::filesystem::path& path) {
  return path.empty() ? std::string{} : path.generic_string();
}

std::filesystem::path TempCrashRoot() {
  std::error_code ec;
  const auto temp = std::filesystem::temp_directory_path(ec);
  if (!ec) {
    return temp / "cursive" / "crash";
  }
  return std::filesystem::path("crash");
}

void EnsureDirectory(const std::filesystem::path& path) {
  if (path.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
  if (path.empty()) {
    return;
  }
  EnsureDirectory(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return;
  }
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string JoinArguments(const std::vector<std::string>& args) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i != 0) {
      oss << ' ';
    }
    oss << args[i];
  }
  return oss.str();
}

std::string Hex32(std::uint32_t value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
  return buffer;
}

std::string Hex64(std::uint64_t value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "0x%016llX",
                static_cast<unsigned long long>(value));
  return buffer;
}

std::string HexCompact64(std::uint64_t value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "0x%llX",
                static_cast<unsigned long long>(value));
  return buffer;
}

std::string Trim(std::string_view text) {
  std::size_t start = 0;
  while (start < text.size() &&
         std::isspace(static_cast<unsigned char>(text[start])) != 0) {
    ++start;
  }
  std::size_t end = text.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return std::string(text.substr(start, end - start));
}

bool IsHexString(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  for (const unsigned char ch : text) {
    if (std::isxdigit(ch) == 0) {
      return false;
    }
  }
  return true;
}

std::optional<std::uint64_t> ParseHexU64(std::string_view text) {
  if (!IsHexString(text)) {
    return std::nullopt;
  }
  try {
    return static_cast<std::uint64_t>(
        std::stoull(std::string(text), nullptr, 16));
  } catch (...) {
    return std::nullopt;
  }
}

CrashArtifacts MakeArtifacts(const std::filesystem::path& root,
                             std::string_view kind,
                             std::uint32_t process_id) {
  EnsureDirectory(root);
  const std::string stem =
      TimestampFileStem() + "_" +
      std::to_string(static_cast<unsigned long>(process_id));
  const std::filesystem::path report_dir =
      root / (stem + "_" + std::string(kind));
  EnsureDirectory(report_dir);
  CrashArtifacts artifacts;
  artifacts.report_dir = report_dir;
  artifacts.text_path = report_dir / "report.txt";
  artifacts.json_path = report_dir / "report.json";
  artifacts.minidump_path = report_dir / "crash.dmp";
  artifacts.stdout_path = report_dir / "stdout.txt";
  artifacts.stderr_path = report_dir / "stderr.txt";
  return artifacts;
}

CrashReport BuildCrashReport(const CrashRuntimeOptions& options,
                             std::string_view kind,
                             std::uint32_t process_id,
                             std::uint32_t thread_id,
                             std::uint32_t exception_code,
                             std::string exception_name,
                             std::string message,
                             const CrashArtifacts& artifacts,
                             std::vector<CrashFrame> frames) {
  CrashReport report;
  report.tool = options.tool_name;
  report.version = options.tool_version;
  report.timestamp_utc = NowUtcString();
  report.kind = std::string(kind);
  report.process_id = process_id;
  report.thread_id = thread_id;
  report.exception_code_value = exception_code;
  report.exception_name = std::move(exception_name);
  report.message = std::move(message);
  report.arguments = options.arguments;
  report.working_directory = options.working_directory;
  report.executable_path = options.executable_path;
  report.artifacts = artifacts;
  report.frames = std::move(frames);
  return report;
}

void EmitCrashOutputs(const CrashRuntimeOptions& options,
                      const CrashReport& report) {
  WriteTextFile(report.artifacts.text_path, CrashSummary(report));
  WriteTextFile(report.artifacts.json_path, CrashReportToJson(report));
  if (options.emit_stderr_summary) {
    const std::string summary = CrashSummary(report);
    std::fwrite(summary.data(), 1, summary.size(), stderr);
    std::fflush(stderr);
  }
  if (options.emit_json_stdout) {
    const std::string json = CrashEnvelopeToJson(report);
    std::fwrite(json.data(), 1, json.size(), stdout);
    std::fwrite("\n", 1, 1, stdout);
    std::fflush(stdout);
  }
}

DebugRunResult RunProcessWithoutDebugger(const DebugRunOptions& options) {
  DebugRunResult result;
  HostProcessSpec spec;
  spec.program = options.program;
  spec.arguments = options.arguments;
  spec.working_directory =
      options.working_directory.empty()
          ? std::optional<std::filesystem::path>(options.program.parent_path())
          : std::optional<std::filesystem::path>(options.working_directory);
  spec.output_mode = HostProcessOutputMode::CaptureSeparate;
  spec.hide_window = true;

  const auto host_result = RunHostProcess(spec);
  if (!host_result.launched) {
    result.launch_error = host_result.error_message;
    return result;
  }

  result.launched = true;
  result.exit_code = host_result.exit_code;
  result.stdout_text = host_result.stdout_text;
  result.stderr_text = host_result.stderr_text;
  return result;
}

}  // namespace cursive::core::crash_debug_detail

namespace cursive::core {

std::filesystem::path DefaultCrashReportRoot(
    const std::filesystem::path& output_root) {
  if (output_root.empty()) {
    return crash_debug_detail::TempCrashRoot();
  }
  return output_root / "logs" / "crash";
}

std::filesystem::path DefaultTargetCrashReportRoot(
    const std::filesystem::path& program_path) {
  if (program_path.empty()) {
    return crash_debug_detail::TempCrashRoot();
  }
  const std::filesystem::path parent = program_path.parent_path();
  if (parent.filename() == "bin") {
    return parent.parent_path() / "logs" / "crash";
  }
  return parent / "logs" / "crash";
}

void ConfigureCrashRuntime(const CrashRuntimeOptions& options) {
  std::lock_guard<std::mutex> lock(crash_debug_detail::StateMutex());
  crash_debug_detail::State().options = options;
  if (crash_debug_detail::State().options.report_root.empty()) {
    crash_debug_detail::State().options.report_root =
        crash_debug_detail::TempCrashRoot();
  }
  if (crash_debug_detail::State().options.working_directory.empty()) {
    std::error_code ec;
    crash_debug_detail::State().options.working_directory =
        std::filesystem::current_path(ec).generic_string();
  }
}

void UpdateCrashReportRoot(const std::filesystem::path& report_root) {
  std::lock_guard<std::mutex> lock(crash_debug_detail::StateMutex());
  crash_debug_detail::State().options.report_root =
      report_root.empty() ? crash_debug_detail::TempCrashRoot() : report_root;
}

void SetCrashJsonStdout(bool enabled) {
  std::lock_guard<std::mutex> lock(crash_debug_detail::StateMutex());
  crash_debug_detail::State().options.emit_json_stdout = enabled;
}

void SetCrashEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(crash_debug_detail::StateMutex());
  crash_debug_detail::State().options.enabled = enabled;
}

bool CrashReportingEnabled() {
  std::lock_guard<std::mutex> lock(crash_debug_detail::StateMutex());
  return crash_debug_detail::State().options.enabled;
}

bool CrashCaptureSupported() {
  return crash_debug_detail::CrashCaptureSupportedBackend();
}

void InstallCrashHandlers() {
  crash_debug_detail::InstallCrashHandlersBackend();
}

void MaybeTriggerCrashFixtureFromEnv() {
  crash_debug_detail::MaybeTriggerCrashFixtureFromEnvBackend();
}

std::string CrashReportToJson(const CrashReport& report) {
  std::ostringstream oss;
  oss << "{";
  oss << "\"tool\":\"" << crash_debug_detail::EscapeJson(report.tool) << "\",";
  oss << "\"version\":\"" << crash_debug_detail::EscapeJson(report.version)
      << "\",";
  oss << "\"timestamp_utc\":\""
      << crash_debug_detail::EscapeJson(report.timestamp_utc) << "\",";
  oss << "\"kind\":\"" << crash_debug_detail::EscapeJson(report.kind) << "\",";
  oss << "\"process_id\":" << report.process_id << ",";
  oss << "\"thread_id\":" << report.thread_id << ",";
  oss << "\"exception_code\":" << report.exception_code_value << ",";
  oss << "\"exception_name\":\""
      << crash_debug_detail::EscapeJson(report.exception_name) << "\",";
  oss << "\"message\":\""
      << crash_debug_detail::EscapeJson(report.message) << "\",";
  oss << "\"working_directory\":\""
      << crash_debug_detail::EscapeJson(report.working_directory) << "\",";
  oss << "\"executable_path\":\""
      << crash_debug_detail::EscapeJson(
             crash_debug_detail::PathString(report.executable_path))
      << "\",";
  oss << "\"arguments\":[";
  for (std::size_t i = 0; i < report.arguments.size(); ++i) {
    if (i != 0) {
      oss << ",";
    }
    oss << "\""
        << crash_debug_detail::EscapeJson(report.arguments[i]) << "\"";
  }
  oss << "],";
  oss << "\"artifacts\":{";
  oss << "\"report_dir\":\""
      << crash_debug_detail::EscapeJson(
             crash_debug_detail::PathString(report.artifacts.report_dir))
      << "\",";
  oss << "\"text_path\":\""
      << crash_debug_detail::EscapeJson(
             crash_debug_detail::PathString(report.artifacts.text_path))
      << "\",";
  oss << "\"json_path\":\""
      << crash_debug_detail::EscapeJson(
             crash_debug_detail::PathString(report.artifacts.json_path))
      << "\",";
  oss << "\"minidump_path\":\""
      << crash_debug_detail::EscapeJson(
             crash_debug_detail::PathString(report.artifacts.minidump_path))
      << "\",";
  oss << "\"stdout_path\":\""
      << crash_debug_detail::EscapeJson(
             crash_debug_detail::PathString(report.artifacts.stdout_path))
      << "\",";
  oss << "\"stderr_path\":\""
      << crash_debug_detail::EscapeJson(
             crash_debug_detail::PathString(report.artifacts.stderr_path))
      << "\"";
  oss << "},";
  oss << "\"frames\":[";
  for (std::size_t i = 0; i < report.frames.size(); ++i) {
    if (i != 0) {
      oss << ",";
    }
    const auto& frame = report.frames[i];
    oss << "{";
    oss << "\"index\":" << frame.index << ",";
    oss << "\"module\":\""
        << crash_debug_detail::EscapeJson(frame.module) << "\",";
    oss << "\"module_path\":\""
        << crash_debug_detail::EscapeJson(frame.module_path) << "\",";
    oss << "\"symbol\":\""
        << crash_debug_detail::EscapeJson(frame.symbol) << "\",";
    oss << "\"file\":\""
        << crash_debug_detail::EscapeJson(frame.file) << "\",";
    oss << "\"line\":" << frame.line << ",";
    oss << "\"address\":\""
        << crash_debug_detail::EscapeJson(
               crash_debug_detail::Hex64(frame.address))
        << "\",";
    oss << "\"module_base\":\""
        << crash_debug_detail::EscapeJson(
               crash_debug_detail::Hex64(frame.module_base))
        << "\",";
    oss << "\"module_offset\":" << frame.module_offset << ",";
    oss << "\"offset\":" << frame.offset << ",";
    oss << "\"inline\":" << (frame.inline_frame ? "true" : "false");
    oss << "}";
  }
  oss << "]";
  oss << "}";
  return oss.str();
}

std::string CrashEnvelopeToJson(const CrashReport& report) {
  std::ostringstream oss;
  oss << "{";
  oss << "\"diagnostics\":[],";
  oss << "\"crash\":" << CrashReportToJson(report);
  oss << "}";
  return oss.str();
}

std::string CrashSummary(const CrashReport& report) {
  std::ostringstream oss;
  oss << "fatal: " << report.message;
  if (!report.exception_name.empty()) {
    oss << " (" << report.exception_name;
    if (report.exception_code_value != 0) {
      oss << ", "
          << crash_debug_detail::Hex32(report.exception_code_value);
    }
    oss << ")";
  }
  oss << "\n";
  if (!report.tool.empty()) {
    oss << "tool: " << report.tool;
    if (!report.version.empty()) {
      oss << " " << report.version;
    }
    oss << "\n";
  }
  if (!report.executable_path.empty()) {
    oss << "executable: " << report.executable_path.generic_string() << "\n";
  }
  if (!report.working_directory.empty()) {
    oss << "cwd: " << report.working_directory << "\n";
  }
  if (!report.artifacts.report_dir.empty()) {
    oss << "report_dir: "
        << report.artifacts.report_dir.generic_string() << "\n";
  }
  if (!report.artifacts.json_path.empty()) {
    oss << "report_json: "
        << report.artifacts.json_path.generic_string() << "\n";
  }
  if (!report.artifacts.minidump_path.empty()) {
    oss << "minidump: "
        << report.artifacts.minidump_path.generic_string() << "\n";
  }
  if (!report.arguments.empty()) {
    oss << "args: " << crash_debug_detail::JoinArguments(report.arguments)
        << "\n";
  }
  if (!report.frames.empty()) {
    oss << "stacktrace:\n";
    const std::size_t limit =
        std::min<std::size_t>(report.frames.size(), 32);
    for (std::size_t i = 0; i < limit; ++i) {
      const auto& frame = report.frames[i];
      oss << "  [" << frame.index << "] "
          << (frame.module.empty() ? "<unknown>" : frame.module) << "!"
          << (frame.symbol.empty() ? "<unknown>" : frame.symbol) << " +"
          << crash_debug_detail::HexCompact64(
                 frame.symbol.empty() ? frame.module_offset : frame.offset)
          << " @ " << crash_debug_detail::Hex64(frame.address);
      if (!frame.file.empty() && frame.line != 0) {
        oss << " (" << frame.file << ":" << frame.line << ")";
      }
      oss << "\n";
    }
  }
  return oss.str();
}

DebugRunResult DebugRunProcess(const DebugRunOptions& options) {
  return crash_debug_detail::DebugRunProcessBackend(options);
}

}  // namespace cursive::core
