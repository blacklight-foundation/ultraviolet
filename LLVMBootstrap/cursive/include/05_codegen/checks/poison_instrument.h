// =================================================================
// File: 05_codegen/checks/poison_instrument.h
// Construct: Poison Instrumentation for Module Initialization
// Spec Section: 6.8 (Cleanup, Drop, and Unwinding), 6.12 (LLVM Backend)
// Spec Rules: PoisonFlag-Set, PoisonFlag-Clear, PoisonFlag-Get, PoisonFlag-OnMove
// =================================================================
#pragma once

#include <string>
#include <vector>

// Forward declarations
namespace llvm {
class GlobalVariable;
}

namespace cursive::codegen {

// Forward declarations
class LLVMEmitter;
struct LowerCtx;

// T-LLVM-016: Poisoning Instrumentation
// Implements poison flag tracking for detecting use of uninitialized/moved memory.
//
// The poison flag mechanism works as follows:
// 1. Each module has a global boolean "poison flag" that tracks initialization state
// 2. If module initialization fails (panics), the poison flag is set to true
// 3. Any subsequent attempt to use the module checks the flag and panics if poisoned
// 4. This prevents use of partially-initialized module state
//
// Per spec section 6.8:
// - SetPoison(m) sets the poison flag for module m
// - InitPanicHandle emits: if panic occurred, SeqIR(SetPoison(m), LowerPanic(InitPanic(m)))

// Set the poison flag for a module
// SPEC_RULE: PoisonFlag-Set
// Emits LLVM IR to store a boolean value to the module's poison flag global
void EmitSetPoison(LLVMEmitter& emitter, const std::string& module_name, bool value);

// Clear the poison flag for a module (set to false)
// SPEC_RULE: PoisonFlag-Clear
// Convenience wrapper for EmitSetPoison(emitter, module_name, false)
void EmitClearPoison(LLVMEmitter& emitter, const std::string& module_name);

// Set the poison flag on move (set to true)
// SPEC_RULE: PoisonFlag-OnMove
// Used when a binding is moved to mark the source as poisoned
void EmitPoisonOnMove(LLVMEmitter& emitter, const std::string& module_name);

// Compute the set of modules to poison on initialization failure
// Returns module paths that should be poisoned if the given module's init fails
// Per spec: when a module init panics, all modules in its dependency graph
// that have been loaded must also be poisoned
std::vector<std::string> PoisonSetFor(const std::string& module_path, const LowerCtx& ctx);

// Note: LLVMEmitter::EmitPoisonCheck is declared in llvm_emit.h as a method
// It implements SPEC_RULE: PoisonCheck, PoisonFlag-Get

}  // namespace cursive::codegen
