// =============================================================================
// File: 05_codegen/llvm/emit/llvm_emit_helpers.cpp
// Canonical internal helper implementation for split LLVM emission owners.
// =============================================================================
#include "05_codegen/llvm/emit/llvm_emit_helpers.h"

namespace ultraviolet::codegen::emit_detail {




    std::optional<AsyncCombinatorKind> AsyncCombinatorKindFromSymbol(
        std::string_view symbol)
    {
      return analysis::LookupBuiltinAsyncCombinatorByRuntimeSymbol(symbol);
    }

    llvm::Value *EmitSliceLenFromAddr(LLVMEmitter &emitter,
                                      llvm::IRBuilder<> &builder,
                                      const analysis::TypeRef &type,
                                      llvm::Value *addr)
    {
      if (!type || !addr || !addr->getType()->isPointerTy())
      {
        return nullptr;
      }

      analysis::TypeRef normalized = analysis::StripPerm(type);
      if (!normalized)
      {
        normalized = type;
      }

      if (!std::holds_alternative<analysis::TypeSlice>(normalized->node) &&
          !std::holds_alternative<analysis::TypeString>(normalized->node) &&
          !std::holds_alternative<analysis::TypeBytes>(normalized->node))
      {
        return nullptr;
      }

      llvm::Type *slice_ll = emitter.GetLLVMType(normalized);
      auto *slice_struct_ty = llvm::dyn_cast_or_null<llvm::StructType>(slice_ll);
      if (!slice_struct_ty || slice_struct_ty->getNumElements() < 2 ||
          !slice_struct_ty->getElementType(1)->isIntegerTy())
      {
        return nullptr;
      }

      llvm::Value *typed_ptr =
          builder.CreateBitCast(addr, llvm::PointerType::get(slice_ll, 0));
      llvm::Value *loaded_slice = builder.CreateLoad(slice_ll, typed_ptr);
      if (!loaded_slice || !loaded_slice->getType()->isStructTy())
      {
        return nullptr;
      }
      return builder.CreateExtractValue(loaded_slice, {1u});
    }

    llvm::Value *EmitIndexLenFromAddr(LLVMEmitter &emitter,
                                      llvm::IRBuilder<> &builder,
                                      const analysis::TypeRef &type,
                                      llvm::Value *addr)
    {
      if (!type || !addr)
      {
        return nullptr;
      }

      analysis::TypeRef normalized = analysis::StripPerm(type);
      if (!normalized)
      {
        normalized = type;
      }

      if (const auto *arr = std::get_if<analysis::TypeArray>(&normalized->node))
      {
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(emitter.GetContext()),
                                      static_cast<std::uint64_t>(arr->length));
      }

