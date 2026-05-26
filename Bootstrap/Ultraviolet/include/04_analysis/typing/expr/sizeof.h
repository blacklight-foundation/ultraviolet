// =================================================================
// File: 03_analysis/types/expr/sizeof.h
// Construct: Sizeof Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Sizeof
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// Type check a sizeof expression
ExprTypeResult TypeSizeofExprImpl(const ScopeContext& ctx,
                                  const ast::SizeofExpr& expr);

}  // namespace ultraviolet::analysis::expr
