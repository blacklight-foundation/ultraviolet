// =================================================================
// File: 03_analysis/types/expr/transmute.h
// Construct: Transmute Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Transmute, T-Transmute-SizeEq, T-Transmute-AlignEq,
//             Transmute-Unsafe-Err
// =================================================================
#pragma once

#include "00_core/span.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// §5.2.12 Transmute Expression Typing
ExprTypeResult TypeTransmuteExprImpl(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::TransmuteExpr& expr,
                                     const TypeEnv& env,
                                     const core::Span& span);

void EmitInvalidTransmuteTargetWarningsInBlock(const ScopeContext& ctx,
                                               const StmtTypeContext& type_ctx,
                                               const ast::Block& block,
                                               const TypeEnv& env);

}  // namespace ultraviolet::analysis::expr
