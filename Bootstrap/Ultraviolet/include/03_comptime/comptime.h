#pragma once

#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

#include "00_core/diagnostics.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::frontend {

struct ComptimeResult {
  std::optional<std::vector<ast::ASTModule>> modules;
  core::DiagnosticStream diags;
};

struct ComptimePassOptions {
  std::filesystem::path project_root;
  std::unordered_map<std::string, std::filesystem::path> source_roots_by_assembly;
  std::optional<std::filesystem::path> fallback_source_root;
};

ComptimeResult ComptimePass(const std::vector<ast::ASTModule>& modules,
                            const ComptimePassOptions& options);

ComptimeResult ComptimePass(const std::vector<ast::ASTModule>& modules,
                            const std::filesystem::path& project_root,
                            const std::filesystem::path& source_root);

ComptimeResult ExecuteComptime(const std::vector<ast::ASTModule>& modules,
                               const ComptimePassOptions& options);

ComptimeResult ExecuteComptime(const std::vector<ast::ASTModule>& modules,
                               const std::filesystem::path& project_root,
                               const std::filesystem::path& source_root);

}  // namespace ultraviolet::frontend
