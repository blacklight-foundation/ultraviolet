// =============================================================================
// Expression Lowering: RaceExpr
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 11.4 (Race Expression)
//   - race { arm -> handler, ... } returns first completed
//   - Two modes: RaceReturn (returns value) and RaceYield (streams)
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 2652-2759: RaceExpr visitor
//
// =============================================================================

#include "05_codegen/lower/expr/race_expr.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/pattern/pattern_common.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/cleanup/cleanup.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/types.h"
#include "00_core/assert_spec.h"
#include <utility>

namespace cursive::codegen {

namespace {

analysis::TypeRef MakeTypePathWithArgs(analysis::TypePath path,
                                       std::vector<analysis::TypeRef> args) {
    analysis::TypePathType node;
    node.path = std::move(path);
    node.generic_args = std::move(args);
    return analysis::MakeType(node);
}

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

LowerCtx MakeBranchCtx(LowerCtx& base) {
    auto saved_value_types = std::move(base.values.value_types);
    auto saved_derived_values = std::move(base.values.derived_values);
    auto saved_drop_glue_types = std::move(base.values.drop_glue_types);

    LowerCtx branch = base;

    base.values.value_types = std::move(saved_value_types);
    base.values.derived_values = std::move(saved_derived_values);
    base.values.drop_glue_types = std::move(saved_drop_glue_types);

    branch.values.value_types.clear();
    branch.values.derived_values.clear();
    branch.values.drop_glue_types.clear();
    branch.values.parent = &base;
    branch.extra_procs.clear();
    return branch;
}

}  // namespace

// =============================================================================
// LowerRaceExpr - Lower a race expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Race)
//   For each arm:
//     Gamma |- LowerExpr(arm.expr) => <IR_async, v_async>
//     Pattern binding scope with handler lowering
//   --------------------------------------------------------
//   Gamma |- LowerExpr(race { ... }) => <IRRace*, v_result>
//
// The race expression:
// 1. Determines mode from first handler (Yield = streaming, Return = returning)
// 2. For each arm:
//    - Lowers async expression
//    - Creates match_value for pattern binding
//    - Pushes scope, registers pattern bindings
//    - Lowers bind pattern
//    - Lowers handler expression
//    - Computes cleanup
//    - Pops scope
//    - Creates IRRaceArm
// 3. Merges contexts from all arms (value_types, derived_values, move states, failures)
// 4. If streaming: creates IRRaceYield with stream_type
// 5. If returning: creates IRRaceReturn with result_type
// =============================================================================

LowerResult LowerRaceExpr(const ast::RaceExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Race");

    // Determine mode from first handler
    bool is_streaming = !expr.arms.empty() &&
                        expr.arms[0].handler.kind == ast::RaceHandlerKind::Yield;

    std::vector<IRRaceArm> ir_arms;
    std::vector<LowerCtx> arm_ctxs;
    std::vector<analysis::TypeRef> error_types;
    ir_arms.reserve(expr.arms.size());
    arm_ctxs.reserve(expr.arms.size());
    error_types.reserve(expr.arms.size());

    for (const auto& arm : expr.arms) {
        IRRaceArm ir_arm;

        // Lower the async expression
        auto async_result = LowerExpr(*arm.expr, ctx);
        ir_arm.async_ir = async_result.ir;
        ir_arm.async_value = async_result.value;

        // Get async type and extract pattern type
        analysis::TypeRef async_type;
        analysis::TypeRef pat_type;
        if (ctx.expr_type) {
            async_type = ctx.expr_type(*arm.expr);
            if (auto sig = analysis::GetAsyncSig(async_type)) {
                // Streaming mode uses Out type, returning mode uses Result type
                pat_type = is_streaming ? sig->out : sig->result;
                if (sig->err) {
                    AppendDistinctType(error_types, sig->err);
                }
            }
        }

        // Create case-analysis value for pattern binding
        IRValue match_value = ctx.FreshTempValue("race_match");
        ir_arm.match_value = match_value;

        // Create arm context with new scope
        LowerCtx arm_ctx = MakeBranchCtx(ctx);
        arm_ctx.PushScope(false, false);

        // Register case-analysis value type
        if (pat_type) {
            arm_ctx.RegisterValueType(match_value, pat_type);
        }

        // Register pattern bindings if pattern exists
        if (arm.pattern) {
            RegisterPatternBindings(*arm.pattern, pat_type, arm_ctx);
        }

        // Lower the bind pattern
        IRPtr bind_ir = arm.pattern
                            ? LowerBindPattern(*arm.pattern, match_value, arm_ctx)
                            : EmptyIR();

        // Lower handler expression
        auto handler_result = LowerExpr(*arm.handler.value, arm_ctx);

        // Compute cleanup for current scope
        CleanupPlan cleanup_plan = ComputeCleanupPlanForCurrentScope(arm_ctx);
        CleanupPlan remainder =
            ComputeCleanupPlanRemainder(CleanupTarget::CurrentScope, arm_ctx);
        IRPtr cleanup_ir = EmitCleanupWithRemainder(cleanup_plan, remainder, arm_ctx);

        // Pop the scope
        arm_ctx.PopScope();

        // Assemble handler IR
        ir_arm.handler_ir = SeqIR({bind_ir, handler_result.ir, cleanup_ir});
        ir_arm.handler_result = handler_result.value;

        // Merge value_types from arm context to parent
        for (const auto& [name, type] : arm_ctx.values.value_types) {
            if (!ctx.values.value_types.count(name)) {
                ctx.values.value_types.emplace(name, type);
            }
        }

        // Merge derived_values from arm context to parent
        for (const auto& [name, info] : arm_ctx.values.derived_values) {
            if (!ctx.values.derived_values.count(name)) {
                ctx.values.derived_values.emplace(name, info);
            }
        }

        // Merge static_types from arm context to parent
        for (const auto& [name, type] : arm_ctx.values.static_types) {
            if (!ctx.values.static_types.count(name)) {
                ctx.values.static_types.emplace(name, type);
            }
        }

        // Merge drop_glue_types from arm context to parent
        for (const auto& [name, type] : arm_ctx.values.drop_glue_types) {
            if (!ctx.values.drop_glue_types.count(name)) {
                ctx.values.drop_glue_types.emplace(name, type);
            }
        }

        // Sync temp counter
        *ctx.temp_counter = std::max(*ctx.temp_counter, *arm_ctx.temp_counter);
        ctx.MergeGeneratedProcsFrom(arm_ctx);

        ir_arms.push_back(std::move(ir_arm));
        arm_ctxs.push_back(std::move(arm_ctx));
    }

    // Build branches for move state merging
    std::vector<const LowerCtx*> branches;
    branches.reserve(arm_ctxs.size());
    for (const auto& arm_ctx : arm_ctxs) {
        branches.push_back(&arm_ctx);
    }
    MergeMoveStates(ctx, branches);

    // Merge failures from all arms
    for (const auto& arm_ctx : arm_ctxs) {
        MergeFailures(ctx, arm_ctx);
    }

    // Create appropriate race IR based on mode
    if (is_streaming) {
        IRRaceYield race;
        race.arms = std::move(ir_arms);
        race.result = ctx.FreshTempValue("race_yield_result");

        IRValue result = race.result;
        analysis::TypeRef handler_type = analysis::MakeTypePrim("()");
        if (ctx.expr_type && !expr.arms.empty()) {
            if (analysis::TypeRef first_handler =
                    ctx.expr_type(*expr.arms.front().handler.value)) {
                handler_type = first_handler;
            }
        }

        analysis::TypeRef error_union = analysis::MakeTypePrim("!");
        if (!error_types.empty()) {
            if (error_types.size() == 1) {
                error_union = error_types.front();
            } else {
                error_union = analysis::MakeTypeUnion(error_types);
            }
        }

        race.stream_type = MakeTypePathWithArgs({"Stream"},
                                                {handler_type, error_union});
        ctx.RegisterValueType(result, race.stream_type);
        return LowerResult{MakeIR(std::move(race)), result};
    } else {
        IRRaceReturn race;
        race.arms = std::move(ir_arms);
        race.result = ctx.FreshTempValue("race_return_result");

        // Get result type from first handler expression type
        if (ctx.expr_type && !expr.arms.empty()) {
            race.result_type = ctx.expr_type(*expr.arms.front().handler.value);
        }

        IRValue result = race.result;
        std::vector<analysis::TypeRef> union_members;
        AppendDistinctType(union_members, race.result_type);
        for (const auto& err : error_types) {
            AppendDistinctType(union_members, err);
        }

        analysis::TypeRef race_value_type = nullptr;
        if (union_members.empty()) {
            race_value_type = race.result_type;
        } else if (union_members.size() == 1) {
            race_value_type = union_members.front();
        } else {
            race_value_type = analysis::MakeTypeUnion(std::move(union_members));
        }
        if (!race_value_type) {
            race_value_type = analysis::MakeTypePrim("()");
        }
        ctx.RegisterValueType(result, race_value_type);
        return LowerResult{MakeIR(std::move(race)), result};
    }
}

}  // namespace cursive::codegen
