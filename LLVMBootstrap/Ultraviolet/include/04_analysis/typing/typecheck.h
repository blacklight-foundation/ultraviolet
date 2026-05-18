#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "00_core/diagnostics.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/memory/init_planner.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

struct TypecheckResult {
  bool ok = false;
  core::DiagnosticStream diags;
  ExprTypeMap expr_types;
  DynamicRefineExprMap dynamic_refine_checks;
  GenericCallSubstMap generic_call_substs;
  SelectedCallTargetMap selected_call_targets;
  std::optional<InitPlan> init_plan;
};

TypecheckResult TypecheckModules(ScopeContext& ctx,
                                 const std::vector<ast::ASTModule>& modules,
                                 const NameMapTable* precomputed_name_maps =
                                     nullptr);

core::DiagnosticStream ValidateComptimeProcedureSignatures(
    ScopeContext& ctx,
    const std::vector<ast::ASTModule>& modules);

}  // namespace ultraviolet::analysis
