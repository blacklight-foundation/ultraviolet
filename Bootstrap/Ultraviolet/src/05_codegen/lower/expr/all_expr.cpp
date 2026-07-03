// =============================================================================
// Expression Lowering: AllExpr
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 11.5 (All Expression)
//   - all { e1, e2, ... } waits for all to complete
//   - If any fails, cancels remaining and propagates first error
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 2761-2789: AllExpr visitor
//
// =============================================================================

#include "05_codegen/lower/expr/all_expr.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/outcome.h"
#include "00_core/assert_spec.h"

namespace ultraviolet::codegen {

namespace {

void AppendDistinctType(std::vector<analysis::TypeRef>& members,
                        const analysis::TypeRef& candidate) {
    if (!candidate) {
        return;
    }
    if (const auto* uni = std::get_if<analysis::TypeUnion>(&candidate->node)) {
        for (const auto& member : uni->members) {
            AppendDistinctType(members, member);
        }
        return;
    }
    for (const auto& existing : members) {
        if (!existing) {
            continue;
        }
        const auto equiv = analysis::TypeEquiv(existing, candidate);
        if (equiv.ok && equiv.equiv) {
            return;
        }
    }
    members.push_back(candidate);
}

}  // namespace

// =============================================================================
// LowerAllExpr - Lower an all expression to IR
// =============================================================================
// SPEC: (Lower-Expr-All)
//   Gamma |- LowerExpr(e1) => <IR_1, v_1>
//   ...
//   Gamma |- LowerExpr(en) => <IR_n, v_n>
//   --------------------------------------------------------
//   Gamma |- LowerExpr(all { e1, ..., en }) => <IRAll, v_tuple>
//
// The all expression:
// 1. Lowers each sub-expression (typically async futures)
// 2. Waits for all to complete
// 3. If any fails, cancels remaining and propagates first error
// 4. Returns a tuple of all results
// =============================================================================

LowerResult LowerAllExpr(const ast::AllExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-All");
    SPEC_RULE("def.21.AsyncComposeIR");
    SPEC_RULE("rule.21.Lower-Expr-All");

    // Build the IRAll node
    IRAll all;
    std::vector<analysis::TypeRef> tuple_elems;
    std::vector<analysis::TypeRef> error_types;
    tuple_elems.reserve(expr.exprs.size());
    error_types.reserve(expr.exprs.size());

    // Lower each async expression
    for (const auto& expr_ptr : expr.exprs) {
        auto result = LowerExpr(*expr_ptr, ctx);
        all.async_irs.push_back(result.ir);
        all.async_values.push_back(result.value);

        // Extract result and error types from async signature
        if (ctx.expr_type) {
            analysis::TypeRef async_type = ctx.expr_type(*expr_ptr);
            if (auto sig = analysis::GetAsyncSig(async_type)) {
                tuple_elems.push_back(sig->result);
                error_types.push_back(sig->err);
            }
        }
    }

    // Create result value
    all.result = ctx.FreshTempValue("all_result");
    IRValue result = all.result;

    // Build tuple type for results if all types were resolved
    analysis::TypeRef tuple_type = nullptr;
    if (tuple_elems.size() == expr.exprs.size()) {
        tuple_type = analysis::MakeTypeTuple(tuple_elems);
        all.tuple_type = tuple_type;
    }
    all.error_types = std::move(error_types);

    // `all` yields Outcome<tuple of results, union of arm errors>.
    analysis::TypeRef result_type = nullptr;
    if (tuple_type) {
        std::vector<analysis::TypeRef> error_members;
        error_members.reserve(all.error_types.size());
        for (const auto& err : all.error_types) {
            AppendDistinctType(error_members, err);
        }
        analysis::TypeRef error_union;
        if (error_members.empty()) {
            error_union = analysis::MakeTypePrim("!");
        } else if (error_members.size() == 1) {
            error_union = error_members.front();
        } else {
            error_union = analysis::MakeTypeUnion(std::move(error_members));
        }
        result_type = analysis::MakeOutcomeType(tuple_type, error_union);
    }
    if (!result_type) {
        result_type = analysis::MakeTypePrim("()");
    }
    ctx.RegisterValueType(result, result_type);

    return LowerResult{MakeIR(std::move(all)), result};
}

}  // namespace ultraviolet::codegen
