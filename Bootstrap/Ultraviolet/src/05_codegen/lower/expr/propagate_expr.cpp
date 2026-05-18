// =============================================================================
// Expression Lowering: PropagateExpr
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16188-16196: (Lower-Expr-Propagate-Success) and (Lower-Expr-Propagate-Return)
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_calls.cpp
//   - Lines 1016-1063: LowerPropagateExpr function
//
// =============================================================================

#include "05_codegen/lower/expr/propagate_expr.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/checks/checks.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/subtyping.h"
#include "04_analysis/typing/outcome.h"
#include "04_analysis/typing/type_predicates.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/layout/layout.h"
#include "00_core/assert_spec.h"
#include "00_core/process_config.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <variant>

namespace ultraviolet::codegen {

namespace {

analysis::TypeRef ResolvePropagateAliasType(const analysis::TypeRef& type,
                                            const LowerCtx& ctx,
                                            std::size_t depth = 0) {
    analysis::TypeRef stripped = analysis::StripPerm(type);
    if (!stripped) {
        stripped = type;
    }
    if (!stripped || depth > 16 || !ctx.sigma) {
        return stripped;
    }

    const auto* path = std::get_if<analysis::TypePathType>(&stripped->node);
    if (!path) {
        return stripped;
    }

    ast::Path syntax_path;
    syntax_path.reserve(path->path.size());
    for (const auto& segment : path->path) {
        syntax_path.push_back(segment);
    }
    const ast::TypeAliasDecl* alias = nullptr;
    if (const auto it = ctx.sigma->types.find(analysis::PathKeyOf(syntax_path));
        it != ctx.sigma->types.end()) {
        alias = std::get_if<ast::TypeAliasDecl>(&it->second);
    }
    if (!alias && syntax_path.size() == 1) {
        const analysis::ScopeContext& scope = ScopeForLowering(ctx);
        const auto resolved = analysis::ResolveTypeName(scope, syntax_path.front());
        if (resolved.has_value() && resolved->origin_opt.has_value()) {
            ast::Path full_path = *resolved->origin_opt;
            full_path.push_back(resolved->target_opt.value_or(syntax_path.front()));
            if (const auto it = ctx.sigma->types.find(analysis::PathKeyOf(full_path));
                it != ctx.sigma->types.end()) {
                alias = std::get_if<ast::TypeAliasDecl>(&it->second);
            }
        }
    }
    if (!alias) {
        return stripped;
    }

    const analysis::ScopeContext& scope = ScopeForLowering(ctx);
    const auto lowered = analysis::layout::LowerTypeForLayout(scope, alias->type);
    if (!lowered.has_value()) {
        return stripped;
    }

    analysis::TypeRef expanded = *lowered;
    if (alias->generic_params &&
        !alias->generic_params->params.empty() &&
        !path->generic_args.empty()) {
        analysis::TypeSubst subst =
            analysis::BuildSubstitution(alias->generic_params->params,
                                        path->generic_args);
        expanded = analysis::InstantiateType(expanded, subst);
    }

    return ResolvePropagateAliasType(expanded, ctx, depth + 1);
}

// Check if a type is a subtype of the procedure return type.
// Returns the success member type if the expression type is a union where
// exactly one member is not a subtype of the return type (that's the success).
std::optional<analysis::TypeRef> SuccessMemberType(const analysis::ScopeContext& scope,
                                                   const analysis::TypeRef& ret_type,
                                                   const analysis::TypeRef& expr_type) {
    if (!ret_type || !expr_type) {
        return std::nullopt;
    }
    analysis::TypeRef propagate_target = ret_type;
    if (const auto async_sig = analysis::GetAsyncSig(ret_type)) {
        propagate_target = async_sig->err;
    }
    if (!propagate_target) {
        return std::nullopt;
    }
    const auto* uni = std::get_if<analysis::TypeUnion>(&expr_type->node);
    if (!uni) {
        return std::nullopt;
    }

    std::optional<analysis::TypeRef> success;
    for (const auto& member : uni->members) {
        const auto sub = analysis::Subtyping(scope, member, propagate_target);
        if (!sub.ok) {
            return std::nullopt;
        }
        if (!sub.subtype) {
            if (success.has_value()) {
                // Multiple non-subtype members - ambiguous
                return std::nullopt;
            }
            success = member;
        }
    }
    if (!success.has_value()) {
        return std::nullopt;
    }
    // Verify all other members are subtypes of the return type
    for (const auto& member : uni->members) {
        if (member == *success) {
            continue;
        }
        const auto sub = analysis::Subtyping(scope, member, propagate_target);
        if (!sub.ok || !sub.subtype) {
            return std::nullopt;
        }
    }
    return success;
}

bool IsUnitType(const analysis::TypeRef& type) {
    if (!type) {
        return false;
    }
    analysis::TypeRef stripped = analysis::StripPerm(type);
    if (!stripped) {
        return false;
    }
    if (const auto* prim = std::get_if<analysis::TypePrim>(&stripped->node)) {
        return prim->name == "()";
    }
    return false;
}

bool IsNeverType(const analysis::TypeRef& type) {
    if (!type) {
        return false;
    }
    analysis::TypeRef stripped = analysis::StripPerm(type);
    if (!stripped) {
        return false;
    }
    if (const auto* prim = std::get_if<analysis::TypePrim>(&stripped->node)) {
        return prim->name == "!";
    }
    return false;
}

std::optional<std::size_t> FindMemberIndex(const std::vector<analysis::TypeRef>& members,
                                           const analysis::TypeRef& target) {
    if (!target) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < members.size(); ++i) {
        const auto equiv = analysis::TypeEquiv(members[i], target);
        if (equiv.ok && equiv.equiv) {
            return i;
        }
    }
    return std::nullopt;
}

IRPatternPtr OutcomeStatePattern(std::string state) {
    auto pattern = std::make_shared<IRPattern>();
    pattern->node = IRModalPattern{std::move(state), std::nullopt};
    return pattern;
}

IRValue RegisterOutcomeFieldValue(const IRValue& base,
                                  std::string state,
                                  std::string field,
                                  analysis::TypeRef field_type,
                                  LowerCtx& ctx) {
    IRValue value = ctx.FreshTempValue("outcome_" + field);
    DerivedValueInfo info;
    info.kind = DerivedValueInfo::Kind::ModalField;
    info.base = base;
    info.modal_state = std::move(state);
    info.field = std::move(field);
    ctx.RegisterDerivedValue(value, std::move(info));
    if (field_type) {
        ctx.RegisterValueType(value, field_type);
    }
    return value;
}

IRValue RegisterOutcomeErrorReturnValue(const IRValue& error_value,
                                        const analysis::OutcomeSig& return_sig,
                                        const analysis::TypeRef& return_type,
                                        LowerCtx& ctx,
                                        std::vector<IRPtr>& ir_parts) {
    analysis::TypeRef state_type = analysis::MakeOutcomeStateType(
        return_sig.value,
        return_sig.error,
        "Error");

    IRValue state_value = ctx.FreshTempValue("propagate_error_outcome");
    DerivedValueInfo record_info;
    record_info.kind = DerivedValueInfo::Kind::RecordLit;
    record_info.fields = {{"error", error_value}};
    ctx.RegisterDerivedValue(state_value, std::move(record_info));
    ctx.RegisterValueType(state_value, state_type);

    IRValue widened = ctx.FreshTempValue("propagate_error_return");
    IRUnaryOp widen;
    widen.op = "widen";
    widen.operand = state_value;
    widen.result = widened;
    widen.operand_type = state_type;
    widen.result_type = return_type;
    ctx.RegisterValueType(widened, return_type);
    ir_parts.push_back(MakeIR(std::move(widen)));
    return widened;
}

}  // namespace

