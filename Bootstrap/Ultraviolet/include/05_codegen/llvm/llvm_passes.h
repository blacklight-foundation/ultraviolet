// =============================================================================
// File: 05_codegen/llvm/llvm_passes.h
// Construct: LLVM Optimization Passes
// Spec Section: 6.12 (implied - optimization is implementation detail)
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "01_project/target_profile.h"

// Forward declarations for LLVM types
namespace llvm {
class Module;
class TargetMachine;
class raw_ostream;
class raw_pwrite_stream;
}  // namespace llvm

namespace ultraviolet::codegen {

// =============================================================================
// Optimization Level Configuration
// =============================================================================

// Optimization level for codegen
enum class OptLevel {
  O0,  // No optimization
  O1,  // Minimal optimization
  O2,  // Standard optimization
  O3,  // Aggressive optimization
  Os,  // Size optimization
  Oz,  // Aggressive size optimization
};

// LTO (Link-Time Optimization) mode
enum class LTOMode {
  None,    // No LTO
  Thin,    // ThinLTO
  Full,    // Full LTO
};

// =============================================================================
// Pass Pipeline Configuration
// =============================================================================

struct PassConfig {
  OptLevel opt_level = OptLevel::O0;
  LTOMode lto_mode = LTOMode::None;
  bool debug_info = true;
  bool verify_input = true;
  bool verify_output = true;
  bool inline_functions = true;
  bool vectorize_loops = false;
  bool vectorize_slp = false;
  bool unroll_loops = false;
};

// =============================================================================
// Pass Pipeline Management (New Pass Manager - LLVM 21)
// =============================================================================

// Run the standard optimization pass pipeline for Ultraviolet
// Uses the LLVM 21 new pass manager with PassBuilder
void RunOptimizationPipeline(llvm::Module& module, const PassConfig& config);

// Run function-level optimization passes
void RunFunctionPasses(llvm::Module& module, const PassConfig& config);

// Run module-level optimization passes
void RunModulePasses(llvm::Module& module, const PassConfig& config);

// =============================================================================
// Individual Pass Control
// =============================================================================

// Run just the verification passes
bool RunVerificationPasses(llvm::Module& module);

// Run dead code elimination
void RunDCE(llvm::Module& module);

// Run constant folding and propagation
void RunConstantFolding(llvm::Module& module);

// Run instruction combining
void RunInstructionCombining(llvm::Module& module);

// Run global value numbering
void RunGVN(llvm::Module& module);

// Run scalar replacement of aggregates
void RunSROA(llvm::Module& module);

// =============================================================================
// Target Machine and Code Generation
// =============================================================================

// Create a target machine for the selected Ultraviolet target profile.
std::unique_ptr<llvm::TargetMachine> CreateTargetMachine(
    OptLevel opt_level,
    project::TargetProfile profile);

// Emit object code to a stream
bool EmitObjectCode(llvm::Module& module,
                    llvm::TargetMachine& target,
                    llvm::raw_pwrite_stream& output);

// Emit assembly code to a stream
bool EmitAssembly(llvm::Module& module,
                  llvm::TargetMachine& target,
                  llvm::raw_pwrite_stream& output);

// Emit LLVM IR to a stream
void EmitLLVMIR(llvm::Module& module,
                llvm::raw_ostream& output,
                bool preserve_use_list_order = false);

// Emit LLVM bitcode to a stream
void EmitBitcode(llvm::Module& module,
                 llvm::raw_ostream& output);

// =============================================================================
// Debug and Diagnostic Passes
// =============================================================================

// Print module statistics
void PrintModuleStats(const llvm::Module& module,
                      llvm::raw_ostream& output);

// Dump module to stderr (for debugging)
void DumpModule(const llvm::Module& module);

// Check module for common issues
bool SanitizeModule(llvm::Module& module, std::string& error_msg);

}  // namespace ultraviolet::codegen
