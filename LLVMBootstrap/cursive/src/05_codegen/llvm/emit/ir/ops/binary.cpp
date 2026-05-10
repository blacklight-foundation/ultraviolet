// =============================================================================
// File: 05_codegen/llvm/emit/ir/ops/binary.cpp
// Canonical owner for LLVM IR binary operation instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

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
    result = EmitTypedEq(&builder, lhs, rhs);
  }
  else if (op == "!=")
  {
    llvm::Value *eq = EmitTypedEq(&builder, lhs, rhs);
    result = builder.CreateNot(AsBool(&builder, eq));
  }
  else if (op == "<")
  {
    result = lhs->getType()->isFloatingPointTy() ? builder.CreateFCmpOLT(lhs, rhs)
                                                 : (int_ops_signed
                                                        ? builder.CreateICmpSLT(lhs, rhs)
                                                        : builder.CreateICmpULT(lhs, rhs));
  }
  else if (op == "<=")
  {
    result = lhs->getType()->isFloatingPointTy() ? builder.CreateFCmpOLE(lhs, rhs)
                                                 : (int_ops_signed
                                                        ? builder.CreateICmpSLE(lhs, rhs)
                                                        : builder.CreateICmpULE(lhs, rhs));
  }
  else if (op == ">")
  {
    result = lhs->getType()->isFloatingPointTy() ? builder.CreateFCmpOGT(lhs, rhs)
                                                 : (int_ops_signed
                                                        ? builder.CreateICmpSGT(lhs, rhs)
                                                        : builder.CreateICmpUGT(lhs, rhs));
  }
  else if (op == ">=")
  {
    result = lhs->getType()->isFloatingPointTy() ? builder.CreateFCmpOGE(lhs, rhs)
                                                 : (int_ops_signed
                                                        ? builder.CreateICmpSGE(lhs, rhs)
                                                        : builder.CreateICmpUGE(lhs, rhs));
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
