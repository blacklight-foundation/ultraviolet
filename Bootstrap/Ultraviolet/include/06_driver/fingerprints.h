#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "00_core/diagnostics.h"
#include "01_project/module_discovery.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::driver {

std::optional<std::string> ComputeFileSourceHash(
    const std::filesystem::path& file,
    core::DiagnosticStream& diags);

std::optional<std::string> HashFileBytes(const std::filesystem::path& file);

std::string HashBytes(std::string_view bytes);

std::string LinkInputFingerprintField(const std::filesystem::path& path);

bool IsExternalDependencyMarker(std::string_view dep);

std::string HashFields(const std::vector<std::string>& fields);

std::optional<std::string> ComputeModuleSourceHash(
    const project::ModuleInfo& module,
    core::DiagnosticStream& diags);

std::unordered_map<std::string, std::vector<std::string>> BuildModuleDeps(
    const std::vector<ast::ASTModule>& modules,
    const std::unordered_set<std::string>& known_modules);

}  // namespace ultraviolet::driver
