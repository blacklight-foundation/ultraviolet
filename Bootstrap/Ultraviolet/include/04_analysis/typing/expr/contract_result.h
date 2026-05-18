// =================================================================
// File: 04_analysis/typing/expr/contract_result.h
// Construct: Contract Result (@result) Intrinsic Type Checking
// Spec Section: 5.8
// Spec Rules: @result intrinsic semantics
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr
{

    // @result intrinsic for postcondition contracts
    //
    // TYPING:
    //   InPostcondition = true
    //   ReturnType(procedure) = T
    //   --------------------------------------------------
    //   Gamma |- @result : T
    //
    // REQUIREMENTS:
    //   - ONLY valid in postcondition context (right of =>)
    //   - Cannot be used in preconditions (left of => or alone)
    //   - References the actual return value
    //
    // USE CASE:
    //   procedure abs(x: i32) -> i32
    //       |: => @result >= 0
    //   {
    //       return if x < 0 { -x } else { x }
    //   }

    ExprTypeResult TypeResultExprImpl(const ScopeContext &ctx,
                                      const StmtTypeContext &type_ctx,
                                      const ast::ResultExpr &expr,
                                      const TypeEnv &env);

} // namespace ultraviolet::analysis::expr
