// =============================================================================
// File: 05_codegen/llvm/emit/ir/ops/binary.cpp
// Canonical owner for LLVM IR binary operation instruction lowering.
// =============================================================================
#include <cstdint>
#include <optional>

#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

namespace {

analysis::TypeRef StripStringBytesWrappers(const LowerCtx *ctx,
                                           analysis::TypeRef type)
{
  analysis::TypeRef current = ResolveAliasType(ctx, type);
  if (!current)
  {
    current = type;
  }

  for (std::size_t depth = 0; current && depth < 8; ++depth)
  {
    if (analysis::TypeRef stripped = analysis::StripPerm(current))
    {
      current = stripped;
    }
    if (const auto *refined = std::get_if<analysis::TypeRefine>(&current->node))
    {
      current = ResolveAliasType(ctx, refined->base);
      if (!current)
      {
        current = refined->base;
      }
      continue;
    }
    break;
  }

  return current;
}

bool SameStringBytesFamily(const LowerCtx *ctx,
                           const analysis::TypeRef &lhs_type,
                           const analysis::TypeRef &rhs_type)
{
  analysis::TypeRef lhs = StripStringBytesWrappers(ctx, lhs_type);
  analysis::TypeRef rhs = StripStringBytesWrappers(ctx, rhs_type);
  if (!lhs || !rhs)
  {
    return false;
  }

  const bool lhs_string = std::holds_alternative<analysis::TypeString>(lhs->node);
  const bool rhs_string = std::holds_alternative<analysis::TypeString>(rhs->node);
  const bool lhs_bytes = std::holds_alternative<analysis::TypeBytes>(lhs->node);
  const bool rhs_bytes = std::holds_alternative<analysis::TypeBytes>(rhs->node);
  return (lhs_string && rhs_string) || (lhs_bytes && rhs_bytes);
}

std::optional<std::uint8_t> OneByteLiteralViewByte(llvm::Value *value)
{
  auto *literal = llvm::dyn_cast_or_null<llvm::ConstantStruct>(value);
  if (!literal || literal->getNumOperands() < 2)
  {
    return std::nullopt;
  }

  auto *len = llvm::dyn_cast<llvm::ConstantInt>(literal->getOperand(1));
  if (!len || !len->equalsInt(1))
  {
    return std::nullopt;
  }

  auto *data = llvm::dyn_cast<llvm::Constant>(literal->getOperand(0));
  if (!data)
  {
    return std::nullopt;
  }

  llvm::Constant *cursor = data;
  for (std::size_t depth = 0; cursor && depth < 8; ++depth)
  {
    if (auto *global = llvm::dyn_cast<llvm::GlobalVariable>(cursor))
    {
      if (auto *bytes =
              llvm::dyn_cast_or_null<llvm::ConstantDataArray>(global->getInitializer()))
      {
        if (bytes->getNumElements() == 1)
        {
          return static_cast<std::uint8_t>(bytes->getElementAsInteger(0));
        }
      }
      return std::nullopt;
    }

    auto *expr = llvm::dyn_cast<llvm::ConstantExpr>(cursor);
    if (!expr)
    {
      return std::nullopt;
    }
    if (expr->isCast() || expr->getOpcode() == llvm::Instruction::GetElementPtr)
    {
      cursor = llvm::dyn_cast<llvm::Constant>(expr->getOperand(0));
      continue;
    }
    return std::nullopt;
  }

  return std::nullopt;
}

llvm::Value *EmitViewEqualsOneByteLiteral(LLVMEmitter &emitter,
                                          llvm::IRBuilder<> &builder,
                                          llvm::Value *view_data,
                                          llvm::Value *view_len,
                                          std::uint8_t literal_byte)
{
  if (!view_data || !view_len || !view_data->getType()->isPointerTy() ||
      !view_len->getType()->isIntegerTy())
  {
    return nullptr;
  }

  llvm::Function *fn =
      builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
  if (!fn)
  {
    return nullptr;
  }

  llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
  llvm::Type *i1_ty = llvm::Type::getInt1Ty(emitter.GetContext());
  llvm::Type *i8_ptr_ty = llvm::PointerType::get(i8_ty, 0);
  llvm::Value *len_is_one = builder.CreateICmpEQ(
      view_len,
      llvm::ConstantInt::get(view_len->getType(), 1),
      "string_eq.literal_len");

  llvm::BasicBlock *origin = builder.GetInsertBlock();
  llvm::BasicBlock *byte_block =
      llvm::BasicBlock::Create(emitter.GetContext(), "string_eq.literal_byte", fn);
  llvm::BasicBlock *done_block =
      llvm::BasicBlock::Create(emitter.GetContext(), "string_eq.literal_done", fn);
  builder.CreateCondBr(len_is_one, byte_block, done_block);

  builder.SetInsertPoint(byte_block);
  llvm::Value *byte_ptr = CoerceTo(&builder, view_data, i8_ptr_ty);
  if (!byte_ptr)
  {
    return nullptr;
  }
  llvm::Value *byte = builder.CreateLoad(i8_ty, byte_ptr, "string_eq.view_byte");
  llvm::Value *byte_equal = builder.CreateICmpEQ(
      byte,
      llvm::ConstantInt::get(i8_ty, literal_byte),
      "string_eq.literal_equal");
  llvm::BasicBlock *byte_exit = builder.GetInsertBlock();
  builder.CreateBr(done_block);

  builder.SetInsertPoint(done_block);
  llvm::PHINode *result = builder.CreatePHI(i1_ty, 2);
  result->addIncoming(llvm::ConstantInt::getFalse(emitter.GetContext()), origin);
  result->addIncoming(byte_equal, byte_exit);
  return result;
}

llvm::Value *EmitStringBytesContentEq(LLVMEmitter &emitter,
                                      llvm::IRBuilder<> &builder,
                                      const LowerCtx *ctx,
                                      const analysis::TypeRef &lhs_type,
                                      llvm::Value *lhs,
                                      const analysis::TypeRef &rhs_type,
                                      llvm::Value *rhs)
{
  if (!SameStringBytesFamily(ctx, lhs_type, rhs_type) || !lhs || !rhs)
  {
    return nullptr;
  }
  if (lhs->getType() != rhs->getType())
  {
    rhs = CoerceTo(&builder, rhs, lhs->getType());
  }
  if (!rhs || lhs->getType() != rhs->getType())
  {
    return nullptr;
  }

  auto *view_ty = llvm::dyn_cast<llvm::StructType>(lhs->getType());
  if (!view_ty || view_ty->getNumElements() < 2 ||
      !view_ty->getElementType(0)->isPointerTy() ||
      !view_ty->getElementType(1)->isIntegerTy())
  {
    return nullptr;
  }

  llvm::Type *i64_ty = llvm::Type::getInt64Ty(emitter.GetContext());
  llvm::Type *i32_ty = llvm::Type::getInt32Ty(emitter.GetContext());
  llvm::Type *i1_ty = llvm::Type::getInt1Ty(emitter.GetContext());
  llvm::Type *i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
  llvm::Type *ptr_ty = emitter.GetOpaquePtr();

  llvm::Value *lhs_data = builder.CreateExtractValue(lhs, {0u});
  llvm::Value *rhs_data = builder.CreateExtractValue(rhs, {0u});
  llvm::Value *lhs_len = builder.CreateExtractValue(lhs, {1u});
  llvm::Value *rhs_len = builder.CreateExtractValue(rhs, {1u});
  if (!lhs_data || !rhs_data || !lhs_len || !rhs_len)
  {
    return nullptr;
  }

  const std::optional<std::uint8_t> lhs_literal_byte =
      OneByteLiteralViewByte(lhs);
  const std::optional<std::uint8_t> rhs_literal_byte =
      OneByteLiteralViewByte(rhs);
  if (lhs_literal_byte.has_value() && rhs_literal_byte.has_value())
  {
    return llvm::ConstantInt::get(
        llvm::Type::getInt1Ty(emitter.GetContext()),
        *lhs_literal_byte == *rhs_literal_byte);
  }
  if (lhs_literal_byte.has_value())
  {
    return EmitViewEqualsOneByteLiteral(
        emitter, builder, rhs_data, rhs_len, *lhs_literal_byte);
  }
  if (rhs_literal_byte.has_value())
  {
    return EmitViewEqualsOneByteLiteral(
        emitter, builder, lhs_data, lhs_len, *rhs_literal_byte);
  }

  lhs_data = CoerceTo(&builder, lhs_data, ptr_ty);
  rhs_data = CoerceTo(&builder, rhs_data, ptr_ty);
  lhs_len = CoerceTo(&builder, lhs_len, i64_ty);
  rhs_len = CoerceTo(&builder, rhs_len, i64_ty);
  if (!lhs_data || !rhs_data || !lhs_len || !rhs_len)
  {
    return nullptr;
  }

  llvm::Value *lengths_equal = builder.CreateICmpEQ(lhs_len, rhs_len);
  llvm::Value *lhs_len_le_rhs = builder.CreateICmpULE(lhs_len, rhs_len);
  llvm::Value *compare_len = builder.CreateSelect(lhs_len_le_rhs, lhs_len, rhs_len);
  llvm::Value *zero_len = builder.CreateICmpEQ(
      compare_len, llvm::ConstantInt::get(i64_ty, 0));
  llvm::Value *one_len = builder.CreateICmpEQ(
      compare_len, llvm::ConstantInt::get(i64_ty, 1));

  llvm::Function *fn =
      builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
  if (!fn)
  {
    auto *memcmp_ty =
        llvm::FunctionType::get(i32_ty, {ptr_ty, ptr_ty, i64_ty}, false);
    llvm::FunctionCallee memcmp_fn =
        emitter.GetModule().getOrInsertFunction("memcmp", memcmp_ty);
    llvm::Value *cmp = builder.CreateCall(memcmp_fn, {lhs_data, rhs_data, compare_len});
    llvm::Value *bytes_equal =
        builder.CreateICmpEQ(cmp, llvm::ConstantInt::get(i32_ty, 0));
    return builder.CreateAnd(lengths_equal, bytes_equal);
  }

  llvm::BasicBlock *origin = builder.GetInsertBlock();
  llvm::BasicBlock *one_check_block =
      llvm::BasicBlock::Create(emitter.GetContext(), "string_eq.one_check", fn);
  llvm::BasicBlock *one_block =
      llvm::BasicBlock::Create(emitter.GetContext(), "string_eq.one", fn);
  llvm::BasicBlock *memcmp_block =
      llvm::BasicBlock::Create(emitter.GetContext(), "string_eq.memcmp", fn);
  llvm::BasicBlock *done_block =
      llvm::BasicBlock::Create(emitter.GetContext(), "string_eq.done", fn);
  builder.CreateCondBr(zero_len, done_block, one_check_block);

  builder.SetInsertPoint(one_check_block);
  builder.CreateCondBr(one_len, one_block, memcmp_block);

  builder.SetInsertPoint(one_block);
  llvm::Type *i8_ptr_ty = llvm::PointerType::get(i8_ty, 0);
  llvm::Value *lhs_byte_ptr = CoerceTo(&builder, lhs_data, i8_ptr_ty);
  llvm::Value *rhs_byte_ptr = CoerceTo(&builder, rhs_data, i8_ptr_ty);
  if (!lhs_byte_ptr || !rhs_byte_ptr)
  {
    return nullptr;
  }
  llvm::Value *lhs_byte =
      builder.CreateLoad(i8_ty, lhs_byte_ptr, "string_eq.lhs_byte");
  llvm::Value *rhs_byte =
      builder.CreateLoad(i8_ty, rhs_byte_ptr, "string_eq.rhs_byte");
  llvm::Value *one_equal = builder.CreateICmpEQ(lhs_byte, rhs_byte);
  llvm::BasicBlock *one_exit = builder.GetInsertBlock();
  builder.CreateBr(done_block);

  builder.SetInsertPoint(memcmp_block);
  auto *memcmp_ty =
      llvm::FunctionType::get(i32_ty, {ptr_ty, ptr_ty, i64_ty}, false);
  llvm::FunctionCallee memcmp_fn =
      emitter.GetModule().getOrInsertFunction("memcmp", memcmp_ty);
  llvm::Value *cmp = builder.CreateCall(memcmp_fn, {lhs_data, rhs_data, compare_len});
  llvm::Value *memcmp_equal =
      builder.CreateICmpEQ(cmp, llvm::ConstantInt::get(i32_ty, 0));
  llvm::BasicBlock *memcmp_exit = builder.GetInsertBlock();
  builder.CreateBr(done_block);

  builder.SetInsertPoint(done_block);
  llvm::PHINode *bytes_equal = builder.CreatePHI(i1_ty, 3);
  bytes_equal->addIncoming(llvm::ConstantInt::getTrue(emitter.GetContext()), origin);
  bytes_equal->addIncoming(one_equal, one_exit);
  bytes_equal->addIncoming(memcmp_equal, memcmp_exit);
  return builder.CreateAnd(lengths_equal, bytes_equal);
}

llvm::Value *HalfBits(LLVMEmitter &emitter,
                      llvm::IRBuilder<> &builder,
                      llvm::Value *value)
{
  if (!value || !value->getType()->isHalfTy())
  {
    return nullptr;
  }
  llvm::Type *i16_ty = llvm::Type::getInt16Ty(emitter.GetContext());
  return builder.CreateBitCast(value, i16_ty, "f16.bits");
}

llvm::Value *HalfIsNaN(LLVMEmitter &emitter,
                       llvm::IRBuilder<> &builder,
                       llvm::Value *bits)
{
  llvm::Type *i16_ty = llvm::Type::getInt16Ty(emitter.GetContext());
  llvm::Value *exponent = builder.CreateAnd(
      bits, llvm::ConstantInt::get(i16_ty, 0x7C00u), "f16.exponent");
  llvm::Value *fraction = builder.CreateAnd(
      bits, llvm::ConstantInt::get(i16_ty, 0x03FFu), "f16.fraction");
  llvm::Value *exponent_all_ones = builder.CreateICmpEQ(
      exponent, llvm::ConstantInt::get(i16_ty, 0x7C00u), "f16.nan_exponent");
  llvm::Value *fraction_nonzero = builder.CreateICmpNE(
      fraction, llvm::ConstantInt::get(i16_ty, 0u), "f16.nan_fraction");
  return builder.CreateAnd(exponent_all_ones, fraction_nonzero, "f16.isnan");
}

llvm::Value *HalfIsZero(LLVMEmitter &emitter,
                        llvm::IRBuilder<> &builder,
                        llvm::Value *bits)
{
  llvm::Type *i16_ty = llvm::Type::getInt16Ty(emitter.GetContext());
  llvm::Value *magnitude = builder.CreateAnd(
      bits, llvm::ConstantInt::get(i16_ty, 0x7FFFu), "f16.magnitude");
  return builder.CreateICmpEQ(
      magnitude, llvm::ConstantInt::get(i16_ty, 0u), "f16.iszero");
}

llvm::Value *HalfSortKey(LLVMEmitter &emitter,
                         llvm::IRBuilder<> &builder,
                         llvm::Value *bits)
{
  llvm::Type *i16_ty = llvm::Type::getInt16Ty(emitter.GetContext());
  llvm::Value *sign = builder.CreateAnd(
      bits, llvm::ConstantInt::get(i16_ty, 0x8000u), "f16.sign");
  llvm::Value *is_negative = builder.CreateICmpNE(
      sign, llvm::ConstantInt::get(i16_ty, 0u), "f16.is_negative");
  llvm::Value *negative_key = builder.CreateXor(
      bits, llvm::ConstantInt::get(i16_ty, 0xFFFFu), "f16.negative_key");
  llvm::Value *positive_key = builder.CreateOr(
      bits, llvm::ConstantInt::get(i16_ty, 0x8000u), "f16.positive_key");
  return builder.CreateSelect(is_negative, negative_key, positive_key,
                              "f16.sort_key");
}

llvm::Value *EmitHalfOrderedCompare(LLVMEmitter &emitter,
                                    llvm::IRBuilder<> &builder,
                                    const std::string &op,
                                    llvm::Value *lhs,
                                    llvm::Value *rhs)
{
  if (!lhs || !rhs || !lhs->getType()->isHalfTy() ||
      !rhs->getType()->isHalfTy())
  {
    return nullptr;
  }

  llvm::Value *lhs_bits = HalfBits(emitter, builder, lhs);
  llvm::Value *rhs_bits = HalfBits(emitter, builder, rhs);
  if (!lhs_bits || !rhs_bits)
  {
    return nullptr;
  }

  llvm::Value *lhs_nan = HalfIsNaN(emitter, builder, lhs_bits);
  llvm::Value *rhs_nan = HalfIsNaN(emitter, builder, rhs_bits);
  llvm::Value *ordered = builder.CreateNot(
      builder.CreateOr(lhs_nan, rhs_nan, "f16.nan"), "f16.ordered");

  llvm::Value *lhs_zero = HalfIsZero(emitter, builder, lhs_bits);
  llvm::Value *rhs_zero = HalfIsZero(emitter, builder, rhs_bits);
  llvm::Value *both_zero = builder.CreateAnd(lhs_zero, rhs_zero, "f16.both_zero");
  llvm::Value *bits_equal =
      builder.CreateICmpEQ(lhs_bits, rhs_bits, "f16.bits_equal");
  llvm::Value *equal =
      builder.CreateAnd(ordered,
                        builder.CreateOr(bits_equal, both_zero, "f16.equal_raw"),
                        "f16.equal");

  llvm::Value *lhs_key = HalfSortKey(emitter, builder, lhs_bits);
  llvm::Value *rhs_key = HalfSortKey(emitter, builder, rhs_bits);
  llvm::Value *less_key =
      builder.CreateICmpULT(lhs_key, rhs_key, "f16.less_key");
  llvm::Value *greater_key =
      builder.CreateICmpUGT(lhs_key, rhs_key, "f16.greater_key");
  llvm::Value *less =
      builder.CreateAnd(ordered,
                        builder.CreateAnd(builder.CreateNot(both_zero),
                                          less_key,
                                          "f16.less_nonzero"),
                        "f16.less");
  llvm::Value *greater =
      builder.CreateAnd(ordered,
                        builder.CreateAnd(builder.CreateNot(both_zero),
                                          greater_key,
                                          "f16.greater_nonzero"),
                        "f16.greater");

  if (op == "<")
  {
    return less;
  }
  if (op == "<=")
  {
    return builder.CreateOr(less, equal, "f16.less_equal");
  }
  if (op == ">")
  {
    return greater;
  }
  if (op == ">=")
  {
    return builder.CreateOr(greater, equal, "f16.greater_equal");
  }
  return nullptr;
}

}  // namespace

