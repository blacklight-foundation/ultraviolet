// =================================================================
// File: 04_codegen/llvm/llvm_ir_panic.h
// Construct: LLVM IR Panic Emission Utilities
// Spec Section: 6.12
// Spec Rules: PanicRecord, PoisonFlag
// =================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "llvm/IR/IRBuilder.h"

// Forward declarations
namespace llvm {
class GlobalVariable;
class Value;
}  // namespace llvm

namespace cursive::codegen {

// Forward declarations
class LLVMEmitter;
struct LowerCtx;

// Convert panic reason string to numeric code
std::uint16_t PanicCodeFromString(const std::string& reason);

// Load the panic output pointer from the current function's local
llvm::Value* LoadPanicOutPtr(LLVMEmitter& emitter,
                             llvm::IRBuilder<>* builder);

// Load the panic code (u32) from the panic record.
// Returns nullptr when panic record access is unavailable.
llvm::Value* LoadPanicCode(LLVMEmitter& emitter,
                           llvm::IRBuilder<>* builder);

// Load the panic flag from the panic record as an i1 condition value. When
// `panic_ptr` is null, the current function's panic out-parameter is used.
llvm::Value* LoadPanicFlag(LLVMEmitter& emitter,
                           llvm::IRBuilder<>* builder,
                           llvm::Value* panic_ptr = nullptr);

// Load the panic code from an explicit panic record pointer. When `panic_ptr`
// is null, the current function's panic out-parameter is used.
llvm::Value* LoadPanicCodeValue(LLVMEmitter& emitter,
                                llvm::IRBuilder<>* builder,
                                llvm::Value* panic_ptr = nullptr);

// Store a panic record with the given code
void StorePanicRecord(LLVMEmitter& emitter,
                      llvm::IRBuilder<>* builder,
                      std::uint16_t code);

// Clear the panic record (set to non-panic state)
void ClearPanicRecord(LLVMEmitter& emitter,
                      llvm::IRBuilder<>* builder);

// Emit a return instruction with default value
void EmitReturn(LLVMEmitter& emitter, llvm::IRBuilder<>* builder);

// Emit conditional panic (sets panic record if condition is false, then
// continues so the following PanicCheck/InitPanicHandle observes it)
void EmitPanicIfFalse(LLVMEmitter& emitter,
                      llvm::IRBuilder<>* builder,
                      llvm::Value* ok,
                      std::uint16_t code);

// Emit conditional panic and return (panics and returns if condition is false)
void EmitPanicReturnIfFalse(LLVMEmitter& emitter,
                            llvm::IRBuilder<>* builder,
                            llvm::Value* ok,
                            std::uint16_t code);

// Get or create a poison flag global for a module
llvm::GlobalVariable* GetOrCreatePoisonFlag(LLVMEmitter& emitter,
                                            const std::vector<std::string>& module_path);

// Resolve the poison flag storage pointer for a module. Hosted libraries route
// this to the active session slot; all other builds use the process-global flag.
llvm::Value* GetPoisonFlagPtr(LLVMEmitter& emitter,
                              const std::vector<std::string>& module_path);

// Split a module path string (e.g. "foo::bar::baz") into components
std::vector<std::string> SplitModulePathString(const std::string& module);

// Check if a function is an init function
bool IsInitFunction(LLVMEmitter& emitter, llvm::Function* func);

// Compute the set of modules to poison on init failure
std::vector<std::string> PoisonSetForInit(const LowerCtx& ctx);

// Store panic record for init function failures
void StoreInitPanicRecord(LLVMEmitter& emitter,
                          llvm::IRBuilder<>* builder,
                          const std::vector<std::string>* poison_modules = nullptr);

}  // namespace cursive::codegen
