// =============================================================================
// MIGRATION: item/foreign_contract_clause.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 18: Foreign Function Interface - Contracts
//   - @foreign_assumes syntax
//   - @foreign_ensures syntax
//   - Error and null result cases
//
// SOURCE: ultraviolet-bootstrap/src/03_analysis/contracts/contract_check.cpp
//
// =============================================================================

#include "04_analysis/contracts/contract_check.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsForeignContract() {
  SPEC_DEF("Foreign-Assumes", "18.3");
  SPEC_DEF("Foreign-Ensures", "18.3");
  SPEC_DEF("Foreign-Error", "18.3");
  SPEC_DEF("Foreign-NullResult", "18.3");
  SPEC_DEF("Foreign-Static", "18.3");
  SPEC_DEF("Foreign-Dynamic", "18.3");
  SPEC_DEF("Foreign-Assume", "18.3");
}

// =============================================================================
// FOREIGN ENSURES KIND (local enum)
// =============================================================================

enum class ForeignEnsuresKind {
  Success,
  Error,
  NullResult
};

// =============================================================================
// PURITY CHECK (same as contract_clause.cpp)
// =============================================================================

static bool ForeignExprIsPure(const ast::ExprPtr& expr) {
  if (!expr) {
    return true;
  }

  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        // Literals are always pure
        if constexpr (std::is_same_v<T, ast::LiteralExpr> ||
                      std::is_same_v<T, ast::PtrNullExpr> ||
                      std::is_same_v<T, ast::TupleExpr>) {
          return true;
        }

        // Identifiers and @result are pure
        if constexpr (std::is_same_v<T, ast::IdentifierExpr> ||
                      std::is_same_v<T, ast::ResultExpr>) {
          return true;
        }

        // Binary/unary operators
        if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          return ForeignExprIsPure(node.lhs) && ForeignExprIsPure(node.rhs);
        }
        if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          return ForeignExprIsPure(node.value);
        }

        // Field access
        if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return ForeignExprIsPure(node.base);
        }

        return false;
      },
      expr->node);
}

}  // namespace

// =============================================================================
// EXPORTED: CheckForeignContractClauses
// =============================================================================

ForeignContractResult CheckForeignContractClauses(
    const ScopeContext& ctx,
    const std::vector<ast::ForeignContractClause>& clauses,
    const TypeRef& return_type) {
  SpecDefsForeignContract();
  ForeignContractResult result;
  result.ok = true;

  for (const auto& clause : clauses) {
    for (const auto& pred : clause.predicates) {
      if (!ForeignExprIsPure(pred)) {
        switch (clause.kind) {
          case ast::ForeignContractKind::Assumes:
            SPEC_RULE("Foreign-Assumes-Pure-Err");
            result.diag_id = "E-SEM-2851";
            break;
          case ast::ForeignContractKind::Ensures:
            SPEC_RULE("Foreign-Ensures-Pure-Err");
            result.diag_id = "E-SEM-2853";
            break;
          case ast::ForeignContractKind::EnsuresError:
            SPEC_RULE("Foreign-Error-Pure-Err");
            result.diag_id = "E-SEM-2853";
            result.has_error_clause = true;
            break;
          case ast::ForeignContractKind::EnsuresNullResult:
            SPEC_RULE("Foreign-NullResult-Pure-Err");
            result.diag_id = "E-SEM-2853";
            result.has_null_clause = true;
            break;
        }
        result.ok = false;
        return result;
      }
    }

    // Track what kinds of clauses we have
    if (clause.kind == ast::ForeignContractKind::EnsuresError) {
      result.has_error_clause = true;
    }
    if (clause.kind == ast::ForeignContractKind::EnsuresNullResult) {
      result.has_null_clause = true;
    }
  }

  (void)ctx;
  (void)return_type;

  SPEC_RULE("ForeignContract-Ok");
  return result;
}

// =============================================================================
// EXPORTED: ValidateForeignAssumes
// =============================================================================

ForeignContractResult ValidateForeignAssumes(
    const ScopeContext& ctx,
    const std::vector<ast::ExprPtr>& predicates) {
  SpecDefsForeignContract();
  ForeignContractResult result;
  result.ok = true;

  for (const auto& pred : predicates) {
    if (!ForeignExprIsPure(pred)) {
      SPEC_RULE("Foreign-Assumes-Pure-Err");
      result.ok = false;
      result.diag_id = "E-SEM-2851";
      return result;
    }
  }

  (void)ctx;

  SPEC_RULE("ForeignAssumes-Ok");
  return result;
}

// =============================================================================
// EXPORTED: ValidateForeignEnsures
// =============================================================================

ForeignContractResult ValidateForeignEnsures(
    const ScopeContext& ctx,
    const ast::ExprPtr& predicate,
    ast::ForeignContractKind kind) {
  SpecDefsForeignContract();
  ForeignContractResult result;
  result.ok = true;

  if (!predicate) {
    return result;
  }

  if (!ForeignExprIsPure(predicate)) {
    switch (kind) {
      case ast::ForeignContractKind::Assumes:
      case ast::ForeignContractKind::Ensures:
        SPEC_RULE("Foreign-Ensures-Pure-Err");
        result.diag_id = "E-SEM-2853";
        break;
      case ast::ForeignContractKind::EnsuresError:
        SPEC_RULE("Foreign-Error-Pure-Err");
        result.diag_id = "E-SEM-2853";
        result.has_error_clause = true;
        break;
      case ast::ForeignContractKind::EnsuresNullResult:
        SPEC_RULE("Foreign-NullResult-Pure-Err");
        result.diag_id = "E-SEM-2853";
        result.has_null_clause = true;
        break;
    }
    result.ok = false;
    return result;
  }

  (void)ctx;

  return result;
}

}  // namespace ultraviolet::analysis

