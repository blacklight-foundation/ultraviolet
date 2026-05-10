// =================================================================
// File: 05_codegen/checks/poison_instrument.h
// Construct: Poison Instrumentation for Module Initialization
// Spec Section: 6.8 (Cleanup, Drop, and Unwinding), 6.12 (LLVM Backend)
// Spec Rules: PoisonFlag-Set, PoisonFlag-Get
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
// Implements module poison tracking for failed static initialization.
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

// Set the poison flag for a module.
// SPEC_RULE: PoisonFlag-Set
// Emits LLVM IR to store a boolean value to the module's poison flag global.
void EmitSetPoison(LLVMEmitter& emitter, const std::string& module_name, bool value);

// Compute the set of modules to poison on initialization failure
// Returns module paths that should be poisoned if the given module's init fails
// Per spec: when a module init panics, all modules in its dependency graph
// that have been loaded must also be poisoned
std::vector<std::string> PoisonSetFor(const std::string& module_path, const LowerCtx& ctx);

// Note: LLVMEmitter::EmitPoisonCheck is declared in llvm_emit.h as a method
// It implements SPEC_RULE: PoisonCheck, PoisonFlag-Get

}  // namespace cursive::codegen
