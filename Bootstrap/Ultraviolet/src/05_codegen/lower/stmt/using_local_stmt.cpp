// =============================================================================
// using_local_stmt.cpp - Lowering for UsingLocalStmt
// =============================================================================
//
// SPEC REFERENCE:
//   SPECIFICATION.md §18.3.6 Lowering
//     (Lower-Stmt-UsingLocal) - UsingLocalStmt lowers to NoOpIR.
//
// `using` is compile-time only: lowering emits no runtime IR, but it still has
// to preserve the aliased binding identity so later address-lowering can target
// the same binding/storage the resolver introduced.
// =============================================================================

#include "00_core/assert_spec.h"
#include "02_source/ast/ast.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_stmt.h"

namespace ultraviolet::codegen {

IRPtr LowerUsingLocalStmt(const ultraviolet::ast::UsingLocalStmt& stmt,
                          LowerCtx& ctx) {
  SPEC_RULE("Lower-Stmt-UsingLocal");
  ctx.RegisterLocalAddrAlias(stmt.alias, stmt.source);
  return std::make_shared<IR>(IR{IROpaque{}});
}

}  // namespace ultraviolet::codegen
