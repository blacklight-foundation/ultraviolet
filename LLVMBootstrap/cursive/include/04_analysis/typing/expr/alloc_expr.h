// =================================================================
// File: 03_analysis/types/expr/alloc.h
// Construct: Allocation Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Alloc-Explicit, T-Alloc-Implicit, Alloc-Region-NotFound-Err,
//             Alloc-Implicit-NoRegion-Err
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

// §5.2.12 Allocation Expression Typing
ExprTypeResult TypeAllocExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::AllocExpr& expr,
                                 const TypeEnv& env);

}  // namespace cursive::analysis::expr
