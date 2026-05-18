#pragma once

// =============================================================================
// Spawn Expression Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 10 (Structured Parallelism)
//   - spawn { body } returns Spawned<T>
//   - Capture semantics for spawn closures
//
// This header declares the spawn expression lowering function which generates
// IRSpawn nodes for parallel task creation.
//

#include "05_codegen/lower/lower_expr.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::codegen {

// =============================================================================
// LowerSpawnExpr - Lower a spawn expression to IR
// =============================================================================
//
// Lowers a spawn expression to IRSpawn. The spawn expression:
//   1. Captures variables from the enclosing scope
//   2. Creates a parallel task that runs within a parallel block
//   3. Returns a Spawned<T> handle that can be waited on
//
// The lowering process:
//   - Collects explicit move captures from spawn options
//   - Collects all implicit captures from the spawn body
//   - Builds a capture environment tuple
//   - Generates a wrapper procedure for the spawn body
//   - Creates IRSpawn with the captured environment and wrapper symbol
//
// @param expr The full expression wrapper containing the spawn node
// @param node The spawn expression AST node
// @param ctx  The lowering context
// @return LowerResult containing the IRSpawn and the Spawned<T> handle value
//
LowerResult LowerSpawnExpr(const ast::Expr& expr,
                           const ast::SpawnExpr& node,
                           LowerCtx& ctx);

}  // namespace ultraviolet::codegen
