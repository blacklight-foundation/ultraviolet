// =============================================================================
// File: 05_codegen/llvm/emit/ir/control/if.cpp
// Canonical owner for LLVM IR if instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

#include <string>
#include <utility>

namespace ultraviolet::codegen::emit_detail {

namespace {

std::string EnsureBlockLabel(llvm::BasicBlock &block, const char *fallback)
{
  if (block.getName().empty())
  {
    block.setName(fallback);
  }
  return block.getName().str();
}

}  // namespace

void IRInstructionVisitor::operator()(const IRIf &node) const
{
  llvm::Function *func = builder.GetInsertBlock()->getParent();
  llvm::Value *raw_cond = emitter.EvaluateIRValue(node.cond);
  const bool cond_defaulted = (raw_cond == nullptr);
  if (!raw_cond)
  {
    raw_cond = DefaultFor(node.cond);
  }
  if (core::IsDebugEnabled("return") && func)
  {
    const std::string func_name = func->getName().str();
    if (func_name.find("ContractPredicateIntegrationShift") != std::string::npos)
    {
      std::string cond_ty = "<null>";
      if (raw_cond && raw_cond->getType())
      {
        std::string cond_ty_buf;
        llvm::raw_string_ostream os(cond_ty_buf);
        raw_cond->getType()->print(os);
        os.flush();
        cond_ty = cond_ty_buf;
      }
      std::cerr << "[llvm-if-debug] func=" << func_name
                << " cond_kind=" << static_cast<int>(node.cond.kind)
                << " cond_name=" << node.cond.name
                << " cond_defaulted=" << (cond_defaulted ? "1" : "0")
                << " cond_llvm_ty=" << cond_ty
                << "\n";
    }
  }
  llvm::Value *cond = raw_cond;
  cond = AsBool(&builder, cond);

  llvm::BasicBlock *then_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "if.then", func);
  llvm::BasicBlock *else_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "if.else", func);
  llvm::BasicBlock *merge_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "if.merge", func);

  IRBranch conditional_branch;
  conditional_branch.cond = node.cond;
  conditional_branch.true_label = EnsureBlockLabel(*then_bb, "if.then");
  conditional_branch.false_label = EnsureBlockLabel(*else_bb, "if.else");
  if (cond_defaulted)
  {
    builder.CreateCondBr(cond, then_bb, else_bb);
  }
  else
  {
    (*this)(conditional_branch);
  }
  const LLVMEmitter::FlowStateSnapshot branch_state =
      emitter.SaveFlowState();

  struct IncomingValue
  {
    llvm::BasicBlock *pred = nullptr;
    llvm::Value *value = nullptr;
    llvm::Value *storage = nullptr;
    IRValue ir_value;
  };

  std::vector<IncomingValue> incoming;
  bool has_fallthrough_arm = false;
  analysis::TypeRef result_type = LookupValueType(node.result);
  llvm::Type *result_ty = ExpectedLLVMType(node.result);
  if (!result_ty && result_type)
  {
    result_ty = emitter.GetLLVMType(result_type);
  }
  const bool aggregate_result =
      result_ty && !result_ty->isVoidTy() &&
      IsAddressBackedAggregateType(result_ty);
  llvm::Value *merged_storage = nullptr;
  if (aggregate_result)
  {
    merged_storage =
        emitter.AcquireReusableAggregateStorage(func, result_ty, "if.result");
    llvm::Type *expected_ptr_ty = llvm::PointerType::get(result_ty, 0);
    if (merged_storage && merged_storage->getType() != expected_ptr_ty)
    {
      merged_storage = builder.CreateBitCast(merged_storage, expected_ptr_ty);
    }
  }

  auto store_aggregate_result = [&](const IRValue &source_ir,
                                    llvm::Value *source_storage,
                                    llvm::Value *source_value) -> bool
  {
    if (!merged_storage || !result_ty)
    {
      return false;
    }
    analysis::TypeRef source_type = LookupValueType(source_ir);
    analysis::TypeRef target_type = result_type ? result_type : source_type;
    if (source_storage && source_storage->getType()->isPointerTy())
    {
      if (TryEmitBitcopyAggregateStorageCopy(
              emitter,
              &builder,
              merged_storage,
              source_storage,
              target_type,
              source_type))
      {
        return true;
      }
      if (TryEmitAggregateStorageTransfer(
              emitter,
              &builder,
              merged_storage,
              source_storage,
              target_type,
              source_type))
      {
        return true;
      }
    }
    if (target_type &&
        StoreIRValueToStorage(
            emitter,
            &builder,
            merged_storage,
            source_ir,
            target_type,
            result_ty,
            1))
    {
      return true;
    }
    if (target_type &&
        TryEmitDerivedAggregateToStorage(
            emitter,
            &builder,
            merged_storage,
            source_ir,
            target_type))
    {
      return true;
    }
    if (!source_value)
    {
      return false;
    }
    llvm::Value *candidate = source_value;
    if (!candidate)
    {
      return false;
    }
    llvm::Value *coerced = CoerceToTyped(
        emitter,
        &builder,
        candidate,
        result_ty,
        source_type,
        result_type);
    if (!coerced)
    {
      coerced = CoerceTo(&builder, candidate, result_ty);
    }
    if (!coerced)
    {
      coerced = llvm::Constant::getNullValue(result_ty);
    }
    builder.CreateStore(coerced, merged_storage);
    return true;
  };

  auto emit_merge_branch = [&]()
  {
    IRBranch merge_branch;
    merge_branch.true_label = EnsureBlockLabel(*merge_bb, "if.merge");
    (*this)(merge_branch);
  };

  builder.SetInsertPoint(then_bb);
  emitter.RestoreFlowState(branch_state);
  emitter.EmitIR(node.then_ir);
  llvm::BasicBlock *then_end = builder.GetInsertBlock();
  if (!then_end->getTerminator())
  {
    llvm::Value *then_storage = emitter.GetAddressableStorage(node.then_value);
    llvm::Value *then_val = nullptr;
    if (aggregate_result && merged_storage)
    {
      if (!store_aggregate_result(node.then_value, then_storage, nullptr))
      {
        then_val = EvaluateOrDefault(node.then_value);
        (void)store_aggregate_result(node.then_value, then_storage, then_val);
      }
    }
    else
    {
      then_val = EvaluateOrDefault(node.then_value);
    }
    has_fallthrough_arm = true;
    emit_merge_branch();
    if (!aggregate_result || !merged_storage)
    {
      incoming.push_back({then_end, then_val, then_storage, node.then_value});
    }
  }

  builder.SetInsertPoint(else_bb);
  emitter.RestoreFlowState(branch_state);
  emitter.EmitIR(node.else_ir);
  llvm::BasicBlock *else_end = builder.GetInsertBlock();
  if (!else_end->getTerminator())
  {
    llvm::Value *else_storage = emitter.GetAddressableStorage(node.else_value);
    llvm::Value *else_val = nullptr;
    if (aggregate_result && merged_storage)
    {
      if (!store_aggregate_result(node.else_value, else_storage, nullptr))
      {
        else_val = EvaluateOrDefault(node.else_value);
        (void)store_aggregate_result(node.else_value, else_storage, else_val);
      }
    }
    else
    {
      else_val = EvaluateOrDefault(node.else_value);
    }
    has_fallthrough_arm = true;
    emit_merge_branch();
    if (!aggregate_result || !merged_storage)
    {
      incoming.push_back({else_end, else_val, else_storage, node.else_value});
    }
  }

  builder.SetInsertPoint(merge_bb);
  emitter.RestoreFlowState(branch_state);
  if (!has_fallthrough_arm)
  {
    if (!merge_bb->getTerminator())
    {
      builder.CreateUnreachable();
    }
    return;
  }

  if (!result_ty)
  {
    result_ty = incoming.front().value
                    ? incoming.front().value->getType()
                    : llvm::Type::getInt64Ty(emitter.GetContext());
  }
  if (!result_ty || result_ty->isVoidTy() ||
      IsZeroSizedLLVMType(emitter, result_ty))
  {
    emitter.SetTempValue(node.result, DefaultFor(node.result));
    return;
  }

  if (aggregate_result && merged_storage)
  {
    emitter.ForgetTempStorage(node.result);
    emitter.SetTempStorage(node.result, merged_storage);
    return;
  }

  if (incoming.empty())
  {
    if (!merge_bb->getTerminator())
    {
      builder.CreateUnreachable();
    }
    return;
  }

  llvm::Value *merged = nullptr;
  auto coerce_in_predecessor = [&](llvm::BasicBlock *pred, llvm::Value *value) -> llvm::Value *
  {
    llvm::Value *candidate = value ? value : llvm::Constant::getNullValue(result_ty);
    if (!candidate)
    {
      return llvm::Constant::getNullValue(result_ty);
    }
    if (pred && pred->getTerminator())
    {
      llvm::IRBuilder<> pred_builder(pred->getTerminator());
      llvm::Value *coerced = CoerceToTyped(
          emitter,
          &pred_builder,
          candidate,
          result_ty,
          nullptr,
          result_type);
      if (!coerced)
      {
        coerced = CoerceTo(&pred_builder, candidate, result_ty);
      }
      return coerced ? coerced : llvm::Constant::getNullValue(result_ty);
    }
    llvm::Value *coerced = CoerceToTyped(
        emitter,
        &builder,
        candidate,
        result_ty,
        nullptr,
        result_type);
    if (!coerced)
    {
      coerced = CoerceTo(&builder, candidate, result_ty);
    }
    return coerced ? coerced : llvm::Constant::getNullValue(result_ty);
  };
  if (incoming.size() == 1)
  {
    merged = coerce_in_predecessor(incoming.front().pred, incoming.front().value);
  }
  else if (result_type)
  {
    IRPhi phi_ir;
    phi_ir.type = result_type;
    phi_ir.value = node.result;
    phi_ir.incoming.reserve(incoming.size());
    for (const auto &entry : incoming)
    {
      if (entry.value)
      {
        emitter.SetTempValue(entry.ir_value, entry.value);
      }
      IRIncoming incoming_value;
      incoming_value.label = EnsureBlockLabel(*entry.pred, "if.pred");
      incoming_value.value = entry.ir_value;
      phi_ir.incoming.push_back(std::move(incoming_value));
    }
    (*this)(phi_ir);
    merged = emitter.GetTempValue(node.result);
  }
  else
  {
    llvm::PHINode *phi = builder.CreatePHI(result_ty, incoming.size(), "if.result");
    for (const auto &entry : incoming)
    {
      llvm::Value *coerced = coerce_in_predecessor(entry.pred, entry.value);
      phi->addIncoming(coerced, entry.pred);
    }
    merged = phi;
  }
  emitter.SetTempValue(node.result, merged);
}

} // namespace ultraviolet::codegen::emit_detail
