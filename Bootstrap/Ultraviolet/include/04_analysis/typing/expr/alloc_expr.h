// =================================================================
// File: 03_analysis/types/expr/alloc.h
// Construct: Region Allocation Expression Type Checking
// Spec Section: 16.8.4
// Spec Rules: T-Internal-Alloc-Explicit, T-New-CurrentRegion, Alloc-Region-NotFound-Err,
//             New-NoActiveRegion-Err
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// §16.8.4 Region Allocation Expression Typing
ExprTypeResult TypeAllocExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::AllocExpr& expr,
                                 const TypeEnv& env);

}  // namespace ultraviolet::analysis::expr
