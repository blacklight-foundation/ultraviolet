#pragma once

#include <optional>
#include <string_view>

#include "04_analysis/memory/calls.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

ExprTypeResult CoerceArrayToSlice(const ScopeContext& ctx,
                                  const TypeRef& type);

}  // namespace cursive::analysis
