// =================================================================
// File: 04_analysis/typing/expr/contract_entry.h
// Construct: Contract Entry (@entry) Intrinsic Type Checking
// Spec Section: 5.8
// Spec Rules: @entry intrinsic semantics
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr
{

    // @entry(expr) intrinsic for postcondition contracts
    //
    // TYPING:
    //   InPostcondition = true
    //   Gamma_entry |- expr : T
    //   BitcopyType(T) or CloneType(T)
    //   --------------------------------------------------
    //   Gamma |- @entry(expr) : T
    //
    // REQUIREMENTS:
    //   - Only valid in postcondition context (right of =>)
    //   - Result type must satisfy BitcopyType
    //   - Expression evaluated in entry environment (parameters at call time)
    //
    // USE CASE:
    //   procedure increment(~!) -> i32
    //       |: self.value >= 0 => @result > @entry(self.value)
    //   {
    //       self.value = self.value + 1
    //       return self.value
    //   }

    ExprTypeResult TypeEntryExprImpl(const ScopeContext &ctx,
                                     const StmtTypeContext &type_ctx,
                                     const ast::EntryExpr &expr,
                                     const TypeEnv &env);

} // namespace ultraviolet::analysis::expr
