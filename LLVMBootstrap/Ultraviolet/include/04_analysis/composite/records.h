#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "04_analysis/memory/calls.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

struct RecordWfResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

RecordWfResult CheckRecordWf(const ScopeContext& ctx,
                             const ast::RecordDecl& record,
                             const ExprTypeFn& type_expr);

ExprTypeResult TypeRecordDefaultCall(const ScopeContext& ctx,
                                     const ast::ExprPtr& callee,
                                     const std::vector<ast::Arg>& args,
                                     const ExprTypeFn& type_expr);

}  // namespace ultraviolet::analysis
