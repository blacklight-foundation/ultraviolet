// =============================================================================
// File: 05_codegen/llvm/emit/ir/memory/ptr.cpp
// Canonical owner for LLVM IR pointer read and write instructions lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRReadPtr &read) const
{
  llvm::Type *result_ty = ExpectedLLVMType(read.result);
  if (!result_ty)
  {
    emitter.SetTempValue(read.result, DefaultFor(read.result));
    return;
  }
  llvm::Value *ptr = EvaluateOrDefault(read.ptr);
  if (!ptr)
  {
    emitter.SetTempValue(read.result, DefaultFor(read.result));
    return;
  }

  llvm::PointerType *typed_ptr_ty = llvm::PointerType::get(result_ty, 0);
  if (ptr->getType()->isIntegerTy())
  {
    ptr = builder.CreateIntToPtr(ptr, typed_ptr_ty);
  }
  else if (!ptr->getType()->isPointerTy())
  {
    emitter.SetTempValue(read.result, DefaultFor(read.result));
    return;
  }
  llvm::Value *typed_ptr = ptr;
  if (typed_ptr->getType() != typed_ptr_ty)
  {
    auto *src_ptr_ty = llvm::dyn_cast<llvm::PointerType>(typed_ptr->getType());
    if (!src_ptr_ty)
    {
      emitter.SetTempValue(read.result, DefaultFor(read.result));
      return;
    }
    if (src_ptr_ty->getAddressSpace() == typed_ptr_ty->getAddressSpace())
    {
      typed_ptr = builder.CreateBitCast(typed_ptr, typed_ptr_ty);
    }
    else
    {
      typed_ptr = CoerceTo(&builder, typed_ptr, typed_ptr_ty);
      if (!typed_ptr)
      {
        emitter.SetTempValue(read.result, DefaultFor(read.result));
        return;
      }
    }
  }

  llvm::Value *loaded = builder.CreateLoad(result_ty, typed_ptr);
  emitter.SetTempValue(read.result, loaded ? loaded : DefaultFor(read.result));
}

void IRInstructionVisitor::operator()(const IRWritePtr &write) const
{
  auto hosted_state_pointer = [&](const IRValue &value) -> bool
  {
    const LowerCtx *active_ctx = emitter.GetCurrentCtx();
    if (!active_ctx || !emitter.IsHostedLibraryBuild())
    {
      return false;
    }

    IRValue cursor = value;
    for (int depth = 0; depth < 16; ++depth)
    {
      if (cursor.kind == IRValue::Kind::Symbol)
      {
        std::string symbol = cursor.name;
        if (auto alias = emitter.LookupSymbolAlias(symbol))
        {
          symbol = *alias;
        }
        return emitter.HasHostedStateSlot(symbol);
      }
      if (cursor.kind != IRValue::Kind::Opaque)
      {
        return false;
      }

      const DerivedValueInfo *derived = active_ctx->LookupDerivedValue(cursor);
      if (!derived)
      {
        return false;
      }

      switch (derived->kind)
      {
      case DerivedValueInfo::Kind::AddrStatic:
      {
        std::string symbol = derived->name;
        if (!derived->static_path.empty() && !derived->name.empty())
        {
          if (auto* lower_ctx = emitter.GetCurrentCtx();
              lower_ctx && lower_ctx->sigma) {
            if (auto addr =
                    StaticAddr(*lower_ctx->sigma,
                               derived->static_path,
                               derived->name)) {
              symbol = addr->name;
            } else {
              symbol = StaticSymPath(derived->static_path,
                                     derived->name);
            }
          } else {
            symbol = StaticSymPath(derived->static_path,
                                   derived->name);
          }
        }
        if (emitter.HasHostedStateSlot(symbol))
        {
          return true;
        }
        if (!derived->name.empty())
        {
          if (auto alias = emitter.LookupSymbolAlias(derived->name))
          {
            if (emitter.HasHostedStateSlot(*alias))
            {
              return true;
            }
          }
          if (emitter.HasHostedStateSlot(derived->name))
          {
            return true;
          }
        }
        return false;
      }
      case DerivedValueInfo::Kind::AddrField:
      case DerivedValueInfo::Kind::AddrTuple:
      case DerivedValueInfo::Kind::AddrIndex:
      case DerivedValueInfo::Kind::AddrDeref:
      case DerivedValueInfo::Kind::LoadFromAddr:
        cursor = derived->base;
        continue;
      default:
        return false;
      }
    }
    return false;
  };

  llvm::Value *ptr = emitter.EvaluateIRValue(write.ptr);
  if (!ptr)
  {
    if (hosted_state_pointer(write.ptr))
    {
      if (const LowerCtx *active_ctx = emitter.GetCurrentCtx())
      {
        const_cast<LowerCtx *>(active_ctx)->ReportCodegenFailure();
      }
      return;
    }
    ptr = DefaultFor(write.ptr);
  }
  llvm::Value *value = EvaluateOrDefault(write.value);
  if (!ptr || !value)
  {
    return;
  }

  llvm::Type *value_ty = value->getType();
  llvm::PointerType *typed_ptr_ty = llvm::PointerType::get(value_ty, 0);
  if (ptr->getType()->isIntegerTy())
  {
    ptr = builder.CreateIntToPtr(ptr, typed_ptr_ty);
  }
  else if (!ptr->getType()->isPointerTy())
  {
    return;
  }

  llvm::Value *typed_ptr = ptr;
  if (typed_ptr->getType() != typed_ptr_ty)
  {
    auto *src_ptr_ty = llvm::dyn_cast<llvm::PointerType>(typed_ptr->getType());
    if (!src_ptr_ty)
    {
      return;
    }
    if (src_ptr_ty->getAddressSpace() == typed_ptr_ty->getAddressSpace())
    {
      typed_ptr = builder.CreateBitCast(typed_ptr, typed_ptr_ty);
    }
    else
    {
      typed_ptr = CoerceTo(&builder, typed_ptr, typed_ptr_ty);
      if (!typed_ptr)
      {
        return;
      }
    }
  }

  builder.CreateStore(value, typed_ptr);
}

} // namespace cursive::codegen::emit_detail
