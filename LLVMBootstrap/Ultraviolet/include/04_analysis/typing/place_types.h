#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "00_core/span.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

struct PlaceTypeResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
  std::string diag_detail;
  std::optional<core::Span> diag_span;
};

using PlaceTypeFn = std::function<PlaceTypeResult(const ast::ExprPtr&)>;

}  // namespace ultraviolet::analysis
