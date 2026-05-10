// =================================================================
// File: 03_analysis/types/expr/range.h
// Construct: Range Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: Range-Full, Range-To, Range-ToInclusive, Range-From,
//             Range-Exclusive, Range-Inclusive
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

// §5.2.12 Range Expression Typing
// Handles: .., ..n, ..=n, n.., n..m, n..=m
ExprTypeResult TypeRangeExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::RangeExpr& expr,
                                 const TypeEnv& env);

}  // namespace cursive::analysis::expr
