// =============================================================================
// File: 05_codegen/llvm/emit/ir/ops/unary.cpp
// Canonical owner for LLVM IR unary operation instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

namespace {

enum class BangKind {
  Bool,
  Integer,
  Unknown,
};

analysis::TypeRef StripPermAndRefine(const analysis::TypeRef &type)
{
  analysis::TypeRef current = StripPermType(type);
  while (current)
  {
    if (const auto *refine = std::get_if<analysis::TypeRefine>(&current->node))
    {
      current = StripPermType(refine->base);
      continue;
    }
    return current;
  }
  return nullptr;
}

BangKind ClassifyBangType(const analysis::TypeRef &type)
{
  analysis::TypeRef stripped = StripPermAndRefine(type);
  if (!stripped)
  {
    return BangKind::Unknown;
  }
  const auto *prim = std::get_if<analysis::TypePrim>(&stripped->node);
  if (!prim)
  {
    return BangKind::Unknown;
  }
  if (prim->name == "bool")
  {
    return BangKind::Bool;
  }
  if (prim->name == "i8" || prim->name == "i16" || prim->name == "i32" ||
      prim->name == "i64" || prim->name == "i128" || prim->name == "isize" ||
      prim->name == "u8" || prim->name == "u16" || prim->name == "u32" ||
      prim->name == "u64" || prim->name == "u128" || prim->name == "usize")
  {
    return BangKind::Integer;
  }
  return BangKind::Unknown;
}

BangKind MergeBangKind(BangKind lhs, BangKind rhs)
{
  if (lhs == BangKind::Bool || rhs == BangKind::Bool)
  {
    return BangKind::Bool;
  }
  if (lhs == BangKind::Integer || rhs == BangKind::Integer)
  {
    return BangKind::Integer;
  }
  return BangKind::Unknown;
}

void ReportUnaryCodegenFailure(const LowerCtx *ctx)
{
  if (ctx)
  {
    const_cast<LowerCtx *>(ctx)->ReportCodegenFailure();
  }
}

} // namespace

