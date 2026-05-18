// =============================================================================
// resolve_expr_list.cpp - Expression List Resolution
// =============================================================================
//
// SPEC REFERENCE:
//   Docs/SPECIFICATION.md §5.1.7 "Resolution Pass" (Lines 7430-7549)
//
// CONTENT:
//   1. ResolveExprList - Resolve a sequence of expressions
//   2. ResolveArgList - Resolve function call arguments
//   3. ResolveMoveArg - Resolve moved argument expression
//   4. ResolveNamedArgList - Resolve named arguments (record construction)
//   5. ResolveFieldInit - Resolve field initializer
//
// =============================================================================

#include "04_analysis/resolve/resolver.h"

#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsExprList() {
  SPEC_DEF("ResolveExprList", "5.1.7");
  SPEC_DEF("ResolveArgList", "5.1.7");
  SPEC_DEF("ResolveMoveArg", "5.1.7");
  SPEC_DEF("ResolveNamedArgList", "5.1.7");
  SPEC_DEF("ResolveFieldInit", "5.1.7");
}

}  // namespace

// =============================================================================
// Public Interface
// =============================================================================

// -----------------------------------------------------------------------------
// ResolveExprList
// -----------------------------------------------------------------------------
// Resolves a sequence of expressions.
// Used for function arguments, array elements, tuple elements, etc.
//
// Implements (Resolve-Expr-List) from §5.1.7:
//   ∀ e ∈ exprs. Γ ⊢ ResolveExpr(e) ⇓ ok
//   → Γ ⊢ ResolveExprList(exprs) ⇓ ok
// -----------------------------------------------------------------------------

ResolveResult<std::vector<ast::ExprPtr>> ResolveExprList(
    ResolveContext& ctx,
    const std::vector<ast::ExprPtr>& exprs) {
  SpecDefsExprList();
  ResolveResult<std::vector<ast::ExprPtr>> result;
  result.ok = true;

  if (exprs.empty()) {
    SPEC_RULE("ResolveExprList-Empty");
    return result;
  }

  result.value.reserve(exprs.size());
  for (const auto& expr : exprs) {
    if (!expr) {
      result.value.push_back(nullptr);
      continue;
    }
    const auto resolved = ResolveExpr(ctx, expr);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.push_back(resolved.value);
    SPEC_RULE("ResolveExprList-Cons");
  }

  return result;
}

// -----------------------------------------------------------------------------
// ResolveArgList
// -----------------------------------------------------------------------------
// Resolves function call arguments.
// Handles both regular and move arguments specially.
//
// Implements (Resolve-Arg-List) from §5.1.7:
//   ∀ arg ∈ args.
//     (arg.is_move → Γ ⊢ ResolveMoveArg(arg.expr) ⇓ ok) ∧
//     (¬arg.is_move → Γ ⊢ ResolveExpr(arg.expr) ⇓ ok)
//   → Γ ⊢ ResolveArgList(args) ⇓ ok
// -----------------------------------------------------------------------------

ResolveResult<std::vector<ast::Arg>> ResolveArgList(
    ResolveContext& ctx,
    const std::vector<ast::Arg>& args) {
  SpecDefsExprList();
  ResolveResult<std::vector<ast::Arg>> result;
  result.ok = true;

  if (args.empty()) {
    SPEC_RULE("ResolveArgList-Empty");
    return result;
  }

  result.value.reserve(args.size());
  for (const auto& arg : args) {
    ast::Arg out_arg = arg;

    // Resolve the argument expression
    const auto resolved = ResolveExpr(ctx, arg.value);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    out_arg.value = resolved.value;

    if (arg.pass == ast::ArgPassKind::Move) {
      SPEC_RULE("ResolveArgList-Move");
    } else if (arg.pass == ast::ArgPassKind::Copy) {
      SPEC_RULE("ResolveArgList-Copy");
    } else {
      SPEC_RULE("ResolveArgList-Value");
    }

    result.value.push_back(std::move(out_arg));
    SPEC_RULE("ResolveArgList-Cons");
  }

  return result;
}

// -----------------------------------------------------------------------------
// ResolveMoveArg
// -----------------------------------------------------------------------------
// Resolves a moved argument expression.
// The expression must be a place expression (checked in later pass).
// -----------------------------------------------------------------------------

ResExprResult ResolveMoveArg(ResolveContext& ctx,
                             const ast::ExprPtr& arg) {
  SpecDefsExprList();

  if (!arg) {
    SPEC_RULE("ResolveMoveArg-None");
    return {true, std::nullopt, std::nullopt, nullptr};
  }

  // Resolve the expression
  const auto resolved = ResolveExpr(ctx, arg);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {}};
  }

  // Place expression validation is deferred to type checking
  SPEC_RULE("ResolveMoveArg-Ok");
  return {true, std::nullopt, std::nullopt, resolved.value};
}

