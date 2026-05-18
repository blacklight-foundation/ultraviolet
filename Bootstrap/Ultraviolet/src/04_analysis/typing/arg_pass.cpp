// =============================================================================
// MIGRATION MAPPING: arg_pass.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   Section 5.2.4: Procedure Calls (lines 8707-8777)
//   - ArgType(a) definition:
//     * move arguments type through MoveArgExpr
//     * copy arguments type through CopyArgExpr
//     * ref arguments type through PlaceType when source provenance is present
//
// SOURCE FILE: ultraviolet-bootstrap/src/03_analysis/memory/calls.cpp
//   (Argument passing semantics)
//
// KEY CONTENT TO MIGRATE:
//   ARGUMENT TYPE COMPUTATION:
//   - For move parameters: Type the expression as a moved value
//   - For reference parameters: Type as a place expression
//
//   ARGUMENT PASS HANDLING:
//   - ArgumentPassExpressions maps explicit pass kind to the checked expression
//   - move transfers source ownership to a consuming parameter
//   - copy duplicates into a fresh owned value
//
//   REFERENCE ARGUMENT HANDLING:
//   - PlaceType judgment for the argument expression
//   - Must be a valid place (lvalue)
//   - Type is the place type with permission preserved
//
//   PARAMETER MODE MATCHING:
//   - ParamMode = `move` requires ownership transfer for provenance-bearing
//     source places
//   - ParamMode = none accepts ref arguments and explicit copy arguments
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
//   3. The distinction between move, copy, and ref args is fundamental
//   4. Place typing preserves permissions while pass expressions model ownership
//
// HELPER FUNCTIONS:
//   ArgType() - Compute type for a single argument
//   CheckArgModeMatch() - Verify move flag matches parameter mode
//   TypeArgPassExpr() - Type an argument with pass semantics
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

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsArgPass() {
  SPEC_DEF("ArgType", "5.2.4");
  SPEC_DEF("ArgumentPassExpressions", "3.3.2.4");
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

// Compute the type of an argument based on its pass kind
// Per spec: ArgType(a) =
//   copy arguments use ExprType(CopyArgExpr(ArgExpr(a)))
//   move arguments use ExprType(MoveArgExpr(ArgExpr(a)))
//   ref arguments with source provenance use PlaceType(ArgExpr(a))
ArgTypeResult ComputeArgType(const ast::Arg& arg,
                             const ExprTypeFn& type_expr,
                             const PlaceTypeFn& type_place) {
  SpecDefsArgPass();

  if (ast::IsMoveArg(arg)) {
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
  } else if (ast::IsCopyArg(arg)) {
    SPEC_RULE("ArgType-Copy");
    auto copy_expr = std::make_shared<ast::Expr>();
    copy_expr->span = arg.span.file.empty() ?
      (arg.value ? arg.value->span : core::Span{}) : arg.span;
    copy_expr->node = ast::CopyExpr{arg.value};
    const auto result = type_expr(copy_expr);
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
    if (ast::IsRefArg(arg) && HasSourceProvenance(arg.value)) {
      SPEC_RULE("Call-Move-Missing");
      return {false, "E-SEM-2534"};
    }
    return {true, std::nullopt};
  }

  if (!param_mode.has_value()) {
    if (ast::IsMoveArg(arg)) {
      SPEC_RULE("Call-Move-Unexpected");
      return {false, "E-SEM-2535"};
    }

    // Additionally, reference parameters require a place expression
    if (ast::IsRefArg(arg) && HasSourceProvenance(arg.value) &&
        !IsPlaceExprInternal(arg.value)) {
      SPEC_RULE("Call-Arg-NotPlace");
      return {false, "E-TYP-1603"};
    }

    return {true, std::nullopt};
  }

  return {true, std::nullopt};
}

// Type an argument with move semantics
// This transfers ownership from the source to the callee
ExprTypeResult TypeArgPassExpr(const ast::Arg& arg, const ExprTypeFn& type_expr) {
  SpecDefsArgPass();
  SPEC_RULE("ArgumentPassExpressions");

  auto pass_expr = std::make_shared<ast::Expr>();
  pass_expr->span = arg.span.file.empty() ?
    (arg.value ? arg.value->span : core::Span{}) : arg.span;
  if (ast::IsCopyArg(arg)) {
    pass_expr->node = ast::CopyExpr{arg.value};
  } else {
    pass_expr->node = ast::MoveExpr{arg.value};
  }

  return type_expr(pass_expr);
}

// Type an argument as a reference (place)
// This preserves permissions and does not transfer ownership
PlaceTypeResult TypeRefArg(const ast::Arg& arg, const PlaceTypeFn& type_place) {
  SpecDefsArgPass();
  SPEC_RULE("PlaceType");

  return type_place(arg.value);
}

}  // namespace ultraviolet::analysis
