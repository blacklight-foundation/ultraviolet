#pragma once

#include <filesystem>

#include "llvm/Support/JSON.h"

#include "06_driver/tooling/document_store.h"
#include "06_driver/tooling/snapshot.h"

namespace ultraviolet::driver::lsp {

llvm::json::Array DiagnosticsForPath(
    const tooling::AnalysisSnapshot& snapshot,
    const tooling::DocumentStore& documents,
    const std::filesystem::path& path);

}  // namespace ultraviolet::driver::lsp
