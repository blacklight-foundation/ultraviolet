#pragma once

#include <optional>

#include "02_source/ast/ast.h"
#include "04_analysis/typing/context.h"

namespace ultraviolet::analysis {

std::optional<bool> ExpressionResultHasResponsibility(
    const ScopeContext& ctx,
    const ast::ExprPtr& expr);

std::optional<bool> CallResultHasResponsibility(const ScopeContext& ctx,
                                                const ast::CallExpr& call);

std::optional<bool> MethodCallResultHasResponsibility(
    const ScopeContext& ctx,
    const ast::MethodCallExpr& call);

std::optional<bool> ProcedureReturnHasResponsibility(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& decl);

std::optional<bool> MethodReturnHasResponsibility(const ScopeContext& ctx,
                                                  const ast::MethodDecl& decl);

std::optional<bool> StateMethodReturnHasResponsibility(
    const ScopeContext& ctx,
    const ast::StateMethodDecl& decl);

std::optional<bool> ClassMethodReturnHasResponsibility(
    const ScopeContext& ctx,
    const ast::ClassMethodDecl& decl);

}  // namespace ultraviolet::analysis
