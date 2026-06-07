// =============================================================================
// Expression Lowering: WaitExpr
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 10.4 (Wait Expression)
//   - wait handle blocks until spawned task completes
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - WaitExpr visitor produces IRWait
//
// =============================================================================

#include "05_codegen/lower/expr/wait_expr.h"

#include <optional>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/typing/type_expr.h"

namespace ultraviolet::codegen {

namespace {

std::optional<analysis::TypeRef> ExtractWaitSpawnedInner(
    const analysis::TypeRef& type) {
    if (!type) {
        return std::nullopt;
    }
    const auto* path = analysis::AppliedTypePath(*type);
    const auto* args = analysis::AppliedTypeArgs(*type);
    if (!path || !args) {
        return std::nullopt;
    }
    if (!analysis::IsSpawnedTypePath(*path)) {
        return std::nullopt;
    }
    if (args->size() != 1) {
        return std::nullopt;
    }
    return (*args)[0];
}

std::optional<std::pair<analysis::TypeRef, analysis::TypeRef>>
ExtractWaitTrackedArgs(const analysis::TypeRef& type) {
    if (!type) {
        return std::nullopt;
    }
    const auto* path = analysis::AppliedTypePath(*type);
    const auto* args = analysis::AppliedTypeArgs(*type);
    if (!path || !args) {
        return std::nullopt;
    }
    if (!analysis::IsTrackedTypePath(*path)) {
        return std::nullopt;
    }
    if (args->size() != 2) {
        return std::nullopt;
    }
    return std::make_pair((*args)[0], (*args)[1]);
}

bool HasActiveHeldKeys(const LowerCtx& ctx) {
    for (const auto& scope : ctx.active_key_scopes) {
        if (!scope.implicit && !scope.acquired_paths.empty()) {
            return true;
        }
    }
    return false;
}

}  // namespace

// =============================================================================
// LowerWaitExpr - Lower a wait expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Wait)
//   Gamma |- LowerExpr(handle) => <IR_h, v_handle>
//   --------------------------------------------------------
//   Gamma |- LowerExpr(wait handle) => <SeqIR(IR_h, IRWait), v_result>
//
// The wait expression blocks until the spawned task completes:
// 1. Lower the handle expression (Spawned<T>)
// 2. Emit IRWait to block until completion
// 3. Return the result value
//
// CRITICAL: Keys MUST NOT be held across wait (suspension point)
// =============================================================================

LowerResult LowerWaitExpr(const ast::WaitExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Wait");
    SPEC_RULE("def.21.SuspensionLoweringForms");

    if (HasActiveHeldKeys(ctx)) {
        SPEC_RULE("rule.21.Lower-Wait-Key-Illegal");
        core::Conformance::Record(
            "rule.21.Lower-Wait-Key-Illegal",
            std::nullopt,
            "source=LowerWaitExpr;held_keys=true;result=codegen_failed;"
            "ir_form=EmptyIR");
        ctx.ReportCodegenFailure();
        return LowerResult{EmptyIR(), IRValue{}};
    }

    // Lower the handle expression
    auto handle_result = LowerExpr(*expr.handle, ctx);

    // Create the wait IR node
    IRWait wait;
    wait.handle = handle_result.value;
    wait.result = ctx.FreshTempValue("wait_result");

    // Register result type from wait target type.
    if (ctx.expr_type) {
        analysis::TypeRef handle_type = ctx.expr_type(*expr.handle);
        analysis::TypeRef stripped = analysis::StripPerm(handle_type);
        if (!stripped) {
            stripped = handle_type;
        }

        analysis::TypeRef wait_type;
        if (stripped) {
            if (const auto inner = ExtractWaitSpawnedInner(stripped)) {
                SPEC_RULE("rule.21.Lower-Wait-Spawned");
                wait.kind = IRWaitKind::Spawned;
                wait_type = *inner;
            } else if (const auto tracked = ExtractWaitTrackedArgs(stripped)) {
                SPEC_RULE("rule.21.Lower-Wait-Tracked");
                wait.kind = IRWaitKind::Tracked;
                std::vector<analysis::TypeRef> members;
                members.push_back(tracked->first);
                members.push_back(tracked->second);
                wait_type = analysis::MakeTypeUnion(std::move(members));
            }
        }

        if (wait_type) {
            ctx.RegisterValueType(wait.result, wait_type);
        }
    }

    IRValue result = wait.result;
    return LowerResult{SeqIR({handle_result.ir, MakeIR(std::move(wait))}), result};
}

}  // namespace ultraviolet::codegen
