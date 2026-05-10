// =============================================================================
// MIGRATION MAPPING: args_ok.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 5.2.4: Procedure Calls (lines 8707-8777)
//   - ArgsOkTJudg = {ArgsOk_T}
//   - Rules: ArgsT-Empty, ArgsT-Cons, ArgsT-Cons-Ref
//   - Rules: T-Call, Call-Extern-Unsafe-Err, Call-Callee-NotFunc,
//            Call-ArgCount-Err, Call-ArgType-Err, Call-Move-Missing,
//            Call-Move-Unexpected, Call-Arg-Packed-Unsafe-Err, Call-Arg-NotPlace
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/memory/calls.cpp
//   (Primary source for argument checking logic)
//
// KEY CONTENT TO MIGRATE:
//   From spec definitions (lines 8709-8717):
//   - ParamMode(param) = mode extraction
//   - ParamType(param) = type extraction
//   - ArgMoved(arg) = moved flag
//   - ArgExpr(arg) = expression
//   - PlaceType(p) = place typing
//   - ArgType(a) = computed argument type
//
//   ARGUMENT MATCHING RULES:
//   - ArgsT-Empty (line 8719-8721): Base case for empty args
//   - ArgsT-Cons (lines 8723-8726): Move parameter case
//     * moved = true required
//     * Type via MovedArg judgment
//   - ArgsT-Cons-Ref (lines 8728-8731): Reference parameter case
//     * moved = false required
//     * AddrOfOk check for place
//     * Type via place typing
//
//   ERROR RULES:
//   - Call-Callee-NotFunc (lines 8743-8746): Callee not callable
//   - Call-ArgCount-Err (lines 8748-8751): Argument count mismatch
//   - Call-ArgType-Err (lines 8753-8756): Argument type mismatch
//   - Call-Move-Missing (lines 8758-8761): Move required but not provided
//   - Call-Move-Unexpected (lines 8763-8766): Move provided but not expected
//   - Call-Arg-Packed-Unsafe-Err (lines 8768-8771): Packed field ref outside unsafe
//   - Call-Arg-NotPlace (lines 8773-8776): Non-place for ref parameter
//
// DEPENDENCIES:
//   - TypeRef for parameter and argument types
//   - Subtyping() for type compatibility checks
//   - IsPlaceExpr() for place expression detection
//   - IsInUnsafeSpan() for unsafe context checking
//   - ExprTypeFn for expression typing
//   - PlaceTypeFn for place typing
//   - PackedField() predicate for packed struct fields
//   - AddrOfOk() for address-of validity
//
// REFACTORING NOTES:
//   1. This implements the ArgsOk_T judgment from the spec
//   2. Move semantics checking is critical for memory safety
//   3. Place expression detection is used for reference parameters
//   4. Consider returning detailed error info (which arg failed, why)
//   5. Packed field checking requires struct layout information
//   6. The extern unsafe check (Call-Extern-Unsafe-Err) should be
//      handled at the call site, not in args_ok
//
// RESULT TYPE:
//   ArgsOkResult { bool ok; optional<string_view> diag_id; vector<TypeRef> arg_types; }
//
// FUNCTION SIGNATURE:
//   ArgsOkResult ArgsOk(
//       const ScopeContext& ctx,
//       const vector<TypeFuncParam>& params,
//       const vector<ast::CallArg>& args,
//       const ExprTypeFn& type_expr,
//       const PlaceTypeFn& type_place);
//
// =============================================================================
//
// NOTE: This file contains argument checking helpers for procedure calls.
// The main TypeCall function is in cursive/src/04_analysis/memory/calls.cpp.
// This file provides supplementary implementations for the ArgsOk judgment.
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
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsArgsOk() {
  SPEC_DEF("ArgsOkTJudg", "5.2.4");
  SPEC_DEF("ParamMode", "5.2.4");
  SPEC_DEF("ParamType", "5.2.4");
  SPEC_DEF("ArgMoved", "5.2.4");
  SPEC_DEF("ArgExpr", "5.2.4");
  SPEC_DEF("ArgType", "5.2.4");
  SPEC_DEF("MovedArg", "3.3.2.4");
  SPEC_DEF("IsPlace", "3.3.3");
}

