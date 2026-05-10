// =============================================================================
// Expression Lowering: EnumLiteralExpr
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16090-16102: (Lower-Expr-Enum-Unit/Tuple/Record)
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 1262-1311: LowerEnumLiteral function
//
// =============================================================================

#include "05_codegen/lower/expr/enum_literal.h"
#include "00_core/assert_spec.h"

namespace cursive::codegen {

namespace {

analysis::TypeRef EnumLiteralLoweredType(const ast::Expr& source_expr,
                                         const LowerCtx& ctx) {
    if (!ctx.expr_type) {
        return nullptr;
    }
    analysis::TypeRef type = ctx.expr_type(source_expr);
    if (type && ctx.active_generic_type_subst.has_value() &&
        !ctx.active_generic_type_subst->empty()) {
        type = analysis::InstantiateType(type, *ctx.active_generic_type_subst);
    }
    return type;
}

void RegisterEnumLiteralLoweredType(const ast::Expr& source_expr,
                                    const IRValue& value,
                                    LowerCtx& ctx) {
    if (analysis::TypeRef type = EnumLiteralLoweredType(source_expr, ctx)) {
        ctx.RegisterValueType(value, type);
    }
}

}  // namespace

// =============================================================================
// LowerEnumLiteral - Lower an enum literal expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Enum-Unit)
//   EnumType(E, V) = unit_variant
//   -----------------------------------------------
//   Gamma |- LowerExpr(EnumLit(E::V)) => <epsilon, v_enum>
//
// SPEC: (Lower-Expr-Enum-Tuple)
//   EnumType(E, V) = tuple_variant    Gamma |- LowerList(es) => <IR, vs>
//   --------------------------------------------------------------------
//   Gamma |- LowerExpr(EnumLit(E::V(es))) => <IR, v_enum>
//
// SPEC: (Lower-Expr-Enum-Record)
//   EnumType(E, V) = record_variant    Gamma |- LowerFieldInits(fs) => <IR, fvs>
//   --------------------------------------------------------------------------
//   Gamma |- LowerExpr(EnumLit(E::V{fs})) => <IR, v_enum>
//
// Enum literals produce a DerivedValueInfo::EnumLit with:
// - The variant name
// - Payload elements (for tuple variants) or fields (for record variants)
// =============================================================================

LowerResult LowerEnumLiteral(const ast::Expr& source_expr,
                             const ast::EnumLiteralExpr& expr,
                             LowerCtx& ctx) {
    // Extract variant name from path
    std::string variant_name = expr.path.empty() ? std::string() : expr.path.back();
    std::vector<std::string> enum_path;
    if (expr.path.size() >= 2) {
      enum_path.assign(expr.path.begin(), expr.path.end() - 1);
    }

    // Unit variant: no payload
    if (!expr.payload_opt.has_value()) {
        SPEC_RULE("Lower-Expr-Enum-Unit");

        IRValue enum_value = ctx.FreshTempValue("enum_unit");
        DerivedValueInfo info;
        info.kind = DerivedValueInfo::Kind::EnumLit;
        info.variant = variant_name;
        info.static_path = enum_path;
        ctx.RegisterDerivedValue(enum_value, info);
        RegisterEnumLiteralLoweredType(source_expr, enum_value, ctx);

        return LowerResult{EmptyIR(), enum_value};
    }

    // Tuple or record variant
    return std::visit(
        [&ctx, &source_expr, &variant_name, &enum_path](const auto& payload) -> LowerResult {
            using T = std::decay_t<decltype(payload)>;

            if constexpr (std::is_same_v<T, ast::EnumPayloadParen>) {
                // Tuple variant: E::V(e1, e2, ...)
                SPEC_RULE("Lower-Expr-Enum-Tuple");

                auto [ir, values] = LowerList(payload.elements, ctx);

                IRValue enum_value = ctx.FreshTempValue("enum_tuple");
                DerivedValueInfo info;
                info.kind = DerivedValueInfo::Kind::EnumLit;
                info.variant = variant_name;
                info.static_path = enum_path;
                info.payload_elems = values;
                ctx.RegisterDerivedValue(enum_value, info);
                RegisterEnumLiteralLoweredType(source_expr, enum_value, ctx);

                return LowerResult{ir, enum_value};
            } else {
                // Record variant: E::V{f1: e1, f2: e2, ...}
                SPEC_RULE("Lower-Expr-Enum-Record");

                auto [ir, field_values] = LowerFieldInits(payload.fields, ctx, true);

                IRValue enum_value = ctx.FreshTempValue("enum_record");
                DerivedValueInfo info;
                info.kind = DerivedValueInfo::Kind::EnumLit;
                info.variant = variant_name;
                info.static_path = enum_path;
                info.payload_fields = field_values;
                ctx.RegisterDerivedValue(enum_value, info);
                RegisterEnumLiteralLoweredType(source_expr, enum_value, ctx);

                return LowerResult{ir, enum_value};
            }
        },
        *expr.payload_opt);
}

}  // namespace cursive::codegen
