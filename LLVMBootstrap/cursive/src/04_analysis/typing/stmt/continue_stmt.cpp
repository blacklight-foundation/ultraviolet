// =============================================================================
// continue_stmt.cpp - Continue statement typing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.2.11: Loop Control Statements
//   - T-Continue (line 9624): Continue statement typing
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/types/type_stmt.cpp
//
// =============================================================================

#include "04_analysis/typing/type_stmt.h"

#include <optional>
#include <string_view>

#include "00_core/assert_spec.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsContinueStmt() {
  SPEC_DEF("T-Continue", "5.2.11");
  SPEC_DEF("Continue-Outside-Loop", "5.2.11");
}

}  // namespace

StmtTypeResult TypeContinueStmt(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::ContinueStmt& node,
                                const TypeEnv& env) {
  SpecDefsContinueStmt();

  // Continue must be inside a loop
  if (type_ctx.loop_flag != LoopFlag::Loop) {
    SPEC_RULE("Continue-Outside-Loop");
    return {false, "Continue-Outside-Loop", {}, {}};
  }

  SPEC_RULE("T-Continue");
  return {true, std::nullopt, env, {}};
}

}  // namespace cursive::analysis
