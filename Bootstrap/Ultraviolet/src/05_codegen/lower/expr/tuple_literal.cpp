// =============================================================================
// MIGRATION MAPPING: expr/tuple_literal.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16075-16078: (Lower-Expr-Tuple)
//     Gamma |- LowerList(es) => <IR, vec_v>
//     ------------------------------------------
//     Gamma |- LowerExpr(TupleExpr(es)) => <IR, (v_1, ..., v_n)>
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - TupleExpr visitor lowers each element in order via LowerList
//   - Creates a DerivedValueInfo of kind TupleLit to represent the tuple value
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRValue, IRPtr)
//   - ultraviolet/include/05_codegen/lower/lower_expr.h (LowerCtx, LowerResult, LowerList)
//
// IMPLEMENTATION NOTES:
//   1. Tuple literals lower each element expression left-to-right via LowerList
//   2. The resulting IRValue is a synthetic temp representing the tuple aggregate
//   3. The elements are stored in a DerivedValueInfo with kind TupleLit
//   4. Materialization happens when the tuple value is used (stored, returned, etc.)
//
// =============================================================================

#include "05_codegen/lower/expr/tuple_literal.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/typing/type_predicates.h"
#include "00_core/assert_spec.h"

namespace ultraviolet::codegen {

// =============================================================================
// LowerTuple - Lower a tuple literal expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Tuple)
//   Gamma |- LowerList(es) => <IR, vec_v>
//   ------------------------------------------
//   Gamma |- LowerExpr(TupleExpr(es)) => <IR, (v_1, ..., v_n)>
//
// Tuple expressions lower their element expressions left-to-right, then
// produce a synthetic tuple value that tracks the element values via the
// DerivedValueInfo mechanism. The actual tuple aggregate is materialized
// when the value is stored or otherwise consumed.
// =============================================================================

LowerResult LowerTuple(
    const ast::TupleExpr& expr,
    LowerCtx& ctx,
    analysis::TypeRef contextual_type) {
    SPEC_RULE("Lower-Expr-Tuple");
    SPEC_RULE("rule.16.Lower-Expr-Tuple");

    if (expr.elements.empty()) {
        analysis::TypeRef unit_type = analysis::MakeTypePrim("()");
        ast::Token unit_token;
        unit_token.kind = ast::TokenKind::Punctuator;
        unit_token.lexeme = "()";
        if (auto bytes = analysis::layout::EncodeConst(unit_type, unit_token)) {
            (void)analysis::layout::ValidValue(
                ScopeForLowering(ctx),
                unit_type,
                *bytes);
        }

        IRValue unit_value = ctx.FreshTempValue("unit");
        ctx.RegisterValueType(unit_value, unit_type);
        return LowerResult{EmptyIR(), unit_value};
    }

    // Lower all element expressions in left-to-right order
    auto [ir, values] = LowerList(expr.elements, ctx);

    // Create a synthetic value to represent the tuple
    IRValue tuple_value = ctx.FreshTempValue("tuple");

    // Register the derived value info so materialization can access elements
    DerivedValueInfo info;
    info.kind = DerivedValueInfo::Kind::TupleLit;
    info.elements = values;
    ctx.RegisterDerivedValue(tuple_value, info);

    // Preserve the concrete tuple type at the tuple-literal definition site.
    // This makes tuple materialization explicit in the lowering path rather
    // than relying on generic post-lowering inference.
    analysis::TypeRef tuple_type;
    if (contextual_type) {
        analysis::TypeRef stripped_type = analysis::StripPerm(contextual_type);
        if (!stripped_type) {
            stripped_type = contextual_type;
        }
        const auto* contextual_tuple = stripped_type
            ? std::get_if<analysis::TypeTuple>(&stripped_type->node)
            : nullptr;
        if (contextual_tuple && contextual_tuple->elements.size() == values.size()) {
            tuple_type = stripped_type;
            for (std::size_t i = 0; i < values.size(); ++i) {
                ctx.RegisterValueType(values[i], contextual_tuple->elements[i]);
            }
        }
    }

    if (!tuple_type) {
        std::vector<analysis::TypeRef> element_types;
        element_types.reserve(values.size());
        bool all_typed = true;
        for (const auto& value : values) {
            analysis::TypeRef element_type = ctx.LookupValueType(value);
            if (!element_type) {
                all_typed = false;
                break;
            }
            element_types.push_back(element_type);
        }
        if (all_typed) {
            tuple_type = analysis::MakeTypeTuple(std::move(element_types));
        }
    }
    if (tuple_type) {
        ctx.RegisterValueType(tuple_value, tuple_type);
    }

    return LowerResult{ir, tuple_value};
}

}  // namespace ultraviolet::codegen