// =============================================================================
// LowerPropagateExpr - Lower a propagate expression (?) to IR
// =============================================================================
// SPEC: (Lower-Expr-Propagate-Success)
//   Gamma |- LowerExpr(e) => <IR_e, v>
//   UnionTypeOf(v) contains success member S
//   v is S => result is extracted success value
//   --------------------------------------------------------
//   Gamma |- LowerExpr(e?) => <SeqIR(IR_e, MatchCheck), v_success>
//
// SPEC: (Lower-Expr-Propagate-Return)
//   Gamma |- LowerExpr(e) => <IR_e, v>
//   UnionTypeOf(v) contains error members E1, E2, ...
//   v is Ei => emit cleanup and return v
//   --------------------------------------------------------
//   Gamma |- LowerExpr(e?) => <SeqIR(IR_e, IfCheck, Cleanup, Return), unreachable>
//
// The propagate operator (?) on a union type:
// 1. If the value is the success member, extract and return it
// 2. If the value is any error member, emit cleanup and return early
// =============================================================================

LowerResult LowerPropagateExpr(const ast::PropagateExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Propagate-Success");
    SPEC_RULE("Lower-Expr-Propagate-Return");
    const bool debug_propagate = core::IsDebugEnabled("propagate");

    // Lower the inner expression
    auto inner_result = LowerExpr(*expr.value, ctx);

    analysis::TypeRef expr_type;
    if (ctx.expr_type) {
        expr_type = ctx.expr_type(*expr.value);
    }
    if (!expr_type) {
        expr_type = ctx.LookupValueType(inner_result.value);
    }
    analysis::TypeRef stripped_expr = ResolvePropagateAliasType(expr_type, ctx);

    if (!ctx.proc_ret_type || !stripped_expr) {
        if (debug_propagate) {
            std::cerr << "[propagate-debug] fallback: missing proc_ret_type or expr_type\n";
        }
        ctx.ReportCodegenFailure();
        return inner_result;
    }

    const analysis::ScopeContext& scope = ScopeForLowering(ctx);
    const std::optional<analysis::OutcomeSig> outcome_sig =
        analysis::OutcomeSigOf(stripped_expr);
    if (outcome_sig.has_value()) {
        const auto async_sig = analysis::GetAsyncSig(ctx.proc_ret_type);
        std::optional<analysis::OutcomeSig> return_outcome_sig;
        if (!async_sig.has_value()) {
            return_outcome_sig = analysis::OutcomeSigOf(ctx.proc_ret_type);
            if (!return_outcome_sig.has_value()) {
                ctx.ReportCodegenFailure();
                return inner_result;
            }
        }

        IRIfCase if_case_ir;
        if_case_ir.scrutinee = inner_result.value;
        if_case_ir.scrutinee_type = stripped_expr;

        IRValue result_value = ctx.FreshTempValue("propagate_result");
        if_case_ir.result = result_value;
        ctx.RegisterValueType(result_value, outcome_sig->value);

        IRIfCaseClause value_arm;
        value_arm.pattern = OutcomeStatePattern("Value");
        value_arm.body = EmptyIR();
        value_arm.value = RegisterOutcomeFieldValue(
            inner_result.value,
            "Value",
            "value",
            outcome_sig->value,
            ctx);
        if_case_ir.arms.push_back(std::move(value_arm));

        IRValue error_value = RegisterOutcomeFieldValue(
            inner_result.value,
            "Error",
            "error",
            outcome_sig->error,
            ctx);

        CleanupPlan cleanup_plan = ComputeCleanupPlanToFunctionRoot(ctx);
        IRPtr cleanup_ir = EmitCleanup(cleanup_plan, ctx);

        IRIfCaseClause error_arm;
        error_arm.pattern = OutcomeStatePattern("Error");
        if (async_sig.has_value()) {
            const std::string saved_error_name = "__uv_async_error_outcome";
            IRBindVar save_error;
            save_error.name = saved_error_name;
            save_error.value = error_value;
            save_error.type = outcome_sig->error;
            save_error.prov = analysis::ProvenanceKind::Bottom;
            ctx.RegisterVar(saved_error_name,
                            outcome_sig->error,
                            /*has_responsibility=*/false,
                            /*is_immovable=*/false,
                            analysis::ProvenanceKind::Bottom);

            IRValue saved_error;
            saved_error.kind = IRValue::Kind::Local;
            saved_error.name = saved_error_name;
            ctx.RegisterValueType(saved_error, outcome_sig->error);

            IRAsyncFail async_fail;
            async_fail.value = saved_error;
            async_fail.result = ctx.FreshTempValue("propagate_failed_async");
            async_fail.async_type = ctx.proc_ret_type;
            async_fail.error_type =
                async_sig->err ? async_sig->err : outcome_sig->error;
            ctx.RegisterValueType(async_fail.result, ctx.proc_ret_type);

            IRReturn ret;
            ret.value = async_fail.result;
            error_arm.body = SeqIR({
                MakeIR(std::move(save_error)),
                cleanup_ir,
                MakeIR(std::move(async_fail)),
                MakeIR(std::move(ret)),
            });
        } else {
            std::vector<IRPtr> return_parts;
            return_parts.push_back(cleanup_ir);
            IRValue return_value = RegisterOutcomeErrorReturnValue(
                error_value,
                *return_outcome_sig,
                ctx.proc_ret_type,
                ctx,
                return_parts);
            IRReturn ret;
            ret.value = return_value;
            return_parts.push_back(MakeIR(std::move(ret)));
            error_arm.body = SeqIR(std::move(return_parts));
        }
        error_arm.value = ctx.FreshTempValue("propagate_unreach");
        if_case_ir.arms.push_back(std::move(error_arm));

        return LowerResult{SeqIR({inner_result.ir, MakeIR(std::move(if_case_ir))}),
                           result_value};
    }

    const auto* union_type =
        std::get_if<analysis::TypeUnion>(&stripped_expr->node);
    if (!union_type || union_type->members.empty()) {
        if (debug_propagate) {
            std::cerr << "[propagate-debug] fallback: expr_type is not a union or Outcome\n";
        }
        ctx.ReportCodegenFailure();
        return inner_result;
    }

    auto success_type = SuccessMemberType(scope, ctx.proc_ret_type, stripped_expr);
    if (!success_type.has_value()) {
        if (debug_propagate) {
            std::cerr << "[propagate-debug] fallback: could not determine success member"
                      << " ret_type=" << analysis::TypeToString(ctx.proc_ret_type)
                      << " expr_type=" << analysis::TypeToString(stripped_expr) << "\n";
        }
        ctx.ReportCodegenFailure();
        return inner_result;
    }

    // Runtime discriminants are based on canonical union layout ordering.
    std::vector<analysis::TypeRef> members = union_type->members;
    if (ctx.sigma) {
        if (const auto layout = ::ultraviolet::analysis::layout::UnionLayoutOf(scope, *union_type)) {
            members = layout->member_list;
        }
    }
    const auto success_index = FindMemberIndex(members, *success_type);
    if (!success_index.has_value()) {
        if (debug_propagate) {
            std::cerr << "[propagate-debug] fallback: success member not in runtime layout"
                      << " success_type=" << analysis::TypeToString(*success_type)
                      << " member_count=" << members.size() << "\n";
        }
        ctx.ReportCodegenFailure();
        return inner_result;
    }
    if (debug_propagate) {
        std::cerr << "[propagate-debug] lower ok: ret_type="
                  << analysis::TypeToString(ctx.proc_ret_type)
                  << " expr_type=" << analysis::TypeToString(stripped_expr)
                  << " success_index=" << *success_index
                  << " success_type=" << analysis::TypeToString(*success_type) << "\n";
    }

    IRIfCase if_case_ir;
    if_case_ir.scrutinee = inner_result.value;
    if_case_ir.scrutinee_type = stripped_expr;
    if_case_ir.arms.reserve(members.size());

    IRValue result_value = ctx.FreshTempValue("propagate_result");
    if_case_ir.result = result_value;
    ctx.RegisterValueType(result_value, *success_type);
    const auto async_sig = analysis::GetAsyncSig(ctx.proc_ret_type);

    for (std::size_t i = 0; i < members.size(); ++i) {
        const bool is_success = i == *success_index;
        const analysis::TypeRef member_type = members[i];

        auto pattern = std::make_shared<IRPattern>();
        pattern->node = IRTypedPattern{"__case" + std::to_string(i), member_type};

        IRValue case_value;
        if (IsUnitType(member_type)) {
            case_value = ctx.FreshTempValue(
                is_success ? "propagate_success_unit" : "propagate_error_unit");
        } else {
            case_value = ctx.FreshTempValue(
                is_success ? "propagate_success" : "propagate_error");
            DerivedValueInfo info;
            info.kind = DerivedValueInfo::Kind::UnionPayload;
            info.base = inner_result.value;
            info.union_index = i;
            ctx.RegisterDerivedValue(case_value, info);
        }
        if (member_type) {
            ctx.RegisterValueType(case_value, member_type);
        }

        IRIfCaseClause arm;
        arm.pattern = std::move(pattern);

        if (is_success) {
            arm.body = EmptyIR();
            arm.value = case_value;
        } else {
            CleanupPlan cleanup_plan = ComputeCleanupPlanToFunctionRoot(ctx);
            IRPtr cleanup_ir = EmitCleanup(cleanup_plan, ctx);
            if (async_sig.has_value()) {
                if (IsNeverType(async_sig->err) || IsNeverType(member_type)) {
                    // Infallible asyncs have no concrete Failed arm. Preserve
                    // the impossible-path semantics without synthesizing
                    // IRAsyncFail in this branch.
                    arm.body = LowerPanic(PanicReason::AsyncFailed, ctx);
                    arm.value = ctx.FreshTempValue("propagate_unreach");
                    if_case_ir.arms.push_back(std::move(arm));
                    continue;
                }
                const std::string saved_error_name =
                    "__uv_async_error_" + std::to_string(i);
                IRBindVar save_error;
                save_error.name = saved_error_name;
                save_error.value = case_value;
                save_error.type = member_type;
                save_error.prov = analysis::ProvenanceKind::Bottom;
                ctx.RegisterVar(saved_error_name,
                                member_type,
                                /*has_responsibility=*/false,
                                /*is_immovable=*/false,
                                analysis::ProvenanceKind::Bottom);

                IRValue saved_error;
                saved_error.kind = IRValue::Kind::Local;
                saved_error.name = saved_error_name;
                if (member_type) {
                    ctx.RegisterValueType(saved_error, member_type);
                }

                IRAsyncFail async_fail;
                async_fail.value = saved_error;
                async_fail.result = ctx.FreshTempValue("propagate_failed_async");
                async_fail.async_type = ctx.proc_ret_type;
                async_fail.error_type =
                    async_sig->err ? async_sig->err : member_type;
                ctx.RegisterValueType(async_fail.result, ctx.proc_ret_type);

                IRReturn ret;
                ret.value = async_fail.result;
                arm.body = SeqIR({
                    MakeIR(std::move(save_error)),
                    cleanup_ir,
                    MakeIR(std::move(async_fail)),
                    MakeIR(std::move(ret)),
                });
            } else {
                IRReturn ret;
                ret.value = case_value;
                arm.body = SeqIR({cleanup_ir, MakeIR(std::move(ret))});
            }
            arm.value = ctx.FreshTempValue("propagate_unreach");
        }
        if_case_ir.arms.push_back(std::move(arm));
    }

    return LowerResult{SeqIR({inner_result.ir, MakeIR(std::move(if_case_ir))}),
                       result_value};
}

}  // namespace ultraviolet::codegen
