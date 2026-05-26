// =============================================================================
// Expression Lowering: TupleAccessExpr
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16109-16112: (Lower-Expr-TupleAccess)
//     Gamma |- LowerExpr(base) => <IR_b, v_b>    TupleValue(v_b, i) = v_i
//     Gamma |- LowerExpr(TupleAccess(base, i)) => <IR_b, v_i>
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 1523-1532: TupleAccessExpr visitor
//
// =============================================================================

#include "05_codegen/lower/expr/tuple_access.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "00_core/assert_spec.h"
#include "04_analysis/typing/type_predicates.h"

#include <cstdlib>

namespace ultraviolet::codegen {

namespace {

analysis::TypeRef TupleElementTypeForIndex(analysis::TypeRef base_type,
                                           std::size_t tuple_index) {
    analysis::TypeRef stripped = analysis::StripPerm(base_type);
    if (!stripped) {
        stripped = base_type;
    }

    const auto* tuple =
        stripped ? std::get_if<analysis::TypeTuple>(&stripped->node) : nullptr;
    if (!tuple || tuple_index >= tuple->elements.size()) {
        return nullptr;
    }

    return tuple->elements[tuple_index];
}

}  // namespace

// =============================================================================
// LowerTupleAccess - Lower a tuple access expression to IR
// =============================================================================
// SPEC: (Lower-Expr-TupleAccess)
//   Gamma |- LowerExpr(base) => <IR_b, v_b>    TupleValue(v_b, i) = v_i
//   ----------------------------------------------------------------
//   Gamma |- LowerExpr(TupleAccess(base, i)) => <IR_b, v_i>
//
// Tuple access produces a derived value with Kind::Tuple that references:
// - The base tuple value
// - The element index to access
//
// The actual load is deferred to LLVM emission, which computes the offset
// based on the tuple type's layout.
// =============================================================================

LowerResult LowerTupleAccess(const ast::TupleAccessExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-TupleAccess");

    // Lower the base expression
    auto base_result = LowerExpr(*expr.base, ctx);
    const auto tuple_index = ast::TupleIndexToSize(expr.index).value();

    analysis::TypeRef base_type = ctx.LookupValueType(base_result.value);
    if (!base_type && ctx.expr_type) {
        base_type = ctx.expr_type(*expr.base);
    }
    if (base_type) {
        ctx.RegisterValueType(base_result.value, base_type);
    }

    // Create a fresh temporary for the element value
    IRValue elem_value = ctx.FreshTempValue("tuple_elem");
    if (analysis::TypeRef elem_type = TupleElementTypeForIndex(base_type, tuple_index)) {
        ctx.RegisterValueType(elem_value, elem_type);
    }

    // Register derived value info for tuple element access
    DerivedValueInfo info;
    info.kind = DerivedValueInfo::Kind::Tuple;
    info.base = base_result.value;
    info.tuple_index = tuple_index;
    ctx.RegisterDerivedValue(elem_value, info);

    return LowerResult{base_result.ir, elem_value};
}

// =============================================================================
// LowerReadPlaceTupleAccess - Lower tuple access for reading as a place
// =============================================================================
// SPEC: (Lower-ReadPlace-Tuple)
//   Similar to LowerTupleAccess but operates on places (l-values).
// =============================================================================

LowerResult LowerReadPlaceTupleAccess(const ast::TupleAccessExpr& node,
                                       const ast::Expr& place,
                                       LowerCtx& ctx) {
    SPEC_RULE("Lower-ReadPlace-Tuple");

    // Lower the base as a place
    auto base_result = LowerReadPlace(*node.base, ctx);

    // Create temporary for the element value
    IRValue elem_value = ctx.FreshTempValue("place_tuple_elem");

    // Register type if available
    if (ctx.expr_type) {
        ctx.RegisterValueType(elem_value, ctx.expr_type(place));
    }

    // Register derived value info
    DerivedValueInfo info;
    info.kind = DerivedValueInfo::Kind::Tuple;
    info.base = base_result.value;
    info.tuple_index = ast::TupleIndexToSize(node.index).value();
    ctx.RegisterDerivedValue(elem_value, info);

    return LowerResult{base_result.ir, elem_value};
}

}  // namespace ultraviolet::codegen
