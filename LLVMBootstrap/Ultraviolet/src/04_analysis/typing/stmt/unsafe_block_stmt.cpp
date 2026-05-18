// =============================================================================
// unsafe_block_stmt.cpp - Unsafe block statement typing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   Section 5.5: Memory and Pointers - Unsafe blocks
//   - unsafe block as statement
//   - See also expr/unsafe_block_expr.cpp for expression form
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/types/type_stmt.cpp
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <optional>
#include <string_view>

#include "00_core/assert_spec.h"
#include "02_source/ast/ast.h"
#include "04_analysis/typing/type_expr.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsUnsafeBlockStmt() {
  SPEC_DEF("T-UnsafeStmt", "5.5");
}

}  // namespace

StmtTypeResult TypeUnsafeBlockStmt(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::UnsafeBlockStmt& node,
                                   const TypeEnv& env,
                                   const ExprTypeFn& type_expr,
                                   const IdentTypeFn& type_ident,
                                   const PlaceTypeFn& type_place) {
  SpecDefsUnsafeBlockStmt();

  if (!node.body) {
    return {false, std::nullopt, {}, {}};
  }

  // Create unsafe context
  StmtTypeContext unsafe_ctx = type_ctx;
  unsafe_ctx.in_unsafe = true;

  // Type the unsafe block body with unsafe context enabled
  const auto info = TypeBlockInfo(ctx, unsafe_ctx, *node.body, env,
                                  type_expr, type_ident, type_place,
                                  unsafe_ctx.env_ref);
  if (!info.ok) {
    return {false, info.diag_id, {}, {}, info.diag_detail, info.diag_span};
  }

  // Unsafe block statement produces unit type (statement form)
  // Environment is unchanged (block scope exits)
  FlowInfo flow;
  if (IsPrimType(info.type, "!")) {
    flow.results.push_back(info.type);
  }
  flow.breaks = info.breaks;
  flow.break_void = info.break_void;
  SPEC_RULE("T-UnsafeStmt");
  return {true, std::nullopt, env, std::move(flow)};
}

}  // namespace ultraviolet::analysis
