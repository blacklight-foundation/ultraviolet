#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ultraviolet::core {

struct CrashFrame {
  std::size_t index = 0;
  std::string module;
  std::string module_path;
  std::string symbol;
  std::string file;
  std::uint32_t line = 0;
  std::uint64_t address = 0;
  std::uint64_t module_base = 0;
  std::uint64_t module_offset = 0;
  std::uint64_t offset = 0;
  bool inline_frame = false;
};

struct CrashArtifacts {
  std::filesystem::path report_dir;
  std::filesystem::path text_path;
  std::filesystem::path json_path;
  std::filesystem::path minidump_path;
  std::filesystem::path stdout_path;
  std::filesystem::path stderr_path;
};

struct CrashReport {
  std::string tool;
  std::string version;
  std::string timestamp_utc;
  std::string kind;
  std::uint32_t process_id = 0;
  std::uint32_t thread_id = 0;
  std::uint32_t exception_code_value = 0;
  std::string exception_name;
  std::string message;
  std::vector<std::string> arguments;
  std::string working_directory;
  std::filesystem::path executable_path;
  CrashArtifacts artifacts;
  std::vector<CrashFrame> frames;
};

struct CrashRuntimeOptions {
  bool enabled = true;
  bool write_minidump = true;
  bool emit_stderr_summary = true;
  bool emit_json_stdout = false;
  std::size_t max_frames = 128;
  std::filesystem::path report_root;
  std::string tool_name;
  std::string tool_version;
  std::vector<std::string> arguments;
  std::string working_directory;
  std::filesystem::path executable_path;
};

struct DebugRunOptions {
  bool enabled = true;
  bool write_minidump = true;
  bool emit_stderr_summary = true;
  std::size_t max_frames = 128;
  std::filesystem::path program;
  std::vector<std::string> arguments;
  std::filesystem::path working_directory;
  std::filesystem::path report_root;
  std::string tool_name;
  std::string tool_version;
};

struct DebugRunResult {
  bool launched = false;
  bool crashed = false;
  int exit_code = -1;
  std::string launch_error;
  std::string stdout_text;
  std::string stderr_text;
  std::optional<CrashReport> crash_report;
};

void ConfigureCrashRuntime(const CrashRuntimeOptions& options);
void UpdateCrashReportRoot(const std::filesystem::path& report_root);
void SetCrashJsonStdout(bool enabled);
void SetCrashEnabled(bool enabled);
bool CrashReportingEnabled();
bool CrashCaptureSupported();
void InstallCrashHandlers();
void MaybeTriggerCrashFixtureFromEnv();

std::filesystem::path DefaultCrashReportRoot(
    const std::filesystem::path& output_root);
std::filesystem::path DefaultTargetCrashReportRoot(
    const std::filesystem::path& program_path);

std::string CrashReportToJson(const CrashReport& report);
std::string CrashEnvelopeToJson(const CrashReport& report);
std::string CrashSummary(const CrashReport& report);

DebugRunResult DebugRunProcess(const DebugRunOptions& options);

}  // namespace ultraviolet::core
