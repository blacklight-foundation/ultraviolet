#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

struct PlaceTypeResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
  std::string diag_detail;
};

using PlaceTypeFn = std::function<PlaceTypeResult(const ast::ExprPtr&)>;

}  // namespace cursive::analysis
