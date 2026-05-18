// =================================================================
// File: 04_analysis/typing/expr/call_type_args.h
// Construct: Call With Type Arguments Expression Type Checking
// Spec Section: 9.4, 13.1.2
// Spec Rules: T-Generic-Call, CallTypeArgs elaboration
// =================================================================
#pragma once

#include <optional>
#include <vector>

#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// Build type substitution for generic procedure call with explicit type arguments
// Returns nullopt if callee is not a generic procedure or arguments don't match
std::optional<TypeSubst> BuildCallTypeArgsSubst(
    const ScopeContext& ctx,
    const ast::ExprPtr& callee,
    const std::vector<std::shared_ptr<ast::Type>>& type_args);

// Type check a call expression with explicit type arguments
// callee<T1, T2, ...>(args)
ExprTypeResult TypeCallTypeArgsExprImpl(const ScopeContext& ctx,
                                        const StmtTypeContext& type_ctx,
                                        const ast::CallTypeArgsExpr& expr,
                                        const TypeEnv& env);

}  // namespace ultraviolet::analysis::expr
