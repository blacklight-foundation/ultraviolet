// =============================================================================
// MIGRATION MAPPING: expr/tuple_literal.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
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

LowerResult LowerTuple(const ast::TupleExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Tuple");

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
        ctx.RegisterValueType(tuple_value, analysis::MakeTypeTuple(std::move(element_types)));
    }

    return LowerResult{ir, tuple_value};
}

}  // namespace ultraviolet::codegen
