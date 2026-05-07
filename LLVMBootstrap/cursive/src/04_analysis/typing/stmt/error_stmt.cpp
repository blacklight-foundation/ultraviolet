// =============================================================================
// error_stmt.cpp - Error statement typing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Error recovery in statement parsing
//   - Error statements from parse failures
//   - Propagation of syntax errors
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

static inline void SpecDefsErrorStmt() {
  SPEC_DEF("T-ErrorStmt", "5.2.11");
}

}  // namespace

StmtTypeResult TypeErrorStmt(const ScopeContext& ctx,
                             const ast::ErrorStmt& node,
                             const TypeEnv& env) {
  (void)ctx;   // Unused but included for consistency
  (void)node;  // Unused
  SpecDefsErrorStmt();

  // Error statements represent parse errors
  // They allow typechecking to continue past errors
  // The environment is unchanged
  SPEC_RULE("T-ErrorStmt");
  return {true, std::nullopt, env, {}};
}

}  // namespace cursive::analysis
