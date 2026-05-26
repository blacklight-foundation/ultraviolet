// =================================================================
// File: 05_codegen/checks/panic.h
// Construct: Panic Infrastructure Declarations
// Spec Section: 6.8 (Cleanup, Drop, and Unwinding)
// Spec Rules: LowerPanic, PanicSym, ClearPanic, PanicCheck, InitPanicHandle
// =================================================================
#pragma once

// This header re-exports panic-related declarations from checks.h.
// The core panic infrastructure is defined in checks.h and includes:
//
// - PanicReason enum - enumeration of all panic reasons (ErrorExpr, DivZero, etc.)
// - PanicCode(PanicReason) -> uint16_t - returns the numeric code for a panic reason
// - PanicReasonString(PanicReason) -> string - returns the string representation
// - LowerPanic(PanicReason, LowerCtx&) -> IRPtr - emit panic IR for a reason
// - PanicSym() -> string - the runtime panic handler symbol
// - ClearPanic(LowerCtx&) -> IRPtr - emit IR to clear the panic flag
// - PanicCheck(LowerCtx&) -> IRPtr - emit IR to check if panic occurred
// - InitPanicHandle(module_path, LowerCtx&) -> IRPtr - module init panic handling
//
// For LLVM-level panic emission utilities, see llvm_ir_panic.h which provides:
// - PanicCodeFromString - convert reason string to numeric code
// - LoadPanicOutPtr - load panic output pointer
// - StorePanicRecord - store panic record with code
// - ClearPanicRecord - clear panic record
// - EmitReturn - emit return instruction with default value
// - EmitPanicIfFalse - conditional panic if condition is false
// - EmitPanicReturnIfFalse - conditional panic and return

#include "05_codegen/checks/checks.h"
#include "05_codegen/abi/abi.h"  // For kPanicOutName

namespace ultraviolet::codegen {

// PanicSite enumeration representing locations where panics can occur
// Maps to PanicReason via PanicReasonOf
enum class PanicSite {
  DivZeroCheck,
  OverflowCheck,
  ShiftCheck,
  BoundsCheck,
  CastCheck,
  NullDerefCheck,
  ExpiredDerefCheck,
  ErrorExprSite,
  ErrorStmtSite,
  InitPanicSite,
  ContractPreSite,
  ContractPostSite,
  AsyncFailedSite,
  OtherSite
};

// Convert a PanicSite to its corresponding PanicReason
// Per spec: PanicReasonOf(DivZeroCheck) = DivZero, etc.
PanicReason PanicReasonOf(PanicSite site);

// Well-known panic output local name used in procedure lowering
// Per spec: PanicOutAddr(sigma) = addr iff LookupVal(sigma, PanicOutName) = RawPtr(`mut`, addr)
// Note: kPanicOutName is defined in abi.h

}  // namespace ultraviolet::codegen
