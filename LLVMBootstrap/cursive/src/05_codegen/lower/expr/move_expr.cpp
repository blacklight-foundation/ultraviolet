// =============================================================================
// Expression Lowering: MoveExpr
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Lines 16238-16241: (Lower-Expr-Move)
//   - Lines 16581-16584: (Lower-MovePlace)
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_expr_places.cpp
//   - Lines 1159-1184: LowerMovePlace function
//
// =============================================================================

#include "05_codegen/lower/expr/move_expr.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "00_core/assert_spec.h"

namespace cursive::codegen {

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

    // Read the value from the place
    auto read_result = LowerReadPlace(place, ctx);

    // Update binding state to mark the place as moved
    if (auto root = PlaceRoot(place)) {
        if (ctx.GetBindingState(*root)) {
            // Check for field-level move (partial move)
            if (auto head = FieldHead(place)) {
                ctx.MarkFieldMoved(*root, *head);
            } else {
                ctx.MarkMoved(*root);
            }
        } else if (ctx.LookupCapture(*root)) {
            // Captured bindings are not tracked in local binding states here.
            // The capture was already validated at capture time.
        } else {
            // Fallback: mark as moved (for globals or untracked bindings)
            ctx.MarkMoved(*root);
        }
    }

    // Emit IRMoveState to record the state change
    IRPlace ir_place = LowerPlace(place, ctx);
    IRMoveState move_state;
    move_state.place = ir_place;

    return LowerResult{
        SeqIR({read_result.ir, MakeIR(std::move(move_state))}),
        read_result.value
    };
}

}  // namespace cursive::codegen
