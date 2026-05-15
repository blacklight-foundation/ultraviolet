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
        if (const IRCall *call = self(self, arm.cleanup_ir, target_value))
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
    home_slot = CreateEntryAlloca(func, slot_ty, *target.name);
    emitter.SetLocalHomeStorage(*target.name, home_slot);
    return home_slot;
  };

  auto value_references_target =
      [&](auto &&self,
          const IRValue &value,
          const std::string &target_name,
          int depth = 0) -> bool
  {
    if (target_name.empty())
    {
      return false;
    }
    if (value.kind == IRValue::Kind::Local && value.name == target_name)
    {
      return true;
    }
    if (depth > 32)
    {
      return true;
    }

    const LowerCtx *active_ctx = emitter.GetCurrentCtx();
    const DerivedValueInfo *derived =
        active_ctx ? active_ctx->LookupDerivedValue(value) : nullptr;
    if (!derived)
    {
      return false;
    }

    auto ref_value = [&](const IRValue &candidate) -> bool
    {
      if (candidate.name.empty() &&
          candidate.kind != IRValue::Kind::Immediate)
      {
        return false;
      }
      return self(self, candidate, target_name, depth + 1);
    };

    auto ref_range = [&](const IRRange &range) -> bool
    {
      if (range.lo.has_value() && ref_value(*range.lo))
      {
        return true;
      }
      if (range.hi.has_value() && ref_value(*range.hi))
      {
        return true;
      }
      return false;
    };

    if (ref_value(derived->base) ||
        ref_value(derived->index) ||
        ref_range(derived->range) ||
        (derived->range_value.has_value() &&
         ref_value(*derived->range_value)) ||
        ref_value(derived->repeat_value) ||
        ref_value(derived->repeat_count))
    {
      return true;
    }
    for (const IRValue &element : derived->elements)
    {
      if (ref_value(element))
      {
        return true;
      }
    }
    for (const DerivedArraySegment &segment : derived->array_segments)
    {
      if (ref_value(segment.value) ||
          (segment.count.has_value() && ref_value(*segment.count)))
      {
        return true;
      }
    }
    for (const auto &[_, field_value] : derived->fields)
    {
      if (ref_value(field_value))
      {
        return true;
      }
    }
    for (const IRValue &payload : derived->payload_elems)
    {
      if (ref_value(payload))
      {
        return true;
      }
    }
    for (const auto &[_, payload_value] : derived->payload_fields)
    {
      if (ref_value(payload_value))
      {
        return true;
      }
    }
    return false;
  };

  auto ir_reads_target =
      [&](auto &&self,
          const IRPtr &ir,
          const ForwardTargetInfo &target) -> bool
  {
    if (!ir || !target.name)
    {
      return false;
    }
    const std::string &target_name = *target.name;
    auto ref_value = [&](const IRValue &value) -> bool
    {
      return value_references_target(
          value_references_target,
          value,
          target_name);
    };
    auto ref_range = [&](const IRRange &range) -> bool
    {
      return (range.lo.has_value() && ref_value(*range.lo)) ||
             (range.hi.has_value() && ref_value(*range.hi));
    };

    if (const auto *call = std::get_if<IRCall>(&ir->node))
    {
      if (ref_value(call->callee))
      {
        return true;
      }
      for (const IRValue &arg : call->args)
      {
        if (ref_value(arg))
        {
          return true;
        }
      }
      return false;
    }
    if (const auto *vtable_call = std::get_if<IRCallVTable>(&ir->node))
    {
      if (ref_value(vtable_call->base))
      {
        return true;
      }
      for (const IRValue &arg : vtable_call->args)
      {
        if (ref_value(arg))
        {
          return true;
        }
      }
      return false;
    }
    if (const auto *nested = std::get_if<IRSeq>(&ir->node))
    {
      for (const auto &child : nested->items)
      {
        if (self(self, child, target))
        {
          return true;
        }
      }
      return false;
    }
    if (const auto *if_ir = std::get_if<IRIf>(&ir->node))
    {
      return ref_value(if_ir->cond) ||
             self(self, if_ir->then_ir, target) ||
             ref_value(if_ir->then_value) ||
             self(self, if_ir->else_ir, target) ||
             ref_value(if_ir->else_value);
    }
    if (const auto *block = std::get_if<IRBlock>(&ir->node))
    {
      return self(self, block->setup, target) ||
             self(self, block->body, target) ||
             ref_value(block->value);
    }
    if (const auto *loop = std::get_if<IRLoop>(&ir->node))
    {
      return self(self, loop->iter_ir, target) ||
             (loop->iter_value.has_value() && ref_value(*loop->iter_value)) ||
             self(self, loop->cond_ir, target) ||
             (loop->cond_value.has_value() && ref_value(*loop->cond_value)) ||
             self(self, loop->body_ir, target) ||
             ref_value(loop->body_value);
    }
    if (const auto *if_case = std::get_if<IRIfCase>(&ir->node))
    {
      if (ref_value(if_case->scrutinee))
      {
        return true;
      }
      for (const IRIfCaseClause &arm : if_case->arms)
      {
        if (self(self, arm.body, target) ||
            self(self, arm.cleanup_ir, target) ||
            ref_value(arm.value))
        {
          return true;
        }
      }
      return false;
    }
    if (const auto *region = std::get_if<IRRegion>(&ir->node))
    {
      return ref_value(region->owner) ||
             self(self, region->body, target) ||
             ref_value(region->value);
    }
    if (const auto *frame = std::get_if<IRFrame>(&ir->node))
    {
      return (frame->region.has_value() && ref_value(*frame->region)) ||
             self(self, frame->body, target) ||
             ref_value(frame->value);
    }
    if (const auto *bind = std::get_if<IRBindVar>(&ir->node))
    {
      return ref_value(bind->value);
    }
    if (const auto *store = std::get_if<IRStoreVar>(&ir->node))
    {
      return ref_value(store->value);
    }
    if (const auto *store_nodrop = std::get_if<IRStoreVarNoDrop>(&ir->node))
    {
      return ref_value(store_nodrop->value);
    }
    if (const auto *store_global = std::get_if<IRStoreGlobal>(&ir->node))
    {
      return ref_value(store_global->value);
    }
    if (const auto *read = std::get_if<IRReadVar>(&ir->node))
    {
      return read->name == target_name;
    }
    if (const auto *unary = std::get_if<IRUnaryOp>(&ir->node))
    {
      return ref_value(unary->operand);
    }
    if (const auto *binary = std::get_if<IRBinaryOp>(&ir->node))
    {
      return ref_value(binary->lhs) || ref_value(binary->rhs);
    }
    if (const auto *cast = std::get_if<IRCast>(&ir->node))
    {
      return ref_value(cast->value);
    }
    if (const auto *transmute = std::get_if<IRTransmute>(&ir->node))
    {
      return ref_value(transmute->value);
    }
    if (const auto *check_index = std::get_if<IRCheckIndex>(&ir->node))
    {
      return ref_value(check_index->base) || ref_value(check_index->index);
    }
    if (const auto *check_range = std::get_if<IRCheckRange>(&ir->node))
    {
      return ref_value(check_range->base) ||
             ref_range(check_range->range) ||
             (check_range->range_value.has_value() &&
              ref_value(*check_range->range_value));
    }
    if (const auto *check_slice_len = std::get_if<IRCheckSliceLen>(&ir->node))
    {
      return ref_value(check_slice_len->base) ||
             ref_range(check_slice_len->range) ||
             (check_slice_len->range_value.has_value() &&
              ref_value(*check_slice_len->range_value)) ||
             ref_value(check_slice_len->value);
    }
    if (const auto *check_op = std::get_if<IRCheckOp>(&ir->node))
    {
      return ref_value(check_op->lhs) ||
             (check_op->rhs.has_value() && ref_value(*check_op->rhs));
    }
    if (const auto *check_cast = std::get_if<IRCheckCast>(&ir->node))
    {
      return ref_value(check_cast->value);
    }
    if (const auto *alloc = std::get_if<IRAlloc>(&ir->node))
    {
      return (alloc->region.has_value() && ref_value(*alloc->region)) ||
             ref_value(alloc->value);
    }
    if (const auto *context_bundle =
            std::get_if<IRContextBundleBuild>(&ir->node))
    {
      return ref_value(context_bundle->root_ctx);
    }
    if (const auto *ret = std::get_if<IRReturn>(&ir->node))
    {
      return ref_value(ret->value);
    }
    if (const auto *result = std::get_if<IRResult>(&ir->node))
    {
      return ref_value(result->value);
    }
    if (const auto *brk = std::get_if<IRBreak>(&ir->node))
    {
      return brk->value.has_value() && ref_value(*brk->value);
    }
    if (const auto *read_ptr = std::get_if<IRReadPtr>(&ir->node))
    {
      return ref_value(read_ptr->ptr);
    }
    if (const auto *write_ptr = std::get_if<IRWritePtr>(&ir->node))
    {
      return ref_value(write_ptr->ptr) || ref_value(write_ptr->value);
    }
    if (const auto *addr_of = std::get_if<IRAddrOf>(&ir->node))
    {
      for (const std::string &ref_sym : addr_of->ref_syms)
      {
        if (ref_sym == target_name)
        {
          return true;
        }
      }
      return false;
    }

    return false;
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
            std::holds_alternative<IRPanicCheck>(seq.items[lookahead]->node) ||
            std::holds_alternative<IRCleanupPanicCheck>(seq.items[lookahead]->node) ||
            std::holds_alternative<IRInitPanicHandle>(seq.items[lookahead]->node))
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
          if (home_slot && home_slot->getType()->isPointerTy() &&
              !ir_reads_target(ir_reads_target, item, target))
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
