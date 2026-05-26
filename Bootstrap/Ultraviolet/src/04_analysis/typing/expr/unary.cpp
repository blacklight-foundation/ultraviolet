// =================================================================
// File: 04_analysis/typing/expr/unary.cpp
// Construct: Unary Expression Type Checking
// Spec Section: 5.2.13
// Spec Rules: T-Not-Bool, T-Not-Int, T-Neg, T-Modal-Widen
// =================================================================
#include "04_analysis/typing/expr/unary.h"

#include "00_core/assert_spec.h"
#include "04_analysis/typing/expr/expr_common.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/modal/modal_widen.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsUnary() {
  SPEC_DEF("T-Not-Bool", "5.2.13");
  SPEC_DEF("T-Not-Int", "5.2.13");
  SPEC_DEF("T-Neg", "5.2.13");
  SPEC_DEF("T-Modal-Widen", "5.2.13");
  SPEC_DEF("T-Modal-Widen-Perm", "5.2.13");
  SPEC_DEF("Widen-AlreadyGeneral", "5.2.13");
  SPEC_DEF("Widen-NonModal", "5.2.13");
}

}  // namespace

// Section 5.2.13 Unary Expression Typing
//
// Typing rule (T-Not-Bool):
// Gamma |- expr : bool
// --------------------------------------------------
// Gamma |- !expr : bool
//
// Typing rule (T-Not-Int):
// Gamma |- expr : T where T is integer type
// --------------------------------------------------
// Gamma |- !expr : T
//
// Typing rule (T-Neg):
// Gamma |- expr : T where T is signed integer or float
// --------------------------------------------------
// Gamma |- -expr : T
//
// Typing rule (T-Modal-Widen):
// Gamma |- expr : Modal@State
// --------------------------------------------------
// Gamma |- widen expr : Modal
//
// Unary operators:
// - ! (logical not): works on bool, returns bool
// - ! (bitwise not): works on integer types, returns same type
// - - (negation): works on signed integers and floats
// - widen: converts state-specific modal to general modal type
//
ExprTypeResult TypeUnaryExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::UnaryExpr& expr,
                                 const TypeEnv& env,
                                 const core::Span& span) {
  ExprTypeResult result;

  // Type the operand
  const auto operand = TypeExpr(
      ctx, WithSharedAccessMode(type_ctx, ast::KeyMode::Read), expr.value, env);
  if (!operand.ok) {
    result.diag_id = operand.diag_id;
    return result;
  }

  const std::string_view op = expr.op;

  // Handle widen operator for modal types
  if (op == "widen") {
    const auto stripped = StripPerm(operand.type);
    if (!stripped) {
      return result;
    }

    // Check if operand is a modal state type
    if (const auto* modal = std::get_if<TypeModalState>(&stripped->node)) {
      const auto* decl = LookupModalDecl(ctx, modal->path);
      if (!decl || !HasState(*decl, modal->state)) {
        return result;
      }

      // Emit warning for large payload widening
      WarnWidenLargePayload(ctx, type_ctx, span, modal->path, modal->state);

      // Preserve permission if present
      if (const auto* perm = std::get_if<TypePerm>(&operand.type->node)) {
        SPEC_RULE("T-Modal-Widen-Perm");
        result.ok = true;
        result.type = MakeTypePerm(perm->perm, ModalRefType(modal->modal_ref));
        return result;
      }

      SPEC_RULE("T-Modal-Widen");
      result.ok = true;
      result.type = ModalRefType(modal->modal_ref);
      return result;
    }

    // Check if already a general modal (error)
      if (const auto* path = AppliedTypePath(*stripped)) {
        if (LookupModalDecl(ctx, *path)) {
          SPEC_RULE("Widen-AlreadyGeneral");
          result.diag_id = "Widen-AlreadyGeneral";
        return result;
      }
    }

    SPEC_RULE("Widen-NonModal");
    result.diag_id = "Widen-NonModal";
    return result;
  }

  // For other operators, require primitive type after stripping wrappers
  const auto stripped = StripPerm(operand.type);
  const auto* refine = stripped ? std::get_if<TypeRefine>(&stripped->node) : nullptr;
  const auto base = refine ? StripPerm(refine->base) : stripped;
  const auto* prim = base ? std::get_if<TypePrim>(&base->node) : nullptr;
  if (!prim) {
    return result;
  }
  const std::string_view name = prim->name;

  // Handle logical/bitwise not
  if (op == "!") {
    if (name == "bool") {
      SPEC_RULE("T-Not-Bool");
      result.ok = true;
      result.type = MakeTypePrim("bool");
      return result;
    }
    if (IsIntType(name)) {
      SPEC_RULE("T-Not-Int");
      result.ok = true;
      result.type = MakeTypePrim(std::string(name));
      return result;
    }
    return result;
  }

  // Handle negation
  if (op == "-") {
    if (IsSignedIntType(name) || IsFloatType(name)) {
      SPEC_RULE("T-Neg");
      result.ok = true;
      result.type = MakeTypePrim(std::string(name));
      return result;
    }
    return result;
  }

  return result;
}

}  // namespace ultraviolet::analysis::expr
