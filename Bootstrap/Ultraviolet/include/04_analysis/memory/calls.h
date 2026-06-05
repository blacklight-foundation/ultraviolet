#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/generics/monomorphize.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

struct ExprTypeResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
  std::string diag_detail;
  std::optional<core::Span> diag_span;
  std::vector<std::string_view> diagnostic_obligation_ids;
};

using ExprTypeFn = std::function<ExprTypeResult(const ast::ExprPtr&)>;

struct ArgCheckResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::string diag_detail;
  std::optional<core::Span> diag_span;
};

using ArgCheckFn =
    std::function<ArgCheckResult(const ast::ExprPtr&, const TypeRef&)>;

struct CallTypeResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
  bool record_callee = false;
  std::string diag_detail;
  std::optional<core::Span> diag_span;
};

struct CallCalleeFacts {
  bool extern_callee = false;
  bool extern_callee_known = false;
  ExprTypeResult callee_type;
  bool callee_type_known = false;
};

// Call-argument helpers shared across typing/borrow/lowering.
bool IsPlaceExprForCall(const ast::ExprPtr& expr);
bool HasSourceProvenance(const ast::ExprPtr& expr);
bool MissingRequiredMoveForConsuming(const std::optional<ParamMode>& mode,
                                     const ast::Arg& arg);
bool UsesCallTempForConsuming(const std::optional<ParamMode>& mode,
                              const ast::Arg& arg);
ast::ExprPtr ArgPassExpr(const ast::Arg& arg);

CallTypeResult TypeCall(const ScopeContext& ctx,
                        const ast::ExprPtr& callee,
                        const std::vector<ast::Arg>& args,
                        const ExprTypeFn& type_expr,
                        const PlaceTypeFn* type_place = nullptr,
                        const ArgCheckFn* check_expr = nullptr,
                        const CallCalleeFacts* callee_facts = nullptr);

// Type check a generic procedure call with type substitution (§13.1.2 T-Generic-Call)
CallTypeResult TypeCallWithSubst(const ScopeContext& ctx,
                                 const ast::ExprPtr& callee,
                                 const std::vector<ast::Arg>& args,
                                 const TypeSubst& subst,
                                 const ExprTypeFn& type_expr,
                                 const PlaceTypeFn* type_place = nullptr,
                                 const ArgCheckFn* check_expr = nullptr,
                                 const CallCalleeFacts* callee_facts = nullptr);

bool IsRecordCallee(const ScopeContext& ctx,
                    const ast::ExprPtr& callee,
                    const std::vector<ast::Arg>& args);

}  // namespace ultraviolet::analysis
