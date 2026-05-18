// =============================================================================
// MIGRATION MAPPING: llvm_backend.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.12 LLVM 21 Backend Requirements (lines 17287-17650)
//   - Full compilation pipeline
//   - Object file generation
//   - Linking coordination
//
// SOURCE FILE: Multiple bootstrap files
//   - ultraviolet-bootstrap/src/04_codegen/llvm/llvm_module.cpp
//   - ultraviolet-bootstrap/src/04_codegen/ir_lowering.cpp (main visitor)
//   - ultraviolet-bootstrap/src/04_codegen/llvm/llvm_mem.cpp
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/llvm/llvm_emit.h
//   - ultraviolet/include/05_codegen/llvm/llvm_passes.h
//   - ultraviolet/include/05_codegen/ir/ir_model.h
//   - llvm/IR/Module.h
//   - llvm/Target/TargetMachine.h
//   - llvm/MC/TargetRegistry.h
//
// REFACTORING NOTES:
//   1. Backend coordinates full IR -> object compilation
//   2. Steps:
//      a. Create LLVMEmitter
//      b. Setup module (target, data layout)
//      c. Emit all IR declarations
//      d. Run optimization passes
//      e. Generate object code
//   3. Target machine configuration
//   4. Error handling and diagnostics
//
// COMPILATION PIPELINE:
//   CompileModule(ir_module):
//     1. emitter = LLVMEmitter(ctx, module_name)
//     2. emitter.SetupModule()
//     3. for decl in ir_module.decls:
//          emitter.EmitDecl(decl)
//     4. RunPasses(emitter.module)
//     5. EmitObjectCode(emitter.module, output)
//
// TARGET CONFIGURATION:
//   - Triple: x86_64-pc-windows-msvc
//   - CPU: generic or specific
//   - Features: as needed
//   - Relocation model: PIC or static
//   - Code model: small/large
//
// OBJECT EMISSION:
//   - Use TargetMachine::emitToFile
//   - Output format: COFF for Windows
//   - Debug info: DWARF or CodeView
// =============================================================================

#include "05_codegen/llvm/llvm_emit.h"
#include "05_codegen/llvm/llvm_passes.h"
#include "05_codegen/llvm/llvm_module.h"
#include "05_codegen/ir/ir_model.h"

#include "00_core/spec_trace.h"
#include "01_project/language_profile.h"
#include "01_project/target_profile.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

#include <memory>
#include <string>
#include <system_error>

