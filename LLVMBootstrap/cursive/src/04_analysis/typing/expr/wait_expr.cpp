// =================================================================
// File: 04_analysis/typing/expr/wait_expr.cpp
// Construct: Wait Expression Type Checking
// Spec Section: 17.2.4
// Spec Rules: T-Wait-Spawn, T-Wait-Future
// =================================================================
#include <utility>

#include "00_core/assert_spec.h"
#include "04_analysis/composite/unions.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsWait() {
  SPEC_DEF("T-Wait", "17.2.4");
  SPEC_DEF("T-Wait-Spawn", "17.2.4");
  SPEC_DEF("T-Wait-Future", "17.2.4");
  SPEC_DEF("Wait-Handle-Err", "18.4.3");
}

std::optional<TypeRef> ExtractWaitSpawnedInner(const TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
    const auto* path = AppliedTypePath(*type);
    const auto* args = AppliedTypeArgs(*type);
    if (!path || !args) {
      return std::nullopt;
    }
    if (!IsSpawnedTypePath(*path)) {
      return std::nullopt;
    }
    if (args->size() != 1) {
      return std::nullopt;
    }
    return (*args)[0];
  }

std::optional<std::pair<TypeRef, TypeRef>> ExtractWaitTrackedArgs(
    const TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
    const auto* path = AppliedTypePath(*type);
    const auto* args = AppliedTypeArgs(*type);
    if (!path || !args) {
      return std::nullopt;
    }
    if (!IsTrackedTypePath(*path)) {
      return std::nullopt;
    }
    if (args->size() != 2) {
      return std::nullopt;
    }
    return std::make_pair((*args)[0], (*args)[1]);
  }

}  // namespace

// Section 17.2.4 Wait Expression Typing
//
// Typing rule (T-Wait-Spawn):
// Gamma |- handle : Spawned<T>
// Key context is empty
// --------------------------------------------------
// Gamma |- wait handle : T
//
// Typing rule (T-Wait-Future):
// Gamma |- handle : Future<T, E>
// Key context is empty
// --------------------------------------------------
// Gamma |- wait handle : T | E
//
// wait blocks until the result is available:
// - For Spawned<T>: waits for parallel task completion, returns T
// - For Future<T, E>: waits for async completion, returns T | E
// - For Tracked<T, E>: waits for tracked async, returns T | E
//
// CRITICAL: Keys MUST NOT be held across wait suspension.
// wait is a suspension point, and holding keys causes deadlock risk.
//
ExprTypeResult TypeWaitExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::WaitExpr& expr,
                            const TypeEnv& env,
                            const ExprTypeFn& type_expr,
                            const IdentTypeFn& /*type_ident*/,
                            const PlaceTypeFn& type_place) {
  SPEC_RULE("T-Wait");
  ExprTypeResult result;

  if (type_ctx.in_speculative) {
    result.diag_id = "E-CON-0092";
    return result;
  }

  // Check key restriction - wait is ill-formed when keys are held
  if (type_ctx.keys_held) {
    result.diag_id = "E-CON-0133";  // wait while key is held
    return result;
  }

  // Type check handle expression as a place (wait implicitly moves the handle)
  // This allows non-Bitcopy Spawned values to be used directly
  PlaceTypeResult handle_result = type_place(expr.handle);
  if (!handle_result.ok) {
    // If place typing fails, try value typing (for expressions that are values)
    ExprTypeResult value_result = type_expr(expr.handle);
    if (!value_result.ok) {
      result.diag_id = value_result.diag_id;
      return result;
    }

    // Check that handle is Spawned<T>
    const auto stripped = StripPerm(value_result.type);
    auto inner = ExtractWaitSpawnedInner(stripped);
    if (inner) {
      SPEC_RULE("T-Wait-Spawn");
      result.ok = true;
      result.type = *inner;
      return result;
    }

    // Check for Tracked<T, E> (Future-like)
    const auto future_args = ExtractWaitTrackedArgs(stripped);
    if (!future_args.has_value()) {
      SPEC_RULE("Wait-Handle-Err");
      result.diag_id = "E-CON-0132";  // wait operand is not Spawned/Tracked
      return result;
    }

    SPEC_RULE("T-Wait-Future");
    const auto union_type =
        MakeTypeUnion({future_args->first, future_args->second});
    result.ok = true;
    if (union_type && std::holds_alternative<TypeUnion>(union_type->node)) {
      const auto intro = TypeUnionIntro(ctx, future_args->first, union_type);
      if (!intro.ok) {
        result.diag_id = intro.diag_id;
        return result;
      }
      result.type = intro.type;
    } else {
      result.type = union_type;
    }
    return result;
  }

  // Handle successful place typing
  const auto stripped = StripPerm(handle_result.type);
  auto inner = ExtractWaitSpawnedInner(stripped);
  if (inner) {
    SPEC_RULE("T-Wait-Spawn");
    result.ok = true;
    result.type = *inner;
    return result;
  }

  // Check for Tracked<T, E>
  const auto future_args = ExtractWaitTrackedArgs(stripped);
  if (!future_args.has_value()) {
    SPEC_RULE("Wait-Handle-Err");
    result.diag_id = "E-CON-0132";  // wait operand is not Spawned/Tracked
    return result;
  }

  SPEC_RULE("T-Wait-Future");
  const auto union_type = MakeTypeUnion({future_args->first, future_args->second});
  result.ok = true;
  if (union_type && std::holds_alternative<TypeUnion>(union_type->node)) {
    const auto intro = TypeUnionIntro(ctx, future_args->first, union_type);
    if (!intro.ok) {
      result.diag_id = intro.diag_id;
      return result;
    }
    result.type = intro.type;
  } else {
    result.type = union_type;
  }
  return result;
}

}  // namespace cursive::analysis::expr
