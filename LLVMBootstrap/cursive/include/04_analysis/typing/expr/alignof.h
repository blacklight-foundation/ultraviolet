// =================================================================
// File: 03_analysis/types/expr/alignof.h
// Construct: Alignof Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Alignof
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

// Type check an alignof expression
ExprTypeResult TypeAlignofExprImpl(const ScopeContext& ctx,
                                   const ast::AlignofExpr& expr);

}  // namespace cursive::analysis::expr
