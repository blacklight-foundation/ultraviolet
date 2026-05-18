// =============================================================================
// Expression Lowering: QualifiedApplyExpr
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16085-16102: Record and Enum literal construction
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - QualifiedApplyExpr visitor
//
// =============================================================================

#include "05_codegen/lower/expr/qualified_apply.h"
#include "00_core/assert_spec.h"

#include <variant>

namespace ultraviolet::codegen {

// =============================================================================
// LowerQualifiedApply - Lower a qualified apply expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Record) or (Lower-Expr-Enum)
//   Qualified apply expressions are constructor calls like:
//   - Record construction: Point{ x: 1, y: 2 } or Point(1, 2)
//   - Enum variant construction: Option::Some(value)
//
// This function lowers the arguments and produces the appropriate
// DerivedValueInfo for record or enum literals.
// =============================================================================

LowerResult LowerQualifiedApply(const ast::QualifiedApplyExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-QualifiedApply");

    return std::visit(
        [&ctx, &expr](const auto& args) -> LowerResult {
            using T = std::decay_t<decltype(args)>;

            if constexpr (std::is_same_v<T, ast::ParenArgs>) {
                // Tuple-style construction: Type(arg1, arg2, ...)
                SPEC_RULE("Lower-Expr-QualifiedApply-Paren");

                // Extract expressions from args
                std::vector<ast::ExprPtr> arg_exprs;
                arg_exprs.reserve(args.args.size());
                for (const auto& arg : args.args) {
                    arg_exprs.push_back(arg.value);
                }

                auto [ir, values] = LowerList(arg_exprs, ctx);

                // Create a tuple-style literal value
                IRValue result = ctx.FreshTempValue("qualified_apply");
                DerivedValueInfo info;
                info.kind = DerivedValueInfo::Kind::TupleLit;
                info.elements = values;
                ctx.RegisterDerivedValue(result, info);

                return LowerResult{ir, result};

            } else {
                // Brace-style construction: Type{ field: value, ... }
                SPEC_RULE("Lower-Expr-QualifiedApply-Brace");

                auto [ir, field_values] = LowerFieldInits(args.fields, ctx, true);

                IRValue result = RegisterLoweredRecordValue(
                    std::move(field_values),
                    std::nullopt,
                    "qualified_apply",
                    ctx);

                return LowerResult{ir, result};
            }
        },
        expr.args);
}

}  // namespace ultraviolet::codegen
