// =================================================================
// File: 03_analysis/types/expr/unary.h
// Construct: Unary Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Not-Bool, T-Not-Int, T-Neg, T-Modal-Widen
// =================================================================
#pragma once

#include "00_core/span.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

// §5.2.12 Unary Expression Typing
// Handles: ! (logical not, bitwise not), - (negation), widen
ExprTypeResult TypeUnaryExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::UnaryExpr& expr,
                                 const TypeEnv& env,
                                 const core::Span& span = core::Span{});

}  // namespace cursive::analysis::expr
