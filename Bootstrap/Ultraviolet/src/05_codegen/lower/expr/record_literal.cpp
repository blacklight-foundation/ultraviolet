// =============================================================================
// record_literal.cpp - Record Literal Expression Lowering
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16085-16088: (Lower-Expr-Record)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - RecordExpr visitor lowers field initializers
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRRecordLiteral, IRFieldInit)
//   - ultraviolet/include/04_analysis/layout/layout.h (::ultraviolet::analysis::layout::RecordLayout)
//
// =============================================================================

#include "05_codegen/lower/expr/record_literal.h"

#include "00_core/assert_spec.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_predicates.h"

#include <type_traits>
#include <utility>
#include <vector>

namespace ultraviolet::codegen {

namespace {

bool PathEquals(const analysis::TypePath& lhs, const analysis::TypePath& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i] != rhs[i]) {
      return false;
    }
  }
  return true;
}

analysis::TypeRef TypeCheckedRecordTargetType(const ast::Expr& full_expr,
                                              const ast::RecordExpr& expr,
                                              LowerCtx& ctx) {
  if (!ctx.expr_type) {
    return nullptr;
  }

  analysis::TypeRef typed = analysis::StripPerm(ctx.expr_type(full_expr));
  if (!typed) {
    typed = ctx.expr_type(full_expr);
  }
  if (!typed) {
    return nullptr;
  }

  return std::visit(
      [&](const auto& target) -> analysis::TypeRef {
        using T = std::decay_t<decltype(target)>;
        if constexpr (std::is_same_v<T, ast::TypePath>) {
          const auto* typed_path = analysis::AppliedTypePath(*typed);
          if (typed_path && PathEquals(*typed_path, target)) {
            return typed;
          }
        } else if constexpr (std::is_same_v<T, ast::ModalStateRef>) {
          if (const auto* modal_state =
                  std::get_if<analysis::TypeModalState>(&typed->node)) {
            if (PathEquals(modal_state->path, target.path) &&
                modal_state->state == target.state) {
              return typed;
            }
          }
        }
        return nullptr;
      },
      expr.target);
}

analysis::TypeRef LowerRecordTargetType(const ast::RecordExpr& expr,
                                        LowerCtx& ctx) {
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  return std::visit(
      [&](const auto& target) -> analysis::TypeRef {
        using T = std::decay_t<decltype(target)>;
        if constexpr (std::is_same_v<T, ast::TypePath>) {
          return analysis::MakeTypePath(target);
        } else if constexpr (std::is_same_v<T, ast::ModalStateRef>) {
          std::vector<analysis::TypeRef> generic_args;
          generic_args.reserve(target.generic_args.size());
          for (const auto& arg : target.generic_args) {
            const auto lowered = analysis::LowerType(scope, arg);
            if (!lowered.ok || !lowered.type) {
              return nullptr;
            }
            generic_args.push_back(lowered.type);
          }
          return analysis::MakeTypeModalState(target.path,
                                              target.state,
                                              std::move(generic_args));
        } else {
          return nullptr;
        }
      },
      expr.target);
}

}  // namespace

// =============================================================================
// Section 6.4 Lower-Expr-Record
// =============================================================================
//
// (Lower-Expr-Record)
// G |- LowerFieldInits(fields) => <IR, vec_f>
// ---------------------------------------------------------------------------
// G |- LowerExpr(RecordExpr(tr, fields)) => <IR, RecordValue(tr, vec_f)>
//
// Record literal expressions lower each field initializer in declaration order
// (left-to-right), collecting the resulting IR and values. The field values
// are consumed by the record construction, so they should not be tracked as
// temporaries requiring cleanup.
//
// This applies uniformly to:
// - Plain record types (TypePath): Point{ x: 1, y: 2 }
// - Modal state record types (ModalStateRef): Connection@Open{ socket: fd }
//
// =============================================================================

LowerResult LowerRecord(const ast::Expr& full_expr,
                        const ast::RecordExpr& expr,
                        LowerCtx& ctx) {
  SPEC_RULE("Lower-Expr-Record");

  // Field initializer values are consumed by the record construction, so they
  // should not be tracked as temporaries. This applies to both regular records
  // (TypePath) and modal state records (ModalStateRef).
  auto [ir, field_values] =
      LowerFieldInits(expr.fields, ctx, /*suppress_temps=*/true);

  // Preserve the concrete record target type so LLVM materialization uses the
  // record layout even in contextual positions (for example, union-typed
  // returns). Falling back to expr_type(expr) can capture the contextual
  // supertype and lose field-layout fidelity.
  analysis::TypeRef record_type = TypeCheckedRecordTargetType(full_expr, expr, ctx);
  if (!record_type) {
    record_type = LowerRecordTargetType(expr, ctx);
  }
  IRValue record_value = RegisterLoweredRecordValue(
      std::move(field_values),
      record_type,
      "record",
      ctx);

  return LowerResult{ir, record_value};
}

}  // namespace ultraviolet::codegen
