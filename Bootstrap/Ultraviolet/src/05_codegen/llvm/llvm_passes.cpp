// =============================================================================
// MIGRATION MAPPING: llvm_passes.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.12 LLVM 21 Backend Requirements
//   - Optimization pass configuration
//   - Pass pipeline construction
//
// SOURCE FILE: No direct bootstrap equivalent
//   - Pass configuration is new infrastructure
//   - Based on LLVM 21 new pass manager patterns
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/llvm/llvm_passes.h
//   - llvm/Passes/PassBuilder.h
//   - llvm/Passes/StandardInstrumentations.h
//   - llvm/Analysis/TargetTransformInfo.h
// =============================================================================

#include "05_codegen/llvm/llvm_passes.h"

#include "01_project/target_profile.h"
#include "05_codegen/llvm/llvm_module.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/ADCE.h"
#include "llvm/Transforms/Scalar/DeadStoreElimination.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/MemCpyOptimizer.h"
#include "llvm/Transforms/Scalar/NewGVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"

#include <string>

namespace ultraviolet::codegen {

// =============================================================================
// Pass Pipeline Configuration
// =============================================================================

namespace {

llvm::OptimizationLevel ToLLVMOptLevel(OptLevel level) {
  switch (level) {
    case OptLevel::O0:
      return llvm::OptimizationLevel::O0;
    case OptLevel::O1:
      return llvm::OptimizationLevel::O1;
    case OptLevel::O2:
      return llvm::OptimizationLevel::O2;
    case OptLevel::O3:
      return llvm::OptimizationLevel::O3;
    case OptLevel::Os:
      return llvm::OptimizationLevel::Os;
    case OptLevel::Oz:
      return llvm::OptimizationLevel::Oz;
    default:
      return llvm::OptimizationLevel::O0;
  }
}

llvm::CodeGenOptLevel ToCodeGenOptLevel(OptLevel level) {
  switch (level) {
    case OptLevel::O0:
      return llvm::CodeGenOptLevel::None;
    case OptLevel::O1:
      return llvm::CodeGenOptLevel::Less;
    case OptLevel::O2:
    case OptLevel::Os:
    case OptLevel::Oz:
      return llvm::CodeGenOptLevel::Default;
    case OptLevel::O3:
      return llvm::CodeGenOptLevel::Aggressive;
    default:
      return llvm::CodeGenOptLevel::None;
  }
}

// Create and register all analysis managers
struct AnalysisManagers {
  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;

