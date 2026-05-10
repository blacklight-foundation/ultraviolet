// =============================================================================
// Expression Lowering: IndexAccessExpr
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16114-16141: Index access rules
//     - (Lower-Expr-Index-Scalar-Static): No bounds check for array with static index
//     - (Lower-Expr-Index-Scalar): Bounds check for slice or dynamic index
//     - (Lower-Expr-Index-Scalar-OOB): Out of bounds panic
//     - (Lower-Expr-Index-Range): Slice with range index
//     - (Lower-Expr-Index-Range-OOB): Range out of bounds panic
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 1533-1571: IndexAccessExpr visitor
//
// =============================================================================

#include "05_codegen/lower/expr/index_access.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/lower/pattern/ir_pattern.h"
#include "00_core/assert_spec.h"

#include <variant>

namespace cursive::codegen {

namespace {

// Helper to convert RangeVal to IRRange
IRRange ToIRRange(const RangeVal& range) {
    IRRange out;
    out.kind = ToIRRangeKind(range.kind);
    out.lo = range.lo;
    out.hi = range.hi;
    return out;
}

}  // namespace

// =============================================================================
// LowerIndexAccess - Lower an index access expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Index-Scalar)
//   Gamma |- LowerExpr(base) => <IR_b, v_b>
//   Gamma |- LowerExpr(index) => <IR_i, v_i>
//   NeedsCheck(base) = true
//   --------------------------------------------------------
//   Gamma |- LowerExpr(base[index]) => <SeqIR(IR_b, IR_i, CheckIndexIR, PanicCheck), v_elem>
//
// SPEC: (Lower-Expr-Index-Range)
//   Gamma |- LowerExpr(base) => <IR_b, v_b>
//   Gamma |- LowerRange(range) => <IR_r, range_val>
//   --------------------------------------------------------
//   Gamma |- LowerExpr(base[range]) => <SeqIR(IR_b, IR_r, CheckRangeIR, PanicCheck), v_slice>
//
// Index access expressions support:
// 1. Scalar index: base[i] - accesses single element
// 2. Range index: base[lo..hi] - creates a slice
//
// Bounds checking is required for:
// - Slices (always)
// - Arrays with dynamic indices
// - Arrays with [[dynamic]] attribute
// =============================================================================

LowerResult LowerIndexAccess(const ast::IndexAccessExpr& expr, LowerCtx& ctx) {
    ast::Expr access_expr;
    access_expr.node = expr;
    access_expr.span = expr.base ? expr.base->span : core::Span{};
    IRPtr key_ir = LowerImplicitKeyAccess(access_expr, ast::KeyMode::Read, ctx);

    // Lower the base expression
    auto base_result = LowerExpr(*expr.base, ctx);

    // Check if index is a range expression
    if (std::holds_alternative<ast::RangeExpr>(expr.index->node)) {
        SPEC_RULE("Lower-Expr-Index-Range");

        const auto& range_node = std::get<ast::RangeExpr>(expr.index->node);
        auto range_result = LowerRangeExpr(range_node, ctx);

        // Build bounds check
        IRCheckRange check;
        check.base = base_result.value;
        check.range = ToIRRange(range_result.value);

        // Create slice value
        IRValue slice_value = ctx.FreshTempValue("slice");
        DerivedValueInfo info;
        info.kind = DerivedValueInfo::Kind::Slice;
        info.base = base_result.value;
        info.range = ToIRRange(range_result.value);
        ctx.RegisterDerivedValue(slice_value, info);

        return LowerResult{
            SeqIR({base_result.ir, range_result.ir, key_ir,
                   MakeIR(std::move(check)), PanicFollowup(ctx)}),
            slice_value
        };
    }

    if (IsRangeIndexExpr(*expr.index, ctx)) {
        SPEC_RULE("Lower-Expr-Index-Range");

        auto range_result = LowerExpr(*expr.index, ctx);
        const auto range_kind = RangeIndexKindOf(*expr.index, ctx);

        IRCheckRange check;
        check.base = base_result.value;
        check.range_value = range_result.value;
        if (range_kind.has_value()) {
            check.range.kind = ToIRRangeKind(*range_kind);
        }

        IRValue slice_value = ctx.FreshTempValue("slice");
        DerivedValueInfo info;
        info.kind = DerivedValueInfo::Kind::Slice;
        info.base = base_result.value;
        info.range_value = range_result.value;
        if (range_kind.has_value()) {
            info.range.kind = ToIRRangeKind(*range_kind);
        }
        ctx.RegisterDerivedValue(slice_value, info);

        return LowerResult{
            SeqIR({base_result.ir, range_result.ir, key_ir,
                   MakeIR(std::move(check)), PanicFollowup(ctx)}),
            slice_value
        };
    }

    // Scalar index access
    SPEC_RULE("Lower-Expr-Index-Scalar");

    auto index_result = LowerExpr(*expr.index, ctx);

    // Determine if bounds check is needed
    const bool needs_check = NeedsIndexCheck(*expr.base, ctx);

    // Build bounds check
    IRCheckIndex check;
    check.base = base_result.value;
    check.index = index_result.value;

    // Create element value
    IRValue elem_value = ctx.FreshTempValue("index_elem");
    DerivedValueInfo info;
    info.kind = DerivedValueInfo::Kind::Index;
    info.base = base_result.value;
    info.index = index_result.value;
    ctx.RegisterDerivedValue(elem_value, info);

    // Build IR sequence
    std::vector<IRPtr> seq;
    seq.push_back(base_result.ir);
    seq.push_back(index_result.ir);
    seq.push_back(key_ir);
    if (needs_check) {
      seq.push_back(MakeIR(std::move(check)));
      seq.push_back(PanicFollowup(ctx));
    }

    return LowerResult{SeqIR(std::move(seq)), elem_value};
}

}  // namespace cursive::codegen
