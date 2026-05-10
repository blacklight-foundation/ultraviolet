#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"

namespace cursive::analysis {

bool FfiByValueOk(const ScopeContext& ctx, const TypeRef& type);

}  // namespace cursive::analysis
