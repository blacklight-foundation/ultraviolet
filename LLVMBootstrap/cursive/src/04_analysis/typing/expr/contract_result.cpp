// =================================================================
// File: 04_analysis/typing/expr/contract_result.cpp
// Construct: Contract Result (@result) Intrinsic Type Checking
// Spec Section: 5.8
// Spec Rules: @result intrinsic semantics
// =================================================================
//
// @RESULT INTRINSIC (@result):
//   1. Valid only in postcondition context (right of =>)
//   2. Type is the procedure's return type
//   3. References the value about to be returned
//
// TYPING:
//   InPostcondition = true
//   ReturnType(procedure) = T
//   --------------------------------------------------
//   Gamma |- @result : T
//
// CONTEXT REQUIREMENTS:
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
//
// EVALUATION:
//   - @result evaluated after procedure body completes
//   - Before actual return to caller
//   - Represents the computed return value
//
// =================================================================

#include "04_analysis/typing/expr/contract_result.h"

#include "00_core/assert_spec.h"
#include "04_analysis/typing/types.h"

namespace cursive::analysis::expr
{

  namespace
  {

    static inline void SpecDefsContractResult()
    {
      SPEC_DEF("@result-Intrinsic", "5.8");
      SPEC_DEF("@result-Context", "5.8");
    }

  } // namespace

  ExprTypeResult TypeResultExprImpl(const ScopeContext & /*ctx*/,
                                    const StmtTypeContext &type_ctx,
                                    const ast::ResultExpr & /*expr*/,
                                    const TypeEnv & /*env*/)
  {
    SpecDefsContractResult();
    ExprTypeResult result;

    // @result only valid in postcondition context
    if (type_ctx.contract_phase != ContractPhase::Postcondition)
    {
      SPEC_RULE("@result-Context");
      result.diag_id = "E-SEM-2806"; // @result outside postcondition
      return result;
    }

    SPEC_RULE("@result-Intrinsic");
    result.ok = true;
    // Return type from the typing context, or unit if not specified
    result.type = type_ctx.return_type ? type_ctx.return_type : MakeTypePrim("()");
    return result;
  }

} // namespace cursive::analysis::expr
