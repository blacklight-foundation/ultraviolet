#pragma once

#include <optional>

#include "06_driver/codegen_cache.h"
#include "06_driver/output_pipeline.h"

namespace cursive::driver {

std::optional<SharedLibraryExports> ResolveSharedLibraryExports(
    const project::Project& project,
    const CodegenCache& cache);

bool PrepareSharedLibraryCodegenContext(
    const project::Project& project,
    CodegenCache& cache,
    const SharedLibraryExports& exports);

}  // namespace cursive::driver