// -----------------------------------------------------------------------------
// ResolveNamedArgList (Field Initializers)
// -----------------------------------------------------------------------------
// Resolves named arguments for record construction.
// Validates no duplicate field names.
//
// Implements (Resolve-Named-Arg-List) from §5.1.7:
//   NoDuplicates(arg_names) ∧
//   ∀ (n, e) ∈ args. Γ ⊢ ResolveExpr(e) ⇓ ok
//   → Γ ⊢ ResolveNamedArgList(args) ⇓ ok
// -----------------------------------------------------------------------------

ResolveResult<std::vector<ast::FieldInit>> ResolveNamedArgList(
    ResolveContext& ctx,
    const std::vector<ast::FieldInit>& fields) {
  SpecDefsExprList();
  ResolveResult<std::vector<ast::FieldInit>> result;
  result.ok = true;

  if (fields.empty()) {
    SPEC_RULE("ResolveNamedArgList-Empty");
    return result;
  }

  // Check for duplicate field names
  std::unordered_set<IdKey> seen_names;
  for (const auto& field : fields) {
    const auto key = IdKeyOf(field.name);
    if (seen_names.find(key) != seen_names.end()) {
      SPEC_RULE("ResolveNamedArgList-DupField");
      return {false, "ResolveNamedArgList-DupField", field.span, {}};
    }
    seen_names.insert(key);
  }

  // Resolve each field value
  result.value.reserve(fields.size());
  for (const auto& field : fields) {
    ast::FieldInit out_field = field;

    const auto resolved = ResolveExpr(ctx, field.value);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    out_field.value = resolved.value;

    result.value.push_back(std::move(out_field));
    SPEC_RULE("ResolveNamedArgList-Cons");
  }

  return result;
}

// -----------------------------------------------------------------------------
// ResolveFieldInit
// -----------------------------------------------------------------------------
// Resolves a single field initializer.
// Field existence is checked in type checking.
// -----------------------------------------------------------------------------

ResolveResult<ast::FieldInit> ResolveFieldInit(
    ResolveContext& ctx,
    const ast::FieldInit& field) {
  SpecDefsExprList();
  ResolveResult<ast::FieldInit> result;
  result.ok = true;
  result.value = field;

  if (!field.value) {
    SPEC_RULE("ResolveFieldInit-None");
    return result;
  }

  const auto resolved = ResolveExpr(ctx, field.value);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {}};
  }

  result.value.value = resolved.value;
  SPEC_RULE("ResolveFieldInit-Ok");
  return result;
}

// -----------------------------------------------------------------------------
// ResolveFieldInitList
// -----------------------------------------------------------------------------
// Resolves a list of field initializers.
// -----------------------------------------------------------------------------

ResolveResult<std::vector<ast::FieldInit>> ResolveFieldInitList(
    ResolveContext& ctx,
    const std::vector<ast::FieldInit>& fields) {
  SpecDefsExprList();
  ResolveResult<std::vector<ast::FieldInit>> result;
  result.ok = true;

  if (fields.empty()) {
    SPEC_RULE("ResolveFieldInitList-Empty");
    return result;
  }

  result.value.reserve(fields.size());
  for (const auto& field : fields) {
    const auto resolved = ResolveFieldInit(ctx, field);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.push_back(resolved.value);
    SPEC_RULE("ResolveFieldInitList-Cons");
  }

  return result;
}

// -----------------------------------------------------------------------------
// ResolveGenericArgs
// -----------------------------------------------------------------------------
// Resolves generic type arguments.
// -----------------------------------------------------------------------------

ResolveResult<std::vector<std::shared_ptr<ast::Type>>> ResolveGenericArgs(
    ResolveContext& ctx,
    const std::vector<std::shared_ptr<ast::Type>>& args) {
  SpecDefsExprList();
  ResolveResult<std::vector<std::shared_ptr<ast::Type>>> result;
  result.ok = true;

  if (args.empty()) {
    SPEC_RULE("ResolveGenericArgs-Empty");
    return result;
  }

  result.value.reserve(args.size());
  for (const auto& arg : args) {
    if (!arg) {
      result.value.push_back(nullptr);
      continue;
    }
    const auto resolved = ResolveType(ctx, arg);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.push_back(resolved.value);
    SPEC_RULE("ResolveGenericArgs-Cons");
  }

  return result;
}

// -----------------------------------------------------------------------------
// ResolveTypeList
// -----------------------------------------------------------------------------
// Resolves a list of types (used for tuple types, function params, etc).
// -----------------------------------------------------------------------------

ResolveResult<std::vector<std::shared_ptr<ast::Type>>> ResolveTypeList(
    ResolveContext& ctx,
    const std::vector<std::shared_ptr<ast::Type>>& types) {
  SpecDefsExprList();
  ResolveResult<std::vector<std::shared_ptr<ast::Type>>> result;
  result.ok = true;

  if (types.empty()) {
    SPEC_RULE("ResolveTypeList-Empty");
    return result;
  }

  result.value.reserve(types.size());
  for (const auto& type : types) {
    if (!type) {
      result.value.push_back(nullptr);
      continue;
    }
    const auto resolved = ResolveType(ctx, type);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.push_back(resolved.value);
    SPEC_RULE("ResolveTypeList-Cons");
  }

  return result;
}

}  // namespace ultraviolet::analysis