// Helper: Check if an expression is a place expression (lvalue)
static bool IsPlaceExprLocal(const ast::ExprPtr& expr) {
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
    return IsPlaceExprLocal(deref->value);
  }
  return false;
}

// Helper: Create a move expression wrapper
static ast::ExprPtr MakeExpr(const core::Span& span, ast::ExprNode node) {
  auto expr = std::make_shared<ast::Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

// Helper: Wrap argument in MoveExpr if it was marked with move
static ast::ExprPtr MovedArgExprLocal(const ast::Arg& arg) {
  if (!arg.moved || !IsPlaceExprLocal(arg.value)) {
    return arg.value;
  }
  core::Span span = arg.span;
  if (!span.file.empty()) {
    return MakeExpr(span, ast::MoveExpr{arg.value});
  }
  if (arg.value) {
    return MakeExpr(arg.value->span, ast::MoveExpr{arg.value});
  }
  return MakeExpr(core::Span{}, ast::MoveExpr{arg.value});
}

// Helper: Strip permission qualifier from type
static TypeRef StripPermLocal(const TypeRef& type) {
  if (!type) {
    return type;
  }
  if (const auto* perm = std::get_if<TypePerm>(&type->node)) {
    return perm->base;
  }
  return type;
}

// Result of address-of validity check
struct AddrOfOkResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

// Check if taking address of an expression is valid
static AddrOfOkResult AddrOfOk(const ast::ExprPtr& expr,
                               const ExprTypeFn& type_expr) {
  if (!IsPlaceExprLocal(expr)) {
    return {false, std::nullopt};
  }
  const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node);
  if (!index) {
    return {true, std::nullopt};
  }
  const auto idx_type = type_expr(index->index);
  if (!idx_type.ok) {
    return {false, idx_type.diag_id};
  }
  const auto idx_stripped = StripPermLocal(idx_type.type);
  if (IsPrimType(idx_stripped, "usize")) {
    return {true, std::nullopt};
  }

  const auto base_type = type_expr(index->base);
  if (!base_type.ok) {
    return {false, base_type.diag_id};
  }
  const auto stripped = StripPermLocal(base_type.type);
  if (stripped) {
    if (std::holds_alternative<TypeArray>(stripped->node)) {
      return {false, "Index-Array-NonUsize"};
    }
    if (std::holds_alternative<TypeSlice>(stripped->node)) {
      return {false, "Index-Slice-NonUsize"};
    }
  }
  return {false, "Index-NonIndexable"};
}

}  // namespace

// This file provides helper implementations used by calls.cpp.
// The main ArgsOk checking is done inline within TypeCall in calls.cpp.
//
// The following functions are exposed for use by other modules:

// Check if an argument expression is a place expression
bool IsArgPlaceExpr(const ast::ExprPtr& expr) {
  SpecDefsArgsOk();
  SPEC_RULE("IsPlace");
  return IsPlaceExprLocal(expr);
}

// Create a moved argument expression wrapper
ast::ExprPtr CreateMovedArgExpr(const ast::Arg& arg) {
  SpecDefsArgsOk();
  SPEC_RULE("MovedArg");
  return MovedArgExprLocal(arg);
}

// Check if address-of is valid for an expression
bool CheckAddrOfOk(const ast::ExprPtr& expr, const ExprTypeFn& type_expr,
                   std::optional<std::string_view>* out_diag) {
  SpecDefsArgsOk();
  const auto result = AddrOfOk(expr, type_expr);
  if (out_diag) {
    *out_diag = result.diag_id;
  }
  return result.ok;
}

}  // namespace cursive::analysis
