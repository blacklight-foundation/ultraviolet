// =============================================================================
// File: 05_codegen/llvm/llvm_emit.cpp
// Construct: LLVM Backend Entry Point
// =============================================================================
#include "05_codegen/llvm/llvm_emit.h"

#include "01_project/language_profile.h"

#include <string>

#include "llvm/IR/Module.h"

namespace ultraviolet::codegen {

llvm::Module *EmitLLVM(const IRDecls &decls,
                       LowerCtx &ctx,
                       llvm::LLVMContext &llvm_ctx,
                       project::TargetProfile profile)
{
  auto emitter =
      std::make_unique<LLVMEmitter>(
          llvm_ctx,
          std::string(project::ActiveLanguageProfile().lower_name) + "_module",
          profile);
  llvm::Module *m = emitter->EmitModule(decls, ctx);
  (void)emitter->ReleaseModule();
  return m;
}

} // namespace ultraviolet::codegen
