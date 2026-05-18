// =============================================================================
// Expression Lowering: PtrNullExpr
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Line 16053-16055: (Lower-Expr-PtrNull)
//     Gamma |- LowerExpr(PtrNullExpr) => <epsilon, Ptr@Null(0x0)>
//
// MIGRATED FROM:
//   - ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 1191-1201: LowerPtrNull function
//   - Lines 255-268: MakeNullPtr helper (not needed for basic lowering)
//
// =============================================================================

#include "05_codegen/lower/expr/null_ptr.h"
#include "00_core/assert_spec.h"

namespace ultraviolet::codegen {

// =============================================================================
// LowerPtrNull - Lower a null pointer expression to IR
// =============================================================================
// SPEC: (Lower-Expr-PtrNull)
//   Gamma |- LowerExpr(PtrNullExpr) => <epsilon, Ptr@Null(0x0)>
//
// Null pointer expressions produce an immediate IRValue with the null
// representation (8 zero bytes for a 64-bit pointer). No IR is emitted
// since the value is constant.
//
// NOTES:
//   - Null pointer literal requires type context for proper typing
//   - The 8-byte representation assumes 64-bit pointers
//   - Result has Ptr@Null state for type tracking
// =============================================================================

LowerResult LowerPtrNull(const ast::PtrNullExpr& /*expr*/, LowerCtx& /*ctx*/) {
    SPEC_RULE("Lower-Expr-PtrNull");

    IRValue value;
    value.kind = IRValue::Kind::Immediate;
    value.name = "null";
    // Null pointer is represented as 8 zero bytes (64-bit null)
    value.bytes = {0, 0, 0, 0, 0, 0, 0, 0};

    return LowerResult{EmptyIR(), value};
}

}  // namespace ultraviolet::codegen
