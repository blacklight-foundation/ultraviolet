// =============================================================================
// File: 04_analysis/typing/expr/null_ptr.cpp
// Ptr::null() Expression Typing
// Spec Section: 5.2.9
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 5.2.9: Type Inference
//   - Chk-Null-Ptr: Ptr::null() with expected Ptr<U, s> where s in {Null, none}
//   - PtrNullExpected: T = TypePtr(U, s) and s in {Null, none}
//   - Syn-PtrNull-Err: Cannot infer Ptr::null() type
//   - Chk-PtrNull-Err: Expected type not compatible with null
//
// =============================================================================

#include <optional>
#include <string_view>
#include <variant>

#include "00_core/assert_spec.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsPtrNull() {
  SPEC_DEF("Chk-Null-Ptr", "5.2.9");
  SPEC_DEF("Syn-PtrNull-Err", "5.2.9");
  SPEC_DEF("Chk-PtrNull-Err", "5.2.9");
  SPEC_DEF("PtrNullExpected", "5.2.9");
}

// Strip permission and refinement wrappers from a type
static TypeRef StripWrappers(const TypeRef& type) {
  if (!type) {
    return type;
  }
  TypeRef cur = type;
  while (cur) {
    if (const auto* perm = std::get_if<TypePerm>(&cur->node)) {
      cur = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&cur->node)) {
      cur = refine->base;
      continue;
    }
    break;
  }
  return cur;
}

// Check if a type is compatible with Ptr::null()
// PtrNullExpected: T = TypePtr(U, s) where s in {Null, none}
static bool PtrNullExpected(const TypeRef& type) {
  const auto stripped = StripWrappers(type);
  if (!stripped) {
    return false;
  }

  const auto* ptr = std::get_if<TypePtr>(&stripped->node);
  if (!ptr) {
    return false;
  }

  // Ptr::null() is compatible with:
  // - Ptr<T>@Null (explicit null state)
  // - Ptr<T> (no state specified)
  // NOT compatible with:
  // - Ptr<T>@Valid (requires non-null)
  // - Ptr<T>@Expired (invalid state)

  if (!ptr->state.has_value()) {
    // No state specified - compatible
    return true;
  }

  return *ptr->state == PtrState::Null;
}

}  // namespace

// Synthesis mode: Error - cannot infer Ptr::null() type without context
ExprTypeResult TypePtrNullExpr([[maybe_unused]] const ScopeContext& ctx,
                               [[maybe_unused]] const ast::PtrNullExpr& expr) {
  SpecDefsPtrNull();
  ExprTypeResult result;

  // Ptr::null() cannot be synthesized - requires expected type
  SPEC_RULE("Syn-PtrNull-Err");
  result.diag_id = "E-TYP-1530";  // Cannot infer type of Ptr::null()
  return result;
}

// Checking mode: OK if expected type is Ptr<T>@Null or Ptr<T>
CheckResult CheckPtrNullExpr([[maybe_unused]] const ScopeContext& ctx,
                             [[maybe_unused]] const ast::PtrNullExpr& expr,
                             const TypeRef& expected) {
  SpecDefsPtrNull();
  CheckResult result;

  if (!expected) {
    result.diag_id = "E-TYP-1530";  // Missing expected type
    return result;
  }

  // Check if expected type is compatible with null pointer
  SPEC_RULE("PtrNullExpected");
  if (!PtrNullExpected(expected)) {
    SPEC_RULE("Chk-PtrNull-Err");
    result.diag_id = "E-TYP-1530";  // Expected type not compatible with Ptr::null()
    return result;
  }

  SPEC_RULE("Chk-Null-Ptr");
  result.ok = true;
  return result;
}

}  // namespace ultraviolet::analysis::expr

