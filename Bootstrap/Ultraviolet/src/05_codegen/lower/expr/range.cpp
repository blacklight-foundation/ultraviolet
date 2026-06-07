// =============================================================================
// MIGRATION MAPPING: expr/range.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16198-16201: (Lower-Expr-Range)
//     Gamma |- LowerRangeExpr(Range(kind, lo_opt, hi_opt)) => <IR, v>
//     ----------------------------------------------------------------
//     Gamma |- LowerExpr(Range(kind, lo_opt, hi_opt)) => <IR, v>
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/checks.cpp
//   - Lines 402-447: LowerRangeExpr implementation
//   - RangeVal produced with kind and optional lo/hi IRValues
//   - Handles all range kinds: Full, From, To, ToInclusive, Exclusive, Inclusive
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRRange, IRRangeKind)
//   - ultraviolet/include/05_codegen/checks/checks.h (RangeVal, LowerRangeResult)
//
// REFACTORING NOTES:
//   - Range expressions lower to a RangeVal with optional lo/hi values
//   - The kind field from ast::RangeKind is converted to IRRangeKind
//   - IR is produced for lowering any bound expressions (lo, hi)
//   - Range values are used for slicing, iteration, and pattern matching
//
// RANGE KINDS:
//   - Full:        .. (no bounds)
//   - From:        start.. (only lo bound)
//   - To:          ..end (only hi bound, exclusive)
//   - ToInclusive: ..=end (only hi bound, inclusive)
//   - Exclusive:   start..end (both bounds, exclusive hi)
//   - Inclusive:   start..=end (both bounds, inclusive hi)
//
// =============================================================================

#include "05_codegen/lower/expr/range.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/lower/pattern/ir_pattern.h"
#include "04_analysis/typing/type_predicates.h"
#include "00_core/assert_spec.h"

