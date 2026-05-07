// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/seq.cpp
// Canonical owner for LLVM IR sequence instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRSeq &seq) const
{
  struct ForwardTargetInfo
  {
    const std::string *name = nullptr;
    const IRValue *value = nullptr;
    analysis::TypeRef bind_type = nullptr;
    llvm::Value *direct_storage = nullptr;
  };

  auto find_call_producing = [&](auto &&self,
                                 const IRPtr &ir,
                                 const IRValue &target_value) -> const IRCall *
  {
    if (!ir)
    {
      return nullptr;
    }
    if (const auto *call = std::get_if<IRCall>(&ir->node))
    {
      if (call->result.kind == target_value.kind &&
          call->result.name == target_value.name)
      {
        return call;
      }
      return nullptr;
    }
    const auto *nested = std::get_if<IRSeq>(&ir->node);
    if (nested)
    {
      for (const auto &child : nested->items)
      {
        if (const IRCall *call = self(self, child, target_value))
        {
          return call;
        }
      }
      return nullptr;
    }
    if (const auto *if_ir = std::get_if<IRIf>(&ir->node))
    {
      if (const IRCall *call = self(self, if_ir->then_ir, target_value))
      {
        return call;
      }
      return self(self, if_ir->else_ir, target_value);
    }
    if (const auto *block = std::get_if<IRBlock>(&ir->node))
    {
      if (const IRCall *call = self(self, block->setup, target_value))
      {
        return call;
      }
      return self(self, block->body, target_value);
    }
    if (const auto *loop = std::get_if<IRLoop>(&ir->node))
    {
      if (const IRCall *call = self(self, loop->iter_ir, target_value))
      {
        return call;
      }
      if (const IRCall *call = self(self, loop->cond_ir, target_value))
      {
        return call;
      }
      return self(self, loop->body_ir, target_value);
    }
    if (const auto *if_case = std::get_if<IRIfCase>(&ir->node))
    {
      for (const auto &arm : if_case->arms)
      {
        if (const IRCall *call = self(self, arm.body, target_value))
        {
          return call;
        }
      }
      return nullptr;
    }
    if (const auto *region = std::get_if<IRRegion>(&ir->node))
    {
      return self(self, region->body, target_value);
    }
    if (const auto *frame = std::get_if<IRFrame>(&ir->node))
    {
      return self(self, frame->body, target_value);
    }
    return nullptr;
  };

  auto target_info = [&](const IRPtr &ir) -> ForwardTargetInfo
  {
    if (!ir)
    {
      return {};
    }
    if (const auto *store = std::get_if<IRStoreVar>(&ir->node))
    {
      return ForwardTargetInfo{&store->name, &store->value, nullptr};
    }
    if (const auto *store_nodrop = std::get_if<IRStoreVarNoDrop>(&ir->node))
    {
      return ForwardTargetInfo{&store_nodrop->name, &store_nodrop->value, nullptr};
    }
    if (const auto *bind = std::get_if<IRBindVar>(&ir->node))
    {
      return ForwardTargetInfo{&bind->name, &bind->value, bind->type};
    }
    if (const auto *ret = std::get_if<IRReturn>(&ir->node))
    {
      llvm::Function *func =
          builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
      const LowerCtx *active_ctx = emitter.GetCurrentCtx();
      if (!func || !active_ctx)
      {
        return {};
      }
      const std::string symbol = std::string(func->getName());
      const LowerCtx::ProcSigInfo *sig = active_ctx->LookupProcSig(symbol);
      if (!sig || !sig->ret)
      {
        return {};
      }
      llvm::Type *ret_ty = emitter.GetLLVMType(sig->ret);
      if (!ret_ty || ret_ty->isVoidTy())
      {
        return {};
      }
      const bool aggregate_ret =
          ret_ty->isArrayTy() ||
          (llvm::isa<llvm::StructType>(ret_ty) &&
           llvm::cast<llvm::StructType>(ret_ty)->getNumElements() != 0);
      if (!aggregate_ret)
      {
        return {};
      }
      ABICallResult abi = ComputeProcABI(emitter, symbol, sig->params, sig->ret);
      if (!abi.valid || !abi.has_sret || func->arg_size() == 0)
      {
        return {};
      }
      return ForwardTargetInfo{nullptr, &ret->value, nullptr, func->getArg(0)};
    }
    return {};
  };

  auto ensure_home_slot = [&](const ForwardTargetInfo &target) -> llvm::Value *
  {
    if (target.direct_storage)
    {
      return target.direct_storage;
    }
    if (!target.name)
    {
      return nullptr;
    }

    llvm::Value *home_slot = emitter.GetLocalBindStorage(*target.name);
    if (home_slot || !target.bind_type)
    {
      return home_slot;
    }

    llvm::Type *slot_ty = emitter.GetLLVMType(target.bind_type);
    if (!slot_ty || slot_ty->isVoidTy())
    {
      return nullptr;
    }

    llvm::Function *func = builder.GetInsertBlock()->getParent();
    llvm::IRBuilder<> entry_builder(&func->getEntryBlock(),
                                    func->getEntryBlock().begin());
    home_slot = entry_builder.CreateAlloca(slot_ty, nullptr, *target.name);
    emitter.SetLocalHomeStorage(*target.name, home_slot);
    return home_slot;
  };

  for (std::size_t index = 0; index < seq.items.size(); ++index)
  {
    if (builder.GetInsertBlock()->getTerminator())
    {
      break;
    }
    const auto &item = seq.items[index];
    if (index + 1 < seq.items.size())
    {
      for (std::size_t lookahead = index + 1;
           lookahead < seq.items.size();
           ++lookahead)
      {
        if (std::holds_alternative<IROpaque>(seq.items[lookahead]->node) ||
            std::holds_alternative<IRPanicCheck>(seq.items[lookahead]->node))
        {
          continue;
        }
        const ForwardTargetInfo target = target_info(seq.items[lookahead]);
        if (!target.value || (!target.name && !target.direct_storage))
        {
          continue;
        }
        if (const auto *call =
                find_call_producing(find_call_producing, item, *target.value))
        {
          llvm::Value *home_slot = ensure_home_slot(target);
          if (home_slot && home_slot->getType()->isPointerTy())
          {
            emitter.SetPreferredResultStorage(call->result, home_slot);
            break;
          }
        }
      }
    }
    emitter.EmitIR(item);
  }
}

} // namespace cursive::codegen::emit_detail