  AnalysisManagers(llvm::PassBuilder& PB) {
    // Register all analyses
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
  }
};

llvm::FunctionPassManager BuildRequiredO0FunctionPipeline() {
  llvm::FunctionPassManager FPM;
  // O0 still needs canonicalization that prevents execution from depending on
  // artificial entry-block stack slots created by the frontend lowering.
  FPM.addPass(llvm::SROAPass(llvm::SROAOptions::ModifyCFG));
  FPM.addPass(llvm::PromotePass());
  FPM.addPass(llvm::InstCombinePass());
  FPM.addPass(llvm::EarlyCSEPass());
  FPM.addPass(llvm::SimplifyCFGPass());
  FPM.addPass(llvm::DSEPass());
  FPM.addPass(llvm::ADCEPass());
  FPM.addPass(llvm::SimplifyCFGPass());
  return FPM;
}

void AddScalarCleanupPipeline(llvm::FunctionPassManager& FPM,
                              bool aggressive) {
  FPM.addPass(llvm::SROAPass(llvm::SROAOptions::ModifyCFG));
  FPM.addPass(llvm::PromotePass());
  FPM.addPass(llvm::EarlyCSEPass());
  FPM.addPass(llvm::InstCombinePass());
  FPM.addPass(llvm::SimplifyCFGPass());
  FPM.addPass(llvm::MemCpyOptPass());
  FPM.addPass(llvm::ReassociatePass());
  FPM.addPass(llvm::NewGVNPass());
  FPM.addPass(llvm::DSEPass());
  FPM.addPass(llvm::ADCEPass());
  if (aggressive) {
    FPM.addPass(llvm::InstCombinePass());
  }
  FPM.addPass(llvm::MemCpyOptPass());
  FPM.addPass(llvm::SimplifyCFGPass());
}

bool TypeContainsLargeArray(llvm::Type* type) {
  if (!type) {
    return false;
  }
  if (auto* array = llvm::dyn_cast<llvm::ArrayType>(type)) {
    if (array->getNumElements() >= 128) {
      return true;
    }
    return TypeContainsLargeArray(array->getElementType());
  }
  if (auto* structure = llvm::dyn_cast<llvm::StructType>(type)) {
    for (llvm::Type* element : structure->elements()) {
      if (TypeContainsLargeArray(element)) {
        return true;
      }
    }
  }
  return false;
}

bool UsesStorageHeavyAggregate(const llvm::Function& func) {
  for (const llvm::Argument& arg : func.args()) {
    if (arg.hasStructRetAttr()) {
      return true;
    }
  }
  for (const llvm::BasicBlock& block : func) {
    for (const llvm::Instruction& inst : block) {
      if (const auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
        if (TypeContainsLargeArray(alloca->getAllocatedType())) {
          return true;
        }
      }
    }
  }
  return false;
}

void AddStorageHeavyCleanupPipeline(llvm::FunctionPassManager& FPM) {
  // Large fixed-size aggregate storage is intentionally addressable after UV
  // lowering. Avoid aggregate-splitting and global value numbering passes that
  // can turn bounded storage cleanup into superlinear compiler work.
  FPM.addPass(llvm::PromotePass());
  FPM.addPass(llvm::EarlyCSEPass());
  FPM.addPass(llvm::InstCombinePass());
  FPM.addPass(llvm::SimplifyCFGPass());
  FPM.addPass(llvm::DSEPass());
  FPM.addPass(llvm::ADCEPass());
  FPM.addPass(llvm::SimplifyCFGPass());
}

}  // namespace

// =============================================================================
// Pass Pipeline Management (New Pass Manager)
// =============================================================================

void RunFunctionPasses(llvm::Module& module, const PassConfig& config) {
  llvm::PassBuilder PB;
  AnalysisManagers AM(PB);

  llvm::FunctionPassManager FPM;

  // Add function-level passes based on optimization level
  switch (config.opt_level) {
    case OptLevel::O0:
      FPM = BuildRequiredO0FunctionPipeline();
      break;

    case OptLevel::O1:
    case OptLevel::O2:
    case OptLevel::Os:
    case OptLevel::Oz:
    case OptLevel::O3:
      // Standard function passes
      FPM.addPass(llvm::PromotePass());
      FPM.addPass(llvm::EarlyCSEPass());
      FPM.addPass(llvm::SimplifyCFGPass());
      break;
  }

  // Run on all functions
  for (auto& func : module) {
    if (!func.isDeclaration()) {
      FPM.run(func, AM.FAM);
    }
  }
}

void RunModulePasses(llvm::Module& module, const PassConfig& config) {
  llvm::PassBuilder PB;
  AnalysisManagers AM(PB);

  llvm::ModulePassManager MPM;

  // Always verify input if requested
  if (config.verify_input) {
    MPM.addPass(llvm::VerifierPass());
  }

  // Create function pass managers for function-level optimizations.
  llvm::FunctionPassManager FPM;
  llvm::FunctionPassManager storage_heavy_fpm;

  // Add optimization passes based on level
  switch (config.opt_level) {
    case OptLevel::O0:
      FPM = BuildRequiredO0FunctionPipeline();
      break;

    case OptLevel::O1:
      // Basic optimization
      AddScalarCleanupPipeline(FPM, /*aggressive=*/false);
      AddStorageHeavyCleanupPipeline(storage_heavy_fpm);
      break;

    case OptLevel::O2:
    case OptLevel::Os:
    case OptLevel::Oz:
      // Standard optimization
      AddScalarCleanupPipeline(FPM, /*aggressive=*/false);
      AddStorageHeavyCleanupPipeline(storage_heavy_fpm);
      break;

    case OptLevel::O3:
      // Aggressive optimization
      AddScalarCleanupPipeline(FPM, /*aggressive=*/true);
      AddStorageHeavyCleanupPipeline(storage_heavy_fpm);
      break;
  }

  if (config.opt_level != OptLevel::O0 && config.inline_functions) {
    MPM.addPass(llvm::AlwaysInlinerPass(/*InsertLifetime=*/false));
  }

  for (auto& func : module) {
    if (func.isDeclaration()) {
      continue;
    }
    if (config.opt_level != OptLevel::O0 && UsesStorageHeavyAggregate(func)) {
      storage_heavy_fpm.run(func, AM.FAM);
    } else {
      FPM.run(func, AM.FAM);
    }
  }

  // Add module-level passes
  if (config.opt_level != OptLevel::O0) {
    MPM.addPass(llvm::GlobalDCEPass());
  }

  // Always verify output if requested
  if (config.verify_output) {
    MPM.addPass(llvm::VerifierPass());
  }

  MPM.run(module, AM.MAM);
}

void RunOptimizationPipeline(llvm::Module& module, const PassConfig& config) {
  llvm::PassBuilder PB;
  AnalysisManagers AM(PB);

  llvm::OptimizationLevel OptLevel = ToLLVMOptLevel(config.opt_level);

  // UV lowering intentionally keeps large fixed-size aggregates in addressable
  // storage. LLVM's full default pipeline can spend disproportionate time on
  // those aggregates before object emission, so release levels use the curated
  // scalar cleanup pipeline below while TargetMachine still receives the
  // requested codegen optimization level.
  if (OptLevel != llvm::OptimizationLevel::O0) {
    if (config.verify_input) {
      llvm::ModulePassManager VerifyMPM;
      VerifyMPM.addPass(llvm::VerifierPass());
      VerifyMPM.run(module, AM.MAM);
    }

    RunModulePasses(module, config);

    if (config.verify_output) {
      llvm::ModulePassManager VerifyMPM;
      VerifyMPM.addPass(llvm::VerifierPass());
      VerifyMPM.run(module, AM.MAM);
    }
  } else {
    if (config.verify_input) {
      llvm::ModulePassManager VerifyMPM;
      VerifyMPM.addPass(llvm::VerifierPass());
      VerifyMPM.run(module, AM.MAM);
    }

    llvm::ModulePassManager MPM;
    MPM.addPass(
        llvm::createModuleToFunctionPassAdaptor(BuildRequiredO0FunctionPipeline()));
    MPM.run(module, AM.MAM);

    if (config.verify_output) {
      llvm::ModulePassManager VerifyMPM;
      VerifyMPM.addPass(llvm::VerifierPass());
      VerifyMPM.run(module, AM.MAM);
    }
  }
}

// =============================================================================
// Individual Pass Control
// =============================================================================

bool RunVerificationPasses(llvm::Module& module) {
  std::string error;
  llvm::raw_string_ostream os(error);
  return !llvm::verifyModule(module, &os);
}

void RunDCE(llvm::Module& module) {
  llvm::PassBuilder PB;
  AnalysisManagers AM(PB);

  llvm::FunctionPassManager FPM;
  FPM.addPass(llvm::ADCEPass());

  for (auto& func : module) {
    if (!func.isDeclaration()) {
      FPM.run(func, AM.FAM);
    }
  }
}

void RunConstantFolding(llvm::Module& module) {
  // Constant folding is implicit in many passes
  // Run early-cse which includes some constant folding
  llvm::PassBuilder PB;
  AnalysisManagers AM(PB);

  llvm::FunctionPassManager FPM;
  FPM.addPass(llvm::EarlyCSEPass());

  for (auto& func : module) {
    if (!func.isDeclaration()) {
      FPM.run(func, AM.FAM);
    }
  }
}

void RunInstructionCombining(llvm::Module& module) {
  llvm::PassBuilder PB;
  AnalysisManagers AM(PB);

  llvm::FunctionPassManager FPM;
  FPM.addPass(llvm::InstCombinePass());

  for (auto& func : module) {
    if (!func.isDeclaration()) {
      FPM.run(func, AM.FAM);
    }
  }
}

void RunGVN(llvm::Module& module) {
  llvm::PassBuilder PB;
  AnalysisManagers AM(PB);

  llvm::FunctionPassManager FPM;
  FPM.addPass(llvm::NewGVNPass());

  for (auto& func : module) {
    if (!func.isDeclaration()) {
      FPM.run(func, AM.FAM);
    }
  }
}

void RunSROA(llvm::Module& module) {
  llvm::PassBuilder PB;
  AnalysisManagers AM(PB);

  llvm::FunctionPassManager FPM;
  FPM.addPass(llvm::SROAPass(llvm::SROAOptions::ModifyCFG));

  for (auto& func : module) {
    if (!func.isDeclaration()) {
      FPM.run(func, AM.FAM);
    }
  }
}

// =============================================================================
// Target Machine and Code Generation
// =============================================================================

std::unique_ptr<llvm::TargetMachine> CreateTargetMachine(
    OptLevel opt_level,
    project::TargetProfile profile) {
  // Initialize target registry
  std::string triple = std::string(project::LLVMTripleOf(profile));
  std::string error;

  const llvm::Target* target =
      llvm::TargetRegistry::lookupTarget(triple, error);
  if (!target) {
    return nullptr;
  }

  // Target options
  llvm::TargetOptions options;
  options.UnsafeFPMath = false;
  options.NoInfsFPMath = false;
  options.NoNaNsFPMath = false;

  // Create target machine
  return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
      triple,
      "generic",  // CPU
      "",         // Features
      options,
      llvm::Reloc::PIC_,
      llvm::CodeModel::Small,
      ToCodeGenOptLevel(opt_level)));
}

