// =============================================================================
// Expression Lowering: AllocExpr
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16253-16256: (Lower-Expr-Alloc)
//     Gamma |- LowerExpr(e) => <IR_e, v>
//     Gamma |- LowerExpr(AllocExpr(r_opt, e)) => <SeqIR(IR_e, AllocIR(r_opt, v)), v_alloc>
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 1662-1706: AllocExpr visitor
//
// =============================================================================

#include "05_codegen/lower/expr/alloc_expr.h"
#include "00_core/assert_spec.h"

namespace cursive::codegen {

// =============================================================================
// LowerAllocExpr - Lower an allocation expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Alloc)
//   Gamma |- LowerExpr(e) => <IR_e, v>
//   Gamma |- LowerExpr(AllocExpr(r_opt, e)) => <SeqIR(IR_e, AllocIR(r_opt, v)), v_alloc>
//
// Allocation expressions (^expr) allocate a value in a region:
// 1. Lower the value expression to get the value to allocate
// 2. Determine the target region (optional name or innermost active region)
// 3. Emit IRAlloc to perform the allocation
// 4. Return a derived value representing the allocated value
//
// NOTES:
//   - ^expr allocates in the innermost active region
//   - r^expr allocates in the specified region r
//   - The result is a pointer (Ptr<T>@Valid) to the allocated value
// =============================================================================

LowerResult LowerAllocExpr(const ast::Expr& expr,
                           const ast::AllocExpr& alloc,
                           LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Alloc");

    // The allocated value is consumed by AllocIR and stored into the target
    // region. Its top-level value must remain owned by the allocation result.
    auto prev_suppress = ctx.suppress_temp_at_depth;
    ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
    auto value_result = LowerExpr(*alloc.value, ctx);
    ctx.suppress_temp_at_depth = prev_suppress;

    // Get the type of the value being allocated
    analysis::TypeRef value_type;
    if (ctx.expr_type) {
        value_type = ctx.expr_type(*alloc.value);
    }
    if (!value_type) {
        value_type = ctx.LookupValueType(value_result.value);
    }

    // Build the IRAlloc node
    IRAlloc ir_alloc;
    ir_alloc.value = value_result.value;
    ir_alloc.type = value_type;

    std::optional<ast::Identifier> resolved_region = alloc.region_opt;
    if (!resolved_region.has_value() && !ctx.active_region_aliases.empty()) {
        resolved_region = ctx.active_region_aliases.back();
    }

    // Handle named or inherited active-region allocation.
    if (resolved_region) {
        ast::IdentifierExpr ident;
        ident.name = *resolved_region;
        ast::Expr region_expr;
        region_expr.span = expr.span;
        region_expr.node = ident;

        auto region_result = LowerExpr(region_expr, ctx);
        ir_alloc.region = region_result.value;

        IRValue ptr_value = ctx.FreshTempValue("alloc_ptr");
        ir_alloc.result = ptr_value;

        IRValue alloc_val = ctx.FreshTempValue("alloc_val");
        DerivedValueInfo info;
        info.kind = DerivedValueInfo::Kind::LoadFromAddr;
        info.base = ptr_value;
        ctx.RegisterDerivedValue(alloc_val, info);
        if (value_type) {
            ctx.RegisterValueType(alloc_val, value_type);
        }

        return LowerResult{
            SeqIR({value_result.ir, region_result.ir, MakeIR(std::move(ir_alloc))}),
            alloc_val
        };
    }

    // Implicit region allocation: ^expr
    // Uses the innermost active region
    IRValue ptr_value = ctx.FreshTempValue("alloc_ptr");
    ir_alloc.result = ptr_value;

    IRValue alloc_val = ctx.FreshTempValue("alloc_val");
    DerivedValueInfo info;
    info.kind = DerivedValueInfo::Kind::LoadFromAddr;
    info.base = ptr_value;
    ctx.RegisterDerivedValue(alloc_val, info);
    if (value_type) {
        ctx.RegisterValueType(alloc_val, value_type);
    }

    return LowerResult{
        SeqIR({value_result.ir, MakeIR(std::move(ir_alloc))}),
        alloc_val
    };
}

}  // namespace cursive::codegen
