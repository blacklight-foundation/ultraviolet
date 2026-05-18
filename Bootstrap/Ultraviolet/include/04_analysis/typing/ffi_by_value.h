#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis {

bool FfiByValueOk(const ScopeContext& ctx, const TypeRef& type);

}  // namespace ultraviolet::analysis
