// =============================================================================
// MIGRATION MAPPING: expr/array_literal.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16080-16083: (Lower-Expr-Array)
//     Gamma |- LowerList(es) => <IR, vec_v>
//     ------------------------------------------
//     Gamma |- LowerExpr(ArrayExpr(es)) => <IR, [v_1, ..., v_n]>
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - ArrayExpr visitor lowers each segment in order via LowerExpr
//   - ArrayRepeatExpr support is normalized onto ArraySegments
//   - Creates DerivedValueInfo of kind ArraySegments
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRValue, IRPtr)
//   - ultraviolet/include/05_codegen/lower/lower_expr.h (LowerCtx, LowerResult, LowerList)
//
// IMPLEMENTATION NOTES:
//   1. Array literals lower each segment expression left-to-right
//   2. Repeat segments lower both value and count expressions
//   3. The resulting IRValue is a synthetic temp representing the array aggregate
//   4. The segment/repeat info are stored in a DerivedValueInfo
//   5. Materialization happens when the array value is used (stored, returned, etc.)
//
// =============================================================================

#include "05_codegen/lower/expr/array_literal.h"
#include "00_core/assert_spec.h"
#include "04_analysis/typing/type_equiv.h"

namespace ultraviolet::codegen {

// =============================================================================
// LowerArrayLiteral - Lower an array literal expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Array)
//   Gamma |- LowerArraySegments(segs) => <IR, vec_v>
//   ------------------------------------------
//   Gamma |- LowerExpr(ArrayExpr(segs)) => <IR, [v_1, ..., v_n]>
//
// Array literal expressions lower their element expressions left-to-right,
// then produce a synthetic array value that tracks the element values via the
// DerivedValueInfo mechanism. The actual array aggregate is materialized
// when the value is stored or otherwise consumed.
// =============================================================================

LowerResult LowerArrayLiteral(const ast::ArrayExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Array");

    auto [ir, lowered_segments] = LowerList(expr.elements, ctx);

    // Create a synthetic value to represent the array
    IRValue array_value = ctx.FreshTempValue("array");

    DerivedValueInfo info;
    info.kind = DerivedValueInfo::Kind::ArraySegments;
    info.array_segments = std::move(lowered_segments);
    ctx.RegisterDerivedValue(array_value, info);

    // Preserve the concrete array type when the element type is known and the
    // literal has a statically known element count.
    std::optional<analysis::TypeRef> element_type;
    bool homogeneous = true;
    std::size_t element_count = 0;
    for (const auto& segment : info.array_segments) {
        analysis::TypeRef current_type = ctx.LookupValueType(segment.value);
        if (!current_type) {
            homogeneous = false;
            break;
        }
        if (!element_type.has_value()) {
            element_type = current_type;
        } else {
            const auto equiv = analysis::TypeEquiv(*element_type, current_type);
            if (!equiv.ok || !equiv.equiv) {
                homogeneous = false;
                break;
            }
        }
        if (segment.kind == DerivedArraySegment::Kind::Element) {
            ++element_count;
        } else {
            homogeneous = false;
            break;
        }
    }
    if (homogeneous && element_type.has_value()) {
        ctx.RegisterValueType(
            array_value,
            analysis::MakeTypeArray(*element_type,
                                    static_cast<std::uint64_t>(element_count)));
    }

    return LowerResult{ir, array_value};
}

// =============================================================================
// LowerArrayRepeat - Lower an array repeat expression to IR
// =============================================================================

LowerResult LowerArrayRepeat(const ast::ArrayRepeatExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Array");

    // Lower value and count expressions
    auto value_result = LowerExpr(*expr.value, ctx);
    auto count_result = LowerExpr(*expr.count, ctx);

    // Create a synthetic value to represent the array
    IRValue array_value = ctx.FreshTempValue("array_repeat");

    // Normalize the repeat form onto the segmented-array path so all array
    // aggregate materialization follows one implementation.
    DerivedValueInfo info;
    info.kind = DerivedValueInfo::Kind::ArraySegments;
    DerivedArraySegment derived_segment;
    derived_segment.kind = DerivedArraySegment::Kind::Repeat;
    derived_segment.value = value_result.value;
    derived_segment.count = count_result.value;
    info.array_segments.push_back(std::move(derived_segment));
    ctx.RegisterDerivedValue(array_value, info);

    return LowerResult{SeqIR({value_result.ir, count_result.ir}), array_value};
}

}  // namespace ultraviolet::codegen