      return EmitSliceLenFromAddr(emitter, builder, normalized, addr);
    }

    llvm::Value *EmitSequenceDataPtrFromAddr(LLVMEmitter &emitter,
                                             llvm::IRBuilder<> &builder,
                                             const analysis::TypeRef &type,
                                             llvm::Value *addr)
    {
      if (!type || !addr || !addr->getType()->isPointerTy())
      {
        return nullptr;
      }

      analysis::TypeRef normalized = analysis::StripPerm(type);
      if (!normalized)
      {
        normalized = type;
      }

      if (!std::holds_alternative<analysis::TypeSlice>(normalized->node))
      {
        return nullptr;
      }

      llvm::Type *sequence_ll = emitter.GetLLVMType(normalized);
      auto *sequence_struct_ty = llvm::dyn_cast_or_null<llvm::StructType>(sequence_ll);
      if (!sequence_struct_ty || sequence_struct_ty->getNumElements() < 2 ||
          !sequence_struct_ty->getElementType(0)->isPointerTy())
      {
        return nullptr;
      }

      llvm::Value *typed_ptr =
          builder.CreateBitCast(addr, llvm::PointerType::get(sequence_ll, 0));
      llvm::Value *loaded_sequence = builder.CreateLoad(sequence_ll, typed_ptr);
      if (!loaded_sequence || !loaded_sequence->getType()->isStructTy())
      {
        return nullptr;
      }
      return builder.CreateExtractValue(loaded_sequence, {0u});
    }



    bool PrepareIndexedSequenceIter(LLVMEmitter &emitter,
                                    llvm::IRBuilder<> &entry_builder,
                                    llvm::IRBuilder<> &builder,
                                    const analysis::TypeRef &type,
                                    llvm::Value *value,
                                    IndexedSequenceIterState &out)
    {
      if (!type || !value)
      {
        return false;
      }

      analysis::TypeRef normalized = analysis::StripPerm(type);
      if (!normalized)
      {
        normalized = type;
      }

      llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());

      if (const auto *arr = std::get_if<analysis::TypeArray>(&normalized->node))
      {
        out.element_type = arr->element;
        out.length = llvm::ConstantInt::get(
            i64_ty, static_cast<std::uint64_t>(arr->length));

        if (auto *array_ty = llvm::dyn_cast<llvm::ArrayType>(value->getType()))
        {
          llvm::AllocaInst *array_slot =
              entry_builder.CreateAlloca(array_ty, nullptr, "iter.value.slot");
          builder.CreateStore(value, array_slot);
          out.array_ptr = array_slot;
          out.array_type = array_ty;
          return true;
        }

        llvm::Type *array_ll = emitter.GetLLVMType(normalized);
        auto *array_ty = llvm::dyn_cast_or_null<llvm::ArrayType>(array_ll);
        if (array_ty && value->getType()->isPointerTy())
        {
          out.array_ptr =
              builder.CreateBitCast(value, llvm::PointerType::get(array_ty, 0));
          out.array_type = array_ty;
          return true;
        }

        return false;
      }

      if (const auto *slice = std::get_if<analysis::TypeSlice>(&normalized->node))
      {
        out.element_type = slice->element;

        llvm::Value *len = nullptr;
        llvm::Value *data_ptr = nullptr;
        if (value->getType()->isStructTy())
        {
          len = builder.CreateExtractValue(value, {1u});
          data_ptr = builder.CreateExtractValue(value, {0u});
        }
        else if (value->getType()->isPointerTy())
        {
          len = EmitSliceLenFromAddr(emitter, builder, normalized, value);
          data_ptr = EmitSequenceDataPtrFromAddr(emitter, builder, normalized, value);
        }

        if (!len || !len->getType()->isIntegerTy() || !data_ptr ||
            !data_ptr->getType()->isPointerTy())
        {
          return false;
        }

        if (len->getType()->getIntegerBitWidth() != 64)
        {
          len = builder.CreateIntCast(len, i64_ty, false);
        }

        out.length = len;
        out.data_ptr = data_ptr;
        return true;
      }

      return false;
    }

    llvm::Value *EmitIndexedSequenceElem(LLVMEmitter &emitter,
                                         llvm::IRBuilder<> &builder,
                                         const IndexedSequenceIterState &iter,
                                         llvm::Value *index);

    bool EmitSeqIterInit(LLVMEmitter &emitter,
                         llvm::IRBuilder<> &entry_builder,
                         llvm::IRBuilder<> &builder,
                         const analysis::TypeRef &type,
                         llvm::Value *value,
                         IndexedSequenceLoweredIterState &out)
    {
      IndexedSequenceIterState prepared;
      if (!PrepareIndexedSequenceIter(
              emitter, entry_builder, builder, type, value, prepared))
      {
        return false;
      }

      llvm::Type *elem_ll = emitter.GetLLVMType(prepared.element_type);
      if (!elem_ll || elem_ll->isVoidTy())
      {
        return false;
      }

      llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
      out.iter = std::move(prepared);
      out.idx_slot =
          entry_builder.CreateAlloca(i64_ty, nullptr, "iter.seq.idx");
      out.elem_slot =
          entry_builder.CreateAlloca(elem_ll, nullptr, "iter.seq.elem");
      builder.CreateStore(llvm::ConstantInt::get(i64_ty, 0), out.idx_slot);
      builder.CreateStore(llvm::Constant::getNullValue(elem_ll), out.elem_slot);
      return true;
    }

    llvm::Value *EmitSeqIterNext(LLVMEmitter &emitter,
                                 llvm::IRBuilder<> &builder,
                                 const IndexedSequenceLoweredIterState &iter)
    {
      if (!iter.idx_slot || !iter.elem_slot || !iter.iter.length ||
          !iter.iter.length->getType()->isIntegerTy())
      {
        return llvm::ConstantInt::getFalse(emitter.GetContext());
      }

      llvm::Function *func = builder.GetInsertBlock()
                                 ? builder.GetInsertBlock()->getParent()
                                 : nullptr;
      if (!func)
      {
        return llvm::ConstantInt::getFalse(emitter.GetContext());
      }

      llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
      llvm::Value *idx_cur = builder.CreateLoad(i64_ty, iter.idx_slot);
      llvm::Value *iter_len = iter.iter.length;
      if (iter_len->getType()->getIntegerBitWidth() != 64)
      {
        iter_len = builder.CreateIntCast(iter_len, i64_ty, false);
      }
      llvm::Value *in_range = builder.CreateICmpULT(idx_cur, iter_len);

      llvm::BasicBlock *next_ok =
          llvm::BasicBlock::Create(emitter.GetContext(), "iter.seq.next.ok", func);
      llvm::BasicBlock *next_done =
          llvm::BasicBlock::Create(emitter.GetContext(), "iter.seq.next.done", func);
      llvm::BasicBlock *next_cont =
          llvm::BasicBlock::Create(emitter.GetContext(), "iter.seq.next.cont", func);

      builder.CreateCondBr(in_range, next_ok, next_done);

      builder.SetInsertPoint(next_ok);
      llvm::Value *elem = EmitIndexedSequenceElem(emitter, builder, iter.iter, idx_cur);
      if (!elem)
      {
        elem = llvm::Constant::getNullValue(iter.elem_slot->getAllocatedType());
      }
      builder.CreateStore(elem, iter.elem_slot);
      llvm::Value *idx_next =
          builder.CreateAdd(idx_cur, llvm::ConstantInt::get(i64_ty, 1));
      builder.CreateStore(idx_next, iter.idx_slot);
      builder.CreateBr(next_cont);

      builder.SetInsertPoint(next_done);
      builder.CreateBr(next_cont);

      builder.SetInsertPoint(next_cont);
      llvm::PHINode *has_value =
          builder.CreatePHI(llvm::Type::getInt1Ty(emitter.GetContext()), 2);
      has_value->addIncoming(
          llvm::ConstantInt::getTrue(emitter.GetContext()), next_ok);
      has_value->addIncoming(
          llvm::ConstantInt::getFalse(emitter.GetContext()), next_done);
      return has_value;
    }

    llvm::Value *LoadSeqIterElem(llvm::IRBuilder<> &builder,
                                 const IndexedSequenceLoweredIterState &iter)
    {
      if (!iter.elem_slot)
      {
        return nullptr;
      }
      return builder.CreateLoad(iter.elem_slot->getAllocatedType(), iter.elem_slot);
    }

    llvm::Value *EmitIndexedSequenceElem(LLVMEmitter &emitter,
                                         llvm::IRBuilder<> &builder,
                                         const IndexedSequenceIterState &iter,
                                         llvm::Value *index)
    {
      if (!iter.element_type || !index || !index->getType()->isIntegerTy())
      {
        return nullptr;
      }

      llvm::Type *elem_ll = emitter.GetLLVMType(iter.element_type);
      if (!elem_ll)
      {
        return nullptr;
      }

      llvm::Value *i64_index = index;
      llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
      if (i64_index->getType()->getIntegerBitWidth() != 64)
      {
        i64_index = builder.CreateIntCast(i64_index, i64_ty, false);
      }

      if (iter.array_ptr && iter.array_type)
      {
        llvm::Value *zero = llvm::ConstantInt::get(i64_ty, 0);
        llvm::Value *elem_ptr = builder.CreateGEP(
            iter.array_type, iter.array_ptr, {zero, i64_index});
        return builder.CreateLoad(elem_ll, elem_ptr);
      }

      if (iter.data_ptr && iter.data_ptr->getType()->isPointerTy())
      {
        llvm::Value *typed_data_ptr = builder.CreateBitCast(
            iter.data_ptr, llvm::PointerType::get(elem_ll, 0));
        llvm::Value *elem_ptr =
            builder.CreateGEP(elem_ll, typed_data_ptr, i64_index);
        return builder.CreateLoad(elem_ll, elem_ptr);
      }

      return nullptr;
    }





    thread_local IRProcPerfContext *g_ir_proc_perf_ctx = nullptr;

    const char *IRNodePerfKindName(std::size_t index)
    {
      static constexpr std::array<const char *, kIRNodePerfKindCount> names = {
          "IROpaque",
          "IRSeq",
          "IRCall",
          "IRCallVTable",
          "IRStoreGlobal",
          "IRReadVar",
          "IRReadPath",
          "IRBindVar",
          "IRStoreVar",
          "IRStoreVarNoDrop",
          "IRReadPlace",
          "IRWritePlace",
          "IRAddrOf",
          "IRReadPtr",
          "IRWritePtr",
          "IRUnaryOp",
          "IRFence",
          "IRBinaryOp",
          "IRCast",
          "IRTransmute",
          "IRCheckIndex",
          "IRCheckRange",
          "IRCheckSliceLen",
          "IRCheckOp",
          "IRCheckCast",
          "IRAlloc",
          "IRContextBundleBuild",
          "IRReturn",
          "IRResult",
          "IRBreak",
          "IRContinue",
          "IRDefer",
          "IRMoveState",
          "IRIf",
          "IRBlock",
          "IRLoop",
          "IRIfCase",
          "IRRegion",
          "IRFrame",
          "IRBranch",
          "IRPhi",
          "IRClearPanic",
          "IRPanicCheck",
          "IRCleanupPanicCheck",
          "IRInitPanicHandle",
          "IRInitPanicRaise",
          "IRCheckPoison",
          "IRLowerPanic",
          "IRParallel",
          "IRSpawn",
          "IRWait",
          "IRCancelCheck",
          "IRCancelSuppress",
          "IRDispatch",
          "IRYield",
          "IRYieldFrom",
          "IRSync",
          "IRRaceReturn",
          "IRRaceYield",
          "IRAll",
          "IRAsyncComplete",
          "IRAsyncFail",
      };
      if (index < names.size())
      {
        return names[index];
      }
      return "IRUnknown";
    }

    long long IRProcPerfTotalSelfMs(const IRProcPerfContext &ctx)
    {
      long long total = 0;
      for (const auto &bucket : ctx.buckets)
      {
        total += bucket.total_self_ms;
      }
      return total;
    }

    void AppendTopIRNodePerf(std::string &line, const IRProcPerfContext &ctx)
    {
      struct TopEntry
      {
        std::size_t idx = 0;
        long long total_self_ms = -1;
        std::size_t count = 0;
        long long max_self_ms = 0;
      };

      std::array<TopEntry, 3> top{};
      for (std::size_t i = 0; i < top.size(); ++i)
      {
        top[i].total_self_ms = -1;
      }

      for (std::size_t i = 0; i < ctx.buckets.size(); ++i)
      {
        const auto &bucket = ctx.buckets[i];
        if (bucket.count == 0 || bucket.total_self_ms <= 0)
        {
          continue;
        }

        TopEntry candidate;
        candidate.idx = i;
        candidate.total_self_ms = bucket.total_self_ms;
        candidate.count = bucket.count;
        candidate.max_self_ms = bucket.max_self_ms;

        for (std::size_t pos = 0; pos < top.size(); ++pos)
        {
          if (candidate.total_self_ms <= top[pos].total_self_ms)
          {
            continue;
          }
          for (std::size_t shift = top.size() - 1; shift > pos; --shift)
          {
            top[shift] = top[shift - 1];
          }
          top[pos] = candidate;
          break;
        }
      }

      for (std::size_t i = 0; i < top.size(); ++i)
      {
        if (top[i].total_self_ms < 0)
        {
          continue;
        }
        line += " ir_top" + std::to_string(i + 1) + "=" +
                IRNodePerfKindName(top[i].idx) + ":" +
                std::to_string(top[i].total_self_ms) + "ms/" +
                std::to_string(top[i].count) + "x(max=" +
                std::to_string(top[i].max_self_ms) + "ms)";
      }
    }

    bool IsClosurePairLLVMType(llvm::Type *ty)
    {
      auto *struct_ty = llvm::dyn_cast_or_null<llvm::StructType>(ty);
      if (!struct_ty || struct_ty->getNumElements() != 2)
      {
        return false;
      }
      return struct_ty->getElementType(0)->isPointerTy() &&
             struct_ty->getElementType(1)->isPointerTy();
    }

    llvm::Function *FunctionFromLLVMValue(llvm::Value *value)
    {
      llvm::Value *current = value;
      while (current)
      {
        if (auto *fn = llvm::dyn_cast<llvm::Function>(current))
        {
          return fn;
        }
        if (auto *ce = llvm::dyn_cast<llvm::ConstantExpr>(current))
        {
          if (ce->isCast() && ce->getNumOperands() >= 1)
          {
            current = ce->getOperand(0);
            continue;
          }
        }
        if (auto *cast = llvm::dyn_cast<llvm::CastInst>(current))
        {
          current = cast->getOperand(0);
          continue;
        }
        break;
      }
      return nullptr;
    }

    std::uint64_t AlignUpBytes(std::uint64_t value, std::uint64_t align)
    {
      if (align == 0)
      {
        return value;
      }
      const std::uint64_t rem = value % align;
      return rem == 0 ? value : (value + (align - rem));
    }

    std::vector<ast::ModulePath> ComputeEntryInitOrder(const LowerCtx &ctx)
    {
      if (!ctx.init_order.empty())
      {
        return ctx.init_order;
      }

      if (!ctx.init_modules.empty())
      {
        const std::size_t n = ctx.init_modules.size();
        if (ctx.init_eager_edges.empty())
        {
          return ctx.init_modules;
        }

        std::vector<std::vector<std::size_t>> outgoing(n);
        std::vector<std::size_t> indeg(n, 0);
        for (const auto &edge : ctx.init_eager_edges)
        {
          if (edge.first >= n || edge.second >= n)
          {
            continue;
          }
          outgoing[edge.first].push_back(edge.second);
          indeg[edge.second] += 1;
        }

        std::set<std::size_t> ready;
        for (std::size_t i = 0; i < n; ++i)
        {
          if (indeg[i] == 0)
          {
            ready.insert(i);
          }
        }

        std::vector<ast::ModulePath> topo;
        topo.reserve(n);
        while (!ready.empty())
        {
          const std::size_t cur = *ready.begin();
          ready.erase(ready.begin());
          topo.push_back(ctx.init_modules[cur]);
          for (const auto succ : outgoing[cur])
          {
            if (indeg[succ] == 0)
            {
              continue;
            }
            indeg[succ] -= 1;
            if (indeg[succ] == 0)
            {
              ready.insert(succ);
            }
          }
        }

        if (topo.size() == n)
        {
          return topo;
        }
        return ctx.init_modules;
      }

      if (ctx.sigma)
      {
        return ComputeInitOrderFromSigma(*ctx.sigma);
      }

      return {};
    }

    llvm::Value *CreateTaggedPayloadI8Ptr(LLVMEmitter &emitter,
                                          llvm::IRBuilder<> *builder,
                                          llvm::StructType *tagged_ty,
                                          llvm::Value *tagged_slot,
                                          std::uint64_t payload_align)
    {
      if (!builder || !tagged_ty || !tagged_slot)
      {
        return nullptr;
      }

      const llvm::DataLayout &dl = emitter.GetModule().getDataLayout();
      llvm::Type *disc_ty = tagged_ty->getElementType(0);
      const std::uint64_t disc_size =
          static_cast<std::uint64_t>(dl.getTypeAllocSize(disc_ty));
      const std::uint64_t payload_off =
          AlignUpBytes(disc_size, std::max<std::uint64_t>(1, payload_align));

      llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
      llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
      llvm::Value *base_i8 = builder->CreateBitCast(
          tagged_slot, llvm::PointerType::get(i8_ty, 0));
      return builder->CreateGEP(
          i8_ty,
          base_i8,
          llvm::ConstantInt::get(i64_ty, payload_off));
    }

    bool IsUnitTypeRef(const analysis::TypeRef &type)
    {
      if (!type)
      {
        return false;
      }
      if (const auto *prim = std::get_if<analysis::TypePrim>(&type->node))
      {
        return prim->name == "()";
      }
      if (const auto *tuple = std::get_if<analysis::TypeTuple>(&type->node))
      {
        return tuple->elements.empty();
      }
      return false;
    }

    bool IsNeverTypeRef(const analysis::TypeRef &type)
    {
      if (!type)
      {
        return false;
      }
      if (const auto *prim = std::get_if<analysis::TypePrim>(&type->node))
      {
        return prim->name == "!";
      }
      return false;
    }

    bool IsRuntimeHandleModalPath(const analysis::TypePath &path)
    {
      return analysis::IsBuiltinRuntimeHandleModalTypePath(path);
    }

    std::uint64_t AsyncStateIndexOrDefault(const analysis::ScopeContext &scope,
                                           std::string_view state_name,
                                           std::uint64_t fallback)
    {
      const ast::ModalDecl *async_decl = analysis::LookupModalDecl(scope, {"Async"});
      if (!async_decl)
      {
        return fallback;
      }
      for (std::size_t i = 0; i < async_decl->states.size(); ++i)
      {
        if (analysis::IdEq(async_decl->states[i].name, std::string(state_name)))
        {
          return static_cast<std::uint64_t>(i);
        }
      }
      return fallback;
    }


    AsyncStateDiscs LoweredAsyncStateDiscs(
        const analysis::ScopeContext &scope,
        const std::optional<::ultraviolet::analysis::layout::LoweredAsyncType> &lowered)
    {
      AsyncStateDiscs discs;
      if (lowered.has_value())
      {
        for (std::size_t i = 0; i < lowered->states.size(); ++i)
        {
          if (lowered->states[i] == "Suspended")
          {
            discs.suspended = static_cast<std::uint64_t>(i);
          }
          else if (lowered->states[i] == "Completed")
          {
            discs.completed = static_cast<std::uint64_t>(i);
          }
          else if (lowered->states[i] == "Failed")
          {
            discs.failed = static_cast<std::uint64_t>(i);
          }
        }
        return discs;
      }

      discs.suspended = AsyncStateIndexOrDefault(scope, "Suspended", 0);
      discs.completed = AsyncStateIndexOrDefault(scope, "Completed", 1);
      discs.failed = AsyncStateIndexOrDefault(scope, "Failed", 2);
      return discs;
    }

    AsyncStateDiscs LoweredAsyncStateDiscs(
        const analysis::ScopeContext &scope,
        const analysis::TypeRef &async_type)
    {
      if (const auto sig = analysis::AsyncSigOf(scope, async_type))
      {
        return LoweredAsyncStateDiscs(scope, *sig);
      }
      return LoweredAsyncStateDiscs(scope, ::ultraviolet::analysis::layout::LowerAsyncType(async_type));
    }

    AsyncStateDiscs LoweredAsyncStateDiscs(
        const analysis::ScopeContext &scope,
        const analysis::AsyncSig &sig)
    {
      return LoweredAsyncStateDiscs(scope, ::ultraviolet::analysis::layout::LowerAsyncType(sig));
    }


    llvm::Value *AsyncFrameAddr(LLVMEmitter &emitter,
                                llvm::IRBuilder<> *builder,
                                llvm::Value *frame_ptr,
                                std::uint64_t offset)
    {
      if (!builder || !frame_ptr)
      {
        return nullptr;
      }
      llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
      llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
      llvm::Value *base = frame_ptr;
      if (base->getType() != llvm::PointerType::get(i8_ty, 0))
      {
        base = builder->CreateBitCast(base, llvm::PointerType::get(i8_ty, 0));
      }
      return builder->CreateGEP(
          i8_ty,
          base,
          llvm::ConstantInt::get(i64_ty, offset));
    }

    llvm::Value *AsyncFrameTypedPtr(LLVMEmitter &emitter,
                                    llvm::IRBuilder<> *builder,
                                    llvm::Value *frame_ptr,
                                    std::uint64_t offset,
                                    llvm::Type *pointee)
    {
      if (!builder || !frame_ptr || !pointee)
      {
        return nullptr;
      }
      llvm::Value *addr = AsyncFrameAddr(emitter, builder, frame_ptr, offset);
      if (!addr)
      {
        return nullptr;
      }
      return builder->CreateBitCast(addr, llvm::PointerType::get(pointee, 0));
    }

    llvm::Value *NullOpaquePtr(LLVMEmitter &emitter)
    {
      return llvm::ConstantPointerNull::get(
          llvm::cast<llvm::PointerType>(emitter.GetOpaquePtr()));
    }

    llvm::Value *CoerceTo(llvm::IRBuilder<> *builder,
                          llvm::Value *value,
                          llvm::Type *target_ty);

    llvm::Value *CoerceToTyped(LLVMEmitter &emitter,
                               llvm::IRBuilder<> *builder,
                               llvm::Value *value,
                               llvm::Type *target_ty,
                               const analysis::TypeRef &source_type,
                               const analysis::TypeRef &target_type);

    llvm::Value *CoerceOrNullOpaquePtr(LLVMEmitter &emitter,
                                       llvm::IRBuilder<> *builder,
                                       llvm::Value *value)
    {
      if (!builder)
      {
        return NullOpaquePtr(emitter);
      }
      if (!value)
      {
        return NullOpaquePtr(emitter);
      }
      llvm::Value *coerced = CoerceTo(builder, value, emitter.GetOpaquePtr());
      if (coerced)
      {
        return coerced;
      }
      if (value->getType()->isPointerTy())
      {
        coerced = builder->CreateBitCast(value, emitter.GetOpaquePtr());
      }
      return coerced ? coerced : NullOpaquePtr(emitter);
    }

    llvm::Value *EmitRuntimeCallBySymbol(LLVMEmitter &emitter,
                                         llvm::IRBuilder<> *builder,
                                         const std::string &symbol,
                                         const std::vector<llvm::Value *> &args)
    {
      if (!builder)
      {
        return nullptr;
      }
      std::optional<RuntimeFuncInfo> info = GetRuntimeFuncInfo(symbol);
      if (!info.has_value())
      {
        return nullptr;
      }

      llvm::Function *fn = emitter.GetModule().getFunction(symbol);
      const bool runtime_c_aggregate_boundary = RuntimeUsesCAggregateABI(symbol);
      const bool runtime_foreign_boundary = RuntimeUsesForeignABI(symbol);
      const bool use_c_abi_aggregate_sret = runtime_c_aggregate_boundary;
      if (!fn)
      {
        ABICallResult abi = ComputeCallABI(
            emitter,
            info->params,
            info->ret,
            use_c_abi_aggregate_sret,
            /*foreign_boundary_mode_independent=*/runtime_foreign_boundary,
            RuntimeUsesExplicitOutResultABI(symbol));
        if (abi.func_type)
        {
          fn = llvm::Function::Create(
              abi.func_type,
              llvm::GlobalValue::ExternalLinkage,
              symbol,
              &emitter.GetModule());
          fn->setCallingConv(llvm::CallingConv::C);
        }
      }
      if (!fn)
      {
        return nullptr;
      }

      return EmitABICall(
          emitter,
          builder,
          fn,
          info->params,
          info->ret,
          args,
          use_c_abi_aggregate_sret,
          /*ffi_import_boundary=*/false,
          /*ffi_import_catch=*/false,
          std::nullopt,
          nullptr,
          nullptr,
          nullptr,
          runtime_foreign_boundary);
    }

    llvm::Value *EmitAsyncResumeRuntimeCall(LLVMEmitter &emitter,
                                            llvm::IRBuilder<> *builder,
                                            llvm::Value *suspended,
                                            llvm::Value *input,
                                            llvm::Value *panic_out)
    {
      if (!builder)
      {
        return nullptr;
      }
      return EmitRuntimeCallBySymbol(
          emitter,
          builder,
          BuiltinSymAsyncResume(),
          {
              CoerceOrNullOpaquePtr(emitter, builder, suspended),
              CoerceOrNullOpaquePtr(emitter, builder, input),
              CoerceOrNullOpaquePtr(emitter, builder, panic_out),
          });
    }

    void StoreAsyncFrameKeySnapshot(LLVMEmitter &emitter,
                                    llvm::IRBuilder<> *builder,
                                    llvm::Value *frame_ptr,
                                    llvm::Value *released_handle)
    {
      if (!builder || !frame_ptr)
      {
        return;
      }
      llvm::Value *slot_ptr = AsyncFrameTypedPtr(
          emitter,
          builder,
          frame_ptr,
          kAsyncFrameKeySnapshotOffset,
          emitter.GetOpaquePtr());
      if (!slot_ptr)
      {
        return;
      }
      llvm::Value *stored = CoerceOrNullOpaquePtr(emitter, builder, released_handle);
      builder->CreateStore(stored, slot_ptr);
    }

    void StoreAsyncFrameHostedEnv(LLVMEmitter &emitter,
                                  llvm::IRBuilder<> *builder,
                                  llvm::Value *frame_ptr,
                                  llvm::Value *hosted_env)
    {
      if (!builder || !frame_ptr)
      {
        return;
      }
      llvm::Value *slot_ptr = AsyncFrameTypedPtr(
          emitter,
          builder,
          frame_ptr,
          kAsyncFrameHostedEnvOffset,
          emitter.GetOpaquePtr());
      if (!slot_ptr)
      {
        return;
      }
      llvm::Value *stored = CoerceOrNullOpaquePtr(emitter, builder, hosted_env);
      builder->CreateStore(stored, slot_ptr);
    }

    llvm::Value *LoadAsyncFrameKeySnapshot(LLVMEmitter &emitter,
                                           llvm::IRBuilder<> *builder,
                                           llvm::Value *frame_ptr)
    {
      if (!builder || !frame_ptr)
      {
        return NullOpaquePtr(emitter);
      }
      llvm::Value *slot_ptr = AsyncFrameTypedPtr(
          emitter,
          builder,
          frame_ptr,
          kAsyncFrameKeySnapshotOffset,
          emitter.GetOpaquePtr());
      if (!slot_ptr)
      {
        return NullOpaquePtr(emitter);
      }
      return builder->CreateLoad(emitter.GetOpaquePtr(), slot_ptr);
    }

    llvm::Value *EmitKeyReleaseAll(LLVMEmitter &emitter,
                                   llvm::IRBuilder<> *builder)
    {
      llvm::Value *released = EmitRuntimeCallBySymbol(
          emitter,
          builder,
          ConcurrencySymKeyReleaseAll(),
          {});
      return CoerceOrNullOpaquePtr(emitter, builder, released);
    }

    void EmitKeyReacquire(LLVMEmitter &emitter,
                          llvm::IRBuilder<> *builder,
                          llvm::Value *released_handle)
    {
      if (!builder)
      {
        return;
      }
      std::vector<llvm::Value *> args;
      args.push_back(CoerceOrNullOpaquePtr(emitter, builder, released_handle));
      (void)EmitRuntimeCallBySymbol(
          emitter,
          builder,
          ConcurrencySymKeyReacquire(),
          args);
    }

    llvm::Value *LoadLocalValue(LLVMEmitter &emitter,
                                llvm::IRBuilder<> *builder,
                                const std::string &name)
    {
      if (!builder)
      {
        return nullptr;
      }
      IRValue local;
      local.kind = IRValue::Kind::Local;
      local.name = name;
      return emitter.EvaluateIRValue(local);
    }

    bool TryEmitBitcopyAggregateStorageCopy(LLVMEmitter &emitter,
                                            llvm::IRBuilder<> *builder,
                                            llvm::Value *dst_storage,
                                            llvm::Value *src_storage,
                                            const analysis::TypeRef &target_type,
                                            const analysis::TypeRef &source_type)
    {
      if (!builder || !dst_storage || !src_storage || !target_type || !source_type)
      {
        return false;
      }
      if (!dst_storage->getType()->isPointerTy() ||
          !src_storage->getType()->isPointerTy())
      {
        return false;
      }

      const LowerCtx *ctx = emitter.GetCurrentCtx();
      if (!ctx)
      {
        return false;
      }

      analysis::TypeRef resolved_target = ResolveAliasType(ctx, target_type);
      analysis::TypeRef resolved_source = ResolveAliasType(ctx, source_type);
      if (!resolved_target || !resolved_source)
      {
        return false;
      }

      const auto equiv = analysis::TypeEquiv(resolved_source, resolved_target);
      if (!equiv.ok || !equiv.equiv)
      {
        return false;
      }

      const analysis::ScopeContext &scope = BuildScope(ctx);
      if (!analysis::BitcopyType(scope, resolved_target))
      {
        return false;
      }

      const auto size =
          ::ultraviolet::analysis::layout::SizeOf(scope, resolved_target);
      const auto align =
          ::ultraviolet::analysis::layout::AlignOf(scope, resolved_target);
      if (!size.has_value() || !align.has_value())
      {
        return false;
      }
      if (*size == 0)
      {
        return true;
      }
      if (*size <= kByValMax)
      {
        return false;
      }

      llvm::Type *llvm_ty = emitter.GetLLVMType(resolved_target);
      if (!llvm_ty || llvm_ty->isVoidTy())
      {
        return false;
      }

      llvm::Type *ptr_ty = llvm::PointerType::get(llvm_ty, 0);
      if (dst_storage->getType() != ptr_ty)
      {
        dst_storage = builder->CreateBitCast(dst_storage, ptr_ty);
      }
      if (src_storage->getType() != ptr_ty)
      {
        src_storage = builder->CreateBitCast(src_storage, ptr_ty);
      }
      if (dst_storage->stripPointerCasts() == src_storage->stripPointerCasts())
      {
        return true;
      }

      llvm::Value *size_value =
          llvm::ConstantInt::get(llvm::Type::getInt64Ty(emitter.GetContext()),
                                 static_cast<std::uint64_t>(*size));
      const std::uint64_t copy_align = std::max<std::uint64_t>(1, *align);
      EmitAggMemcpy(emitter, dst_storage, src_storage, size_value, copy_align);
      return true;
    }

    bool TryEmitAggregateStorageTransfer(LLVMEmitter &emitter,
                                         llvm::IRBuilder<> *builder,
                                         llvm::Value *dst_storage,
                                         llvm::Value *src_storage,
                                         const analysis::TypeRef &target_type,
                                         const analysis::TypeRef &source_type)
    {
      if (!builder || !dst_storage || !src_storage || !target_type || !source_type)
      {
        return false;
      }
      if (!dst_storage->getType()->isPointerTy() ||
          !src_storage->getType()->isPointerTy())
      {
        return false;
      }

      const LowerCtx *ctx = emitter.GetCurrentCtx();
      if (!ctx)
      {
        return false;
      }

      analysis::TypeRef resolved_target = ResolveAliasType(ctx, target_type);
      analysis::TypeRef resolved_source = ResolveAliasType(ctx, source_type);
      if (!resolved_target || !resolved_source)
      {
        return false;
      }

      const auto equiv = analysis::TypeEquiv(resolved_source, resolved_target);
      if (!equiv.ok || !equiv.equiv)
      {
        return false;
      }

      llvm::Type *llvm_ty = emitter.GetLLVMType(resolved_target);
      if (!llvm_ty || llvm_ty->isVoidTy() ||
          !(llvm_ty->isStructTy() || llvm_ty->isArrayTy()))
      {
        return false;
      }

      const analysis::ScopeContext &scope = BuildScope(ctx);
      const auto size =
          ::ultraviolet::analysis::layout::SizeOf(scope, resolved_target);
      const auto align =
          ::ultraviolet::analysis::layout::AlignOf(scope, resolved_target);
      if (!size.has_value() || !align.has_value())
      {
        return false;
      }
      if (*size == 0)
      {
        return true;
      }

      llvm::Type *ptr_ty = llvm::PointerType::get(llvm_ty, 0);
      if (dst_storage->getType() != ptr_ty)
      {
        dst_storage = builder->CreateBitCast(dst_storage, ptr_ty);
      }
      if (src_storage->getType() != ptr_ty)
      {
        src_storage = builder->CreateBitCast(src_storage, ptr_ty);
      }
      if (dst_storage->stripPointerCasts() == src_storage->stripPointerCasts())
      {
        return true;
      }

      llvm::Value *size_value =
          llvm::ConstantInt::get(llvm::Type::getInt64Ty(emitter.GetContext()),
                                 static_cast<std::uint64_t>(*size));
      const std::uint64_t transfer_align = std::max<std::uint64_t>(1, *align);
      EmitAggMemcpy(emitter, dst_storage, src_storage, size_value, transfer_align);
      return true;
    }

