// =============================================================================
// Expression Lowering: MoveExpr
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16238-16241: (Lower-Expr-Move)
//   - Lines 16581-16584: (Lower-MovePlace)
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_places.cpp
//   - Lines 1159-1184: LowerMovePlace function
//
// =============================================================================

#include "05_codegen/lower/expr/move_expr.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "00_core/assert_spec.h"

namespace ultraviolet::codegen {

namespace {

bool IsStaticRoot(const std::string& root, const LowerCtx& ctx) {
    if (const auto alias = ctx.LookupLocalAddrAlias(root)) {
        return alias->kind == LocalAddrAlias::Kind::Static;
    }
    if (!ctx.resolve_name) {
        return false;
    }
    const auto resolved = ctx.resolve_name(root);
    return resolved.has_value() && !resolved->empty();
}

bool MarkMovedAliasBinding(const LocalAddrAlias& alias,
                           const ast::Expr& place,
                           LowerCtx& ctx) {
    auto it = ctx.binding_states.find(alias.binding_name);
    if (it == ctx.binding_states.end()) {
        SPEC_RULE("UpdateValid-Err");
        ctx.ReportCodegenFailure();
        return true;
    }

    for (BindingState& state : it->second) {
        if (state.binding_id != alias.binding_id) {
            continue;
        }
        if (auto head = FieldHead(place)) {
            SPEC_RULE("UpdateValid-PartialMove-Init");
            SPEC_RULE("UpdateValid-PartialMove-Step");
            state.moved_fields.push_back(*head);
        } else {
            SPEC_RULE("UpdateValid-MoveRoot");
            state.is_moved = true;
        }
        return true;
    }

    SPEC_RULE("UpdateValid-Err");
    ctx.ReportCodegenFailure();
    return true;
}

IRPtr MarkMovedPlace(const ast::Expr& place, LowerCtx& ctx) {
    if (auto root = PlaceRoot(place)) {
        if (auto alias = ctx.LookupLocalAddrAlias(*root)) {
            switch (alias->kind) {
                case LocalAddrAlias::Kind::Binding:
                    MarkMovedAliasBinding(*alias, place, ctx);
                    break;
                case LocalAddrAlias::Kind::Capture:
                    break;
                case LocalAddrAlias::Kind::Static:
                    break;
            }
        } else if (ctx.GetBindingState(*root)) {
            if (auto head = FieldHead(place)) {
                ctx.MarkFieldMoved(*root, *head);
            } else {
                ctx.MarkMoved(*root);
            }
        } else if (ctx.LookupCapture(*root)) {
            // Captured bindings were validated when the capture was formed.
        } else if (IsStaticRoot(*root, ctx)) {
            // Static storage is represented by the emitted MoveStateIR below.
        } else {
            ctx.MarkMoved(*root);
        }
    }

    IRMoveState move_state;
    move_state.place = LowerPlace(place, ctx);
    return MakeIR(std::move(move_state));
}

}  // namespace

// =============================================================================
// LowerMovePlace - Lower a move expression to IR
// =============================================================================
// SPEC: (Lower-MovePlace)
//   Gamma |- LowerReadPlace(place) => <IR_r, v>
//   Gamma' = UpdateValid(Gamma, place, Moved)
//   place_repr = LowerPlace(place)
//   -----------------------------------------------------
//   Gamma |- LowerMovePlace(place) => <SeqIR(IR_r, MoveStateIR(place_repr)), v>
//
// Move expressions transfer ownership of a value out of a place. This:
// 1. Reads the value from the place
// 2. Marks the place as moved in the context (for move tracking)
// 3. Emits IRMoveState to record the state change
//
// The place root is used to update the binding state:
// - If it's a root variable: mark the whole variable as moved
// - If it's a field access: mark that field as moved (partial move)
// =============================================================================

LowerResult LowerMovePlace(const ast::Expr& place, LowerCtx& ctx) {
    SPEC_RULE("Lower-MovePlace");

    auto read_result = LowerReadPlace(place, ctx);
    IRPtr move_state = MarkMovedPlace(place, ctx);

    return LowerResult{
        SeqIR({read_result.ir, move_state}),
        read_result.value
    };
}

LowerResult LowerMovePlaceAsRef(const ast::Expr& place, LowerCtx& ctx) {
    SPEC_RULE("Lower-MovePlace");

    auto addr_result = LowerAddrOf(place, ctx, AddressUseKind::TransientNoEscape);
    IRPtr move_state = MarkMovedPlace(place, ctx);

    return LowerResult{
        SeqIR({addr_result.ir, move_state}),
        addr_result.value
    };
}

LowerResult LowerCopyExpr(const ast::CopyExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Copy");
    if (!expr.value) {
        return LowerResult{EmptyIR(), IRValue{}};
    }
    return LowerExpr(*expr.value, ctx);
}

}  // namespace ultraviolet::codegen
