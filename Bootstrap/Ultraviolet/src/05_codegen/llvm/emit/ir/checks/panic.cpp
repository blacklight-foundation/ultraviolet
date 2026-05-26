// =============================================================================
// File: 05_codegen/llvm/emit/ir/checks/panic.cpp
// Canonical owner for LLVM IR panic-state instructions lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRClearPanic &) const
{
  ClearPanicRecord(emitter, &builder);
}

namespace {

llvm::AllocaInst *CreatePanicCheckAlloca(llvm::IRBuilder<> &builder,
                                         llvm::Type *type,
                                         llvm::StringRef name)
{
  llvm::Function *function = builder.GetInsertBlock()
                                ? builder.GetInsertBlock()->getParent()
                                : nullptr;
  if (!function)
  {
    return builder.CreateAlloca(type, nullptr, name);
  }
  llvm::IRBuilder<> entry_builder(&function->getEntryBlock(),
                                  function->getEntryBlock().begin());
  return entry_builder.CreateAlloca(type, nullptr, name);
}

llvm::Value *MaterializePanicCheckValueRef(llvm::IRBuilder<> &builder,
                                           llvm::Value *value,
                                           llvm::Type *storage_ty,
                                           llvm::StringRef name)
{
  llvm::AllocaInst *slot = CreatePanicCheckAlloca(builder, storage_ty, name);
  if (slot && value)
  {
    builder.CreateStore(value, slot);
  }
  return slot;
}

void EmitPanicCheckImpl(LLVMEmitter &emitter,
                        llvm::IRBuilder<> &builder,
                        const IRPtr &cleanup_ir)
{
  llvm::Value *panic_ptr = LoadPanicOutPtr(emitter, &builder);
  if (!panic_ptr)
  {
    return;
  }
  llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
  llvm::Type *i8_ptr_ty = llvm::PointerType::get(emitter.GetContext(), 0);
  llvm::Value *flag_ptr = CoerceTo(&builder, panic_ptr, i8_ptr_ty);
  if (!flag_ptr)
  {
    return;
  }
  llvm::Value *flag = builder.CreateLoad(i8_ty, flag_ptr);
  llvm::Value *has_panic = builder.CreateICmpNE(flag, llvm::ConstantInt::get(i8_ty, 0));

  llvm::Function *func = builder.GetInsertBlock()->getParent();
  llvm::BasicBlock *panic_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "panic.take", func);
  llvm::BasicBlock *cont_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "panic.cont", func);
  builder.CreateCondBr(has_panic, panic_bb, cont_bb);

  builder.SetInsertPoint(panic_bb);
  if (cleanup_ir)
  {
    emitter.EmitIR(cleanup_ir);
  }
  const bool entry_stub_panic =
      func && func->getName() == EntrySym() &&
      emitter.GetCurrentCtx() && emitter.GetCurrentCtx()->executable_project;
  if (entry_stub_panic && !builder.GetInsertBlock()->getTerminator())
  {
    llvm::Value *panic_code = LoadPanicCode(emitter, &builder);
    if (!panic_code)
    {
      panic_code = llvm::ConstantInt::get(
          llvm::Type::getInt32Ty(emitter.GetContext()), 1);
    }
    llvm::Function *runtime_panic_fn =
        emitter.GetModule().getFunction(RuntimePanicSym());
    if (!runtime_panic_fn)
    {
      llvm::FunctionType *panic_ty = llvm::FunctionType::get(
          llvm::Type::getVoidTy(emitter.GetContext()),
          {emitter.GetOpaquePtr()},
          false);
      runtime_panic_fn = llvm::Function::Create(
          panic_ty,
          llvm::GlobalValue::ExternalLinkage,
          RuntimePanicSym(),
          &emitter.GetModule());
      runtime_panic_fn->setCallingConv(llvm::CallingConv::C);
    }
    llvm::Type *i32_ty = llvm::Type::getInt32Ty(emitter.GetContext());
    panic_code = CoerceTo(&builder, panic_code, i32_ty);
    if (!panic_code)
    {
      panic_code = llvm::ConstantInt::get(i32_ty, 1);
    }
    llvm::Value *panic_code_ref = MaterializePanicCheckValueRef(
        builder, panic_code, i32_ty, "panic_check_code");
    builder.CreateCall(runtime_panic_fn->getFunctionType(),
                       runtime_panic_fn,
                       {panic_code_ref});
    builder.CreateUnreachable();
    builder.SetInsertPoint(cont_bb);
    return;
  }
  if (!builder.GetInsertBlock()->getTerminator())
  {
    EmitReturn(emitter, &builder);
  }

  builder.SetInsertPoint(cont_bb);
}

}  // namespace

void IRInstructionVisitor::operator()(const IRPanicCheck &) const
{
  EmitPanicCheckImpl(emitter, builder, IRPtr{});
}

void IRInstructionVisitor::operator()(const IRCleanupPanicCheck &check) const
{
  EmitPanicCheckImpl(emitter, builder, check.cleanup_ir);
}

void IRInstructionVisitor::operator()(const IRLowerPanic &panic) const
{
  const std::uint16_t code = PanicCodeFromString(panic.reason);
  StorePanicRecord(emitter, &builder, code);
  if (panic.cleanup_ir)
  {
    emitter.EmitIR(panic.cleanup_ir);
  }
  if (!builder.GetInsertBlock()->getTerminator())
  {
    EmitReturn(emitter, &builder);
  }
}

void IRInstructionVisitor::operator()(const IRInitPanicRaise &raise) const
{
  StoreInitPanicRecord(emitter, &builder, &raise.poison_modules);
  if (raise.cleanup_ir)
  {
    emitter.EmitIR(raise.cleanup_ir);
  }
  if (!builder.GetInsertBlock()->getTerminator())
  {
    EmitReturn(emitter, &builder);
  }
}

void IRInstructionVisitor::operator()(const IRInitPanicHandle &handle) const
{
  llvm::Value *panic_ptr = LoadPanicOutPtr(emitter, &builder);
  if (!panic_ptr)
  {
    return;
  }
  llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
  llvm::Type *i8_ptr_ty = llvm::PointerType::get(emitter.GetContext(), 0);
  llvm::Value *flag_ptr = CoerceTo(&builder, panic_ptr, i8_ptr_ty);
  if (!flag_ptr)
  {
    return;
  }
  llvm::Value *flag = builder.CreateLoad(i8_ty, flag_ptr);
  llvm::Value *has_panic =
      builder.CreateICmpNE(flag, llvm::ConstantInt::get(i8_ty, 0));

  llvm::Function *func = builder.GetInsertBlock()->getParent();
  llvm::BasicBlock *panic_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "init.panic.take", func);
  llvm::BasicBlock *cont_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "init.panic.cont", func);
  builder.CreateCondBr(has_panic, panic_bb, cont_bb);

  builder.SetInsertPoint(panic_bb);
  StoreInitPanicRecord(emitter, &builder, &handle.poison_modules);
  if (handle.cleanup_ir)
  {
    emitter.EmitIR(handle.cleanup_ir);
  }
  if (!builder.GetInsertBlock()->getTerminator())
  {
    EmitReturn(emitter, &builder);
  }

  builder.SetInsertPoint(cont_bb);
}

} // namespace ultraviolet::codegen::emit_detail