bool EmitObjectCode(llvm::Module& module,
                    llvm::TargetMachine& target,
                    llvm::raw_pwrite_stream& output) {
  // LLVM 21 object emission is exposed through this pass-manager API.
  llvm::legacy::PassManager pm;

  if (target.addPassesToEmitFile(pm, output, nullptr,
                                 llvm::CodeGenFileType::ObjectFile)) {
    return false;  // Target doesn't support object emission
  }

  pm.run(module);
  return true;
}

bool EmitAssembly(llvm::Module& module,
                  llvm::TargetMachine& target,
                  llvm::raw_pwrite_stream& output) {
  // LLVM 21 assembly emission is exposed through this pass-manager API.
  llvm::legacy::PassManager pm;

  if (target.addPassesToEmitFile(pm, output, nullptr,
                                 llvm::CodeGenFileType::AssemblyFile)) {
    return false;  // Target doesn't support assembly emission
  }

  pm.run(module);
  return true;
}

void EmitLLVMIR(llvm::Module& module,
                llvm::raw_ostream& output,
                bool preserve_use_list_order) {
  module.print(output, nullptr, preserve_use_list_order);
}

void EmitBitcode(llvm::Module& module, llvm::raw_ostream& output) {
  // Use module's native bitcode writing
  llvm::WriteBitcodeToFile(module, output);
}