namespace ultraviolet::codegen {

namespace {

// Helper to convert RangeVal to IRRange for derived value registration
IRRange ToIRRange(const RangeVal& range) {
    IRRange out;
    out.kind = ToIRRangeKind(range.kind);
    out.lo = range.lo;
    out.hi = range.hi;
    return out;
}

bool IsRangeMemberKind(const analysis::TypeRef& type, IRRangeKind kind) {
    analysis::TypeRef stripped = analysis::StripPerm(type);
    if (!stripped) {
        return false;
    }

    switch (kind) {
        case IRRangeKind::To:
            return std::holds_alternative<analysis::TypeRangeTo>(
                stripped->node);
        case IRRangeKind::ToInclusive:
            return std::holds_alternative<analysis::TypeRangeToInclusive>(
                stripped->node);
        case IRRangeKind::Full:
            return std::holds_alternative<analysis::TypeRangeFull>(
                stripped->node);
        case IRRangeKind::From:
            return std::holds_alternative<analysis::TypeRangeFrom>(
                stripped->node);
        case IRRangeKind::Exclusive:
            return std::holds_alternative<analysis::TypeRange>(
                stripped->node);
        case IRRangeKind::Inclusive:
            return std::holds_alternative<analysis::TypeRangeInclusive>(
                stripped->node);
    }

    return false;
}

analysis::TypeRef UniqueRangeUnionMember(
    const analysis::TypeRef& contextual_type,
    IRRangeKind kind) {
    analysis::TypeRef stripped = analysis::StripPerm(contextual_type);
    const auto* union_type =
        stripped ? std::get_if<analysis::TypeUnion>(&stripped->node) : nullptr;
    if (!union_type) {
        return nullptr;
    }

    analysis::TypeRef match;
    for (const analysis::TypeRef& member : union_type->members) {
        if (!IsRangeMemberKind(member, kind)) {
            continue;
        }
        if (match) {
            return nullptr;
        }
        match = member;
    }
    return match;
}

}  // namespace

// =============================================================================
// LowerRangeExpr - Lower a range expression to IR and RangeVal
// =============================================================================
// SPEC: (Lower-Expr-Range)
//   Gamma |- LowerRangeExpr(Range(kind, lo_opt, hi_opt)) => <IR, v>
//   ----------------------------------------------------------------
//   Gamma |- LowerExpr(Range(kind, lo_opt, hi_opt)) => <IR, v>
//
// Range expressions produce a RangeVal describing the bounds. The IR
// contains any instructions needed to evaluate the bound expressions.
// =============================================================================

LowerRangeResult LowerRangeExpr(const ast::RangeExpr& expr, LowerCtx& ctx) {
    RangeVal value;
    value.kind = expr.kind;
    std::vector<IRPtr> ir_parts;

    // Helper lambda to lower an optional bound expression.
    auto lower_opt = [&](const ast::ExprPtr& opt, std::optional<IRValue>& out) {
        auto result = LowerOpt(opt, ctx);
        ir_parts.push_back(result.first);
        out = result.second;
    };

    // Lower bounds based on range kind
    switch (expr.kind) {
        case ast::RangeKind::Full:
            // Lower-Range-Full: no subexpressions
            SPEC_RULE("Lower-Range-Full");
            lower_opt(expr.lhs, value.lo);
            lower_opt(expr.rhs, value.hi);
            break;

        case ast::RangeKind::From:
            // Lower-Range-From: only lo bound
            SPEC_RULE("Lower-Range-From");
            lower_opt(expr.lhs, value.lo);
            lower_opt(expr.rhs, value.hi);
            break;

        case ast::RangeKind::To:
            // Lower-Range-To: only hi bound (exclusive)
            SPEC_RULE("Lower-Range-To");
            lower_opt(expr.lhs, value.lo);
            lower_opt(expr.rhs, value.hi);
            break;

        case ast::RangeKind::ToInclusive:
            // Lower-Range-ToInclusive: only hi bound (inclusive)
            SPEC_RULE("Lower-Range-ToInclusive");
            lower_opt(expr.lhs, value.lo);
            lower_opt(expr.rhs, value.hi);
            break;

        case ast::RangeKind::Exclusive:
            // Lower-Range-Exclusive: both bounds, hi is exclusive
            SPEC_RULE("Lower-Range-Exclusive");
            lower_opt(expr.lhs, value.lo);
            lower_opt(expr.rhs, value.hi);
            break;

        case ast::RangeKind::Inclusive:
            // Lower-Range-Inclusive: both bounds, hi is inclusive
            SPEC_RULE("Lower-Range-Inclusive");
            lower_opt(expr.lhs, value.lo);
            lower_opt(expr.rhs, value.hi);
            break;
    }

    RecordLoweringChecksJudgementMember(
        ctx,
        LoweringChecksJudgementMember::LowerRangeExpr);
    return LowerRangeResult{SeqIR(std::move(ir_parts)), value};
}

// =============================================================================
// LowerRange - Lower a range expression as a full LowerExpr call
// =============================================================================
// This function wraps LowerRangeExpr to produce a LowerResult suitable for
// use in the main LowerExpr dispatch. The RangeVal is captured in a
// DerivedValueInfo for later materialization.
// =============================================================================

LowerResult LowerRange(const ast::Expr& expr,
                       const ast::RangeExpr& range_expr,
                       LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Range");
    SPEC_RULE("rule.16.Lower-Expr-Range");

    auto range_result = LowerRangeExpr(range_expr, ctx);

    // Create a fresh temporary value to represent the range
    IRValue range_value = ctx.FreshTempValue("range");

    // Register the range as a derived value for later materialization
    DerivedValueInfo info;
    info.kind = DerivedValueInfo::Kind::RangeLit;
    info.range = ToIRRange(range_result.value);
    ctx.RegisterDerivedValue(range_value, info);
    if (ctx.expr_type) {
        if (analysis::TypeRef range_type = ctx.expr_type(expr)) {
            if (analysis::TypeRef source_type =
                    UniqueRangeUnionMember(range_type, info.range.kind)) {
                range_type = source_type;
            }
            ctx.RegisterValueType(range_value, range_type);
        }
    }

    return LowerResult{range_result.ir, range_value};
}

}  // namespace ultraviolet::codegen
