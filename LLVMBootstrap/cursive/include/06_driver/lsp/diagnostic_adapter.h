#pragma once

#include <filesystem>

#include "llvm/Support/JSON.h"

#include "06_driver/tooling/document_store.h"
#include "06_driver/tooling/snapshot.h"

namespace cursive::driver::lsp {

llvm::json::Array DiagnosticsForPath(
    const tooling::AnalysisSnapshot& snapshot,
    const tooling::DocumentStore& documents,
    const std::filesystem::path& path);

}  // namespace cursive::driver::lsp
