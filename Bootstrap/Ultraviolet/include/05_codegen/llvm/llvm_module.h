// =============================================================================
// File: 05_codegen/llvm/llvm_module.h
// Construct: LLVM Module-Level IR Setup
// Spec Section: 6.12.1, 6.12.6, 6.12.7
// Spec Rules: TargetTriple, TargetDataLayout, RuntimeDecls, LLVMToolchain
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "01_project/target_profile.h"
#include "01_project/llvm_toolchain.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/ir/ir_model.h"

// Forward declarations for LLVM types
namespace llvm {
class Comdat;
class GlobalVariable;
class LLVMContext;
class Module;
class Type;
}  // namespace llvm

namespace ultraviolet::codegen {

// Forward declarations
class LLVMEmitter;
struct LowerCtx;

// =============================================================================
// §6.12.1 LLVM Module Header
// =============================================================================

// LLVMHeader = [TargetDataLayout(LLVMDataLayout), TargetTriple(LLVMTriple)]
// Sets up the module with the required target triple and data layout
void SetupModuleHeader(llvm::Module& module,
                       project::TargetProfile profile);

// =============================================================================
// §6.12.7 LLVM Toolchain Version
// =============================================================================

// Validate that the current LLVM version matches the required version
bool ValidateLLVMVersion();

// =============================================================================
// §6.12.6 Runtime Declarations
// =============================================================================

// RuntimeSyms - Set of runtime function symbols that must be declared
// RuntimeSig(sym) = ⟨params, ret⟩

// Declare all runtime functions in the module
void DeclareRuntimeFunctions(LLVMEmitter& emitter);
void DeclareRuntimeFunctions(LLVMEmitter& emitter,
                             const std::vector<std::string>& symbols);

// Runtime declaration invariants for the emitted LLVM module.
std::vector<std::string> DeclSyms(const llvm::Module& module);
bool RuntimeDeclsOk(const llvm::Module& module);
bool RuntimeDeclsCover(const llvm::Module& module, const IRDecls& decls);

// Get the runtime symbol for a specific builtin operation
std::string_view GetRuntimeSymbol(std::string_view operation);

// Check if a symbol is a runtime function
bool IsRuntimeSymbol(std::string_view symbol);

// =============================================================================
// Module Creation and Management
// =============================================================================

// Create a new LLVM module with proper setup
std::unique_ptr<llvm::Module> CreateModule(llvm::LLVMContext& context,
                                           const std::string& name,
                                           project::TargetProfile profile);

// =============================================================================
// Global Variable and COMDAT Management
// =============================================================================

// Create a COMDAT group for generated declarations that require one.
llvm::Comdat* GetOrCreateComdat(llvm::Module& module, const std::string& name);

// Create a global variable with zero initialization
llvm::GlobalVariable* CreateZeroInitGlobal(llvm::Module& module,
                                           llvm::Type* type,
                                           const std::string& name,
                                           bool is_const);

// Create a poison flag global for module initialization tracking
// GetOrCreatePoisonFlag is declared in llvm_ir_panic.h

// =============================================================================
// Symbol Management
// =============================================================================

// Check if a symbol is generated drop glue.
bool IsDropGlueSymbol(std::string_view symbol);

// Get the mangled symbol prefix for drop glue
std::string_view GetDropGluePrefix();

// Emit-key helpers for generated declaration uniqueness.
std::optional<std::string> EmitKey(const IRDecl& decl);
std::vector<std::string> EmitKeys(const IRDecls& decls);
bool UniqueEmits(const IRDecls& decls);

// =============================================================================
// Module Finalization
// =============================================================================

// Finalize a module (add any required metadata, validate structure)
void FinalizeModule(llvm::Module& module);

// Verify module is well-formed
bool VerifyModule(llvm::Module& module);

}  // namespace ultraviolet::codegen
