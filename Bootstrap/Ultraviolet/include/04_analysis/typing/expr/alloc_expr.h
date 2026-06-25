// =================================================================
// File: 03_analysis/types/expr/alloc.h
// Construct: Internal Allocation Expression Type Checking
// Spec Section: 16.8.4
// Spec Rules: T-Internal-Alloc-Explicit, T-Internal-Alloc-Implicit, Alloc-Region-NotFound-Err,
//             Alloc-Implicit-NoRegion-Err
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// §16.8.4 Internal Allocation Expression Typing
ExprTypeResult TypeAllocExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::AllocExpr& expr,
                                 const TypeEnv& env);

}  // namespace ultraviolet::analysis::expr
