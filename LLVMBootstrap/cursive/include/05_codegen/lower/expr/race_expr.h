#pragma once

// =============================================================================
// Expression Lowering: RaceExpr
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 11.4 (Race Expression)
//   - race { arm -> handler, ... } returns first completed
//   - Two modes: RaceReturn (returns value) and RaceYield (streams)
//
// =============================================================================

#include "05_codegen/lower/lower_expr.h"
#include "02_source/ast/ast.h"

namespace cursive::codegen {

// =============================================================================
// LowerRaceExpr - Lower a race expression to IR
// =============================================================================
//
// SPEC: (Lower-Expr-Race)
//   Determines mode from first handler (Yield = streaming, Return = returning).
//   For each arm:
//     - Lowers async expression
//     - Creates match_value for pattern binding
//     - Pushes scope, registers pattern bindings
//     - Lowers bind pattern and handler expression
//     - Computes cleanup, pops scope
//     - Creates IRRaceArm
//   Merges contexts from all arms.
//   Creates IRRaceYield (streaming) or IRRaceReturn (returning).
//
// =============================================================================

LowerResult LowerRaceExpr(const ast::RaceExpr& expr, LowerCtx& ctx);

}  // namespace cursive::codegen
