#pragma once

#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "04_analysis/memory/calls.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

struct CheckResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::string diag_detail;
  std::optional<core::Span> diag_span;
};

struct Constraint {
  TypeRef lhs;
  TypeRef rhs;
  bool requires_subtyping = false;
};

using ConstraintSet = std::vector<Constraint>;

using TypeSubstitution = std::unordered_map<std::uint32_t, TypeRef>;

struct SolveResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeSubstitution subst;
};

using IfCaseCheckFn =
    std::function<CheckResult(const ast::IfCaseExpr&, const TypeRef& expected)>;

using CheckExprFn = std::function<CheckResult(const ast::ExprPtr&, const TypeRef&)>;

using IdentTypeFn = std::function<ExprTypeResult(std::string_view name)>;

ExprTypeResult InferExpr(const ScopeContext& ctx,
                         const ast::ExprPtr& expr,
                         const ExprTypeFn& type_expr,
                         const PlaceTypeFn& type_place,
                         const IdentTypeFn& type_ident);

ExprTypeResult InferExpr(const ScopeContext& ctx,
                         const ast::ExprPtr& expr,
                         const ExprTypeFn& type_expr,
                         const PlaceTypeFn& type_place,
                         const IdentTypeFn& type_ident,
                         ConstraintSet* constraints_out);

ExprTypeResult InferExpr(const ScopeContext& ctx,
                         const ast::ExprPtr& expr,
                         const ExprTypeFn& type_expr,
                         const PlaceTypeFn& type_place,
                         const IdentTypeFn& type_ident,
                         const IfCaseCheckFn& if_case_check);

ExprTypeResult InferExpr(const ScopeContext& ctx,
                         const ast::ExprPtr& expr,
                         const ExprTypeFn& type_expr,
                         const PlaceTypeFn& type_place,
                         const IdentTypeFn& type_ident,
                         const IfCaseCheckFn& if_case_check,
                         ConstraintSet* constraints_out);

CheckResult CheckExpr(const ScopeContext& ctx,
                      const ast::ExprPtr& expr,
                      const TypeRef& expected,
                      const ExprTypeFn& type_expr,
                      const PlaceTypeFn& type_place,
                      const IdentTypeFn& type_ident);

CheckResult CheckExpr(const ScopeContext& ctx,
                      const ast::ExprPtr& expr,
                      const TypeRef& expected,
                      const ExprTypeFn& type_expr,
                      const PlaceTypeFn& type_place,
                      const IdentTypeFn& type_ident,
                      const IfCaseCheckFn& if_case_check);

SolveResult Solve(const ScopeContext& ctx, const ConstraintSet& constraints);
TypeRef ApplySubstitution(const TypeRef& type, const TypeSubstitution& subst);

}  // namespace ultraviolet::analysis