void IRInstructionVisitor::operator()(const IRUnaryOp &unary) const
{
  llvm::Value *operand = EvaluateOrDefault(unary.operand);
  llvm::Value *result = nullptr;
  if (unary.op == "!")
  {
    const LowerCtx *active_ctx = emitter.GetCurrentCtx();
    analysis::TypeRef operand_type =
        active_ctx ? ResolveAliasType(active_ctx, active_ctx->LookupValueType(unary.operand))
                   : nullptr;
    analysis::TypeRef result_type =
        active_ctx ? ResolveAliasType(active_ctx, active_ctx->LookupValueType(unary.result))
                   : nullptr;
    if (!operand_type) {
      operand_type = ResolveAliasType(active_ctx, unary.operand_type);
    }
    if (!result_type) {
      result_type = ResolveAliasType(active_ctx, unary.result_type);
    }
    BangKind bang_kind = MergeBangKind(
        ClassifyBangType(operand_type),
        ClassifyBangType(result_type));
    if (bang_kind == BangKind::Unknown && operand->getType()->isIntegerTy(1))
    {
      bang_kind = BangKind::Bool;
    }

    if (bang_kind == BangKind::Bool)
    {
      // bool `!` must invert truthiness (0/1), not payload bits.
      // i8 bools require canonicalization through AsBool first.
      result = builder.CreateNot(AsBool(&builder, operand));
    }
    else if (bang_kind == BangKind::Integer && operand->getType()->isIntegerTy())
    {
      // Integer `!` is bitwise not per spec.
      result = builder.CreateNot(operand);
    }
    else if (bang_kind == BangKind::Unknown)
    {
      ReportUnaryCodegenFailure(active_ctx);
      result = DefaultFor(unary.result);
    }
    else
    {
      ReportUnaryCodegenFailure(active_ctx);
      result = DefaultFor(unary.result);
    }
  }
  else if (unary.op == "-" && operand->getType()->isIntegerTy())
  {
    result = builder.CreateNeg(operand);
  }
  else if (unary.op == "-" && operand->getType()->isFloatingPointTy())
  {
    result = builder.CreateFNeg(operand);
  }
  else if (unary.op == "~" && operand->getType()->isIntegerTy())
  {
    result = builder.CreateNot(operand);
  }
  else if (unary.op == "widen")
  {
    const LowerCtx *active_ctx = emitter.GetCurrentCtx();
    analysis::TypeRef operand_type =
        active_ctx ? StripPermType(active_ctx->LookupValueType(unary.operand)) : nullptr;
    analysis::TypeRef result_type =
        active_ctx ? StripPermType(active_ctx->LookupValueType(unary.result)) : nullptr;
    if (!operand_type)
    {
      operand_type = StripPermType(unary.operand_type);
    }
    if (!result_type)
    {
      result_type = StripPermType(unary.result_type);
    }

    const auto *modal_state =
        operand_type ? std::get_if<analysis::TypeModalState>(&operand_type->node) : nullptr;
    const auto *modal_ref =
        result_type ? std::get_if<analysis::TypePathType>(&result_type->node) : nullptr;

    auto bitcopy_to_type = [&](llvm::Value *src, llvm::Type *dst_ty) -> llvm::Value *
    {
      if (!src || !dst_ty)
      {
        return nullptr;
      }
      llvm::Function *current_fn =
          builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
      if (!current_fn)
      {
        return nullptr;
      }
      llvm::IRBuilder<> entry_builder(
          &current_fn->getEntryBlock(),
          current_fn->getEntryBlock().begin());
      llvm::AllocaInst *dst_slot = entry_builder.CreateAlloca(dst_ty);
      llvm::AllocaInst *src_slot = entry_builder.CreateAlloca(src->getType());
      builder.CreateStore(src, src_slot);

      llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
      llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
      llvm::Value *dst_i8 = builder.CreateBitCast(dst_slot, llvm::PointerType::get(i8_ty, 0));
      llvm::Value *src_i8 = builder.CreateBitCast(src_slot, llvm::PointerType::get(i8_ty, 0));

      const llvm::DataLayout &dl = emitter.GetModule().getDataLayout();
      const std::uint64_t src_size =
          static_cast<std::uint64_t>(dl.getTypeAllocSize(src->getType()));
      const std::uint64_t dst_size =
          static_cast<std::uint64_t>(dl.getTypeAllocSize(dst_ty));
      const std::uint64_t copy_size = std::min(src_size, dst_size);
      if (copy_size > 0)
      {
        builder.CreateMemCpy(
            dst_i8,
            llvm::Align(1),
            src_i8,
            llvm::Align(1),
            llvm::ConstantInt::get(i64_ty, copy_size));
      }
      return builder.CreateLoad(dst_ty, dst_slot);
    };

    if (active_ctx && modal_state)
    {
      const analysis::ScopeContext &scope = BuildScope(active_ctx);
      const ast::ModalDecl *modal_decl =
          analysis::LookupModalDecl(scope, modal_state->path);
      if (!modal_decl && modal_ref)
      {
        modal_decl = analysis::LookupModalDecl(scope, modal_ref->path);
      }
      if (modal_decl)
      {
        llvm::Type *target_ty = ExpectedLLVMType(unary.result);
        if (!target_ty && result_type)
        {
          target_ty = emitter.GetLLVMType(result_type);
        }
        if (!target_ty)
        {
          target_ty = operand ? operand->getType() : nullptr;
        }

        if (const auto modal_layout = ::ultraviolet::analysis::layout::ModalLayoutOf(scope, *modal_decl, modal_state->generic_args))
        {
          if (modal_layout->disc_type.has_value())
          {
            auto *target_struct = llvm::dyn_cast_or_null<llvm::StructType>(target_ty);
            if (target_struct && target_struct->getNumElements() >= 1)
            {
              std::optional<std::uint64_t> state_index;
              for (std::size_t i = 0; i < modal_decl->states.size(); ++i)
              {
                if (analysis::IdEq(modal_decl->states[i].name, modal_state->state))
                {
                  state_index = static_cast<std::uint64_t>(i);
                  break;
                }
              }

              llvm::Function *current_fn =
                  builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
              if (state_index.has_value() && current_fn)
              {
                llvm::IRBuilder<> entry_builder(
                    &current_fn->getEntryBlock(),
                    current_fn->getEntryBlock().begin());
                llvm::AllocaInst *dst_slot = entry_builder.CreateAlloca(target_struct);
                builder.CreateStore(llvm::Constant::getNullValue(target_struct), dst_slot);

                llvm::Value *disc_ptr = builder.CreateStructGEP(target_struct, dst_slot, 0);
                llvm::Type *disc_ty = target_struct->getElementType(0);
                llvm::Value *disc_value = disc_ty->isIntegerTy()
                                              ? llvm::ConstantInt::get(disc_ty, *state_index)
                                              : nullptr;
                if (!disc_value)
                {
                  disc_value = llvm::Constant::getNullValue(disc_ty);
                }
                builder.CreateStore(disc_value, disc_ptr);

                llvm::Value *payload_i8 = CreateTaggedPayloadI8Ptr(
                    emitter,
                    &builder,
                    target_struct,
                    dst_slot,
                    modal_layout->payload_align);
                if (payload_i8 && operand)
                {
                  llvm::AllocaInst *src_slot = entry_builder.CreateAlloca(operand->getType());
                  builder.CreateStore(operand, src_slot);

                  llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
                  llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
                  llvm::Value *src_i8 = builder.CreateBitCast(
                      src_slot,
                      llvm::PointerType::get(i8_ty, 0));

                  const llvm::DataLayout &dl = emitter.GetModule().getDataLayout();
                  const std::uint64_t src_size =
                      static_cast<std::uint64_t>(dl.getTypeAllocSize(operand->getType()));
                  const std::uint64_t copy_size =
                      std::min(src_size, modal_layout->payload_size);
                  if (copy_size > 0)
                  {
                    builder.CreateMemCpy(
                        payload_i8,
                        llvm::Align(1),
                        src_i8,
                        llvm::Align(1),
                        llvm::ConstantInt::get(i64_ty, copy_size));
                  }
                }
                result = builder.CreateLoad(target_struct, dst_slot);
              }
            }
          }
          else if (target_ty)
          {
            result = bitcopy_to_type(operand, target_ty);
          }
        }
      }
    }
  }
  if (!result)
  {
    result = DefaultFor(unary.result);
  }
  if (llvm::Type *expected = ExpectedLLVMType(unary.result))
  {
    result = CoerceTo(&builder, result, expected);
  }
  emitter.SetTempValue(unary.result, result);
}

} // namespace ultraviolet::codegen::emit_detail
