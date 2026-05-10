// =============================================================================
// record_literal.cpp - Record Literal Expression Lowering
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16085-16088: (Lower-Expr-Record)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - RecordExpr visitor lowers field initializers
//
// DEPENDENCIES:
//   - cursive/include/05_codegen/ir/ir_model.h (IRRecordLiteral, IRFieldInit)
//   - cursive/include/04_analysis/layout/layout.h (::cursive::analysis::layout::RecordLayout)
//
// =============================================================================

#include "05_codegen/lower/expr/record_literal.h"

#include "00_core/assert_spec.h"
#include "04_analysis/typing/type_lower.h"

#include <type_traits>
#include <utility>
#include <vector>

namespace cursive::codegen {

namespace {

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

LowerResult LowerRecord(const ast::RecordExpr& expr, LowerCtx& ctx) {
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
  IRValue record_value = RegisterLoweredRecordValue(
      std::move(field_values),
      LowerRecordTargetType(expr, ctx),
      "record",
      ctx);

  return LowerResult{ir, record_value};
}

}  // namespace cursive::codegen
