// =============================================================================
// MIGRATION MAPPING: arg_pass.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.2.4: Procedure Calls (lines 8707-8777)
//   - ArgType(a) definition (lines 8715-8717):
//     * If ArgMoved(a) = true: ExprType(MovedArg(ArgMoved(a), ArgExpr(a)))
//     * If ArgMoved(a) = false: PlaceType(ArgExpr(a))
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/memory/calls.cpp
//   (Argument passing semantics)
//
// KEY CONTENT TO MIGRATE:
//   ARGUMENT TYPE COMPUTATION:
//   - For move parameters: Type the expression as a moved value
//   - For reference parameters: Type as a place expression
//
//   MOVE ARGUMENT HANDLING:
//   - MovedArg judgment combines move flag with expression
//   - Move invalidates the source binding
//   - Type is the owned type (not a reference)
//
//   REFERENCE ARGUMENT HANDLING:
//   - PlaceType judgment for the argument expression
//   - Must be a valid place (lvalue)
//   - Type is the place type with permission preserved
//
//   PARAMETER MODE MATCHING:
//   - ParamMode = `move` requires ArgMoved = true for provenance-bearing
//     source places
//   - ParamMode = none requires ArgMoved = false
//   - Mismatch errors: Call-Move-Missing, Call-Move-Unexpected
//
// DEPENDENCIES:
//   - TypeRef for types
//   - ParamMode enum (None, Move)
//   - IsPlaceExpr() for place detection
//   - ExprTypeFn for expression typing
//   - PlaceTypeFn for place typing
//   - Subtyping() for compatibility check
//
// REFACTORING NOTES:
//   1. This is closely related to args_ok.cpp but focuses on individual arg
//   2. Consider combining into a unified argument handling module
//   3. The distinction between moved and reference args is fundamental
//   4. Place typing preserves permissions while move typing transfers ownership
//
// HELPER FUNCTIONS:
//   ArgType() - Compute type for a single argument
//   CheckArgModeMatch() - Verify move flag matches parameter mode
//   TypeMovedArg() - Type an argument with move semantics
//   TypeRefArg() - Type an argument as a reference
//
// =============================================================================
//
// NOTE: This file contains argument passing semantics helpers.
// The main argument validation is in calls.cpp, with support from args_ok.cpp.
// This file provides the ArgType computation logic.
//
// =============================================================================
#include "04_analysis/memory/calls.h"

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsArgPass() {
  SPEC_DEF("ArgType", "5.2.4");
  SPEC_DEF("MovedArg", "3.3.2.4");
  SPEC_DEF("PlaceType", "3.3.3");
  SPEC_DEF("ParamMode", "3.3.2.3");
}

// Helper: Check if an expression is a place expression
static bool IsPlaceExprInternal(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  if (std::holds_alternative<ast::IdentifierExpr>(expr->node)) {
    return true;
  }
  if (std::holds_alternative<ast::FieldAccessExpr>(expr->node)) {
    return true;
  }
  if (std::holds_alternative<ast::TupleAccessExpr>(expr->node)) {
    return true;
  }
  if (std::holds_alternative<ast::IndexAccessExpr>(expr->node)) {
    return true;
  }
  if (const auto* deref = std::get_if<ast::DerefExpr>(&expr->node)) {
    return IsPlaceExprInternal(deref->value);
  }
  return false;
}

}  // namespace

// Result of argument type computation
struct ArgTypeResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
};

// Compute the type of an argument based on whether it's moved or not
// Per spec: ArgType(a) =
//   If ArgMoved(a) = true: ExprType(MovedArg(ArgMoved(a), ArgExpr(a)))
//   If ArgMoved(a) = false: PlaceType(ArgExpr(a))
ArgTypeResult ComputeArgType(const ast::Arg& arg,
                             const ExprTypeFn& type_expr,
                             const PlaceTypeFn& type_place) {
  SpecDefsArgPass();

  if (arg.moved) {
    // Move parameter: type as moved expression
    SPEC_RULE("ArgType-Move");

    // Create MoveExpr wrapper if the value is a place
    ast::ExprPtr expr_to_type = arg.value;
    if (IsPlaceExprInternal(arg.value)) {
      auto move_expr = std::make_shared<ast::Expr>();
      move_expr->span = arg.span.file.empty() ?
        (arg.value ? arg.value->span : core::Span{}) : arg.span;
      move_expr->node = ast::MoveExpr{arg.value};
      expr_to_type = move_expr;
    }

    const auto result = type_expr(expr_to_type);
    return {result.ok, result.diag_id, result.type};
  } else {
    // Reference parameter: provenance-bearing sources stay by-place;
    // provenance-less expressions are typed as call-temporary values.
    SPEC_RULE("ArgType-Ref");

    if (HasSourceProvenance(arg.value)) {
      const auto place = type_place(arg.value);
      return {place.ok, place.diag_id, place.type};
    }

    const auto expr = type_expr(arg.value);
    return {expr.ok, expr.diag_id, expr.type};
  }
}

// Check if the argument's move flag matches the parameter mode
// Returns true if matching, false with diagnostic if mismatched
struct ArgModeCheckResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

ArgModeCheckResult CheckArgModeMatch(const ast::Arg& arg,
                                     const std::optional<ParamMode>& param_mode) {
  SpecDefsArgPass();

  // ParamMode::Move requires explicit move for provenance-bearing sources.
  if (param_mode.has_value() && *param_mode == ParamMode::Move) {
    if (!arg.moved && HasSourceProvenance(arg.value)) {
      SPEC_RULE("Call-Move-Missing");
      return {false, "E-SEM-2534"};
    }
    return {true, std::nullopt};
  }

  // No mode (reference parameter) requires moved=false
  if (!param_mode.has_value()) {
    if (arg.moved) {
      SPEC_RULE("Call-Move-Unexpected");
      return {false, "E-SEM-2535"};
    }

    // Additionally, reference parameters require a place expression
    if (HasSourceProvenance(arg.value) && !IsPlaceExprInternal(arg.value)) {
      SPEC_RULE("Call-Arg-NotPlace");
      return {false, "E-TYP-1603"};
    }

    return {true, std::nullopt};
  }

  return {true, std::nullopt};
}

// Type an argument with move semantics
// This transfers ownership from the source to the callee
ExprTypeResult TypeMovedArg(const ast::Arg& arg, const ExprTypeFn& type_expr) {
  SpecDefsArgPass();
  SPEC_RULE("MovedArg");

  // Create MoveExpr wrapper
  auto move_expr = std::make_shared<ast::Expr>();
  move_expr->span = arg.span.file.empty() ?
    (arg.value ? arg.value->span : core::Span{}) : arg.span;
  move_expr->node = ast::MoveExpr{arg.value};

  return type_expr(move_expr);
}

// Type an argument as a reference (place)
// This preserves permissions and does not transfer ownership
PlaceTypeResult TypeRefArg(const ast::Arg& arg, const PlaceTypeFn& type_place) {
  SpecDefsArgPass();
  SPEC_RULE("PlaceType");

  return type_place(arg.value);
}

}  // namespace cursive::analysis
