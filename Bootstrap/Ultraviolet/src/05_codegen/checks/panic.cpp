// =============================================================================
// MIGRATION MAPPING: panic.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.8 Cleanup and Panic Handling (lines 16973-17050)
//   - CleanupJudg definitions (line 16973)
//   - PanicCode mapping (lines 16997-17010)
//   - LowerPanic judgment
//   - PanicSym runtime symbol
//   - ClearPanic judgment
//   - PanicCheck judgment
//   - InitPanicHandle judgment (line 16989)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/checks.cpp
//   - Lines 97-131: PanicCode function
//   - Lines 133-165: PanicReasonString function
//   - Lines 351-397: PanicSym, LowerPanic, ClearPanic, PanicCheck, InitPanicHandle
//
// SOURCE FILE: ultraviolet-bootstrap/src/runtime/runtime_interface.cpp
//   - RuntimePanicSym definition
//   - Runtime panic handler interface
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/checks/panic.h
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRLowerPanic, IRClearPanic, IRPanicCheck)
//   - ultraviolet/include/05_codegen/cleanup/cleanup.h (CleanupPlan, EmitCleanupOnPanic)
//   - ultraviolet/include/runtime/runtime_interface.h
//
// REFACTORING NOTES:
//   1. Panic handling uses out-parameter, not exceptions
//   2. LowerPanic: emit cleanup IR + panic IR
//   3. ClearPanic: clear panic flag (for recovery)
//   4. PanicCheck: check panic flag after call
//   5. InitPanicHandle: module init panic with poison propagation
//   6. PanicSym returns runtime panic handler symbol
//
// PANIC IR GENERATION:
//   LowerPanic(reason, ctx):
//     1. Get panic reason string
//     2. Compute cleanup plan to function root
//     3. Emit cleanup IR
//     4. Create IRLowerPanic node
//     5. Return SeqIR(trace, panic)
//
//   PanicCheck(ctx):
//     1. Compute cleanup plan
//     2. Create IRPanicCheck with cleanup
//     3. Return SeqIR(trace, check)
//
//   InitPanicHandle(module, ctx):
//     1. Compute poison module set (transitive deps)
//     2. Create IRInitPanicHandle
//     3. Return SeqIR(trace, handle)
//
// PANIC RECORD STRUCTURE:
//   PanicRecord = {
//     panicked: bool,
//     code: u16,
//     message: string,
//     file: string,
//     line: u32,
//     column: u32
//   }
// =============================================================================

#include "05_codegen/checks/panic.h"
#include "05_codegen/checks/checks.h"
#include "00_core/assert_spec.h"

namespace ultraviolet::codegen {

// Convert a PanicSite to its corresponding PanicReason
// Per spec: PanicReasonOf(DivZeroCheck) = DivZero, etc.
PanicReason PanicReasonOf(PanicSite site) {
  SPEC_RULE("PanicReasonOf");
  switch (site) {
    case PanicSite::DivZeroCheck:
      return PanicReason::DivZero;
    case PanicSite::OverflowCheck:
      return PanicReason::Overflow;
    case PanicSite::ShiftCheck:
      return PanicReason::Shift;
    case PanicSite::BoundsCheck:
      return PanicReason::Bounds;
    case PanicSite::CastCheck:
      return PanicReason::Cast;
    case PanicSite::NullDerefCheck:
      return PanicReason::NullDeref;
    case PanicSite::ExpiredDerefCheck:
      return PanicReason::ExpiredDeref;
    case PanicSite::ErrorExprSite:
      return PanicReason::ErrorExpr;
    case PanicSite::ErrorStmtSite:
      return PanicReason::ErrorStmt;
    case PanicSite::InitPanicSite:
      return PanicReason::InitPanic;
    case PanicSite::ContractPreSite:
      return PanicReason::ContractPre;
    case PanicSite::ContractPostSite:
      return PanicReason::ContractPost;
    case PanicSite::AsyncFailedSite:
      return PanicReason::AsyncFailed;
    case PanicSite::OtherSite:
    default:
      return PanicReason::Other;
  }
}

}  // namespace ultraviolet::codegen
