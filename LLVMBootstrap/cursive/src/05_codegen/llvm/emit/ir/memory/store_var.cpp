// =============================================================================
// File: 05_codegen/llvm/emit/ir/memory/store_var.cpp
// Canonical owner for LLVM IR variable store instructions lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRStoreVar &store) const
{
  llvm::Value *slot = emitter.GetLocal(store.name);
  llvm::Value *source_storage = emitter.GetAddressableStorage(store.value);
  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  analysis::TypeRef target_type = emitter.LookupLocalType(store.name);
  if (!target_type && active_ctx)
  {
    if (const BindingState *state = active_ctx->GetBindingState(store.name))
    {
      target_type = state->type;
    }
  }
  analysis::TypeRef source_type = LookupValueType(store.value);
  if (!slot)
  {
    slot = emitter.GetLocalHomeStorage(store.name);
    if (slot)
    {
      emitter.SetLocal(store.name, slot);
    }
  }
  if (!slot)
  {
    llvm::Value *value = nullptr;
    llvm::Type *slot_ty = target_type ? emitter.GetLLVMType(target_type) : nullptr;
    if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(source_storage))
    {
      if (!slot_ty)
      {
        slot_ty = alloca->getAllocatedType();
      }
    }
    if (!slot_ty)
    {
      value = EvaluateOrDefault(store.value);
      if (value)
      {
        slot_ty = value->getType();
      }
    }
    if (!slot_ty || slot_ty->isVoidTy())
    {
      slot_ty = llvm::Type::getInt64Ty(emitter.GetContext());
    }

    llvm::Function *func = builder.GetInsertBlock()->getParent();
    llvm::IRBuilder<> entry_builder(&func->getEntryBlock(),
                                   func->getEntryBlock().begin());
    llvm::Value *new_slot =
        entry_builder.CreateAlloca(slot_ty, nullptr, store.name);
    emitter.RegisterLocalBindStorage(store.name, new_slot);

    if (TryEmitDerivedAggregateToStorage(
            emitter,
            &builder,
            new_slot,
            store.value,
            target_type ? target_type : source_type))
    {
      emitter.ReleaseTempStorage(store.value);
      return;
    }

    if (TryEmitBitcopyAggregateStorageCopy(
            emitter,
            &builder,
            new_slot,
            source_storage,
            target_type ? target_type : source_type,
            source_type))
    {
      emitter.ReleaseTempStorage(store.value);
      return;
    }

    if (!value)
    {
      value = EvaluateOrDefault(store.value);
    }
    if (!value)
    {
      value = llvm::Constant::getNullValue(slot_ty);
    }
    else if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(new_slot))
    {
      if (target_type)
      {
        value = CoerceToTyped(
            emitter,
            &builder,
            value,
            alloca->getAllocatedType(),
            source_type,
            target_type);
      }
      else
      {
        value = CoerceTo(&builder, value, alloca->getAllocatedType());
      }
      if (!value)
      {
        value = llvm::Constant::getNullValue(alloca->getAllocatedType());
      }
    }

    builder.CreateStore(value, new_slot);
    emitter.ReleaseTempStorage(store.value);
    return;
  }
  if (TryEmitDerivedAggregateToStorage(
          emitter,
          &builder,
          slot,
          store.value,
          target_type ? target_type : source_type))
  {
    emitter.ReleaseTempStorage(store.value);
    return;
  }
  if (source_storage)
  {
    if (TryEmitBitcopyAggregateStorageCopy(
            emitter,
            &builder,
            slot,
            source_storage,
            target_type,
            source_type))
    {
      emitter.ReleaseTempStorage(store.value);
      return;
    }
    if (source_storage == slot)
    {
      emitter.ForgetTempStorage(store.value);
      return;
    }
  }
  llvm::Value *value = EvaluateOrDefault(store.value);
  if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(slot))
  {
    if (target_type)
    {
      value = CoerceToTyped(
          emitter,
          &builder,
          value,
          alloca->getAllocatedType(),
          source_type,
          target_type);
    }
    else
    {
      value = CoerceTo(&builder, value, alloca->getAllocatedType());
    }
    if (!value)
    {
      value = llvm::Constant::getNullValue(alloca->getAllocatedType());
    }
  }
  builder.CreateStore(value, slot);
  emitter.ReleaseTempStorage(store.value);
}

void IRInstructionVisitor::operator()(const IRStoreVarNoDrop &store) const
{
  (*this)(IRStoreVar{store.name, store.value});
}

} // namespace cursive::codegen::emit_detail
