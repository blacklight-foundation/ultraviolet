// =================================================================
// File: 03_analysis/types/expr/call.h
// Construct: Call Expression Type Checking
// Spec Section: 5.2.12, 13.1.2
// Spec Rules: T-Call, T-Generic-Call
// =================================================================
#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

struct GenericCallSubstResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> diag_span;
  TypeSubst subst;
};

// Build substitution map for a generic procedure call (13.1.2)
std::optional<TypeSubst> BuildGenericCallSubst(
    const ScopeContext& ctx,
    const ast::ExprPtr& callee,
    const std::vector<std::shared_ptr<ast::Type>>& generic_args);

// Infer substitution map for generic call arguments.
// expected_return participates in bidirectional inference when provided.
GenericCallSubstResult InferGenericCallSubst(
    const ScopeContext& ctx,
    const ast::ExprPtr& callee,
    const std::vector<ast::Arg>& args,
    const std::optional<TypeRef>& expected_return,
    const ExprTypeFn& type_expr,
    const PlaceTypeFn* type_place = nullptr);

// Persist the analysis-elaborated generic call substitution for codegen.
void RecordGenericCallSubst(const ScopeContext& ctx,
                            const ast::CallExpr& call,
                            const TypeSubst& subst);

// Type check a call expression
ExprTypeResult TypeCallExprImpl(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::CallExpr& expr,
                                const TypeEnv& env);

// Debug-only profiling summary for call lookup/typechecking hot paths.
void LogCallLookupPerfSummary();

}  // namespace cursive::analysis::expr

