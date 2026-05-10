#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

struct ConstLenResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<std::uint64_t> value;
};

ConstLenResult ConstLen(const ScopeContext& ctx, const ast::ExprPtr& expr);

struct TypeEquivResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  bool equiv = false;
};

TypeEquivResult TypeEquiv(const TypeRef& lhs, const TypeRef& rhs);

}  // namespace cursive::analysis