namespace ultraviolet::codegen {

// =============================================================================
// Backend Configuration
// =============================================================================

namespace {

// Backend output format
enum class OutputFormat {
  Object,     // .obj / .o
  Assembly,   // .asm / .s
  LLVMIR,     // .ll
  Bitcode,    // .bc
};

// Backend compilation options
struct BackendOptions {
  OptLevel opt_level = OptLevel::O0;
  OutputFormat output_format = OutputFormat::Object;
  bool emit_debug_info = true;
  bool verify_module = true;
  std::string cpu = "generic";
  std::string features = "";
  std::string output_path;
};

}  // namespace

// =============================================================================
// Compilation Pipeline
// =============================================================================

// Full compilation pipeline: IR -> LLVM IR -> optimized -> object code
bool CompileModule(const IRDecls& decls,
                   LowerCtx& ctx,
                   llvm::LLVMContext& llvm_ctx,
                   const BackendOptions& options,
                   project::TargetProfile profile) {
  SPEC_RULE("CompileModule-Pipeline");

  // Step 1: Create emitter and emit LLVM IR
  auto emitter =
      std::make_unique<LLVMEmitter>(
          llvm_ctx,
          std::string(project::ActiveLanguageProfile().lower_name) + "_module",
          profile);
  llvm::Module* module = emitter->EmitModule(decls, ctx);

  if (!module) {
    SPEC_RULE("CompileModule-Err-EmitFailed");
    return false;
  }

  // Step 2: Verify the generated module
  if (options.verify_module) {
    std::string verify_error;
    llvm::raw_string_ostream verify_os(verify_error);
    if (llvm::verifyModule(*module, &verify_os)) {
      SPEC_RULE("CompileModule-Err-Verify");
      return false;
    }
  }

  // Step 3: Run optimization passes
  PassConfig pass_config;
  pass_config.opt_level = options.opt_level;
  pass_config.debug_info = options.emit_debug_info;
  pass_config.verify_input = options.verify_module;
  pass_config.verify_output = options.verify_module;

  RunOptimizationPipeline(*module, pass_config);

  // Step 4: Emit output based on format
  if (options.output_path.empty()) {
    SPEC_RULE("CompileModule-Err-NoOutput");
    return false;
  }

  std::error_code ec;
  llvm::raw_fd_ostream output(options.output_path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    SPEC_RULE("CompileModule-Err-OpenFile");
    return false;
  }

  switch (options.output_format) {
    case OutputFormat::LLVMIR: {
      SPEC_RULE("CompileModule-EmitIR");
      EmitLLVMIR(*module, output, false);
      break;
    }

    case OutputFormat::Bitcode: {
      SPEC_RULE("CompileModule-EmitBC");
      EmitBitcode(*module, output);
      break;
    }

    case OutputFormat::Assembly:
    case OutputFormat::Object: {
      auto target_machine = CreateTargetMachine(options.opt_level, profile);
      if (!target_machine) {
        SPEC_RULE("CompileModule-Err-Target");
        return false;
      }

      bool success;
      if (options.output_format == OutputFormat::Assembly) {
        SPEC_RULE("CompileModule-EmitAsm");
        success = EmitAssembly(*module, *target_machine, output);
      } else {
        SPEC_RULE("CompileModule-EmitObj");
        success = EmitObjectCode(*module, *target_machine, output);
      }

      if (!success) {
        SPEC_RULE("CompileModule-Err-Emit");
        return false;
      }
      break;
    }
  }

  SPEC_RULE("CompileModule-Ok");
  return true;
}

// =============================================================================
// Object File Generation
// =============================================================================

bool EmitObjectFile(llvm::Module& module,
                    const std::string& output_path,
                    OptLevel opt_level,
                    project::TargetProfile profile) {
  SPEC_RULE("EmitObjectFile");

  auto target_machine = CreateTargetMachine(opt_level, profile);
  if (!target_machine) {
    return false;
  }

  std::error_code ec;
  llvm::raw_fd_ostream output(output_path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    return false;
  }

  return EmitObjectCode(module, *target_machine, output);
}

bool EmitAssemblyFile(llvm::Module& module,
                      const std::string& output_path,
                      OptLevel opt_level,
                      project::TargetProfile profile) {
  SPEC_RULE("EmitAssemblyFile");

  auto target_machine = CreateTargetMachine(opt_level, profile);
  if (!target_machine) {
    return false;
  }

  std::error_code ec;
  llvm::raw_fd_ostream output(output_path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    return false;
  }

  return EmitAssembly(module, *target_machine, output);
}

bool EmitIRFile(llvm::Module& module, const std::string& output_path) {
  SPEC_RULE("EmitIRFile");

  std::error_code ec;
  llvm::raw_fd_ostream output(output_path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    return false;
  }

  EmitLLVMIR(module, output, false);
  return true;
}

bool EmitBitcodeFile(llvm::Module& module, const std::string& output_path) {
  SPEC_RULE("EmitBitcodeFile");

  std::error_code ec;
  llvm::raw_fd_ostream output(output_path, ec, llvm::sys::fs::OF_None);
  if (ec) {
    return false;
  }

  EmitBitcode(module, output);
  return true;
}

// =============================================================================
// Simplified Entry Points
// =============================================================================

llvm::Module* CompileToLLVM(const IRDecls& decls,
                            LowerCtx& ctx,
                            llvm::LLVMContext& llvm_ctx,
                            OptLevel opt_level,
                            project::TargetProfile profile) {
  SPEC_RULE("CompileToLLVM");

  // Create emitter and emit module
  auto emitter =
      std::make_unique<LLVMEmitter>(
          llvm_ctx,
          std::string(project::ActiveLanguageProfile().lower_name) + "_module",
          profile);
  llvm::Module* module = emitter->EmitModule(decls, ctx);

  if (!module) {
    return nullptr;
  }

  // Verify
  std::string verify_error;
  llvm::raw_string_ostream verify_os(verify_error);
  if (llvm::verifyModule(*module, &verify_os)) {
    return nullptr;
  }

  // Optimize
  PassConfig config;
  config.opt_level = opt_level;
  RunOptimizationPipeline(*module, config);

  // Transfer ownership
  return emitter->ReleaseModule().release();
}

bool CompileToObject(const IRDecls& decls,
                     LowerCtx& ctx,
                     llvm::LLVMContext& llvm_ctx,
                     const std::string& output_path,
                     OptLevel opt_level,
                     project::TargetProfile profile) {
  BackendOptions options;
  options.opt_level = opt_level;
  options.output_format = OutputFormat::Object;
  options.output_path = output_path;

  return CompileModule(decls, ctx, llvm_ctx, options, profile);
}

bool CompileToAssembly(const IRDecls& decls,
                       LowerCtx& ctx,
                       llvm::LLVMContext& llvm_ctx,
                       const std::string& output_path,
                       OptLevel opt_level,
                       project::TargetProfile profile) {
  BackendOptions options;
  options.opt_level = opt_level;
  options.output_format = OutputFormat::Assembly;
  options.output_path = output_path;

  return CompileModule(decls, ctx, llvm_ctx, options, profile);
}

// =============================================================================
// Target Information
// =============================================================================

std::string GetTargetTriple(project::TargetProfile profile) {
  return std::string(project::LLVMTripleOf(profile));
}

std::string GetTargetDataLayout(project::TargetProfile profile) {
  return std::string(project::LLVMDataLayoutOf(profile));
}

std::string GetHostTriple() {
  return llvm::sys::getDefaultTargetTriple();
}

bool IsTargetSupported(const std::string& triple) {
  std::string error;
  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, error);
  return target != nullptr;
}

// =============================================================================
// Debug Utilities
// =============================================================================

void DumpModuleToStderr(llvm::Module& module) {
  DumpModule(module);
}

bool VerifyAndReport(llvm::Module& module, std::string& error_msg) {
  llvm::raw_string_ostream os(error_msg);
  return !llvm::verifyModule(module, &os);
}

}  // namespace ultraviolet::codegen
