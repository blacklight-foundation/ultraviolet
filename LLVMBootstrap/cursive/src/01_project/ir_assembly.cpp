#include "01_project/ir_assembly.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/crash_debug.h"
#include "00_core/host/services.h"
#include "00_core/host_primitives.h"

namespace cursive::project {

namespace {

std::filesystem::path MakeTempAsmPath(std::string_view extension) {
  static std::atomic<unsigned long long> counter{0};
  const auto stamp = std::chrono::high_resolution_clock::now()
                         .time_since_epoch()
                         .count();
  const auto id = counter.fetch_add(1, std::memory_order_relaxed);
  auto path = std::filesystem::temp_directory_path() /
              ("cursive-llvm-as-" + std::to_string(stamp) + "-" +
               std::to_string(id));
  path.replace_extension(std::string(extension));
  return path;
}

bool WriteBytes(const std::filesystem::path& path, std::string_view bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return out.good();
}

std::optional<std::string> ReadBytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  std::string bytes((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
  if (!in.good() && !in.eof()) {
    return std::nullopt;
  }
  return bytes;
}

void RemoveTempFile(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

bool RunAssemblerProcess(const std::filesystem::path& tool,
                         const std::filesystem::path& input_path,
                         const std::filesystem::path& output_path) {
  if (core::CrashReportingEnabled() && core::CrashCaptureSupported()) {
    core::DebugRunOptions options;
    options.program = tool;
    options.working_directory = output_path.parent_path();
    options.report_root = core::DefaultTargetCrashReportRoot(output_path);
    options.tool_name = "cursive-llvm-as";
    options.arguments.push_back(input_path.generic_string());
    options.arguments.push_back("-o");
    options.arguments.push_back(output_path.generic_string());
    const auto result = core::DebugRunProcess(options);
    return result.launched && result.exit_code == 0;
  }
  core::HostProcessSpec spec;
  spec.program = tool;
  spec.arguments.push_back(input_path.generic_string());
  spec.arguments.push_back("-o");
  spec.arguments.push_back(output_path.generic_string());
  spec.hide_window = true;
  const auto result = core::RunHostProcess(spec);
  return result.launched && result.exit_code == 0;
}

std::optional<std::string> AssembleIRViaTool(const std::filesystem::path& tool,
                                             std::string_view ir_text) {
  const auto input_path = MakeTempAsmPath(".ll");
  const auto output_path = MakeTempAsmPath(".bc");

  if (!WriteBytes(input_path, ir_text)) {
    RemoveTempFile(input_path);
    RemoveTempFile(output_path);
    return std::nullopt;
  }

  if (!RunAssemblerProcess(tool, input_path, output_path)) {
    RemoveTempFile(input_path);
    RemoveTempFile(output_path);
    return std::nullopt;
  }

  auto bytes = ReadBytes(output_path);
  RemoveTempFile(input_path);
  RemoveTempFile(output_path);
  return bytes;
}

}  // namespace

std::optional<std::string> AssembleIRWithDeps(const std::filesystem::path& tool,
                                              std::string_view ir_text,
                                              InvokeFn invoke) {
  std::optional<std::string> bytes;
  if (invoke != nullptr) {
    bytes = invoke(tool, ir_text);
  } else {
    bytes = AssembleIRViaTool(tool, ir_text);
  }
  if (!bytes.has_value()) {
    core::HostPrimFail(core::HostPrim::AssembleIR, true);
    SPEC_RULE("AssembleIR-Err");
    return std::nullopt;
  }
  SPEC_RULE("AssembleIR-Ok");
  return bytes;
}

std::optional<std::string> AssembleIR(const std::filesystem::path& tool,
                                      std::string_view ir_text) {
  return AssembleIRWithDeps(tool, ir_text, nullptr);
}

}  // namespace cursive::project
