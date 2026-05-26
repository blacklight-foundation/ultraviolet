// =============================================================================
// File: 04_analysis/typing/expr/move_expr.cpp
// Move Expression Typing
// Spec Section: 5.6
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 5.6: Ownership and Move Semantics
//   - move place transfers ownership
//   - Source becomes invalid after move
//   - Result is the moved value
//
// =============================================================================

#include "04_analysis/typing/expr/move_expr.h"

#include <optional>
#include <string_view>
#include <variant>

#include "00_core/assert_spec.h"
#include "04_analysis/memory/calls.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsMoveExpr() {
  SPEC_DEF("T-Move", "5.6");
  SPEC_DEF("T-Copy", "16.8.4");
  SPEC_DEF("Move-Immovable-Err", "5.6");
  SPEC_DEF("Move-AlreadyMoved-Err", "5.6");
  SPEC_DEF("Move-NotPlace-Err", "5.6");
  SPEC_DEF("BindingState", "5.6");
  SPEC_DEF("Movability", "5.6");
}

// Check if expression is a place expression (lvalue)
static bool IsPlaceExpr(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        // Place expressions: identifier, field access, tuple access, index access, deref
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return true;
        }
        return false;
      },
      expr->node);
}

}  // namespace

}  // namespace ultraviolet::analysis::expr

// TypePlace is in ultraviolet::analysis namespace, not ultraviolet::analysis::expr
// We need to use the function from the parent namespace
namespace ultraviolet::analysis::expr {

// (T-Move) - Main implementation matching header declaration
ExprTypeResult TypeMoveExprImpl(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::MoveExpr& expr,
                                const TypeEnv& env) {
  SpecDefsMoveExpr();
  ExprTypeResult result;
  if (type_ctx.require_pure) {
    result.diag_id = "E-SEM-2802";
    return result;
  }

  // TypePlace is in ultraviolet::analysis namespace, not expr - use full qualification
  const auto place = ::ultraviolet::analysis::TypePlace(ctx, type_ctx, expr.place, env);
  if (!place.ok) {
    result.diag_id = place.diag_id;
    result.diag_detail = place.diag_detail;
    result.diag_span = place.diag_span;
    return result;
  }

  SPEC_RULE("T-Move");
  result.ok = true;
  result.type = place.type;
  return result;
}

ExprTypeResult TypeCopyExprImpl(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::CopyExpr& expr,
                                const TypeEnv& env) {
  SpecDefsMoveExpr();
  ExprTypeResult result;
  const auto value = ::ultraviolet::analysis::TypeExpr(ctx, type_ctx, expr.value, env);
  if (!value.ok) {
    result.diag_id = value.diag_id;
    result.diag_detail = value.diag_detail;
    result.diag_span = value.diag_span;
    return result;
  }
  if (!BitcopyType(ctx, value.type)) {
    SPEC_RULE("ValueUse-NonBitcopyPlace");
    result.diag_id = "E-UNS-0107";
    result.diag_span = expr.value ? std::optional<core::Span>(expr.value->span) : std::nullopt;
    return result;
  }
  SPEC_RULE("T-Copy");
  result.ok = true;
  result.type = value.type;
  return result;
}

// Extended overload with explicit type functions
ExprTypeResult TypeMoveExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::MoveExpr& expr,
                            const TypeEnv& env,
                            const ExprTypeFn& type_expr,
                            const PlaceTypeFn& type_place) {
  SpecDefsMoveExpr();
  ExprTypeResult result;

  if (!expr.place) {
    return result;
  }

  // 1. Verify operand is a place expression
  if (!IsPlaceExpr(expr.place)) {
    SPEC_RULE("Move-NotPlace-Err");
    result.diag_id = "Move-NotPlace-Err";
    return result;
  }

  // 2. Type the place expression
  const auto place_result = type_place(expr.place);
  if (!place_result.ok) {
    result.diag_id = place_result.diag_id;
    result.diag_detail = place_result.diag_detail;
    result.diag_span = place_result.diag_span;
    return result;
  }

  // 3. Check movability (binding must be movable, not := )
  // This would check the binding's mutability/movability flags
  // Movable: let x = v, var x = v
  // Immovable: let x := v, var x := v

  // 4. Check binding state (must be Alive, not Moved)
  // This would check the liveness analysis state

  // 5. Result type is the place's type
  SPEC_RULE("T-Move");
  result.ok = true;
  result.type = place_result.type;
  return result;
}

}  // namespace ultraviolet::analysis::expr
