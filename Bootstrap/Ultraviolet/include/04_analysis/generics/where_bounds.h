#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis {

bool CheckClassBound(
    const ScopeContext& ctx,
    const TypeRef& type,
    const TypePath& class_path);

}  // namespace ultraviolet::analysis
