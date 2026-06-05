// =================================================================
// File: 04_analysis/typing/expr/qualified_name.cpp
// Construct: Qualified Name Expression Type Checking
// Spec Section: 5.1
// Spec Rules: T-Proc-As-Value, Path resolution
// =================================================================
//
// NOTE: QualifiedNameExpr represents a qualified path used as a value:
//   - module::name - value in module
//   - module::Type::name - associated value in type
//   - Enum::Variant - enum unit variant as value
//
// However, by the time type checking occurs, these expressions should have been
// resolved by the name resolution pass into their specific forms:
//   - IdentifierExpr (for local bindings)
//   - PathExpr (for fully resolved paths to procedures/statics)
//   - EnumLiteralExpr (for unit enum variants)
//
// If a QualifiedNameExpr reaches the type checker, it means name resolution
// failed to resolve the path, and we should emit an error.
//
// The actual value path typing is handled by:
//   - identifier.cpp (TypeIdentifierExprImpl)
//   - path.cpp (TypePathExprImpl)
//
// =============================================================================

#include "00_core/assert_spec.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsQualifiedName() {
  SPEC_DEF("Expr-Unresolved-Err", "5.1");
  SPEC_DEF("def.16.QualifiedNameResolution", "16.1.3");
  SPEC_DEF("rule.16.Expr-Unresolved-Err", "16.1.4");
  SPEC_DEF("req.16.QualifiedNameEliminatedBeforeTyping", "16.1.4");
  SPEC_DEF("diag.16.LiteralAndNameExpressions", "16.1.7");
}

}  // namespace

// §5.1 QualifiedNameExpr should be resolved during name resolution.
// If it reaches type checking, it means resolution failed.
//
// This is intentionally a minimal implementation that returns an error,
// as the actual typing of qualified value paths happens after resolution
// transforms them into their specific forms (IdentifierExpr, PathExpr, etc.)
ExprTypeResult TypeQualifiedNameExprImpl(const ScopeContext& /*ctx*/,
                                         const StmtTypeContext& /*type_ctx*/,
                                         const ast::QualifiedNameExpr& /*expr*/,
                                         const TypeEnv& /*env*/) {
  SPEC_RULE("Expr-Unresolved-Err");
  SPEC_RULE("def.16.QualifiedNameResolution");
  SPEC_RULE("rule.16.Expr-Unresolved-Err");
  SPEC_RULE("req.16.QualifiedNameEliminatedBeforeTyping");
  SPEC_RULE("diag.16.LiteralAndNameExpressions");
  ExprTypeResult result;
  result.diag_id = "ResolveExpr-Ident-Err";
  return result;
}

}  // namespace ultraviolet::analysis::expr
