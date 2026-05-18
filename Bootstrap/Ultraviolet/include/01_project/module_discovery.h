#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/diagnostics.h"
#include "01_project/language_profile.h"

namespace ultraviolet::project {

struct ModuleInfo {
  std::string path;
  std::filesystem::path dir;
};

struct ModulesResult {
  std::vector<ModuleInfo> modules;
  core::DiagnosticStream diags;
};

struct CompilationUnitResult {
  std::vector<std::filesystem::path> files;
  core::DiagnosticStream diags;
};

CompilationUnitResult CompilationUnit(const std::filesystem::path& module_dir);
CompilationUnitResult CompilationUnit(const std::filesystem::path& module_dir,
                                      const LanguageProfile& language);

ModulesResult Modules(const std::filesystem::path& source_root,
                      std::string_view assembly_name);
ModulesResult Modules(const std::filesystem::path& source_root,
                      std::string_view assembly_name,
                      const LanguageProfile& language);

}  // namespace ultraviolet::project
