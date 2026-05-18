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
    const auto* path = std::get_if<analysis::TypePathType>(&type->node);
    if (!path) {
        return std::nullopt;
    }
    if (!analysis::IsSpawnedTypePath(path->path)) {
        return std::nullopt;
    }
    if (path->generic_args.size() != 1) {
        return std::nullopt;
    }
    return path->generic_args[0];
}

std::optional<std::pair<analysis::TypeRef, analysis::TypeRef>>
ExtractWaitTrackedArgs(const analysis::TypeRef& type) {
    if (!type) {
        return std::nullopt;
    }
    const auto* path = std::get_if<analysis::TypePathType>(&type->node);
    if (!path) {
        return std::nullopt;
    }
    if (!analysis::IsTrackedTypePath(path->path)) {
        return std::nullopt;
    }
    if (path->generic_args.size() != 2) {
        return std::nullopt;
    }
    return std::make_pair(path->generic_args[0], path->generic_args[1]);
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
                wait_type = *inner;
            } else if (const auto tracked = ExtractWaitTrackedArgs(stripped)) {
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
