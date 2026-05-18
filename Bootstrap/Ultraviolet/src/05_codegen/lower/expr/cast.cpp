// =============================================================================
// MIGRATION MAPPING: expr/cast.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16178-16181: (Lower-Expr-Cast)
//   - Lines 16338-16346: (Lower-Cast) and (Lower-Cast-Panic)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_calls.cpp
//   - CastExpr visitor with type conversion
//   - Panic for invalid casts (e.g., truncation overflow)
//
// DEPENDENCIES:
//   - ultraviolet/src/05_codegen/ir/ir_model.h (IRCast, IRValue)
//   - ultraviolet/src/05_codegen/checks/checks.h (LowerPanic, PanicReason::Cast)
//
// CAST KINDS:
//   - Numeric widening (i32 -> i64)
//   - Numeric narrowing (i64 -> i32) - may panic
//   - Pointer casts
//   - Bool to int, int to bool
//
// =============================================================================

#include "05_codegen/lower/expr/cast.h"

#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/dyn_dispatch/dyn_dispatch.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_predicates.h"

namespace ultraviolet::codegen {

// =============================================================================
// Section 6.4 LowerCast - lower cast expression
// =============================================================================
//
// (Lower-Cast)
// Gamma |- LowerExpr(e) => <IR, v>    S = ExprType(e)    CastVal(S, T, v) => v'
// -------------------------------------------------------------------------------
// Gamma |- LowerCast(e, T) => <IR, v'>
//
// (Lower-Cast-Panic)
// Gamma |- LowerExpr(e) => <IR, v>    S = ExprType(e)    CastVal(S, T, v) undefined
// Gamma |- LowerPanic(Cast) => IR_k
// -------------------------------------------------------------------------------
// Gamma |- LowerCast(e, T) => <SeqIR(IR, IR_k), v_unreach>

LowerResult LowerCast(const ast::Expr& expr,
                      analysis::TypeRef target_type,
                      LowerCtx& ctx) {
  SPEC_RULE("Lower-Cast");
  SPEC_RULE("Lower-Cast-Panic");

  // Check if target type is a dynamic class type ($ClassName)
  // In that case, we need to pack the value into a fat pointer (data + vtable)
  if (target_type) {
    auto stripped_target = analysis::StripPerm(target_type);
    if (stripped_target &&
        std::holds_alternative<analysis::TypeDynamic>(stripped_target->node)) {
      SPEC_RULE("Eval-Dynamic-Form");
      SPEC_RULE("Eval-Dynamic-Form-Ctrl");

      const auto* dyn =
          std::get_if<analysis::TypeDynamic>(&stripped_target->node);
      if (!dyn || !ctx.sigma || !ctx.expr_type) {
        ctx.ReportCodegenFailure();
        IRValue dyn_value = ctx.FreshTempValue("dyn");
        return LowerResult{EmptyIR(), dyn_value};
      }

      analysis::TypeRef expr_type = ctx.expr_type(expr);
      analysis::TypeRef stripped_expr =
          expr_type ? analysis::StripPerm(expr_type) : expr_type;

      // Look up the class declaration in sigma
      analysis::PathKey class_key;
      for (const auto& seg : dyn->path) {
        class_key.push_back(seg);
      }
      const auto class_it = ctx.sigma->classes.find(class_key);
      if (!stripped_expr || class_it == ctx.sigma->classes.end()) {
        ctx.ReportCodegenFailure();
        IRValue dyn_value = ctx.FreshTempValue("dyn");
        return LowerResult{EmptyIR(), dyn_value};
      }

      // Use DynPack to create the fat pointer (data_ptr, vtable_sym)
      DynPackResult pack =
          DynPack(stripped_expr, expr, dyn->path, class_it->second, ctx);

      IRValue dyn_value = ctx.FreshTempValue("dyn");
      // DynPack is represented as a derived opaque value that LLVM materializes
      // into a fat pointer {data_ptr, vtable_ptr}; there is no local slot here.
      dyn_value.kind = IRValue::Kind::Opaque;
      dyn_value.vtable_sym = pack.vtable_sym;
      DerivedValueInfo info;
      info.kind = DerivedValueInfo::Kind::DynLit;
      info.base = pack.data_ptr;
      info.vtable_sym = pack.vtable_sym;
      info.dyn_impl_type = stripped_expr;
      info.dyn_class_path = dyn->path;
      ctx.RegisterDerivedValue(dyn_value, info);

      return LowerResult{pack.ir, dyn_value};
    }
  }

  // For non-dynamic casts, lower the expression first
  auto expr_result = LowerExpr(expr, ctx);

  IRValue result_value = ctx.FreshTempValue("cast");

  // If no target type, return the expression result unchanged
  if (!target_type) {
    return LowerResult{expr_result.ir, result_value};
  }

  std::vector<IRPtr> parts;
  parts.push_back(expr_result.ir);

  // Emit a runtime check for cast validity (overflow, truncation, etc.)
  // The IRCheckCast will set the panic flag if the cast would fail
  IRCheckCast check;
  check.target = target_type;
  check.value = expr_result.value;
  parts.push_back(MakeIR(std::move(check)));

  // Check if panic occurred and handle it
  parts.push_back(PanicFollowup(ctx));

  // Emit the actual cast operation
  IRCast cast;
  cast.target = target_type;
  cast.value = expr_result.value;
  cast.result = result_value;
  parts.push_back(MakeIR(std::move(cast)));

  return LowerResult{SeqIR(std::move(parts)), result_value};
}

// =============================================================================
// Section 6.4 LowerCastExpr - lower cast expression from AST
// =============================================================================
//
// (Lower-Expr-Cast)
// Gamma |- LowerCast(e, T) => <IR, v>
// -----------------------------------------------
// Gamma |- LowerExpr(Cast(e, T)) => <IR, v>

LowerResult LowerCastExpr(const ast::CastExpr& expr, LowerCtx& ctx) {
  SPEC_RULE("Lower-Expr-Cast");

  // Determine the target type from the AST type annotation
  analysis::TypeRef target_type;
  if (expr.type && ctx.sigma) {
    const analysis::ScopeContext& scope = ScopeForLowering(ctx);
    if (auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, expr.type)) {
      target_type = *lowered;
    }
  }

  // Fall back to type inference if AST type is not available or lowering failed
  if (!target_type && ctx.expr_type) {
    ast::Expr wrapped{expr.value->span, expr.value->node};
    target_type = ctx.expr_type(wrapped);
  }

  return LowerCast(*expr.value, target_type, ctx);
}

}  // namespace ultraviolet::codegen
