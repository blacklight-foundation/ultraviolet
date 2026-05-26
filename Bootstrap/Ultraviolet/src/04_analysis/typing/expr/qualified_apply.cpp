// =================================================================
// File: 04_analysis/typing/expr/qualified_apply.cpp
// Construct: Qualified Apply Expression Type Checking
// Spec Section: 5.3.2, 5.3.3, 5.7
// Spec Rules: T-Record-Literal, T-Enum-Variant, T-Modal-State-Intro
// =================================================================
//
// NOTE: QualifiedApplyExpr represents the syntax for qualified construction:
//   - RecordPath{ field: value, ... }
//   - EnumPath::Variant(args) or EnumPath::Variant{ fields }
//   - ModalPath@State{ fields }
//
// However, by the time type checking occurs, these expressions should have been
// resolved by the name resolution pass into their specific forms:
//   - RecordExpr for record construction
//   - EnumLiteralExpr for enum variant construction
//   - RecordExpr with ModalStateRef for modal state construction
//
// If a QualifiedApplyExpr reaches the type checker, it means name resolution
// failed to resolve the path, and we should emit an error.
//
// This file exists to handle the error case and provide a clear diagnostic.
// The actual construction typing is handled by:
//   - record_literal.cpp (TypeRecordExprImpl)
//   - enum_literal.cpp (TypeEnumLiteralExprImpl)
//
// =============================================================================

#include "00_core/assert_spec.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsQualifiedApply() {
  SPEC_DEF("Expr-Unresolved-Err", "5.1");
}

}  // namespace

// §5.1 QualifiedApplyExpr should be resolved during name resolution.
// If it reaches type checking, it means resolution failed.
//
// This is intentionally a minimal implementation that returns an error,
// as the actual typing of qualified constructions happens after resolution
// transforms them into their specific forms (RecordExpr, EnumLiteralExpr, etc.)
ExprTypeResult TypeQualifiedApplyExprImpl(const ScopeContext& /*ctx*/,
                                          const StmtTypeContext& /*type_ctx*/,
                                          const ast::QualifiedApplyExpr& /*expr*/,
                                          const TypeEnv& /*env*/) {
  SPEC_RULE("Expr-Unresolved-Err");
  ExprTypeResult result;
  result.diag_id = "ResolveExpr-Ident-Err";
  return result;
}

}  // namespace ultraviolet::analysis::expr
