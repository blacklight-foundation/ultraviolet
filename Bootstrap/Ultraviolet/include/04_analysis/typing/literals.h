#pragma once

#include <optional>
#include <string_view>

#include "04_analysis/memory/calls.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

struct LiteralCheckResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

ExprTypeResult TypeLiteralExpr(const ScopeContext& ctx,
                               const ast::LiteralExpr& expr);

LiteralCheckResult CheckLiteralExpr(const ScopeContext& ctx,
                                    const ast::LiteralExpr& expr,
                                    const TypeRef& expected);

bool NullLiteralExpected(const TypeRef& expected);

}  // namespace ultraviolet::analysis
