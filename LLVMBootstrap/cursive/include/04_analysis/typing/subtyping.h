#pragma once

#include <optional>
#include <string_view>

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"

namespace cursive::analysis {

struct SubtypingResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  bool subtype = false;
};

SubtypingResult Subtyping(const ScopeContext& ctx,
                          const TypeRef& lhs,
                          const TypeRef& rhs);

bool PermissionAdmits(Permission caller, Permission required);

SubtypingResult ArgumentTypeCompatible(const ScopeContext& ctx,
                                       const TypeRef& actual,
                                       const TypeRef& expected,
                                       const std::optional<ParamMode>& mode);

}  // namespace cursive::analysis
