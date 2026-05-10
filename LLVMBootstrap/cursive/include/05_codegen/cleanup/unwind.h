#pragma once

// Unwind and panic handling support for Cursive codegen.
// Provides unwinding infrastructure for deterministic destruction during panics.
//
// This header is part of the cleanup subsystem (§6.8, §7.4) and provides:
// - Panic record access and manipulation
// - Unwind state tracking
// - Double-panic detection and abort
// - FFI boundary unwinding control (§2.7)

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "05_codegen/ir/ir_model.h"
#include "05_codegen/abi/abi.h"
#include "04_analysis/typing/types.h"

namespace cursive::codegen {

// Forward declarations
struct LowerCtx;

// ============================================================================
// §6.8 Panic Record Access
// ============================================================================

// Name of the panic output parameter in lowered procedures
// Note: kPanicOutName and PanicOutType() are defined in abi.h

// PanicAccess provides pointers for reading/writing the panic record
struct PanicAccess {
  IRValue panic_ptr;   // Pointer to the panic record tuple
  IRValue flag_ptr;    // Pointer to the panic flag (bool)
  IRValue code_ptr;    // Pointer to the panic code (u32)
};

// Build IR to access the panic record from the panic_out parameter
PanicAccess BuildPanicAccess(LowerCtx& ctx);

// PanicSnapshot holds the result of reading the panic record
struct PanicSnapshot {
  IRPtr ir;            // IR to execute to read the values
  IRValue flag;        // The panic flag value (bool)
  IRValue code;        // The panic code value (u32)
};

// §6.8 PanicRecordOf - Read the current panic record state
PanicSnapshot ReadPanicRecord(const PanicAccess& access, LowerCtx& ctx);

// §6.8 WritePanicRecord - Write a new panic state
IRPtr WritePanicRecord(const PanicAccess& access,
                       const IRValue& flag,
                       const IRValue& code);

// ============================================================================
// §7.4 Unwind State
// ============================================================================

// UnwindState tracks the current unwinding state during cleanup.
// Per §7.4:
// - CleanupLoop(scope, sigma, c) where c in {ok, panic}
// - ExitDone(c, sigma) - cleanup completed
// - Abort - double panic detected
enum class UnwindStatus {
  Ok,       // Normal cleanup, no panic in progress
  Panic,    // Single panic, unwinding in progress
  Abort,    // Double panic, program will abort
};

// UnwindContext tracks state across multiple cleanup actions
struct UnwindContext {
  UnwindStatus status = UnwindStatus::Ok;
  IRValue panicking;          // Current panic flag value
  IRValue panic_code;         // Current panic code value
  std::uint32_t cleanup_depth = 0;  // Nesting depth of cleanup
};

// Create a new unwind context for cleanup emission
UnwindContext CreateUnwindContext(bool start_panicking, LowerCtx& ctx);

// ============================================================================
// §7.4 Double Panic Detection
// ============================================================================

// Per §7.4 (Cleanup-Step-Drop-Abort, Cleanup-Step-Defer-Abort):
// If a cleanup action panics while already panicking, the program aborts.

// Emit IR to check for double panic and abort if detected
// Returns IR that: if (panicking && new_panic) { call panic(code) }
IRPtr EmitDoublePanicCheck(const IRValue& panicking,
                           const IRValue& new_panic_flag,
                           const IRValue& new_panic_code,
                           LowerCtx& ctx);

// ============================================================================
// §7.4 Cleanup Loop Emission
// ============================================================================

// Per §7.4 (Cleanup-Step-*):
// For each cleanup action, we:
// 1. Clear the panic flag before the action
// 2. Execute the action
// 3. Check for double panic (abort if detected)
// 4. Update the panicking state
// 5. Continue to next action

// Emit the cleanup loop for a single action
// Returns IR that handles panic state around the action
IRPtr EmitCleanupActionWithUnwind(IRPtr action_ir,
                                  UnwindContext& unwind,
                                  LowerCtx& ctx);

// ============================================================================
// §2.7 FFI Boundary Unwinding
// ============================================================================

// Per §2.7:
// UnwindMode(proc) = "abort" | "catch"
// - "abort": Panic/unwind at FFI boundary causes program abort
// - "catch": Boundary converts panics/unwinds

enum class UnwindMode {
  Abort,    // Default: abort on unwind at FFI boundary
  Catch,    // Convert between Cursive panics and foreign unwinds
};

// Get the unwind mode for a procedure from its attributes
UnwindMode GetUnwindMode(const ast::AttributeList& attrs);

// Emit IR for FFI boundary panic handling based on unwind mode
// For "catch" mode: converts Cursive panic to error indicator
IRPtr EmitFFIBoundaryCheck(UnwindMode mode,
                           const IRValue& return_value,
                           analysis::TypeRef return_type,
                           LowerCtx& ctx);

// ============================================================================
// §7.4 Unwind Step Emission
// ============================================================================

// Per §7.4 (Unwind-Step, Unwind-Abort):
// Unwind(f_1::fs, sigma) -> Unwind(fs, sigma') on CleanupScope ok
// Unwind(f_1::fs, sigma) -> Abort on CleanupScope panic

// Emit IR for unwinding through procedure frames
// This is called when returning from a panicking procedure
IRPtr EmitUnwindStep(IRPtr cleanup_ir, LowerCtx& ctx);

// Emit IR for aborting on unwind failure
IRPtr EmitUnwindAbort(const IRValue& panic_code, LowerCtx& ctx);

// ============================================================================
// §6.8 Panic Propagation Helpers
// ============================================================================

// Check if a procedure needs the panic_out parameter
// Per §6.8: Non-FFI procedures that can call other non-FFI procedures
bool NeedsPanicOut(const std::string& symbol);

// Register a procedure as needing panic_out
void RegisterPanicOutProcedure(const std::string& symbol, LowerCtx& ctx);

// Emit IR to propagate panic to caller after cleanup
// Used after EmitCleanupOnPanic to return with panic state
IRPtr EmitPanicPropagate(LowerCtx& ctx);

// ============================================================================
// Helper: Create IR values for panic state
// ============================================================================

// Create an IR value for a boolean immediate
IRValue BoolImmediate(bool value);

// Create an IR value for a u32 immediate
IRValue U32Immediate(std::uint32_t value);

// Create an IR value for a usize immediate
IRValue USizeImmediate(std::size_t value);

// Create an IR value representing unit ()
IRValue UnitValue();

// Check if an IR node is a no-op (empty or opaque)
bool IsNoopIR(const IRPtr& ir);

// ============================================================================
// Anchor function for SPEC_RULE markers
// ============================================================================

void AnchorUnwindRules();

}  // namespace cursive::codegen
