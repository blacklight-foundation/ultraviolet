#pragma once

#include <filesystem>
#include <string>

#include "llvm/Support/JSON.h"

#include "06_driver/tooling/snapshot.h"

namespace cursive::driver::lsp {

llvm::json::Object SemanticTokensFull(
    const std::filesystem::path& path,
    const std::string& text_utf8,
    const tooling::AnalysisSnapshot& snapshot);

llvm::json::Object SemanticTokensFull(
    const std::filesystem::path& path,
    const std::string& text_utf8,
    const tooling::AnalysisSnapshot* snapshot);

}  // namespace cursive::driver::lsp