namespace {

    llvm::Value *TypedStoragePointer(LLVMEmitter &emitter,
                                     llvm::IRBuilder<> *builder,
                                     llvm::Value *storage,
                                     llvm::Type *pointee)
    {
      if (!builder || !storage || !pointee || !storage->getType()->isPointerTy())
      {
        return nullptr;
      }
      llvm::Type *ptr_ty = llvm::PointerType::get(pointee, 0);
      if (storage->getType() == ptr_ty)
      {
        return storage;
      }
      (void)emitter;
      return builder->CreateBitCast(storage, ptr_ty);
    }

    analysis::TypeRef LookupStorageValueType(LLVMEmitter &emitter,
                                             const IRValue &value)
    {
      if (const LowerCtx *ctx = emitter.GetCurrentCtx())
      {
        if (analysis::TypeRef type = ctx->LookupValueType(value))
        {
          return type;
        }
      }
      if (value.kind == IRValue::Kind::Local)
      {
        return emitter.LookupLocalType(value.name);
      }
      return nullptr;
    }

    llvm::Value *ByteOffsetPointer(LLVMEmitter &emitter,
                                   llvm::IRBuilder<> *builder,
                                   llvm::Value *storage,
                                   llvm::Type *pointee,
                                   std::uint64_t offset)
    {
      if (!builder || !storage || !pointee || !storage->getType()->isPointerTy())
      {
        return nullptr;
      }
      llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
      llvm::Type *i8_ptr_ty = llvm::PointerType::get(i8_ty, 0);
      llvm::Value *base_i8 = storage;
      if (base_i8->getType() != i8_ptr_ty)
      {
        base_i8 = builder->CreateBitCast(base_i8, i8_ptr_ty);
      }
      llvm::Value *field_i8 = base_i8;
      if (offset != 0)
      {
        field_i8 = builder->CreateGEP(
            i8_ty,
            base_i8,
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(emitter.GetContext()),
                                   offset));
      }
      return builder->CreateBitCast(field_i8, llvm::PointerType::get(pointee, 0));
    }

    void EmitStorageMemZero(LLVMEmitter &emitter,
                            llvm::IRBuilder<> *builder,
                            llvm::Value *storage,
                            const analysis::ScopeContext &scope,
                            const analysis::TypeRef &type)
    {
      if (!builder || !storage || !type || !storage->getType()->isPointerTy())
      {
        return;
      }
      const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, type);
      const auto align = ::ultraviolet::analysis::layout::AlignOf(scope, type);
      if (!size.has_value() || *size == 0)
      {
        return;
      }
      llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
      llvm::Value *dst_i8 = storage;
      llvm::Type *i8_ptr_ty = llvm::PointerType::get(i8_ty, 0);
      if (dst_i8->getType() != i8_ptr_ty)
      {
        dst_i8 = builder->CreateBitCast(dst_i8, i8_ptr_ty);
      }
      builder->CreateMemSet(
          dst_i8,
          llvm::ConstantInt::get(i8_ty, 0),
          llvm::ConstantInt::get(llvm::Type::getInt64Ty(emitter.GetContext()),
                                 static_cast<std::uint64_t>(*size)),
          llvm::MaybeAlign(std::max<std::uint64_t>(1, align.value_or(1))));
    }

    bool StoreIRValueToStorage(LLVMEmitter &emitter,
                               llvm::IRBuilder<> *builder,
                               llvm::Value *dst_storage,
                               const IRValue &value,
                               const analysis::TypeRef &target_type,
                               llvm::Type *target_ll,
                               std::uint64_t align)
    {
      if (!builder || !dst_storage || !target_type)
      {
        return false;
      }
      if (!target_ll)
      {
        target_ll = emitter.GetLLVMType(target_type);
      }
      if (!target_ll || target_ll->isVoidTy())
      {
        return false;
      }

      analysis::TypeRef source_type = LookupStorageValueType(emitter, value);
      if (llvm::Value *source_storage = emitter.GetAddressableStorage(value))
      {
        if (source_storage->stripPointerCasts() ==
            dst_storage->stripPointerCasts())
        {
          return true;
        }
        if (TryEmitBitcopyAggregateStorageCopy(
                emitter,
                builder,
                dst_storage,
                source_storage,
                target_type,
                source_type))
        {
          return true;
        }
      }
      else if ((target_ll->isStructTy() || target_ll->isArrayTy()) &&
               TryEmitDerivedAggregateToStorage(
                   emitter, builder, dst_storage, value, target_type))
      {
        return true;
      }

      llvm::Value *stored = emitter.EvaluateIRValue(value);
      stored = CoerceToTyped(
          emitter, builder, stored, target_ll, source_type, target_type);
      if (!stored)
      {
        stored = llvm::Constant::getNullValue(target_ll);
      }
      llvm::Value *typed_dst =
          TypedStoragePointer(emitter, builder, dst_storage, target_ll);
      if (!typed_dst)
      {
        return false;
      }
      llvm::StoreInst *store = builder->CreateStore(stored, typed_dst);
      if (align != 0)
      {
        store->setAlignment(llvm::Align(std::max<std::uint64_t>(1, align)));
      }
      return true;
    }

    bool TryEmitRecordLiteralToStorage(LLVMEmitter &emitter,
                                       llvm::IRBuilder<> *builder,
                                       llvm::Value *dst_storage,
                                       const DerivedValueInfo &derived,
                                       const analysis::TypeRef &record_type,
                                       const analysis::ScopeContext &scope)
    {
      llvm::Type *record_ll = emitter.GetLLVMType(record_type);
      if (!llvm::dyn_cast_or_null<llvm::StructType>(record_ll))
      {
        return false;
      }

      struct FieldStore {
        IRValue value;
        analysis::TypeRef type;
        llvm::Type *llvm_type = nullptr;
        std::uint64_t offset = 0;
        std::uint64_t align = 1;
      };

      std::vector<FieldStore> stores;
      stores.reserve(derived.fields.size());
      for (const auto &[field_name, field_value] : derived.fields)
      {
        auto meta = ResolveFieldAccessMeta(scope, record_type, field_name);
        if (!meta.has_value() || !meta->field_type ||
            meta->index >= meta->aggregate_fields.size())
        {
          return false;
        }
        const auto layout = ComputeLayoutLLVMRecord(
            emitter,
            scope,
            meta->aggregate_type,
            meta->aggregate_fields,
            meta->layout_options);
        if (!layout.has_value() || meta->index >= layout->fields.size())
        {
          return false;
        }
        const auto &field = layout->fields[meta->index];
        if (field.recursive_indirect)
        {
          return false;
        }
        FieldStore store;
        store.value = field_value;
        store.type = meta->field_type;
        store.llvm_type = field.llvm_type ? field.llvm_type
                                          : emitter.GetLLVMType(meta->field_type);
        store.offset = field.offset;
        store.align = std::max<std::uint64_t>(1, field.align);
        if (!store.llvm_type || store.llvm_type->isVoidTy())
        {
          return false;
        }
        stores.push_back(std::move(store));
      }

      EmitStorageMemZero(emitter, builder, dst_storage, scope, record_type);
      for (const auto &store : stores)
      {
        llvm::Value *field_ptr = ByteOffsetPointer(
            emitter, builder, dst_storage, store.llvm_type, store.offset);
        if (!field_ptr)
        {
          return false;
        }
        (void)StoreIRValueToStorage(emitter,
                                    builder,
                                    field_ptr,
                                    store.value,
                                    store.type,
                                    store.llvm_type,
                                    store.align);
      }
      return true;
    }

    std::optional<std::uint64_t> EvaluateConstantCount(LLVMEmitter &emitter,
                                                       const IRValue &count_value)
    {
      llvm::Value *count = emitter.EvaluateIRValue(count_value);
      auto *constant = llvm::dyn_cast_or_null<llvm::ConstantInt>(count);
      if (!constant)
      {
        return std::nullopt;
      }
      return constant->getZExtValue();
    }

    bool StoreRepeatedArrayElement(LLVMEmitter &emitter,
                                   llvm::IRBuilder<> *builder,
                                   llvm::ArrayType *array_ll,
                                   llvm::Value *array_ptr,
                                   const IRValue &value,
                                   const analysis::TypeRef &element_type,
                                   llvm::Type *element_ll,
                                   std::uint64_t elem_align,
                                   std::uint64_t first_index,
                                   std::uint64_t count)
    {
      if (!builder || !array_ll || !array_ptr || !element_type || !element_ll)
      {
        return false;
      }
      if (count == 0)
      {
        return true;
      }

      const bool aggregate_element =
          element_ll->isStructTy() || element_ll->isArrayTy();
      llvm::Value *repeat_storage = nullptr;
      llvm::Value *repeat_value = nullptr;

      if (aggregate_element)
      {
        llvm::Function *func =
            builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent()
                                      : nullptr;
        if (!func)
        {
          return false;
        }
        llvm::IRBuilder<> entry_builder(&func->getEntryBlock(),
                                        func->getEntryBlock().begin());
        repeat_storage = entry_builder.CreateAlloca(
            element_ll, nullptr, "array.repeat.value");
        if (!StoreIRValueToStorage(emitter,
                                   builder,
                                   repeat_storage,
                                   value,
                                   element_type,
                                   element_ll,
                                   elem_align))
        {
          return false;
        }
      }
      else
      {
        analysis::TypeRef source_type = LookupStorageValueType(emitter, value);
        repeat_value = emitter.EvaluateIRValue(value);
        repeat_value = CoerceToTyped(
            emitter, builder, repeat_value, element_ll, source_type, element_type);
        if (!repeat_value)
        {
          repeat_value = llvm::Constant::getNullValue(element_ll);
        }
      }

      auto store_one = [&](llvm::Value *elem_ptr) {
        if (repeat_storage)
        {
          if (!TryEmitBitcopyAggregateStorageCopy(emitter,
                                                  builder,
                                                  elem_ptr,
                                                  repeat_storage,
                                                  element_type,
                                                  element_type))
          {
            llvm::Value *loaded = builder->CreateLoad(element_ll, repeat_storage);
            llvm::StoreInst *store = builder->CreateStore(loaded, elem_ptr);
            store->setAlignment(
                llvm::Align(std::max<std::uint64_t>(1, elem_align)));
          }
          return;
        }
        llvm::StoreInst *store = builder->CreateStore(repeat_value, elem_ptr);
        store->setAlignment(llvm::Align(std::max<std::uint64_t>(1, elem_align)));
      };

      constexpr std::uint64_t kUnrolledRepeatLimit = 32;
      if (count > kUnrolledRepeatLimit)
      {
        llvm::Function *func =
            builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent()
                                      : nullptr;
        if (!func)
        {
          return false;
        }
        llvm::LLVMContext &context = emitter.GetContext();
        llvm::BasicBlock *preheader = builder->GetInsertBlock();
        llvm::BasicBlock *loop_bb =
            llvm::BasicBlock::Create(context, "array.repeat.fill", func);
        llvm::BasicBlock *done_bb =
            llvm::BasicBlock::Create(context, "array.repeat.done", func);
        llvm::Type *i64_ty = llvm::Type::getInt64Ty(context);
        llvm::Value *start =
            llvm::ConstantInt::get(i64_ty, static_cast<std::uint64_t>(first_index));
        llvm::Value *limit = llvm::ConstantInt::get(
            i64_ty, static_cast<std::uint64_t>(first_index + count));
        builder->CreateBr(loop_bb);
        builder->SetInsertPoint(loop_bb);
        llvm::PHINode *idx = builder->CreatePHI(i64_ty, 2, "array.repeat.index");
        idx->addIncoming(start, preheader);
        llvm::Value *zero = llvm::ConstantInt::get(i64_ty, 0);
        llvm::Value *elem_ptr =
            builder->CreateInBoundsGEP(array_ll, array_ptr, {zero, idx});
        store_one(elem_ptr);
        llvm::Value *next = builder->CreateAdd(
            idx, llvm::ConstantInt::get(i64_ty, 1));
        llvm::Value *finished = builder->CreateICmpEQ(next, limit);
        idx->addIncoming(next, loop_bb);
        builder->CreateCondBr(finished, done_bb, loop_bb);
        builder->SetInsertPoint(done_bb);
        return true;
      }

      for (std::uint64_t offset = 0; offset < count; ++offset)
      {
        llvm::Value *elem_ptr = builder->CreateConstInBoundsGEP2_64(
            array_ll, array_ptr, 0, first_index + offset);
        store_one(elem_ptr);
      }
      return true;
    }

    bool TryEmitArrayAggregateToStorage(LLVMEmitter &emitter,
                                        llvm::IRBuilder<> *builder,
                                        llvm::Value *dst_storage,
                                        const DerivedValueInfo &derived,
                                        const analysis::TypeRef &array_type,
                                        const analysis::ScopeContext &scope)
    {
      const auto *array = std::get_if<analysis::TypeArray>(&array_type->node);
      if (!array || !array->element)
      {
        return false;
      }
      llvm::Type *array_ll_ty = emitter.GetLLVMType(array_type);
      auto *array_ll = llvm::dyn_cast_or_null<llvm::ArrayType>(array_ll_ty);
      llvm::Type *element_ll = emitter.GetLLVMType(array->element);
      if (!array_ll || !element_ll || array_ll->getElementType() != element_ll)
      {
        return false;
      }

      struct SegmentWrite {
        IRValue value;
        std::uint64_t count = 1;
        bool repeat = false;
      };
      std::vector<SegmentWrite> writes;
      std::uint64_t total_count = 0;

      if (derived.kind == DerivedValueInfo::Kind::ArrayLit)
      {
        writes.reserve(derived.elements.size());
        for (const IRValue &element : derived.elements)
        {
          writes.push_back(SegmentWrite{element, 1, false});
          ++total_count;
        }
      }
      else if (derived.kind == DerivedValueInfo::Kind::ArrayRepeat)
      {
        auto count = EvaluateConstantCount(emitter, derived.repeat_count);
        if (!count.has_value())
        {
          return false;
        }
        writes.push_back(SegmentWrite{derived.repeat_value, *count, true});
        total_count += *count;
      }
      else if (derived.kind == DerivedValueInfo::Kind::ArraySegments)
      {
        writes.reserve(derived.array_segments.size());
        for (const DerivedArraySegment &segment : derived.array_segments)
        {
          if (segment.kind == DerivedArraySegment::Kind::Element)
          {
            writes.push_back(SegmentWrite{segment.value, 1, false});
            ++total_count;
            continue;
          }
          if (!segment.count.has_value())
          {
            return false;
          }
          auto count = EvaluateConstantCount(emitter, *segment.count);
          if (!count.has_value())
          {
            return false;
          }
          writes.push_back(SegmentWrite{segment.value, *count, true});
          total_count += *count;
        }
      }
      else
      {
        return false;
      }

      if (total_count > array->length || array_ll->getNumElements() != array->length)
      {
        return false;
      }
      if (total_count < array->length)
      {
        EmitStorageMemZero(emitter, builder, dst_storage, scope, array_type);
      }

      llvm::Value *array_ptr =
          TypedStoragePointer(emitter, builder, dst_storage, array_ll);
      if (!array_ptr)
      {
        return false;
      }
      const std::uint64_t elem_align = std::max<std::uint64_t>(
          1,
          ::ultraviolet::analysis::layout::AlignOf(scope, array->element).value_or(1));

      std::uint64_t index = 0;
      for (const SegmentWrite &write : writes)
      {
        if (write.repeat)
        {
          if (!StoreRepeatedArrayElement(emitter,
                                         builder,
                                         array_ll,
                                         array_ptr,
                                         write.value,
                                         array->element,
                                         element_ll,
                                         elem_align,
                                         index,
                                         write.count))
          {
            return false;
          }
          index += write.count;
          continue;
        }
        llvm::Value *elem_ptr =
            builder->CreateConstInBoundsGEP2_64(array_ll, array_ptr, 0, index);
        (void)StoreIRValueToStorage(emitter,
                                    builder,
                                    elem_ptr,
                                    write.value,
                                    array->element,
                                    element_ll,
                                    elem_align);
        ++index;
      }
      return true;
    }

} // namespace

    bool TryEmitDerivedAggregateToStorage(LLVMEmitter &emitter,
                                          llvm::IRBuilder<> *builder,
                                          llvm::Value *dst_storage,
                                          const IRValue &value,
                                          const analysis::TypeRef &target_type)
    {
      if (!builder || !dst_storage || !target_type ||
          !dst_storage->getType()->isPointerTy())
      {
        return false;
      }
      const LowerCtx *ctx = emitter.GetCurrentCtx();
      if (!ctx)
      {
        return false;
      }
      const DerivedValueInfo *derived = ctx->LookupDerivedValue(value);
      if (!derived)
      {
        return false;
      }
      analysis::TypeRef resolved_target = ResolveAliasType(ctx, target_type);
      if (!resolved_target)
      {
        return false;
      }
      const analysis::ScopeContext &scope = BuildScope(ctx);
      if (std::holds_alternative<analysis::TypeUnion>(resolved_target->node))
      {
        // Union storage must be materialized through the union packer so the
        // discriminant follows canonical member order.
        return false;
      }
      switch (derived->kind)
      {
      case DerivedValueInfo::Kind::RecordLit:
        return TryEmitRecordLiteralToStorage(
            emitter, builder, dst_storage, *derived, resolved_target, scope);
      case DerivedValueInfo::Kind::ArrayLit:
      case DerivedValueInfo::Kind::ArrayRepeat:
      case DerivedValueInfo::Kind::ArraySegments:
        return TryEmitArrayAggregateToStorage(
            emitter, builder, dst_storage, *derived, resolved_target, scope);
      default:
        return false;
      }
    }

    bool StoreProcedureOutValue(LLVMEmitter &emitter,
                                llvm::IRBuilder<> *builder,
                                llvm::Function *func,
                                const std::string &symbol,
                                const LowerCtx::ProcSigInfo *sig,
                                llvm::Value *value,
                                const analysis::TypeRef &source_type)
    {
      if (!builder || !func || !sig)
      {
        return false;
      }

      llvm::Type *out_ty = emitter.GetLLVMType(sig->ret);
      if (!out_ty || out_ty->isVoidTy())
      {
        return false;
      }

      llvm::Value *stored =
          CoerceToTyped(emitter, builder, value, out_ty, source_type, sig->ret);
      if (!stored)
      {
        stored = CoerceTo(builder, value, out_ty);
      }
      if (!stored)
      {
        stored = llvm::Constant::getNullValue(out_ty);
      }

      auto store_to_ptr = [&](llvm::Value *out_ptr) -> bool
      {
        if (!out_ptr)
        {
          return false;
        }
        llvm::Type *target_ptr_ty = llvm::PointerType::get(out_ty, 0);
        if (out_ptr->getType()->isIntegerTy())
        {
          out_ptr = builder->CreateIntToPtr(out_ptr, target_ptr_ty);
        }
        else
        {
          llvm::Value *coerced = CoerceTo(builder, out_ptr, target_ptr_ty);
          if (coerced)
          {
            out_ptr = coerced;
          }
          else if (out_ptr->getType()->isPointerTy())
          {
            out_ptr = builder->CreateBitCast(out_ptr, target_ptr_ty);
          }
        }
        if (!out_ptr || !out_ptr->getType()->isPointerTy())
        {
          return false;
        }
        builder->CreateStore(stored, out_ptr);
        return true;
      };

      if (HasNamedParam(sig->params, kAsyncOutParamName))
      {
        llvm::Value *explicit_out =
            LoadLocalValue(emitter, builder, std::string(kAsyncOutParamName));
        if (!explicit_out)
        {
          if (const LowerCtx *ctx = emitter.GetCurrentCtx())
          {
            const_cast<LowerCtx *>(ctx)->ReportCodegenFailure();
          }
          return false;
        }
        if (!store_to_ptr(explicit_out))
        {
          if (const LowerCtx *ctx = emitter.GetCurrentCtx())
          {
            const_cast<LowerCtx *>(ctx)->ReportCodegenFailure();
          }
          return false;
        }
        return true;
      }

      ABICallResult abi = ComputeProcABI(emitter, symbol, sig->params, sig->ret);
      if (!abi.valid || !abi.has_sret || func->arg_size() == 0)
      {
        return false;
      }

      return store_to_ptr(func->getArg(0));
    }

    llvm::Value *ResolveProcedureOutPtr(LLVMEmitter &emitter,
                                        llvm::IRBuilder<> *builder,
                                        llvm::Function *func,
                                        const std::string &symbol,
                                        const LowerCtx::ProcSigInfo *sig)
    {
      if (!builder || !func || !sig)
      {
        return nullptr;
      }

      llvm::Type *out_ty = emitter.GetLLVMType(sig->ret);
      if (!out_ty || out_ty->isVoidTy())
      {
        return nullptr;
      }

      auto normalize_out_ptr = [&](llvm::Value *out_ptr) -> llvm::Value *
      {
        if (!out_ptr)
        {
          return nullptr;
        }

        llvm::Type *target_ptr_ty = llvm::PointerType::get(out_ty, 0);
        if (out_ptr->getType()->isIntegerTy())
        {
          out_ptr = builder->CreateIntToPtr(out_ptr, target_ptr_ty);
        }
        else
        {
          llvm::Value *coerced = CoerceTo(builder, out_ptr, target_ptr_ty);
          if (coerced)
          {
            out_ptr = coerced;
          }
          else if (out_ptr->getType()->isPointerTy())
          {
            out_ptr = builder->CreateBitCast(out_ptr, target_ptr_ty);
          }
        }

        return (out_ptr && out_ptr->getType()->isPointerTy()) ? out_ptr : nullptr;
      };

      if (HasNamedParam(sig->params, kAsyncOutParamName))
      {
        llvm::Value *explicit_out =
            LoadLocalValue(emitter, builder, std::string(kAsyncOutParamName));
        return normalize_out_ptr(explicit_out);
      }

      ABICallResult abi = ComputeProcABI(emitter, symbol, sig->params, sig->ret);
      if (!abi.valid || !abi.has_sret || func->arg_size() == 0)
      {
        return nullptr;
      }

      return normalize_out_ptr(func->getArg(0));
    }

    std::optional<std::size_t> ParseTupleFieldIndex(std::string_view text)
    {
      if (text.empty())
      {
        return std::nullopt;
      }
      std::size_t index = 0;
      for (char ch : text)
      {
        if (ch < '0' || ch > '9')
        {
          return std::nullopt;
        }
        index = index * 10 + static_cast<std::size_t>(ch - '0');
      }
      return index;
    }


    analysis::TypeRef ResolveAliasTypeInScope(const analysis::ScopeContext &scope,
                                              const analysis::TypeRef &type,
                                              std::size_t depth)
    {
      analysis::TypeRef stripped = analysis::StripPerm(type);
      if (!stripped)
      {
        stripped = type;
      }
      if (!stripped || depth > 16)
      {
        return stripped;
      }

      const analysis::TypePath *path = analysis::AppliedTypePath(*stripped);
      const std::vector<analysis::TypeRef> *generic_args =
          analysis::AppliedTypeArgs(*stripped);
      if (!path)
      {
        return stripped;
      }

      ast::Path syntax_path;
      syntax_path.reserve(path->size());
      for (const auto &seg : *path)
      {
        syntax_path.push_back(seg);
      }
      const auto it = scope.sigma.types.find(analysis::PathKeyOf(syntax_path));
      if (it == scope.sigma.types.end())
      {
        return stripped;
      }

      const auto *alias = std::get_if<ast::TypeAliasDecl>(&it->second);
      if (!alias)
      {
        return stripped;
      }

      const auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, alias->type);
      if (!lowered.has_value())
      {
        return stripped;
      }

      analysis::TypeRef inst = *lowered;
      if (alias->generic_params &&
          !alias->generic_params->params.empty() &&
          generic_args &&
          !generic_args->empty())
      {
        analysis::TypeSubst subst =
            analysis::BuildSubstitution(alias->generic_params->params,
                                        *generic_args);
        inst = analysis::InstantiateType(inst, subst);
      }
      return ResolveAliasTypeInScope(scope, inst, depth + 1);
    }

    std::optional<FieldAccessMeta> ResolveFieldAccessMeta(
        const analysis::ScopeContext &scope,
        const analysis::TypeRef &base_type,
        std::string_view field_name)
    {
      analysis::TypeRef stripped = ResolveAliasTypeInScope(scope, base_type);
      if (!stripped)
      {
        stripped = base_type;
      }
      if (!stripped)
      {
        return std::nullopt;
      }
      if (analysis::TypeRef no_perm = analysis::StripPerm(stripped))
      {
        stripped = no_perm;
      }
      for (std::size_t depth = 0; depth < 8 && stripped; ++depth)
      {
        analysis::TypeRef pointee;
        if (const auto *raw = std::get_if<analysis::TypeRawPtr>(&stripped->node))
        {
          pointee = raw->element;
        }
        else if (const auto *ptr = std::get_if<analysis::TypePtr>(&stripped->node))
        {
          pointee = ptr->element;
        }
        else
        {
          break;
        }

        analysis::TypeRef resolved_pointee = ResolveAliasTypeInScope(scope, pointee);
        stripped = resolved_pointee ? resolved_pointee : pointee;
        if (analysis::TypeRef no_perm = analysis::StripPerm(stripped))
        {
          stripped = no_perm;
        }
      }

      if (const auto *tuple = std::get_if<analysis::TypeTuple>(&stripped->node))
      {
        std::optional<std::size_t> index = ParseTupleFieldIndex(field_name);
        if (!index.has_value())
        {
          if (tuple->elements.size() == 1)
          {
            index = 0;
          }
          else
          {
            return std::nullopt;
          }
        }
        if (*index >= tuple->elements.size())
        {
          return std::nullopt;
        }
        FieldAccessMeta meta;
        meta.aggregate_type = stripped;
        meta.index = *index;
        meta.field_type = tuple->elements[*index];
        meta.aggregate_fields = tuple->elements;
        return meta;
      }

      if (const auto *modal_state = std::get_if<analysis::TypeModalState>(&stripped->node))
      {
        const ast::ModalDecl *modal_decl = analysis::LookupModalDecl(scope, modal_state->path);
        if (!modal_decl)
        {
          return std::nullopt;
        }
        analysis::TypeSubst modal_subst;
        if (modal_decl->generic_params && !modal_decl->generic_params->params.empty())
        {
          if (modal_state->generic_args.size() > modal_decl->generic_params->params.size())
          {
            return std::nullopt;
          }
          modal_subst = analysis::BuildSubstitution(
              modal_decl->generic_params->params,
              modal_state->generic_args);
        }

        const ast::StateBlock *state_block = nullptr;
        for (const auto &state : modal_decl->states)
        {
          if (analysis::IdEq(state.name, modal_state->state))
          {
            state_block = &state;
            break;
          }
        }
        if (!state_block)
        {
          return std::nullopt;
        }

        FieldAccessMeta meta;
        meta.aggregate_type = stripped;
        bool found = false;
        std::size_t field_index = 0;
        for (const auto &member : state_block->members)
        {
          const auto *field = std::get_if<ast::StateFieldDecl>(&member);
          if (!field)
          {
            continue;
          }
          auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, field->type);
          analysis::TypeRef lowered_type = analysis::MakeTypePrim("u8");
          if (lowered.has_value())
          {
            lowered_type = *lowered;
            if (!modal_subst.empty())
            {
              lowered_type = analysis::InstantiateType(lowered_type, modal_subst);
            }
          }
          meta.aggregate_fields.push_back(lowered_type);
          if (analysis::IdEq(field->name, field_name))
          {
            meta.index = field_index;
            meta.field_type = lowered_type;
            found = true;
          }
          ++field_index;
        }
        if (!found)
        {
          return std::nullopt;
        }
        return meta;
      }

      const analysis::TypePath *path = analysis::AppliedTypePath(*stripped);
      const std::vector<analysis::TypeRef> *generic_args =
          analysis::AppliedTypeArgs(*stripped);
      if (!path)
      {
        return std::nullopt;
      }
      const ast::RecordDecl *record = analysis::LookupRecordDecl(scope, *path);
      if (!record && !path->empty())
      {
        auto suffix_matches = [&](const analysis::PathKey &key) -> bool
        {
          if (key.size() < path->size())
          {
            return false;
          }
          const std::size_t offset = key.size() - path->size();
          for (std::size_t i = 0; i < path->size(); ++i)
          {
            if (!analysis::IdEq(key[offset + i], (*path)[i]))
            {
              return false;
            }
          }
          return true;
        };

        auto in_current_module = [&](const analysis::PathKey &key) -> bool
        {
          if (scope.current_module.empty() || key.size() < scope.current_module.size())
          {
            return false;
          }
          for (std::size_t i = 0; i < scope.current_module.size(); ++i)
          {
            if (!analysis::IdEq(key[i], scope.current_module[i]))
            {
              return false;
            }
          }
          return true;
        };

        const ast::RecordDecl *unique_suffix_match = nullptr;
        bool suffix_ambiguous = false;
        const ast::RecordDecl *module_suffix_match = nullptr;
        bool module_ambiguous = false;

        for (const auto &[type_key, type_decl] : scope.sigma.types)
        {
          const auto *rec = std::get_if<ast::RecordDecl>(&type_decl);
          if (!rec || !suffix_matches(type_key))
          {
            continue;
          }
          if (!unique_suffix_match)
          {
            unique_suffix_match = rec;
          }
          else if (unique_suffix_match != rec)
          {
            suffix_ambiguous = true;
          }
          if (in_current_module(type_key))
          {
            if (!module_suffix_match)
            {
              module_suffix_match = rec;
            }
            else if (module_suffix_match != rec)
            {
              module_ambiguous = true;
            }
          }
        }

        if (module_suffix_match && !module_ambiguous)
        {
          record = module_suffix_match;
        }
        else if (unique_suffix_match && !suffix_ambiguous)
        {
          record = unique_suffix_match;
        }
      }
      if (!record)
      {
        return std::nullopt;
      }
      analysis::TypeSubst record_subst;
      ::ultraviolet::analysis::layout::RecordLayoutOptions record_layout_options{};
      if (record->generic_params && !record->generic_params->params.empty())
      {
        const std::size_t arg_count = generic_args ? generic_args->size() : 0;
        if (arg_count > record->generic_params->params.size())
        {
          return std::nullopt;
        }
        record_subst = analysis::BuildSubstitution(
            record->generic_params->params,
            generic_args ? *generic_args : std::vector<analysis::TypeRef>{});
      }
      record_layout_options = ::ultraviolet::analysis::layout::ResolveRecordLayoutOptions(record->attrs);

      FieldAccessMeta meta;
      meta.aggregate_type = stripped;
      bool found = false;
      std::size_t field_index = 0;
      for (const auto &member : record->members)
      {
        const auto *field = std::get_if<ast::FieldDecl>(&member);
        if (!field)
        {
          continue;
        }
        auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, field->type);
        analysis::TypeRef lowered_type = analysis::MakeTypePrim("u8");
        if (lowered.has_value())
        {
          lowered_type = *lowered;
          if (!record_subst.empty())
          {
            lowered_type = analysis::InstantiateType(lowered_type, record_subst);
          }
        }
        meta.aggregate_fields.push_back(lowered_type);
        if (analysis::IdEq(field->name, field_name))
        {
          meta.index = field_index;
          meta.field_type = lowered_type;
          found = true;
        }
        ++field_index;
      }
      if (!found)
      {
        return std::nullopt;
      }
      meta.layout_options = record_layout_options;
      return meta;
    }

    llvm::Value *CoerceTo(llvm::IRBuilder<> *builder,
                          llvm::Value *value,
                          llvm::Type *target_ty)
    {
      if (!builder || !value || !target_ty)
      {
        return value;
      }
      if (value->getType() == target_ty)
      {
        return value;
      }
      return CoerceValue(builder, value, target_ty);
    }

    llvm::Value *CoerceBoolTo(llvm::IRBuilder<> *builder,
                              llvm::Value *value,
                              llvm::Type *target_ty)
    {
      if (!builder || !value || !target_ty)
      {
        return value;
      }

      llvm::Value *predicate = AsBool(builder, value);
      if (!predicate)
      {
        return value;
      }
      if (target_ty->isIntegerTy(1))
      {
        return predicate;
      }
      if (target_ty->isIntegerTy())
      {
        return builder->CreateZExtOrTrunc(predicate, target_ty);
      }
      return CoerceTo(builder, predicate, target_ty);
    }

    analysis::TypeRef StripPermType(const analysis::TypeRef &type)
    {
      if (!type)
      {
        return nullptr;
      }
      if (analysis::TypeRef stripped = analysis::StripPerm(type))
      {
        return stripped;
      }
      return type;
    }

    analysis::TypeRef ResolveAliasType(const LowerCtx *ctx,
                                       const analysis::TypeRef &type,
                                       std::size_t depth)
    {
      analysis::TypeRef stripped = StripPermType(type);
      if (!stripped || !ctx || !ctx->sigma || depth > 16)
      {
        return stripped;
      }
      const analysis::ScopeContext &scope = BuildScope(ctx);
      return ResolveAliasTypeInScope(scope, stripped, depth);
    }

    bool IsUnitType(const analysis::TypeRef &type)
    {
      analysis::TypeRef stripped = StripPermType(type);
      if (!stripped)
      {
        return false;
      }
      const auto *prim = std::get_if<analysis::TypePrim>(&stripped->node);
      return prim && prim->name == "()";
    }

    bool IsBoolType(const analysis::TypeRef &type)
    {
      analysis::TypeRef stripped = StripPermType(type);
      if (!stripped)
      {
        return false;
      }
      const auto *prim = std::get_if<analysis::TypePrim>(&stripped->node);
      return prim && prim->name == "bool";
    }

    bool IsBoolBinOp(std::string_view op)
    {
      return op == "==" || op == "===" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
             op == ">=" || op == "&&" || op == "||";
    }

    bool IsNeverType(const analysis::TypeRef &type)
    {
      analysis::TypeRef stripped = StripPermType(type);
      if (!stripped)
      {
        return false;
      }
      const auto *prim = std::get_if<analysis::TypePrim>(&stripped->node);
      return prim && prim->name == "!";
    }

    std::optional<std::size_t> FindUnionMemberIndex(
        const std::vector<analysis::TypeRef> &members,
        const analysis::TypeRef &member_type)
    {
      analysis::TypeRef stripped_member = StripPermType(member_type);
      if (!stripped_member)
      {
        return std::nullopt;
      }
      for (std::size_t i = 0; i < members.size(); ++i)
      {
        const auto equiv = analysis::TypeEquiv(members[i], stripped_member);
        if (equiv.ok && equiv.equiv)
        {
          return i;
        }
      }
      return std::nullopt;
    }

    std::optional<std::size_t> InferUnionMemberIndexFromValue(
        LLVMEmitter &emitter,
        llvm::Value *source_value,
        const std::vector<analysis::TypeRef> &members)
    {
      if (!source_value || members.empty())
      {
        return std::nullopt;
      }

      llvm::Type *source_ty = source_value->getType();

      std::optional<std::size_t> exact_bool_match;
      if (source_ty->isIntegerTy() && source_ty->getIntegerBitWidth() > 1)
      {
        // For non-boolean integer immediates, prefer numeric members over bool.
        for (std::size_t i = 0; i < members.size(); ++i)
        {
          llvm::Type *member_ty = emitter.GetLLVMType(members[i]);
          if (!member_ty || member_ty != source_ty)
          {
            continue;
          }
          if (IsBoolType(members[i]))
          {
            exact_bool_match = i;
            continue;
          }
          return i;
        }
      }
      else
      {
        for (std::size_t i = 0; i < members.size(); ++i)
        {
          llvm::Type *member_ty = emitter.GetLLVMType(members[i]);
          if (member_ty && member_ty == source_ty)
          {
            return i;
          }
        }
      }

      // Bool literals are currently materialized as i1 immediates.
      if (source_ty->isIntegerTy(1))
      {
        for (std::size_t i = 0; i < members.size(); ++i)
        {
          if (IsBoolType(members[i]))
          {
            return i;
          }
        }
      }

      // Integer fallback: choose the narrowest non-bool integer member that can hold source bits.
      if (source_ty->isIntegerTy())
      {
        const unsigned source_bits = source_ty->getIntegerBitWidth();
        std::optional<std::size_t> best_index;
        unsigned best_bits = 0;
        for (std::size_t i = 0; i < members.size(); ++i)
        {
          if (IsBoolType(members[i]))
          {
            continue;
          }
          llvm::Type *member_ty = emitter.GetLLVMType(members[i]);
          if (!member_ty || !member_ty->isIntegerTy())
          {
            continue;
          }
          const unsigned member_bits = member_ty->getIntegerBitWidth();
          if (member_bits < source_bits)
          {
            continue;
          }
          if (!best_index.has_value() || member_bits < best_bits)
          {
            best_index = i;
            best_bits = member_bits;
          }
        }
        if (best_index.has_value())
        {
          return best_index;
        }

        // For integer literals lowered as wider immediates (e.g., i64 fallback),
        // allow narrowing when the constant value provably fits the member width.
        if (const auto *cst = llvm::dyn_cast<llvm::ConstantInt>(source_value))
        {
          const llvm::APInt source_ap = cst->getValue();
          std::optional<std::size_t> narrow_index;
          unsigned narrow_bits = 0;
          for (std::size_t i = 0; i < members.size(); ++i)
          {
            if (IsBoolType(members[i]))
            {
              continue;
            }
            llvm::Type *member_ty = emitter.GetLLVMType(members[i]);
            if (!member_ty || !member_ty->isIntegerTy())
            {
              continue;
            }
            const unsigned member_bits = member_ty->getIntegerBitWidth();
            llvm::APInt probe = source_ap;
            if (member_bits < source_ap.getBitWidth())
            {
              probe = source_ap.trunc(member_bits);
              const bool fits_signed = probe.sext(source_ap.getBitWidth()) == source_ap;
              const bool fits_unsigned = probe.zext(source_ap.getBitWidth()) == source_ap;
              if (!fits_signed && !fits_unsigned)
              {
                continue;
              }
            }
            if (!narrow_index.has_value() || member_bits < narrow_bits)
            {
              narrow_index = i;
              narrow_bits = member_bits;
            }
          }
          if (narrow_index.has_value())
          {
            return narrow_index;
          }
        }
      }

      if (exact_bool_match.has_value())
      {
        return exact_bool_match;
      }

      return std::nullopt;
    }

    bool UnionDebugEnabled()
    {
      return core::IsDebugEnabled("union");
    }

    llvm::Value *PackUnionFromMember(LLVMEmitter &emitter,
                                     llvm::IRBuilder<> *builder,
                                     llvm::Value *source_value,
                                     llvm::Type *target_ty,
                                     const analysis::TypeRef &source_type,
                                     const analysis::TypeRef &target_type)
    {
      if (!builder || !source_value || !target_ty)
      {
        return nullptr;
      }

      analysis::TypeRef stripped_target = StripPermType(target_type);
      const auto *target_union =
          stripped_target ? std::get_if<analysis::TypeUnion>(&stripped_target->node) : nullptr;
      if (!target_union)
      {
        return nullptr;
      }

      const LowerCtx *ctx = emitter.GetCurrentCtx();
      if (!ctx || !ctx->sigma)
      {
        return nullptr;
      }
      const analysis::ScopeContext &scope = BuildScope(ctx);
      const auto current_fn_name = [&]() -> std::string
      {
        if (!builder || !builder->GetInsertBlock())
        {
          return "<no-func>";
        }
        if (llvm::Function *fn = builder->GetInsertBlock()->getParent())
        {
          return fn->getName().str();
        }
        return "<no-func>";
      };
      const auto union_layout = ::ultraviolet::analysis::layout::UnionLayoutOf(scope, *target_union);
      if (!union_layout.has_value())
      {
        if (UnionDebugEnabled())
        {
          std::cerr << "[union-debug] fn=" << current_fn_name() << " pack: no layout\n";
        }
        return nullptr;
      }
      if (UnionDebugEnabled())
      {
        std::cerr << "[union-debug] fn=" << current_fn_name() << " members:";
        for (const auto &member : union_layout->member_list)
        {
          analysis::TypeRef stripped = StripPermType(member);
          if (const auto *prim = stripped ? std::get_if<analysis::TypePrim>(&stripped->node) : nullptr)
          {
            std::cerr << " " << prim->name;
          }
          else if (stripped && std::holds_alternative<analysis::TypeUnion>(stripped->node))
          {
            std::cerr << " <union>";
          }
          else if (stripped && std::holds_alternative<analysis::TypeTuple>(stripped->node))
          {
            std::cerr << " <tuple>";
          }
          else
          {
            std::cerr << " <other>";
          }
        }
        std::cerr << "\n";
      }

      std::optional<std::size_t> member_index =
          FindUnionMemberIndex(union_layout->member_list, source_type);
      if (!member_index.has_value())
      {
        member_index = InferUnionMemberIndexFromValue(emitter, source_value, union_layout->member_list);
      }
      if (!member_index.has_value() || *member_index >= union_layout->member_list.size())
      {
        if (UnionDebugEnabled())
        {
          std::cerr << "[union-debug] fn=" << current_fn_name() << " pack: no member index src_ty="
                    << (source_value ? source_value->getType()->isIntegerTy() ? "int" : "other" : "null")
                    << " members=" << union_layout->member_list.size() << "\n";
        }
        return nullptr;
      }
      if (UnionDebugEnabled())
      {
        std::cerr << "[union-debug] fn=" << current_fn_name()
                  << " pack: selected member index=" << *member_index
                  << " niche=" << (union_layout->niche ? 1 : 0) << "\n";
      }
      const analysis::TypeRef member_type = union_layout->member_list[*member_index];

      if (union_layout->niche)
      {
        std::optional<std::size_t> payload_index;
        for (std::size_t i = 0; i < union_layout->member_list.size(); ++i)
        {
          if (!IsUnitType(union_layout->member_list[i]))
          {
            payload_index = i;
            break;
          }
        }
        if (!payload_index.has_value())
        {
          return nullptr;
        }
        if (*member_index != *payload_index)
        {
          return llvm::Constant::getNullValue(target_ty);
        }
        llvm::Value *packed = source_value;
        if (llvm::Type *payload_ty = emitter.GetLLVMType(member_type))
        {
          packed = CoerceTo(builder, packed, payload_ty);
          if (!packed)
          {
            packed = llvm::Constant::getNullValue(payload_ty);
          }
        }
        return packed;
      }

      auto *union_struct_ty = llvm::dyn_cast<llvm::StructType>(target_ty);
      if (!union_struct_ty || union_struct_ty->getNumElements() < 2)
      {
        return nullptr;
      }

      llvm::Value *union_value = llvm::Constant::getNullValue(union_struct_ty);
      llvm::Type *disc_ty = union_struct_ty->getElementType(0);
      llvm::Value *disc = CoerceTo(
          builder,
          llvm::ConstantInt::get(llvm::Type::getInt64Ty(emitter.GetContext()), *member_index),
          disc_ty);
      if (!disc)
      {
        disc = llvm::Constant::getNullValue(disc_ty);
      }
      union_value = builder->CreateInsertValue(union_value, disc, {0u});

      if (IsUnitType(member_type))
      {
        return union_value;
      }

      llvm::Type *member_ty = emitter.GetLLVMType(member_type);
      if (!member_ty)
      {
        return union_value;
      }

      llvm::Value *coerced_member = CoerceTo(builder, source_value, member_ty);
      if (!coerced_member)
      {
        coerced_member = llvm::Constant::getNullValue(member_ty);
      }

      llvm::Function *current_fn =
          builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
      if (!current_fn)
      {
        return union_value;
      }

      llvm::IRBuilder<> entry_builder(
          &current_fn->getEntryBlock(),
          current_fn->getEntryBlock().begin());
      llvm::AllocaInst *union_slot = entry_builder.CreateAlloca(union_struct_ty);
      builder->CreateStore(union_value, union_slot);

      llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
          emitter,
          builder,
          union_struct_ty,
          union_slot,
          union_layout->payload_align);
      if (!payload_i8)
      {
        return union_value;
      }
      llvm::Value *field_ptr = builder->CreateBitCast(
          payload_i8,
          llvm::PointerType::get(member_ty, 0));
      llvm::StoreInst *store = builder->CreateStore(coerced_member, field_ptr);
      store->setAlignment(llvm::Align(1));
      return builder->CreateLoad(union_struct_ty, union_slot);
    }

    llvm::Value *UnpackUnionToMember(LLVMEmitter &emitter,
                                     llvm::IRBuilder<> *builder,
                                     llvm::Value *source_value,
                                     llvm::Type *target_ty,
                                     const analysis::TypeRef &source_type,
                                     const analysis::TypeRef &target_type)
    {
      if (!builder || !source_value || !target_ty)
      {
        return nullptr;
      }

      analysis::TypeRef stripped_source = StripPermType(source_type);
      const auto *source_union =
          stripped_source ? std::get_if<analysis::TypeUnion>(&stripped_source->node) : nullptr;
      if (!source_union)
      {
        return nullptr;
      }

      const LowerCtx *ctx = emitter.GetCurrentCtx();
      if (!ctx || !ctx->sigma)
      {
        return nullptr;
      }
      const analysis::ScopeContext &scope = BuildScope(ctx);
      const auto union_layout =
          ::ultraviolet::analysis::layout::UnionLayoutOf(scope, *source_union);
      if (!union_layout.has_value())
      {
        return nullptr;
      }

      const std::optional<std::size_t> member_index =
          FindUnionMemberIndex(union_layout->member_list, target_type);
      if (!member_index.has_value() ||
          *member_index >= union_layout->member_list.size())
      {
        return nullptr;
      }

      const analysis::TypeRef member_type = union_layout->member_list[*member_index];
      if (!member_type)
      {
        return nullptr;
      }

      if (union_layout->niche)
      {
        if (llvm::Value *coerced = CoerceTo(builder, source_value, target_ty))
        {
          return coerced;
        }
        return llvm::Constant::getNullValue(target_ty);
      }

      if (IsUnitType(member_type))
      {
        return llvm::Constant::getNullValue(target_ty);
      }

      auto *union_struct_ty = llvm::dyn_cast<llvm::StructType>(source_value->getType());
      if (!union_struct_ty || union_struct_ty->getNumElements() < 2)
      {
        return nullptr;
      }

      llvm::Function *current_fn =
          builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
      if (!current_fn)
      {
        return nullptr;
      }

      llvm::IRBuilder<> entry_builder(
          &current_fn->getEntryBlock(),
          current_fn->getEntryBlock().begin());
      llvm::AllocaInst *union_slot = entry_builder.CreateAlloca(union_struct_ty);
      builder->CreateStore(source_value, union_slot);

      llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
          emitter,
          builder,
          union_struct_ty,
          union_slot,
          union_layout->payload_align);
      if (!payload_i8)
      {
        return llvm::Constant::getNullValue(target_ty);
      }

      llvm::Value *member_ptr =
          builder->CreateBitCast(payload_i8, llvm::PointerType::get(target_ty, 0));
      llvm::LoadInst *load = builder->CreateLoad(target_ty, member_ptr);
      load->setAlignment(llvm::Align(1));
      return load;
    }

    llvm::Value *RepackUnionToUnion(LLVMEmitter &emitter,
                                    llvm::IRBuilder<> *builder,
                                    llvm::Value *source_value,
                                    llvm::Type *target_ty,
                                    const analysis::TypeRef &source_type,
                                    const analysis::TypeRef &target_type)
    {
      if (!builder || !source_value || !target_ty)
      {
        return nullptr;
      }

      const LowerCtx *ctx = emitter.GetCurrentCtx();
      if (!ctx || !ctx->sigma)
      {
        return nullptr;
      }

      analysis::TypeRef stripped_source = ResolveAliasType(ctx, source_type);
      analysis::TypeRef stripped_target = ResolveAliasType(ctx, target_type);
      const auto *source_union =
          stripped_source ? std::get_if<analysis::TypeUnion>(&stripped_source->node) : nullptr;
      const auto *target_union =
          stripped_target ? std::get_if<analysis::TypeUnion>(&stripped_target->node) : nullptr;
      if (!source_union || !target_union)
      {
        return nullptr;
      }

      const analysis::ScopeContext &scope = BuildScope(ctx);
      const auto source_layout =
          ::ultraviolet::analysis::layout::UnionLayoutOf(scope, *source_union);
      const auto target_layout =
          ::ultraviolet::analysis::layout::UnionLayoutOf(scope, *target_union);
      if (!source_layout.has_value() || !target_layout.has_value())
      {
        return nullptr;
      }

      for (const analysis::TypeRef &member_type : source_layout->member_list)
      {
        if (!FindUnionMemberIndex(target_layout->member_list, member_type).has_value())
        {
          return nullptr;
        }
      }

      llvm::Function *current_fn =
          builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
      if (!current_fn)
      {
        return nullptr;
      }

      llvm::IRBuilder<> entry_builder(
          &current_fn->getEntryBlock(),
          current_fn->getEntryBlock().begin());
      llvm::AllocaInst *target_slot = entry_builder.CreateAlloca(target_ty);
      builder->CreateStore(llvm::Constant::getNullValue(target_ty), target_slot);

      auto source_value_for = [&](std::size_t member_index) -> llvm::Value *
      {
        if (member_index >= source_layout->member_list.size())
        {
          return nullptr;
        }
        const analysis::TypeRef member_type = source_layout->member_list[member_index];
        llvm::Type *member_ty = emitter.GetLLVMType(member_type);
        if (IsUnitType(member_type))
        {
          return member_ty ? llvm::Constant::getNullValue(member_ty)
                           : llvm::ConstantInt::get(llvm::Type::getInt8Ty(emitter.GetContext()), 0);
        }
        if (!member_ty)
        {
          return nullptr;
        }
        return UnpackUnionToMember(
            emitter,
            builder,
            source_value,
            member_ty,
            stripped_source,
            member_type);
      };

      auto store_target_member = [&](std::size_t member_index) -> void
      {
        if (llvm::Value *member_value = source_value_for(member_index))
        {
          const analysis::TypeRef member_type = source_layout->member_list[member_index];
          if (llvm::Value *packed = PackUnionFromMember(
                  emitter,
                  builder,
                  member_value,
                  target_ty,
                  member_type,
                  stripped_target))
          {
            builder->CreateStore(packed, target_slot);
          }
        }
      };

      if (source_layout->niche)
      {
        std::optional<std::size_t> payload_index;
        std::optional<std::size_t> unit_index;
        for (std::size_t i = 0; i < source_layout->member_list.size(); ++i)
        {
          if (IsUnitType(source_layout->member_list[i]))
          {
            unit_index = i;
          }
          else
          {
            payload_index = i;
          }
        }
        if (!payload_index.has_value() || !unit_index.has_value())
        {
          return nullptr;
        }

        llvm::BasicBlock *merge_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "union.widen.merge", current_fn);
        llvm::Value *zero = llvm::Constant::getNullValue(source_value->getType());
        llvm::Value *has_payload = source_value->getType()->isPointerTy()
                                       ? builder->CreateICmpNE(source_value, zero)
                                       : builder->CreateICmpNE(source_value, zero);
        llvm::BasicBlock *payload_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "union.widen.payload", current_fn);
        llvm::BasicBlock *unit_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "union.widen.unit", current_fn);
        builder->CreateCondBr(has_payload, payload_bb, unit_bb);

        builder->SetInsertPoint(payload_bb);
        store_target_member(*payload_index);
        builder->CreateBr(merge_bb);

        builder->SetInsertPoint(unit_bb);
        store_target_member(*unit_index);
        builder->CreateBr(merge_bb);

        builder->SetInsertPoint(merge_bb);
        return builder->CreateLoad(target_ty, target_slot);
      }

      auto *source_struct_ty = llvm::dyn_cast<llvm::StructType>(source_value->getType());
      if (!source_struct_ty || source_struct_ty->getNumElements() < 1)
      {
        return nullptr;
      }

      llvm::Value *disc = builder->CreateExtractValue(source_value, {0u});
      if (!disc || !disc->getType()->isIntegerTy())
      {
        return nullptr;
      }

      llvm::BasicBlock *merge_bb =
          llvm::BasicBlock::Create(emitter.GetContext(), "union.widen.merge", current_fn);
      llvm::BasicBlock *default_bb =
          llvm::BasicBlock::Create(emitter.GetContext(), "union.widen.default", current_fn);
      llvm::SwitchInst *switch_inst =
          builder->CreateSwitch(disc, default_bb, source_layout->member_list.size());

      for (std::size_t i = 0; i < source_layout->member_list.size(); ++i)
      {
        llvm::BasicBlock *case_bb =
            llvm::BasicBlock::Create(emitter.GetContext(), "union.widen.case", current_fn);
        switch_inst->addCase(
            llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(disc->getType()), i),
            case_bb);

        builder->SetInsertPoint(case_bb);
        store_target_member(i);
        builder->CreateBr(merge_bb);
      }

      builder->SetInsertPoint(default_bb);
      builder->CreateBr(merge_bb);

      builder->SetInsertPoint(merge_bb);
      return builder->CreateLoad(target_ty, target_slot);
    }

    llvm::Value *RepackAsyncToAsync(LLVMEmitter &emitter,
                                    llvm::IRBuilder<> *builder,
                                    llvm::Value *value,
                                    llvm::Type *target_ty,
                                    const analysis::TypeRef &source_type,
                                    const analysis::TypeRef &target_type)
    {
      if (!builder || !value || !target_ty || !source_type || !target_type)
      {
        return nullptr;
      }

      const std::optional<analysis::AsyncSig> source_sig =
          analysis::GetAsyncSig(source_type);
      const std::optional<analysis::AsyncSig> target_sig =
          analysis::GetAsyncSig(target_type);
      if (!source_sig.has_value() || !target_sig.has_value())
      {
        return nullptr;
      }

      llvm::Type *source_ty = emitter.GetLLVMType(source_type);
      auto *source_struct_ty = llvm::dyn_cast_or_null<llvm::StructType>(source_ty);
      auto *target_struct_ty = llvm::dyn_cast<llvm::StructType>(target_ty);
      llvm::Function *current_fn =
          builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
      const LowerCtx *ctx = emitter.GetCurrentCtx();
      if (!source_struct_ty || !target_struct_ty || !current_fn || !ctx)
      {
        return nullptr;
      }

      llvm::IRBuilder<> entry_builder(
          &current_fn->getEntryBlock(),
          current_fn->getEntryBlock().begin());
      llvm::Value *source_storage = nullptr;
      if (value->getType()->isPointerTy())
      {
        source_storage = TypedStoragePointer(emitter, builder, value, source_struct_ty);
      }
      else
      {
        llvm::Value *source_value = value;
        if (source_value->getType() != source_struct_ty)
        {
          if (llvm::Value *coerced = CoerceTo(builder, source_value, source_struct_ty))
          {
            source_value = coerced;
          }
        }
        if (source_value->getType() != source_struct_ty)
        {
          return nullptr;
        }
        llvm::AllocaInst *slot = entry_builder.CreateAlloca(source_struct_ty);
        builder->CreateStore(source_value, slot);
        source_storage = slot;
      }
      if (!source_storage)
      {
        return nullptr;
      }

      const analysis::ScopeContext scope = BuildScope(ctx);
      llvm::AllocaInst *target_slot = entry_builder.CreateAlloca(target_struct_ty);
      EmitStorageMemZero(emitter, builder, target_slot, scope, target_type);

      llvm::Value *source_value =
          builder->CreateLoad(source_struct_ty, source_storage);
      llvm::Value *source_disc = builder->CreateExtractValue(source_value, {0u});
      if (!source_disc || !source_disc->getType()->isIntegerTy())
      {
        return nullptr;
      }

      const llvm::DataLayout &dl = emitter.GetModule().getDataLayout();
      llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
      llvm::Value *source_payload = CreateTaggedPayloadI8Ptr(
          emitter,
          builder,
          source_struct_ty,
          source_storage,
          ::ultraviolet::analysis::layout::kPtrAlign);
      llvm::Value *target_payload = CreateTaggedPayloadI8Ptr(
          emitter,
          builder,
          target_struct_ty,
          target_slot,
          ::ultraviolet::analysis::layout::kPtrAlign);
      if (!source_payload || !target_payload)
      {
        return nullptr;
      }

      auto store_disc = [&](std::uint64_t disc_value) -> void
      {
        llvm::Value *disc_ptr =
            builder->CreateStructGEP(target_struct_ty, target_slot, 0);
        builder->CreateStore(
            llvm::ConstantInt::get(source_disc->getType(), disc_value),
            disc_ptr);
      };

      auto copy_raw_payload = [&]() -> void
      {
        const std::uint64_t source_size =
            static_cast<std::uint64_t>(dl.getTypeAllocSize(source_struct_ty));
        const std::uint64_t target_size =
            static_cast<std::uint64_t>(dl.getTypeAllocSize(target_struct_ty));
        constexpr std::uint64_t payload_offset = kAsyncPayloadFramePtrOffset;
        if (source_size <= payload_offset || target_size <= payload_offset)
        {
          return;
        }
        const std::uint64_t copy_size =
            std::min(source_size - payload_offset, target_size - payload_offset);
        if (copy_size == 0)
        {
          return;
        }
        builder->CreateMemCpy(
            target_payload,
            llvm::Align(1),
            source_payload,
            llvm::Align(1),
            llvm::ConstantInt::get(i64_ty, copy_size));
      };

      auto store_payload = [&](const analysis::TypeRef &source_payload_type,
                               const analysis::TypeRef &target_payload_type) -> void
      {
        if (!source_payload_type || !target_payload_type ||
            IsUnitTypeRef(target_payload_type) ||
            IsNeverTypeRef(target_payload_type))
        {
          return;
        }
        llvm::Type *source_payload_ty = emitter.GetLLVMType(source_payload_type);
        llvm::Type *target_payload_ty = emitter.GetLLVMType(target_payload_type);
        if (!source_payload_ty || !target_payload_ty ||
            source_payload_ty->isVoidTy() || target_payload_ty->isVoidTy())
        {
          return;
        }
        llvm::Value *typed_source_payload = builder->CreateBitCast(
            source_payload, llvm::PointerType::get(source_payload_ty, 0));
        llvm::Value *payload_value =
            builder->CreateLoad(source_payload_ty, typed_source_payload);
        if (payload_value->getType() != target_payload_ty)
        {
          if (llvm::Value *coerced = CoerceToTyped(
                  emitter,
                  builder,
                  payload_value,
                  target_payload_ty,
                  source_payload_type,
                  target_payload_type))
          {
            payload_value = coerced;
          }
          else if (llvm::Value *plain =
                       CoerceTo(builder, payload_value, target_payload_ty))
          {
            payload_value = plain;
          }
          else
          {
            payload_value = llvm::Constant::getNullValue(target_payload_ty);
          }
        }
        llvm::Value *typed_target_payload = builder->CreateBitCast(
            target_payload, llvm::PointerType::get(target_payload_ty, 0));
        builder->CreateStore(payload_value, typed_target_payload);
      };

      const AsyncStateDiscs source_discs =
          LoweredAsyncStateDiscs(scope, *source_sig);
      const AsyncStateDiscs target_discs =
          LoweredAsyncStateDiscs(scope, *target_sig);

      llvm::BasicBlock *completed_bb =
          llvm::BasicBlock::Create(
              emitter.GetContext(), "async.repack.completed", current_fn);
      llvm::BasicBlock *default_bb =
          llvm::BasicBlock::Create(
              emitter.GetContext(), "async.repack.default", current_fn);
      llvm::BasicBlock *merge_bb =
          llvm::BasicBlock::Create(
              emitter.GetContext(), "async.repack.merge", current_fn);
      llvm::SwitchInst *switch_inst =
          builder->CreateSwitch(source_disc, default_bb, 2);
      switch_inst->addCase(
          llvm::ConstantInt::get(
              llvm::cast<llvm::IntegerType>(source_disc->getType()),
              source_discs.completed),
          completed_bb);

      llvm::BasicBlock *failed_bb = nullptr;
      if (source_discs.failed.has_value() && target_discs.failed.has_value())
      {
        failed_bb = llvm::BasicBlock::Create(
            emitter.GetContext(), "async.repack.failed", current_fn);
        switch_inst->addCase(
            llvm::ConstantInt::get(
                llvm::cast<llvm::IntegerType>(source_disc->getType()),
                *source_discs.failed),
            failed_bb);
      }

      builder->SetInsertPoint(completed_bb);
      store_disc(target_discs.completed);
      store_payload(source_sig->result, target_sig->result);
      builder->CreateBr(merge_bb);

      if (failed_bb)
      {
        builder->SetInsertPoint(failed_bb);
        store_disc(*target_discs.failed);
        store_payload(source_sig->err, target_sig->err);
        builder->CreateBr(merge_bb);
      }

      builder->SetInsertPoint(default_bb);
      store_disc(0);
      copy_raw_payload();
      builder->CreateBr(merge_bb);

      builder->SetInsertPoint(merge_bb);
      return builder->CreateLoad(target_struct_ty, target_slot);
    }

    llvm::Value *CoerceToTyped(LLVMEmitter &emitter,
                               llvm::IRBuilder<> *builder,
                               llvm::Value *value,
                               llvm::Type *target_ty,
                               const analysis::TypeRef &source_type,
                               const analysis::TypeRef &target_type)
    {
      if (!builder || !value || !target_ty)
      {
        return value;
      }

      const LowerCtx *ctx = emitter.GetCurrentCtx();
      const auto current_fn_name = [&]() -> std::string
      {
        if (!builder || !builder->GetInsertBlock())
        {
          return "<no-func>";
        }
        if (llvm::Function *fn = builder->GetInsertBlock()->getParent())
        {
          return fn->getName().str();
        }
        return "<no-func>";
      };
      analysis::TypeRef stripped_source = ResolveAliasType(ctx, source_type);
      analysis::TypeRef stripped_target = ResolveAliasType(ctx, target_type);
      const auto *source_func =
          stripped_source ? std::get_if<analysis::TypeFunc>(&stripped_source->node) : nullptr;
      const auto *target_closure =
          stripped_target ? std::get_if<analysis::TypeClosure>(&stripped_target->node) : nullptr;
      if (source_func && target_closure && target_ty &&
          IsClosurePairLLVMType(target_ty))
      {
        llvm::Value *code_ptr = CoerceOrNullOpaquePtr(emitter, builder, value);
        llvm::Value *env_ptr = NullOpaquePtr(emitter);
        llvm::Value *closure_value = llvm::UndefValue::get(target_ty);
        closure_value = builder->CreateInsertValue(closure_value, env_ptr, {0u});
        closure_value = builder->CreateInsertValue(closure_value, code_ptr, {1u});
        return closure_value;
      }

      if (stripped_source && stripped_target &&
          analysis::GetAsyncSig(stripped_source).has_value() &&
          analysis::GetAsyncSig(stripped_target).has_value())
      {
        bool same_async_type = false;
        const auto async_equiv = analysis::TypeEquiv(stripped_source, stripped_target);
        same_async_type = async_equiv.ok && async_equiv.equiv;
        if (!same_async_type)
        {
          if (llvm::Value *repacked = RepackAsyncToAsync(
                  emitter,
                  builder,
                  value,
                  target_ty,
                  stripped_source,
                  stripped_target))
          {
            return repacked;
          }
        }
      }

      const auto *source_union =
          stripped_source ? std::get_if<analysis::TypeUnion>(&stripped_source->node) : nullptr;
      const auto *early_target_union =
          stripped_target ? std::get_if<analysis::TypeUnion>(&stripped_target->node) : nullptr;
      if (source_union && !early_target_union)
      {
        if (llvm::Value *unpacked = UnpackUnionToMember(
                emitter, builder, value, target_ty, stripped_source, stripped_target))
        {
          return unpacked;
        }
      }

      if (IsBoolType(stripped_target))
      {
        return CoerceBoolTo(builder, value, target_ty);
      }

      const auto *source_array =
          stripped_source ? std::get_if<analysis::TypeArray>(&stripped_source->node) : nullptr;
      const auto *target_array =
          stripped_target ? std::get_if<analysis::TypeArray>(&stripped_target->node) : nullptr;
      if (source_array && target_array &&
          source_array->length == target_array->length)
      {
        auto *target_array_ty = llvm::dyn_cast<llvm::ArrayType>(target_ty);
        llvm::Value *array_value = value;
        if (array_value && array_value->getType()->isPointerTy())
        {
          llvm::Type *source_array_ty = emitter.GetLLVMType(stripped_source);
          if (source_array_ty)
          {
            llvm::Value *typed_ptr = array_value;
            llvm::Type *source_ptr_ty = llvm::PointerType::get(source_array_ty, 0);
            if (typed_ptr->getType() != source_ptr_ty)
            {
              typed_ptr = builder->CreateBitCast(typed_ptr, source_ptr_ty);
            }
            array_value = builder->CreateLoad(source_array_ty, typed_ptr);
          }
        }
        if (target_array_ty && array_value && array_value->getType()->isArrayTy() &&
            llvm::cast<llvm::ArrayType>(array_value->getType())->getNumElements() ==
                target_array_ty->getNumElements())
        {
          llvm::Value *out = llvm::UndefValue::get(target_array_ty);
          llvm::Type *target_elem_ty = target_array_ty->getElementType();
          for (std::uint64_t i = 0; i < target_array->length; ++i)
          {
            llvm::Value *elem =
                builder->CreateExtractValue(array_value, {static_cast<unsigned>(i)});
            llvm::Value *coerced = CoerceToTyped(
                emitter,
                builder,
                elem,
                target_elem_ty,
                source_array->element,
                target_array->element);
            if (!coerced)
            {
              coerced = CoerceTo(builder, elem, target_elem_ty);
            }
            if (!coerced)
            {
              coerced = llvm::Constant::getNullValue(target_elem_ty);
            }
            out = builder->CreateInsertValue(out, coerced, {static_cast<unsigned>(i)});
          }
          return out;
        }
      }

      const auto *source_tuple =
          stripped_source ? std::get_if<analysis::TypeTuple>(&stripped_source->node) : nullptr;
      const auto *target_tuple =
          stripped_target ? std::get_if<analysis::TypeTuple>(&stripped_target->node) : nullptr;
      if (source_tuple && target_tuple &&
          source_tuple->elements.size() == target_tuple->elements.size() &&
          ctx && ctx->sigma)
      {
        llvm::Type *source_ll = emitter.GetLLVMType(stripped_source);
        auto *source_struct_ty = llvm::dyn_cast_or_null<llvm::StructType>(source_ll);
        auto *target_struct_ty = llvm::dyn_cast<llvm::StructType>(target_ty);
        llvm::Function *current_fn =
            builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
        const analysis::ScopeContext scope = BuildScope(ctx);
        const auto source_layout =
            ::ultraviolet::analysis::layout::RecordLayoutOf(scope, source_tuple->elements);
        const auto target_layout =
            ::ultraviolet::analysis::layout::RecordLayoutOf(scope, target_tuple->elements);

        if (source_struct_ty && target_struct_ty && current_fn &&
            source_layout.has_value() && target_layout.has_value() &&
            source_layout->offsets.size() == source_tuple->elements.size() &&
            target_layout->offsets.size() == target_tuple->elements.size())
        {
          llvm::IRBuilder<> entry_builder(
              &current_fn->getEntryBlock(),
              current_fn->getEntryBlock().begin());
          llvm::Value *source_storage = nullptr;
          if (value->getType()->isPointerTy())
          {
            source_storage = TypedStoragePointer(emitter, builder, value, source_struct_ty);
          }
          else
          {
            llvm::Value *source_value = value;
            if (source_value->getType() != source_struct_ty)
            {
              if (llvm::Value *coerced = CoerceTo(builder, source_value, source_struct_ty))
              {
                source_value = coerced;
              }
            }
            if (source_value->getType() == source_struct_ty)
            {
              llvm::AllocaInst *slot = entry_builder.CreateAlloca(source_struct_ty);
              builder->CreateStore(source_value, slot);
              source_storage = slot;
            }
          }

          if (source_storage)
          {
            llvm::AllocaInst *target_slot = entry_builder.CreateAlloca(target_struct_ty);
            EmitStorageMemZero(emitter, builder, target_slot, scope, stripped_target);

            for (std::size_t i = 0; i < source_tuple->elements.size(); ++i)
            {
              llvm::Type *source_elem_ty = emitter.GetLLVMType(source_tuple->elements[i]);
              llvm::Type *target_elem_ty = emitter.GetLLVMType(target_tuple->elements[i]);
              if (!source_elem_ty || !target_elem_ty)
              {
                continue;
              }

              llvm::Value *source_elem_ptr = ByteOffsetPointer(
                  emitter,
                  builder,
                  source_storage,
                  source_elem_ty,
                  source_layout->offsets[i]);
              llvm::Value *target_elem_ptr = ByteOffsetPointer(
                  emitter,
                  builder,
                  target_slot,
                  target_elem_ty,
                  target_layout->offsets[i]);
              if (!source_elem_ptr || !target_elem_ptr)
              {
                continue;
              }

              llvm::LoadInst *source_load =
                  builder->CreateLoad(source_elem_ty, source_elem_ptr);
              source_load->setAlignment(llvm::Align(1));
              llvm::Value *coerced = CoerceToTyped(
                  emitter,
                  builder,
                  source_load,
                  target_elem_ty,
                  source_tuple->elements[i],
                  target_tuple->elements[i]);
              if (!coerced)
              {
                coerced = CoerceTo(builder, source_load, target_elem_ty);
              }
              if (!coerced)
              {
                coerced = llvm::Constant::getNullValue(target_elem_ty);
              }

              llvm::StoreInst *store = builder->CreateStore(coerced, target_elem_ptr);
              store->setAlignment(llvm::Align(1));
            }

            return builder->CreateLoad(target_struct_ty, target_slot);
          }
        }
      }

      const auto *target_union =
          stripped_target ? std::get_if<analysis::TypeUnion>(&stripped_target->node) : nullptr;
      if (!target_union)
      {
        return CoerceTo(builder, value, target_ty);
      }

      if (UnionDebugEnabled())
      {
        const std::string source_text =
            stripped_source ? analysis::TypeToString(stripped_source) : std::string("<null>");
        const std::string target_text =
            stripped_target ? analysis::TypeToString(stripped_target) : std::string("<null>");
        std::cerr << "[union-debug] fn=" << current_fn_name()
                  << " typed-coerce: source_type="
                  << (stripped_source ? "known" : "unknown")
                  << " source=" << source_text
                  << " target=" << target_text
                  << " source_llvm="
                  << (value && value->getType()->isIntegerTy() ? "int" : value && value->getType()->isStructTy() ? "struct"
                                                                     : value && value->getType()->isPointerTy()  ? "ptr"
                                                                                                                 : "other")
                  << " target_llvm="
                  << (target_ty && target_ty->isIntegerTy() ? "int" : target_ty && target_ty->isStructTy() ? "struct"
                                                                  : target_ty && target_ty->isPointerTy()  ? "ptr"
                                                                                                           : "other")
                  << "\n";
      }
      if (stripped_source && std::holds_alternative<analysis::TypeUnion>(stripped_source->node))
      {
        const auto equiv = analysis::TypeEquiv(stripped_source, stripped_target);
        if (equiv.ok && equiv.equiv)
        {
          // Preserve direct union->union coercion only when the runtime value is
          // already carried in union representation. If the source value is still
          // a concrete member payload (e.g. literal bound to union-typed place),
          // we must materialize tag+payload via PackUnionFromMember below.
          if (value->getType() == target_ty)
          {
            return CoerceTo(builder, value, target_ty);
          }
        }

        if (llvm::Value *repacked = RepackUnionToUnion(
                emitter,
                builder,
                value,
                target_ty,
                stripped_source,
                stripped_target))
        {
          return repacked;
        }
      }

      if (llvm::Value *packed = PackUnionFromMember(
              emitter, builder, value, target_ty, stripped_source, stripped_target))
      {
        return packed;
      }

      return CoerceTo(builder, value, target_ty);
    }

    llvm::Value *AsBool(llvm::IRBuilder<> *builder, llvm::Value *value)
    {
      if (!builder || !value)
      {
        return llvm::ConstantInt::getFalse(builder->getContext());
      }
      llvm::Type *ty = value->getType();
      if (ty->isIntegerTy(1))
      {
        return value;
      }
      if (ty->isIntegerTy())
      {
        llvm::Value *zero = llvm::ConstantInt::get(ty, 0);
        return builder->CreateICmpNE(value, zero);
      }
      if (ty->isPointerTy())
      {
        llvm::Value *null_ptr = llvm::ConstantPointerNull::get(
            llvm::cast<llvm::PointerType>(ty));
        return builder->CreateICmpNE(value, null_ptr);
      }
      return llvm::ConstantInt::getFalse(builder->getContext());
    }

    llvm::Value *EmitTypedEq(llvm::IRBuilder<> *builder,
                             llvm::Value *lhs,
                             llvm::Value *rhs);

    std::string LLVMValueRepr(llvm::Value *value)
    {
      if (!value)
      {
        return "<null>";
      }
      std::string out;
      llvm::raw_string_ostream os(out);
      value->print(os);
      return os.str();
    }

    llvm::Value *EmitAggregateEq(llvm::IRBuilder<> *builder,
                                 llvm::Value *lhs,
                                 llvm::Value *rhs)
    {
      if (!builder || !lhs || !rhs)
      {
        return llvm::ConstantInt::getFalse(builder->getContext());
      }
      if (lhs->getType() != rhs->getType())
      {
        return nullptr;
      }

      llvm::Type *ty = lhs->getType();
      if (auto *struct_ty = llvm::dyn_cast<llvm::StructType>(ty))
      {
        llvm::Value *acc = llvm::ConstantInt::getTrue(builder->getContext());
        for (unsigned i = 0; i < struct_ty->getNumElements(); ++i)
        {
          llvm::Value *lhs_elem = builder->CreateExtractValue(lhs, {i});
          llvm::Value *rhs_elem = builder->CreateExtractValue(rhs, {i});
          llvm::Value *elem_eq = EmitTypedEq(builder, lhs_elem, rhs_elem);
          acc = builder->CreateAnd(acc, AsBool(builder, elem_eq));
        }
        return acc;
      }

      if (auto *array_ty = llvm::dyn_cast<llvm::ArrayType>(ty))
      {
        llvm::Value *acc = llvm::ConstantInt::getTrue(builder->getContext());
        for (uint64_t i = 0; i < array_ty->getNumElements(); ++i)
        {
          llvm::Value *lhs_elem = builder->CreateExtractValue(lhs, {static_cast<unsigned>(i)});
          llvm::Value *rhs_elem = builder->CreateExtractValue(rhs, {static_cast<unsigned>(i)});
          llvm::Value *elem_eq = EmitTypedEq(builder, lhs_elem, rhs_elem);
          acc = builder->CreateAnd(acc, AsBool(builder, elem_eq));
        }
        return acc;
      }

      return nullptr;
    }

    llvm::Value *EmitTypedEq(llvm::IRBuilder<> *builder,
                             llvm::Value *lhs,
                             llvm::Value *rhs)
    {
      if (!builder || !lhs || !rhs)
      {
        return llvm::ConstantInt::getFalse(builder->getContext());
      }

      if (lhs->getType() != rhs->getType())
      {
        rhs = CoerceTo(builder, rhs, lhs->getType());
        if (!rhs || lhs->getType() != rhs->getType())
        {
          return llvm::ConstantInt::getFalse(builder->getContext());
        }
      }

      llvm::Type *ty = lhs->getType();
      if (ty->isFloatingPointTy())
      {
        return builder->CreateFCmpOEQ(lhs, rhs);
      }
      if (ty->isIntegerTy() || ty->isPointerTy())
      {
        return builder->CreateICmpEQ(lhs, rhs);
      }
      if (llvm::Value *aggregate_eq = EmitAggregateEq(builder, lhs, rhs))
      {
        return aggregate_eq;
      }
      return llvm::ConstantInt::getFalse(builder->getContext());
    }

    std::string BasePlaceIdentifier(const std::string &repr)
    {
      std::size_t i = 0;
      while (i < repr.size() && std::isspace(static_cast<unsigned char>(repr[i])))
      {
        ++i;
      }
      if (i >= repr.size())
      {
        return {};
      }
      const unsigned char first = static_cast<unsigned char>(repr[i]);
      if (!(std::isalpha(first) || repr[i] == '_'))
      {
        return {};
      }
      std::size_t j = i + 1;
      while (j < repr.size())
      {
        const unsigned char ch = static_cast<unsigned char>(repr[j]);
        if (!(std::isalnum(ch) || repr[j] == '_'))
        {
          break;
        }
        ++j;
      }
      return repr.substr(i, j - i);
    }



}  // namespace ultraviolet::codegen::emit_detail
