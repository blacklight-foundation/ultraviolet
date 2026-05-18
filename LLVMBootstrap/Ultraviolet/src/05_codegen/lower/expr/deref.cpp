// =============================================================================
// MIGRATION MAPPING: expr/deref.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16248-16251: (Lower-Expr-Deref)
//     Gamma |- LowerExpr(e) => <IR_e, v_ptr>    Gamma |- LowerRawDeref(v_ptr) => <IR_d, v>
//     ------------------------------------------------------------------------------------
//     Gamma |- LowerExpr(Deref(e)) => <SeqIR(IR_e, IR_d), v>
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_places.cpp
//   - Lines 467-486: LowerReadPlace for DerefExpr
//   - Lines 855-863: LowerWritePlace for DerefExpr
//   - LowerRawDeref helper in checks.cpp (lines 492-564)
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRReadPtr, IRWritePtr, IRValue)
//   - ultraviolet/include/05_codegen/checks/checks.h (LowerRawDeref)
//
// =============================================================================

#include "05_codegen/lower/expr/deref.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/intrinsics/builtins.h"
#include "04_analysis/typing/type_predicates.h"
#include "00_core/assert_spec.h"

#include <variant>

namespace ultraviolet::codegen {

// ============================================================================
// LowerReadPlaceDeref - Lower dereference expression for reading
// ============================================================================
//
// Implements spec rule: Lower-Expr-Deref (Lower-ReadPlace-Deref)
//
// Gamma |- LowerExpr(e) => <IR_e, v_ptr>    Gamma |- LowerRawDeref(v_ptr) => <IR_d, v>
// ------------------------------------------------------------------------------------
// Gamma |- LowerExpr(Deref(e)) => <SeqIR(IR_e, IR_d), v>
//
// The dereference expression first evaluates the pointer expression to get
// a pointer value, then uses LowerRawDeref to generate the appropriate
// read IR based on the pointer type:
//
// - For safe pointers (Ptr<T>):
//   - @Valid: Direct read (Lower-RawDeref-Safe)
//   - @Null: Panic (Lower-RawDeref-Null)
//   - @Expired: Panic (Lower-RawDeref-Expired)
//
// - For raw pointers (*imm T, *mut T):
//   - Unchecked read (Lower-RawDeref-Raw)
//
// If no type information is available, falls back to a simple IRReadPtr.
// ============================================================================

LowerResult LowerReadPlaceDeref(const ast::DerefExpr& node,
                                 const ast::Expr& place,
                                 LowerCtx& ctx) {
  SPEC_RULE("Lower-ReadPlace-Deref");

  // Dereference operand is a general expression, not necessarily a place.
  // Using LowerReadPlace here turns `*(&x)` into an opaque temporary and can
  // spuriously trigger null-deref panics at runtime.
  auto ptr_result = LowerExpr(*node.value, ctx);

  // Get the type of the pointer expression for appropriate dispatch
  analysis::TypeRef ptr_type = ctx.LookupValueType(ptr_result.value);
  if (ctx.expr_type) {
    analysis::TypeRef expr_ptr_type = ctx.expr_type(*node.value);
    auto pointer_specificity = [](const analysis::TypeRef& type) -> int {
      if (!type) {
        return 0;
      }
      analysis::TypeRef stripped = analysis::StripPerm(type);
      if (!stripped) {
        stripped = type;
      }
      if (const auto* ptr = std::get_if<analysis::TypePtr>(&stripped->node)) {
        return ptr->state.has_value() ? 3 : 2;
      }
      if (std::holds_alternative<analysis::TypeRawPtr>(stripped->node)) {
        return 2;
      }
      return 1;
    };
    if (pointer_specificity(expr_ptr_type) > pointer_specificity(ptr_type)) {
      ptr_type = expr_ptr_type;
    }
  }

  // If we have type information, use LowerRawDeref for proper handling
  // of different pointer types (safe vs raw, and safe pointer states)
  if (ptr_type) {
    auto deref_result = LowerRawDeref(ptr_result.value, ptr_type, ctx);
    return LowerResult{SeqIR(std::vector<IRPtr>{ptr_result.ir, deref_result.ir}),
                       deref_result.value};
  }

  // Fallback: no type information, emit a simple IRReadPtr
  // This should only happen in incomplete type checking scenarios. Emit
  // conservative null/expired guards so pointer panic semantics remain observable.
  IRCheckOp null_check;
  null_check.op = "nonnull";
  null_check.reason = PanicReasonString(PanicReason::NullDeref);
  null_check.lhs = ptr_result.value;

  IRCall active_call;
  active_call.callee.kind = IRValue::Kind::Symbol;
  active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
  active_call.args.push_back(ptr_result.value);
  IRValue active_value = ctx.FreshTempValue("addr_active");
  active_call.result = active_value;
  ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));

  IRCheckOp active_check;
  active_check.op = "addr_active";
  active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
  active_check.lhs = active_value;

  IRReadPtr read;
  read.ptr = ptr_result.value;
  IRValue value = ctx.FreshTempValue("deref");
  read.result = value;

  // Register the type of the dereferenced value if possible
  if (ctx.expr_type) {
    ctx.RegisterValueType(value, ctx.expr_type(place));
  }

  return LowerResult{
      SeqIR(std::vector<IRPtr>{
          ptr_result.ir,
          MakeIR(std::move(null_check)),
          PanicFollowup(ctx),
          MakeIR(std::move(active_call)),
          MakeIR(std::move(active_check)),
          PanicFollowup(ctx),
          MakeIR(std::move(read)),
      }),
      value};
}

