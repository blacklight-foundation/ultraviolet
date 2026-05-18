// =============================================================================
// File: 05_codegen/llvm/llvm_backend.h
// Construct: LLVM Backend Coordination
// Spec Section: 6.12 LLVM 21 Backend Requirements
// =============================================================================
#pragma once

#include <memory>
#include <string>

#include "01_project/target_profile.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/llvm/llvm_passes.h"

// Forward declarations for LLVM types
namespace llvm {
class LLVMContext;
class Module;
}  // namespace llvm

namespace ultraviolet::codegen {

// Forward declarations
struct LowerCtx;

// =============================================================================
// Compilation Pipeline Entry Points
// =============================================================================

// Compile IR to optimized LLVM module
// Returns ownership of the LLVM module, or nullptr on failure
llvm::Module* CompileToLLVM(const IRDecls& decls,
                            LowerCtx& ctx,
                            llvm::LLVMContext& llvm_ctx,
                            OptLevel opt_level,
                            project::TargetProfile profile);

// Compile IR directly to object file
bool CompileToObject(const IRDecls& decls,
                     LowerCtx& ctx,
                     llvm::LLVMContext& llvm_ctx,
                     const std::string& output_path,
                     OptLevel opt_level,
                     project::TargetProfile profile);

// Compile IR directly to assembly file
bool CompileToAssembly(const IRDecls& decls,
                       LowerCtx& ctx,
                       llvm::LLVMContext& llvm_ctx,
                       const std::string& output_path,
                       OptLevel opt_level,
                       project::TargetProfile profile);

// =============================================================================
// Object File Generation
// =============================================================================

// Emit object file from LLVM module
bool EmitObjectFile(llvm::Module& module,
                    const std::string& output_path,
                    OptLevel opt_level,
                    project::TargetProfile profile);

// Emit assembly file from LLVM module
bool EmitAssemblyFile(llvm::Module& module,
                      const std::string& output_path,
                      OptLevel opt_level,
                      project::TargetProfile profile);

// Emit LLVM IR file from module
bool EmitIRFile(llvm::Module& module, const std::string& output_path);

// Emit LLVM bitcode file from module
bool EmitBitcodeFile(llvm::Module& module, const std::string& output_path);

// =============================================================================
// Target Information
// =============================================================================

std::string GetTargetTriple(project::TargetProfile profile);

std::string GetTargetDataLayout(project::TargetProfile profile);

// Get the host system's target triple
std::string GetHostTriple();

// Check if a target triple is supported
bool IsTargetSupported(const std::string& triple);

// =============================================================================
// Debug Utilities
// =============================================================================

// Dump module to stderr for debugging
void DumpModuleToStderr(llvm::Module& module);

// Verify module and report errors
bool VerifyAndReport(llvm::Module& module, std::string& error_msg);

}  // namespace ultraviolet::codegen
