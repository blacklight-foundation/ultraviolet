// =============================================================================
// using_local_stmt.cpp - Lowering for UsingLocalStmt
// =============================================================================
//
// SPEC REFERENCE:
//   Docs/SPECIFICATION.md §18.3.6 Lowering
//     (Lower-Stmt-UsingLocal) - UsingLocalStmt lowers to NoOpIR.
//
// `using` is compile-time only: lowering emits no runtime IR, but it still has
// to preserve the aliased binding identity so later address-lowering can target
// the same binding/storage the resolver introduced.
// =============================================================================

#include "00_core/assert_spec.h"
#include "00_core/spec_trace.h"
#include "02_source/ast/ast.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_stmt.h"

namespace ultraviolet::codegen {

IRPtr LowerUsingLocalStmt(const ultraviolet::ast::UsingLocalStmt& stmt,
                          LowerCtx& ctx) {
  SPEC_RULE("Lower-Stmt-UsingLocal");
  ctx.RegisterLocalAddrAlias(stmt.alias, stmt.source);
  core::Conformance::Record(
      "req.18.UsingLocalNoRuntimeEffect",
      std::nullopt,
      "source=LowerUsingLocalStmt;operation=RegisterLocalAddrAlias;"
      "runtime_state_changed=false");
  core::Conformance::Record(
      "rule.18.Lower-Stmt-UsingLocal",
      std::nullopt,
      "source=LowerUsingLocalStmt;ir_form=NoOpIR;alias_registered=true");
  core::Conformance::Record(
      "req.18.UsingLocalNoRuntimeIR",
      std::nullopt,
      "source=LowerUsingLocalStmt;ir_form=IROpaque;runtime_ir=false");
  return std::make_shared<IR>(IR{IROpaque{}});
}

}  // namespace ultraviolet::codegen
