#pragma once

// =============================================================================
// Dispatch Expression Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 10.5 (Dispatch - Data Parallelism)
//   - dispatch i in range [opts] { body }
//   - Reduction operators: +, *, min, max, and, or, custom
//   - Options: reduce, ordered, chunk
//
// =============================================================================

#include "05_codegen/lower/lower_expr.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::codegen {

// LowerDispatchExpr - Lower a dispatch expression to IR
//
// Dispatch expression: dispatch pattern in range [opts] { body }
//
// Steps:
//   1. Lower range expression
//   2. Collect captures for body
//   3. Build capture environment tuple
//   4. Handle reduce options (Add, Mul, Min, Max, And, Or, Custom)
//   5. For custom reduce, generate reduce wrapper procedure
//   6. Generate body wrapper procedure with params: elem ptr, env ptr, result ptr, panic out
//   7. Create IRDispatch with pattern, range, body_fn, env, reduce info, chunk size, ordered flag
//
// Parameters:
//   node - The dispatch expression AST node
//   ctx  - Lowering context
//
// Returns:
//   LowerResult containing the IR sequence and result value
LowerResult LowerDispatchExpr(const ast::DispatchExpr& node, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