// ============================================================================
// LowerWritePlaceDeref - Lower dereference expression for writing
// ============================================================================
//
// Implements spec rules: Lower-WritePlace-Deref, LowerWriteSub-Deref
//
// For writing through a pointer:
// 1. Evaluate the pointer expression to get the address
// 2. Optionally read and drop the old value (if allow_drop is true)
// 3. Write the new value through the pointer
//
// Note: For raw pointers, this must be in an unsafe block per language rules.
// The type checking phase ensures this invariant.
// ============================================================================

IRPtr LowerWritePlaceDeref(const ast::DerefExpr& node,
                            const ast::Expr& place,
                            const IRValue& value,
                            bool allow_drop,
                            LowerCtx& ctx) {
  SPEC_RULE(allow_drop ? "Lower-WritePlace-Deref" : "LowerWriteSub-Deref");

  // Dereference operand is a general expression, not necessarily a place.
  auto ptr_result = LowerExpr(*node.value, ctx);

  analysis::TypeRef ptr_type = ctx.LookupValueType(ptr_result.value);
  if (ctx.expr_type) {
    analysis::TypeRef expr_ptr_type = ctx.expr_type(*node.value);
    auto pointer_specificity = [](const analysis::TypeRef& type) -> int {
      if (!type) {
        return 0;
      }
      analysis::TypeRef stripped = analysis::StripPerm(type);
      if (!stripped) {
        stripped = type;
      }
      if (const auto* ptr = std::get_if<analysis::TypePtr>(&stripped->node)) {
        return ptr->state.has_value() ? 3 : 2;
      }
      if (std::holds_alternative<analysis::TypeRawPtr>(stripped->node)) {
        return 2;
      }
      return 1;
    };
    if (pointer_specificity(expr_ptr_type) > pointer_specificity(ptr_type)) {
      ptr_type = expr_ptr_type;
    }
  }
  ptr_type = analysis::StripPerm(ptr_type);

  if (ptr_type) {
    if (const auto* ptr = std::get_if<analysis::TypePtr>(&ptr_type->node)) {
      if (ptr->state.has_value()) {
        if (*ptr->state == analysis::PtrState::Null) {
          return SeqIR(std::vector<IRPtr>{ptr_result.ir, LowerPanic(PanicReason::NullDeref, ctx)});
        }
        if (*ptr->state == analysis::PtrState::Expired) {
          return SeqIR(std::vector<IRPtr>{ptr_result.ir, LowerPanic(PanicReason::ExpiredDeref, ctx)});
        }
        if (*ptr->state == analysis::PtrState::Valid) {
          IRCheckOp null_check;
          null_check.op = "nonnull";
          null_check.reason = PanicReasonString(PanicReason::NullDeref);
          null_check.lhs = ptr_result.value;

          IRCall active_call;
          active_call.callee.kind = IRValue::Kind::Symbol;
          active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
          active_call.args.push_back(ptr_result.value);
          IRValue active_value = ctx.FreshTempValue("addr_active");
          active_call.result = active_value;
          ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));

          IRCheckOp active_check;
          active_check.op = "addr_active";
          active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
          active_check.lhs = active_value;

          IRWritePtr write;
          write.ptr = ptr_result.value;
          write.value = value;
          return SeqIR(std::vector<IRPtr>{
              ptr_result.ir,
              MakeIR(std::move(null_check)),
              PanicFollowup(ctx),
              MakeIR(std::move(active_call)),
              MakeIR(std::move(active_check)),
              PanicFollowup(ctx),
              MakeIR(std::move(write)),
          });
        }
      } else {
        IRCheckOp null_check;
        null_check.op = "nonnull";
        null_check.reason = PanicReasonString(PanicReason::NullDeref);
        null_check.lhs = ptr_result.value;

        IRCall active_call;
        active_call.callee.kind = IRValue::Kind::Symbol;
        active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
        active_call.args.push_back(ptr_result.value);
        IRValue active_value = ctx.FreshTempValue("addr_active");
        active_call.result = active_value;
        ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));

        IRCheckOp active_check;
        active_check.op = "addr_active";
        active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
        active_check.lhs = active_value;

        IRWritePtr write;
        write.ptr = ptr_result.value;
        write.value = value;
        return SeqIR(std::vector<IRPtr>{
            ptr_result.ir,
            MakeIR(std::move(null_check)),
            PanicFollowup(ctx),
            MakeIR(std::move(active_call)),
            MakeIR(std::move(active_check)),
            PanicFollowup(ctx),
            MakeIR(std::move(write)),
        });
      }
    } else if (const auto* raw = std::get_if<analysis::TypeRawPtr>(&ptr_type->node)) {
      if (raw->qual == analysis::RawPtrQual::Imm) {
        return SeqIR(std::vector<IRPtr>{ptr_result.ir, LowerPanic(PanicReason::Other, ctx)});
      }
    } else if (const auto* path = std::get_if<analysis::TypePathType>(&ptr_type->node)) {
      if (!path->path.empty() && path->path.back() == "Ptr") {
        IRCheckOp null_check;
        null_check.op = "nonnull";
        null_check.reason = PanicReasonString(PanicReason::NullDeref);
        null_check.lhs = ptr_result.value;

        IRCall active_call;
        active_call.callee.kind = IRValue::Kind::Symbol;
        active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
        active_call.args.push_back(ptr_result.value);
        IRValue active_value = ctx.FreshTempValue("addr_active");
        active_call.result = active_value;
        ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));

        IRCheckOp active_check;
        active_check.op = "addr_active";
        active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
        active_check.lhs = active_value;

        IRWritePtr write;
        write.ptr = ptr_result.value;
        write.value = value;
        return SeqIR(std::vector<IRPtr>{
            ptr_result.ir,
            MakeIR(std::move(null_check)),
            PanicFollowup(ctx),
            MakeIR(std::move(active_call)),
            MakeIR(std::move(active_check)),
            PanicFollowup(ctx),
            MakeIR(std::move(write)),
        });
      }
    }
  }

  // Write the new value through the pointer
  // Note: Unlike field/tuple writes, deref writes in the bootstrap code
  // do NOT perform drop-on-assign. This is because:
  // 1. The pointer's target ownership is typically managed elsewhere
  // 2. For raw pointers (*mut T), the user is responsible for drop
  // 3. For safe pointers (Ptr<T>), the region manages the memory
  IRCheckOp null_check;
  null_check.op = "nonnull";
  null_check.reason = PanicReasonString(PanicReason::NullDeref);
  null_check.lhs = ptr_result.value;

  IRCall active_call;
  active_call.callee.kind = IRValue::Kind::Symbol;
  active_call.callee.name = BuiltinModalSymRegionAddrIsActive();
  active_call.args.push_back(ptr_result.value);
  IRValue active_value = ctx.FreshTempValue("addr_active");
  active_call.result = active_value;
  ctx.RegisterValueType(active_value, analysis::MakeTypePrim("bool"));

  IRCheckOp active_check;
  active_check.op = "addr_active";
  active_check.reason = PanicReasonString(PanicReason::ExpiredDeref);
  active_check.lhs = active_value;

  IRWritePtr write;
  write.ptr = ptr_result.value;
  write.value = value;

  return SeqIR(std::vector<IRPtr>{
      ptr_result.ir,
      MakeIR(std::move(null_check)),
      PanicFollowup(ctx),
      MakeIR(std::move(active_call)),
      MakeIR(std::move(active_check)),
      PanicFollowup(ctx),
      MakeIR(std::move(write)),
  });
}

// ============================================================================
// IsDerefPlace - Check if expression is a valid dereference place
// ============================================================================
//
// Determines whether an expression represents a dereference that can be
// used as a place (assignable location).
// ============================================================================

bool IsDerefPlace(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::AttributedExpr>) {
          return IsDerefPlace(node.expr);
        }
        return false;
      },
      expr->node);
}

}  // namespace ultraviolet::codegen
