// =============================================================================
// MIGRATION: item/where_clause.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 9: Generics - Where Clauses
//   - Predicate constraints: where Bitcopy(T)
//
// SOURCE: ultraviolet-bootstrap/src/03_analysis/types/generics.cpp
//
// =============================================================================

#include "04_analysis/typing/type_decls.h"

#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/composite/classes.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsWhereClause() {
  SPEC_DEF("WhereClause", "9.2");
  SPEC_DEF("WhereClassBound", "9.2");
  SPEC_DEF("WhereValidation", "9.2");
}

}  // namespace

// =============================================================================
// EXPORTED: ProcessWhereClause
// =============================================================================

WhereClauseResult ProcessWhereClause(
    const ScopeContext& ctx,
    const std::vector<ast::PredicateReq>& predicates,
    const std::vector<std::string>& type_params) {
  SpecDefsWhereClause();
  WhereClauseResult result;
  result.ok = true;
  (void)type_params;

  for (const auto& predicate : predicates) {
    // Verify predicate name is valid
    if (predicate.pred != "Bitcopy" && predicate.pred != "Clone" &&
        predicate.pred != "Drop" && predicate.pred != "FfiSafe" &&
        predicate.pred != "GpuSafe") {
      SPEC_RULE("WhereClause-PredicateNotFound");
      result.ok = false;
      result.diag_id = "E-TYP-2302";
      return result;
    }

    // Ensure where predicate type syntax lowers successfully.
    if (!predicate.type) {
      SPEC_RULE("WhereClause-PredicateTypeMissing");
      result.ok = false;
      result.diag_id = "E-TYP-2302";
      return result;
    }

    const auto lowered = LowerType(ctx, predicate.type);
    if (!lowered.ok) {
      result.ok = false;
      result.diag_id = lowered.diag_id;
      return result;
    }
  }

  SPEC_RULE("WhereClause-Ok");
  return result;
}

}  // namespace ultraviolet::analysis