void IRInstructionVisitor::operator()(const IRBinaryOp &bin) const
{
  llvm::Function *debug_fn =
      builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
  const std::string debug_fn_name =
      debug_fn ? debug_fn->getName().str() : std::string();
  const bool debug_binop = core::IsDebugEnabled("binop") &&
                           debug_fn_name.find("PropagationMaybeDouble") != std::string::npos;
  const bool debug_contract_binop = core::IsDebugEnabled("return") &&
                                    debug_fn_name.find("ContractPredicateIntegrationShift") != std::string::npos;
  if (debug_binop)
  {
    const LowerCtx *active_ctx = emitter.GetCurrentCtx();
    analysis::TypeRef lhs_ty = active_ctx ? active_ctx->LookupValueType(bin.lhs) : nullptr;
    analysis::TypeRef rhs_ty = active_ctx ? active_ctx->LookupValueType(bin.rhs) : nullptr;
    analysis::TypeRef res_ty = active_ctx ? active_ctx->LookupValueType(bin.result) : nullptr;
    std::cerr << "[binop-debug] fn=" << debug_fn_name
              << " op=" << bin.op
              << " result=" << bin.result.name
              << " lhs_kind=" << static_cast<int>(bin.lhs.kind)
              << " lhs_name=" << bin.lhs.name
              << " lhs_type=" << (lhs_ty ? analysis::TypeToString(lhs_ty) : std::string("<null>"))
              << " rhs_kind=" << static_cast<int>(bin.rhs.kind)
              << " rhs_name=" << bin.rhs.name
              << " rhs_type=" << (rhs_ty ? analysis::TypeToString(rhs_ty) : std::string("<null>"))
              << " result_type=" << (res_ty ? analysis::TypeToString(res_ty) : std::string("<null>"))
              << "\n";
  }

  // Do not fold immediate equality from raw IR lexeme/byte payload equality.
  // Distinct immediate construction paths can encode equivalent typed values
  // with different byte widths (for example i32 vs i64 literal payload width),
  // and raw-byte folding is unsound. Evaluate both sides and use typed equality
  // so coercion rules decide semantic equivalence.

  llvm::Value *lhs = EvaluateOrDefault(bin.lhs);
  llvm::Value *rhs = EvaluateOrDefault(bin.rhs);
  llvm::Value *result = nullptr;

  if (lhs->getType() != rhs->getType())
  {
    if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy())
    {
      unsigned width =
          std::max(lhs->getType()->getIntegerBitWidth(),
                   rhs->getType()->getIntegerBitWidth());
      llvm::Type *common = llvm::Type::getIntNTy(emitter.GetContext(), width);
      lhs = CoerceTo(&builder, lhs, common);
      rhs = CoerceTo(&builder, rhs, common);
    }
    else if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy())
    {
      llvm::Type *common = emitter.GetOpaquePtr();
      lhs = CoerceTo(&builder, lhs, common);
      rhs = CoerceTo(&builder, rhs, common);
    }
    else
    {
      rhs = CoerceTo(&builder, rhs, lhs->getType());
    }
  }

  const LowerCtx *type_ctx = emitter.GetCurrentCtx();
  analysis::TypeRef lhs_type = nullptr;
  analysis::TypeRef rhs_type = nullptr;
  if (type_ctx)
  {
    lhs_type = type_ctx->LookupValueType(bin.lhs);
    rhs_type = type_ctx->LookupValueType(bin.rhs);
  }
  if (!lhs_type && bin.lhs.kind == IRValue::Kind::Local)
  {
    lhs_type = emitter.LookupLocalType(bin.lhs.name);
  }
  if (!rhs_type && bin.rhs.kind == IRValue::Kind::Local)
  {
    rhs_type = emitter.LookupLocalType(bin.rhs.name);
  }
  if (IsBoolType(ResolveAliasType(type_ctx, lhs_type)))
  {
    lhs = CoerceBoolTo(&builder, lhs, lhs->getType());
  }
  if (IsBoolType(ResolveAliasType(type_ctx, rhs_type)))
  {
    rhs = CoerceBoolTo(&builder, rhs, rhs->getType());
  }
  auto is_integer_type = [](const analysis::TypeRef &type) -> bool
  {
    analysis::TypeRef stripped = analysis::StripPerm(type);
    if (!stripped)
    {
      return false;
    }
    auto *prim = std::get_if<analysis::TypePrim>(&stripped->node);
    if (!prim)
    {
      return false;
    }
    const std::string &name = prim->name;
    if (name == "isize" || name == "usize")
    {
      return true;
    }
    return !name.empty() && (name[0] == 'i' || name[0] == 'u');
  };
  const bool int_ops_signed = [&]() -> bool
  {
    if (lhs_type && is_integer_type(lhs_type))
    {
      return IsSignedIntegerType(lhs_type);
    }
    if (rhs_type && is_integer_type(rhs_type))
    {
      return IsSignedIntegerType(rhs_type);
    }
    return true;
  }();

  const std::string &op = bin.op;
  if (op == "+")
  {
    result = lhs->getType()->isFloatingPointTy()
                 ? builder.CreateFAdd(lhs, rhs)
                 : EmitCheckedAdd(emitter, lhs, rhs, int_ops_signed);
  }
  else if (op == "-")
  {
    result = lhs->getType()->isFloatingPointTy()
                 ? builder.CreateFSub(lhs, rhs)
                 : EmitCheckedSub(emitter, lhs, rhs, int_ops_signed);
  }
  else if (op == "*")
  {
    result = lhs->getType()->isFloatingPointTy()
                 ? builder.CreateFMul(lhs, rhs)
                 : EmitCheckedMul(emitter, lhs, rhs, int_ops_signed);
  }
  else if (op == "**")
  {
    if (lhs->getType()->isFloatingPointTy() &&
        lhs->getType() == rhs->getType())
    {
      llvm::Function *pow_fn = llvm::Intrinsic::getDeclaration(
          &emitter.GetModule(), llvm::Intrinsic::pow, lhs->getType());
      if (pow_fn)
      {
        result = builder.CreateCall(pow_fn, {lhs, rhs});
      }
    }
    else if (lhs->getType()->isIntegerTy() &&
             rhs->getType()->isIntegerTy())
    {
      llvm::Function *current_fn =
          builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
      if (current_fn)
      {
        const bool signed_int = [&]() -> bool
        {
          if (analysis::TypeRef lhs_type = LookupValueType(bin.lhs))
          {
            return IsSignedIntegerType(lhs_type);
          }
          return true;
        }();

        llvm::Type *int_ty = lhs->getType();
        llvm::IRBuilder<> entry_builder(
            &current_fn->getEntryBlock(),
            current_fn->getEntryBlock().begin());
        llvm::AllocaInst *result_slot = entry_builder.CreateAlloca(int_ty);
        llvm::AllocaInst *exp_slot = entry_builder.CreateAlloca(rhs->getType());

        builder.CreateStore(llvm::ConstantInt::get(int_ty, 1), result_slot);
        builder.CreateStore(rhs, exp_slot);

        if (signed_int)
        {
          llvm::Value *exp_start = builder.CreateLoad(rhs->getType(), exp_slot);
          llvm::Value *non_negative = builder.CreateICmpSGE(
              exp_start, llvm::ConstantInt::get(rhs->getType(), 0));
          EmitPanicReturnIfFalse(
              emitter,
              &builder,
              non_negative,
              PanicCode(PanicReason::Overflow));
        }

        llvm::BasicBlock *loop_cond =
            llvm::BasicBlock::Create(emitter.GetContext(), "pow.cond", current_fn);
        llvm::BasicBlock *loop_body =
            llvm::BasicBlock::Create(emitter.GetContext(), "pow.body", current_fn);
        llvm::BasicBlock *loop_done =
            llvm::BasicBlock::Create(emitter.GetContext(), "pow.done", current_fn);

        builder.CreateBr(loop_cond);

        builder.SetInsertPoint(loop_cond);
        llvm::Value *exp_curr = builder.CreateLoad(rhs->getType(), exp_slot);
        llvm::Value *exp_is_zero = builder.CreateICmpEQ(
            exp_curr, llvm::ConstantInt::get(rhs->getType(), 0));
        builder.CreateCondBr(exp_is_zero, loop_done, loop_body);

        builder.SetInsertPoint(loop_body);
        llvm::Value *res_curr = builder.CreateLoad(int_ty, result_slot);
        llvm::Intrinsic::ID mul_intrinsic = signed_int
                                                ? llvm::Intrinsic::smul_with_overflow
                                                : llvm::Intrinsic::umul_with_overflow;
        llvm::Function *mul_fn = llvm::Intrinsic::getDeclaration(
            &emitter.GetModule(), mul_intrinsic, int_ty);
        llvm::Value *mul_pair = builder.CreateCall(mul_fn, {res_curr, lhs});
        llvm::Value *mul_value = builder.CreateExtractValue(mul_pair, {0u});
        llvm::Value *mul_overflow = builder.CreateExtractValue(mul_pair, {1u});
        llvm::Value *no_overflow = builder.CreateNot(mul_overflow);
        EmitPanicReturnIfFalse(
            emitter,
            &builder,
            no_overflow,
            PanicCode(PanicReason::Overflow));
        builder.CreateStore(mul_value, result_slot);

        llvm::Value *exp_next = builder.CreateSub(
            exp_curr, llvm::ConstantInt::get(rhs->getType(), 1));
        builder.CreateStore(exp_next, exp_slot);
        builder.CreateBr(loop_cond);

        builder.SetInsertPoint(loop_done);
        result = builder.CreateLoad(int_ty, result_slot);
      }
    }
  }
  else if (op == "/")
  {
    result = lhs->getType()->isIntegerTy()
                 ? EmitCheckedDiv(emitter, lhs, rhs, int_ops_signed)
                 : builder.CreateFDiv(lhs, rhs);
  }
  else if (op == "%")
  {
    result = lhs->getType()->isIntegerTy()
                 ? EmitCheckedRem(emitter, lhs, rhs, int_ops_signed)
                 : builder.CreateFRem(lhs, rhs);
  }
  else if (op == "==" || op == "===")
  {
    result = EmitStringBytesContentEq(emitter, builder, type_ctx, lhs_type, lhs,
                                      rhs_type, rhs);
    if (!result)
    {
      result = EmitTypedEq(&builder, lhs, rhs);
    }
  }
  else if (op == "!=")
  {
    llvm::Value *eq = EmitStringBytesContentEq(emitter, builder, type_ctx,
                                               lhs_type, lhs, rhs_type, rhs);
    if (!eq)
    {
      eq = EmitTypedEq(&builder, lhs, rhs);
    }
    result = builder.CreateNot(AsBool(&builder, eq));
  }
  else if (op == "<")
  {
    if (llvm::Value *half_compare =
            EmitHalfOrderedCompare(emitter, builder, op, lhs, rhs))
    {
      result = half_compare;
    }
    else
    {
      result = lhs->getType()->isFloatingPointTy() ? builder.CreateFCmpOLT(lhs, rhs)
                                                   : (int_ops_signed
                                                          ? builder.CreateICmpSLT(lhs, rhs)
                                                          : builder.CreateICmpULT(lhs, rhs));
    }
  }
  else if (op == "<=")
  {
    if (llvm::Value *half_compare =
            EmitHalfOrderedCompare(emitter, builder, op, lhs, rhs))
    {
      result = half_compare;
    }
    else
    {
      result = lhs->getType()->isFloatingPointTy() ? builder.CreateFCmpOLE(lhs, rhs)
                                                   : (int_ops_signed
                                                          ? builder.CreateICmpSLE(lhs, rhs)
                                                          : builder.CreateICmpULE(lhs, rhs));
    }
  }
  else if (op == ">")
  {
    if (llvm::Value *half_compare =
            EmitHalfOrderedCompare(emitter, builder, op, lhs, rhs))
    {
      result = half_compare;
    }
    else
    {
      result = lhs->getType()->isFloatingPointTy() ? builder.CreateFCmpOGT(lhs, rhs)
                                                   : (int_ops_signed
                                                          ? builder.CreateICmpSGT(lhs, rhs)
                                                          : builder.CreateICmpUGT(lhs, rhs));
    }
  }
  else if (op == ">=")
  {
    if (llvm::Value *half_compare =
            EmitHalfOrderedCompare(emitter, builder, op, lhs, rhs))
    {
      result = half_compare;
    }
    else
    {
      result = lhs->getType()->isFloatingPointTy() ? builder.CreateFCmpOGE(lhs, rhs)
                                                   : (int_ops_signed
                                                          ? builder.CreateICmpSGE(lhs, rhs)
                                                          : builder.CreateICmpUGE(lhs, rhs));
    }
  }
  else if (op == "&")
  {
    result = builder.CreateAnd(lhs, rhs);
  }
  else if (op == "|")
  {
    result = builder.CreateOr(lhs, rhs);
  }
  else if (op == "^")
  {
    result = builder.CreateXor(lhs, rhs);
  }
  else if (op == "<<")
  {
    result = EmitCheckedShl(emitter, lhs, rhs);
  }
  else if (op == ">>")
  {
    result = EmitCheckedShr(emitter, lhs, rhs, int_ops_signed);
  }
  else if (op == "&&")
  {
    result = builder.CreateAnd(AsBool(&builder, lhs), AsBool(&builder, rhs));
  }
  else if (op == "||")
  {
    result = builder.CreateOr(AsBool(&builder, lhs), AsBool(&builder, rhs));
  }

  if (!result)
  {
    result = DefaultFor(bin.result);
  }
  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  analysis::TypeRef target_type =
      active_ctx ? active_ctx->LookupValueType(bin.result) : nullptr;
  analysis::TypeRef source_type = nullptr;
  if (IsBoolBinOp(bin.op))
  {
    source_type = analysis::MakeTypePrim("bool");
  }
  else
  {
    if (active_ctx)
    {
      source_type = active_ctx->LookupValueType(bin.lhs);
      if (!source_type)
      {
        source_type = active_ctx->LookupValueType(bin.rhs);
      }
    }
    if (!source_type && bin.lhs.kind == IRValue::Kind::Local)
    {
      source_type = emitter.LookupLocalType(bin.lhs.name);
    }
    if (!source_type && bin.rhs.kind == IRValue::Kind::Local)
    {
      source_type = emitter.LookupLocalType(bin.rhs.name);
    }
  }
  if (llvm::Type *expected = ExpectedLLVMType(bin.result))
  {
    if (IsBoolBinOp(bin.op))
    {
      // Boolean operators are semantically boolean. Keep their value in a
      // scalar boolean representation even if stale type metadata points to
      // an unrelated non-boolean type.
      result = AsBool(&builder, result);
      // Never coerce bool operators to pointer-typed temporaries. The
      // generic coercion path materializes null for int->ptr and can force
      // conditionals to constant-false despite a true comparison result.
      if (expected->isIntegerTy())
      {
        if (llvm::Value *coerced = CoerceTo(&builder, result, expected))
        {
          result = coerced;
        }
      }
    }
    else if (target_type)
    {
      llvm::Value *coerced = CoerceToTyped(
          emitter,
          &builder,
          result,
          expected,
          source_type,
          target_type);
      if (coerced)
      {
        result = coerced;
      }
      else
      {
        result = llvm::Constant::getNullValue(expected);
      }
    }
    else
    {
      result = CoerceTo(&builder, result, expected);
    }
  }
  emitter.SetTempValue(bin.result, result);
  if (debug_contract_binop)
  {
    analysis::TypeRef result_type = nullptr;
    if (const LowerCtx *active_ctx = emitter.GetCurrentCtx())
    {
      result_type = active_ctx->LookupValueType(bin.result);
    }
    std::string llvm_ty = "<null>";
    if (result && result->getType())
    {
      std::string llvm_ty_buf;
      llvm::raw_string_ostream os(llvm_ty_buf);
      result->getType()->print(os);
      os.flush();
      llvm_ty = llvm_ty_buf;
    }
    bool is_const = false;
    bool const_bool = false;
    if (auto *cint = llvm::dyn_cast<llvm::ConstantInt>(result))
    {
      is_const = true;
      const_bool = cint->getValue().isOne();
    }
    std::cerr << "[llvm-binop-debug] fn=" << debug_fn_name
              << " op=" << bin.op
              << " result=" << bin.result.name
              << " result_type="
              << (result_type ? analysis::TypeToString(result_type)
                              : std::string("<null>"))
              << " llvm_ty=" << llvm_ty
              << " is_const=" << (is_const ? "1" : "0");
    if (is_const)
    {
      std::cerr << " const_bool=" << (const_bool ? "1" : "0");
    }
    std::cerr << "\n";
  }
}

} // namespace cursive::codegen::emit_detail
