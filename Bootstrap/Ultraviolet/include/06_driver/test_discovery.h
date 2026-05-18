#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "00_core/span.h"
#include "01_project/project.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::driver {

enum class SourceNativeTestScopeKind {
  AllTests,
  AssemblyTests,
  ModuleTests,
  SourceFileTests,
  DirectoryTests,
};

struct SourceNativeTestScope {
  SourceNativeTestScopeKind kind = SourceNativeTestScopeKind::AllTests;
  std::string assembly_name;
  ast::ModulePath module_path;
  std::filesystem::path path;
};

struct SourceNativeTestTargetResolution {
  std::optional<SourceNativeTestScope> scope;
  std::string unknown_target;
};

struct SourceNativeTestDescriptor {
  std::string assembly_name;
  ast::ModulePath module_path;
  std::string procedure_name;
  std::string stable_identity;
  std::string display_name;
  std::filesystem::path source_file;
  std::size_t source_file_order = 0;
  bool requires_context = false;
  std::vector<std::string> coverage_references;
  core::Span span;
  std::size_t declaration_order = 0;
};

struct SourceNativeTestDiscoveryResult {
  std::vector<SourceNativeTestDescriptor> tests;
};

SourceNativeTestDiscoveryResult DiscoverSourceNativeTests(
    std::string_view assembly_name,
    const std::vector<ast::ASTModule>& modules);

SourceNativeTestTargetResolution ResolveSourceNativeTestTarget(
    const project::Project& project,
    const std::filesystem::path& current_directory,
    const std::optional<std::string>& target);

std::vector<SourceNativeTestDescriptor> SelectSourceNativeTests(
    const project::Project& project,
    const SourceNativeTestScope& scope,
    const std::vector<SourceNativeTestDescriptor>& tests);

}  // namespace ultraviolet::driver
