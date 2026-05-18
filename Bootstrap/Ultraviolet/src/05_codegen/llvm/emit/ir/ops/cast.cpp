// =============================================================================
// File: 05_codegen/llvm/emit/ir/ops/cast.cpp
// Canonical owner for LLVM IR cast instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRCast &cast) const
{
  llvm::Value *src = EvaluateOrDefault(cast.value);
  llvm::Type *target_ty = emitter.GetLLVMType(cast.target);
  if (!src || !target_ty)
  {
    emitter.SetTempValue(cast.result, DefaultFor(cast.result));
    return;
  }

  auto is_signed_type = [this](const analysis::TypeRef &type) -> bool
  {
    if (!type)
    {
      return false;
    }
    analysis::TypeRef stripped = analysis::StripPerm(type);
    if (!stripped)
    {
      stripped = type;
    }
    return this->IsSignedIntegerType(stripped);
  };

  const bool signed_src = is_signed_type(LookupValueType(cast.value));
  const bool signed_dst = is_signed_type(cast.target);
  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  const bool source_is_bool =
      IsBoolType(ResolveAliasType(active_ctx, LookupValueType(cast.value)));
  const bool target_is_bool =
      IsBoolType(ResolveAliasType(active_ctx, cast.target));

  llvm::Value *out = nullptr;
  llvm::Type *src_ty = src->getType();

  if (target_is_bool || (source_is_bool && target_ty->isIntegerTy()))
  {
    out = CoerceBoolTo(&builder, src, target_ty);
  }
  else if (src_ty == target_ty)
  {
    out = src;
  }
  else if (target_ty->isIntegerTy(1))
  {
    out = AsBool(&builder, src);
  }
  else if (src_ty->isIntegerTy(1) && target_ty->isIntegerTy())
  {
    out = builder.CreateZExt(src, target_ty);
  }
  else if (src_ty->isIntegerTy() && target_ty->isIntegerTy())
  {
    out = builder.CreateIntCast(src, target_ty, signed_src);
  }
  else if (src_ty->isIntegerTy() && target_ty->isFloatingPointTy())
  {
    out = signed_src
              ? builder.CreateSIToFP(src, target_ty)
              : builder.CreateUIToFP(src, target_ty);
  }
  else if (src_ty->isFloatingPointTy() && target_ty->isIntegerTy())
  {
    out = signed_dst
              ? builder.CreateFPToSI(src, target_ty)
              : builder.CreateFPToUI(src, target_ty);
  }
  else if (src_ty->isFloatingPointTy() && target_ty->isFloatingPointTy())
  {
    out = builder.CreateFPCast(src, target_ty);
  }
  else if (src_ty->isPointerTy() && target_ty->isPointerTy())
  {
    out = builder.CreateBitCast(src, target_ty);
  }
  else if (src_ty->isPointerTy() && target_ty->isIntegerTy())
  {
    out = builder.CreatePtrToInt(src, target_ty);
  }
  else if (src_ty->isIntegerTy() && target_ty->isPointerTy())
  {
    out = builder.CreateIntToPtr(src, target_ty);
  }

  if (!out)
  {
    out = CoerceTo(&builder, src, target_ty);
  }
  if (!out)
  {
    out = llvm::Constant::getNullValue(target_ty);
  }
  emitter.SetTempValue(cast.result, out);
}

} // namespace ultraviolet::codegen::emit_detail