// =============================================================================
// Debug and Diagnostic Passes
// =============================================================================

void PrintModuleStats(const llvm::Module& module, llvm::raw_ostream& output) {
  output << "Module: " << module.getName() << "\n";

  // Count functions, globals, and aliases
  std::size_t num_functions = 0;
  std::size_t num_globals = 0;
  std::size_t num_aliases = 0;
  for (const auto& f : module.functions()) {
    (void)f;
    ++num_functions;
  }
  for (const auto& g : module.globals()) {
    (void)g;
    ++num_globals;
  }
  for (const auto& a : module.aliases()) {
    (void)a;
    ++num_aliases;
  }

  output << "  Functions: " << num_functions << "\n";
  output << "  Globals: " << num_globals << "\n";
  output << "  Aliases: " << num_aliases << "\n";

  std::size_t total_insts = 0;
  std::size_t total_bbs = 0;
  for (const auto& func : module) {
    total_bbs += func.size();
    for (const auto& bb : func) {
      total_insts += bb.size();
    }
  }
  output << "  Basic Blocks: " << total_bbs << "\n";
  output << "  Instructions: " << total_insts << "\n";
}

void DumpModule(const llvm::Module& module) {
  module.print(llvm::errs(), nullptr);
}

bool SanitizeModule(llvm::Module& module, std::string& error_msg) {
  llvm::raw_string_ostream os(error_msg);
  return !llvm::verifyModule(module, &os);
}

}  // namespace ultraviolet::codegen
