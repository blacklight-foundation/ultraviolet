#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "00_core/diagnostics.h"
#include "00_core/source_load.h"
#include "01_project/module_discovery.h"
#include "02_source/parser/parser.h"

namespace ultraviolet::frontend {

using UnsafeSpanMap = std::unordered_map<std::string, std::vector<core::Span>>;

struct ReadBytesResult {
  std::optional<std::vector<std::uint8_t>> bytes;
  core::DiagnosticStream diags;
};

struct InspectResult {
  core::DiagnosticStream diags;
};

struct ParseFileTelemetry {
  std::string module_path;
  std::filesystem::path file_path;
  std::size_t byte_count = 0;
  std::int64_t read_ms = 0;
  std::int64_t load_ms = 0;
  std::int64_t inspect_ms = 0;
  std::int64_t parse_ms = 0;
  std::int64_t total_ms = 0;
  std::size_t diagnostics = 0;
  bool ok = false;
};

struct ParseModuleTelemetry {
  std::string module_path;
  std::filesystem::path module_dir;
  std::size_t file_count = 0;
  std::size_t byte_count = 0;
  std::int64_t compilation_unit_ms = 0;
  std::int64_t total_ms = 0;
  std::size_t diagnostics = 0;
  bool ok = false;
};

struct ParseModuleDeps {
  project::CompilationUnitResult (*compilation_unit)(
      const std::filesystem::path& module_dir);
  ReadBytesResult (*read_bytes)(const std::filesystem::path& path);
  core::SourceLoadResult (*load_source)(
      std::string_view path,
      const std::vector<std::uint8_t>& bytes);
  ast::ParseFileResult (*parse_file)(const core::SourceFile& source);
  core::DiagnosticStream (*inspect_source)(const core::SourceFile& source) =
      nullptr;
  std::function<void(const ParseFileTelemetry&)> file_telemetry;
  std::function<void(const ParseModuleTelemetry&)> module_telemetry;
  bool parallel_modules = false;
};

struct ParseModuleResult {
  std::optional<ast::ASTModule> module;
  UnsafeSpanMap unsafe_spans_by_file;
  core::DiagnosticStream diags;
};

struct ParseModulesResult {
  std::optional<std::vector<ast::ASTModule>> modules;
  UnsafeSpanMap unsafe_spans_by_file;
  core::DiagnosticStream diags;
};

ReadBytesResult ReadBytesDefault(const std::filesystem::path& path);

ParseModuleResult ParseModuleWithDeps(std::string_view module_path,
                                      const std::filesystem::path& source_root,
                                      std::string_view assembly_name,
                                      const ParseModuleDeps& deps);

ParseModulesResult ParseModulesWithDeps(
    const std::vector<project::ModuleInfo>& modules,
    const std::filesystem::path& source_root,
    std::string_view assembly_name,
    const ParseModuleDeps& deps);

ParseModuleResult ParseModule(std::string_view module_path,
                              const std::filesystem::path& source_root,
                              std::string_view assembly_name);

ParseModulesResult ParseModules(const std::vector<project::ModuleInfo>& modules,
                                const std::filesystem::path& source_root,
                                std::string_view assembly_name);

}  // namespace ultraviolet::frontend
