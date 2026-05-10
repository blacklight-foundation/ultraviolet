#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "00_core/span.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

struct BindSelfParam {
  TypeRef type;
  std::optional<ParamMode> mode;
  std::optional<Permission> recv_perm;  // Receiver permission (~, ~!, ~%)
};

struct BindCheckResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
};

BindCheckResult BindCheckBody(const ScopeContext& ctx,
                              const ast::ModulePath& module_path,
                              const std::vector<ast::Param>& params,
                              const std::shared_ptr<ast::Block>& body,
                              const std::optional<BindSelfParam>& self_param);

// Debug-only profiling summary for borrow binding checks.
void LogBorrowBindPerfSummary();

}  // namespace cursive::analysis
