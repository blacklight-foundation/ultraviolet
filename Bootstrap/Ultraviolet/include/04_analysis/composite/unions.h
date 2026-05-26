#pragma once

#include <optional>
#include <string_view>

#include "04_analysis/memory/calls.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

struct UnionAccessResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

ExprTypeResult TypeUnionIntro(const ScopeContext& ctx,
                              const TypeRef& value_type,
                              const TypeRef& union_type);

UnionAccessResult CheckUnionDirectAccess(const ScopeContext& ctx,
                                         const ast::FieldAccessExpr& expr,
                                         const ExprTypeFn& type_expr);

}  // namespace ultraviolet::analysis
