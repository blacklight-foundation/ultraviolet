#pragma once

#include "05_codegen/lower/lower_expr.h"

namespace ultraviolet::codegen {

// =============================================================================
// LowerParallelExpr - Lower a parallel expression
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 10 (Structured Parallelism)
//   - Rule: Lower-Expr-Parallel
//
// Lowers a parallel expression which consists of:
//   - A domain expression ($ExecutionDomain)
//   - Optional options (cancel token, name)
//   - A body block containing spawn/dispatch expressions
//
// The function handles:
//   1. Lowering the domain expression
//   2. Setting up parallel_collect for collectable spawn/dispatch expressions
//   3. Lowering the body block
//   4. Creating IRParallel with domain, body, result
//   5. Handling options: cancel token and name
//   6. Result handling:
//      - If explicit result (tail not collectable), use body result
//      - Otherwise collect spawned values, wait for them, create tuple result
//
LowerResult LowerParallelExpr(const ast::ParallelExpr& node, LowerCtx& ctx);

}  // namespace ultraviolet::codegen
