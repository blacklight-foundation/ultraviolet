#pragma once

#include "00_core/crash_debug.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cursive::core::crash_debug_detail {

struct RuntimeState {
  CrashRuntimeOptions options;
  bool installed = false;
};

RuntimeState& State();
std::mutex& StateMutex();
std::atomic<bool>& HandlingCrash();
CrashRuntimeOptions CrashOptionsSnapshot();

std::string EscapeJson(std::string_view value);
std::string NowUtcString();
std::string TimestampFileStem();
std::string PathString(const std::filesystem::path& path);
std::filesystem::path TempCrashRoot();
void EnsureDirectory(const std::filesystem::path& path);
void WriteTextFile(const std::filesystem::path& path, std::string_view text);
std::string JoinArguments(const std::vector<std::string>& args);
std::string Hex32(std::uint32_t value);
std::string Hex64(std::uint64_t value);
std::string HexCompact64(std::uint64_t value);
std::string Trim(std::string_view text);
std::optional<std::uint64_t> ParseHexU64(std::string_view text);

CrashArtifacts MakeArtifacts(const std::filesystem::path& root,
                             std::string_view kind,
                             std::uint32_t process_id);
CrashReport BuildCrashReport(const CrashRuntimeOptions& options,
                             std::string_view kind,
                             std::uint32_t process_id,
                             std::uint32_t thread_id,
                             std::uint32_t exception_code,
                             std::string exception_name,
                             std::string message,
                             const CrashArtifacts& artifacts,
                             std::vector<CrashFrame> frames);
void EmitCrashOutputs(const CrashRuntimeOptions& options,
                      const CrashReport& report);
DebugRunResult RunProcessWithoutDebugger(const DebugRunOptions& options);

bool CrashCaptureSupportedBackend();
void InstallCrashHandlersBackend();
void MaybeTriggerCrashFixtureFromEnvBackend();
DebugRunResult DebugRunProcessBackend(const DebugRunOptions& options);

}  // namespace cursive::core::crash_debug_detail
