// =============================================================================
// File: 05_codegen/llvm/emit/ir/checks/check_range.cpp
// Canonical owner for LLVM IR range check instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRCheckRange &check) const
{
  auto len_opt = StaticLengthOf(check.base);
  llvm::Value *dynamic_len =
      len_opt.has_value() ? nullptr : DynamicLengthOf(check.base);
  if (!len_opt.has_value() && dynamic_len == nullptr)
  {
    llvm::Value *base = EvaluateOrDefault(check.base);
    if (base)
    {
      if (auto *arr_ty = llvm::dyn_cast<llvm::ArrayType>(base->getType()))
      {
        len_opt = arr_ty->getNumElements();
      }
    }
  }
  if (!len_opt.has_value() && dynamic_len == nullptr)
  {
    return;
  }
  llvm::Type *i64 = llvm::Type::getInt64Ty(emitter.GetContext());
  llvm::Value *len = nullptr;
  if (len_opt.has_value())
  {
    len = llvm::ConstantInt::get(i64, *len_opt);
  }
  else
  {
    len = dynamic_len;
    if (!len || !len->getType()->isIntegerTy())
    {
      return;
    }
    if (len->getType()->getIntegerBitWidth() != 64)
    {
      len = builder.CreateIntCast(len, i64, false);
    }
  }

  auto as_u64 = [&](const std::optional<IRValue> &value, std::uint64_t default_value) -> llvm::Value *
  {
    if (!value.has_value())
    {
      return llvm::ConstantInt::get(i64, default_value);
    }
    llvm::Value *raw = EvaluateOrDefault(*value);
    if (!raw || !raw->getType()->isIntegerTy())
    {
      return nullptr;
    }
    if (raw->getType()->getIntegerBitWidth() != 64)
    {
      raw = builder.CreateIntCast(raw, i64, false);
    }
    return raw;
  };

  auto runtime_range =
      check.range_value.has_value()
          ? ResolveRangeValue(*check.range_value, i64, check.range.kind)
                                    : std::nullopt;

  llvm::Value *ok = llvm::ConstantInt::getTrue(emitter.GetContext());
  if (runtime_range.has_value())
  {
    switch (runtime_range->kind)
    {
    case IRRangeKind::Full:
      break;
    case IRRangeKind::From:
    {
      llvm::Value *lo = runtime_range->lo
                            ? runtime_range->lo
                            : llvm::ConstantInt::get(i64, 0);
      ok = builder.CreateICmpULE(lo, len);
      break;
    }
    case IRRangeKind::To:
    {
      llvm::Value *hi = runtime_range->hi ? runtime_range->hi : len;
      ok = builder.CreateICmpULE(hi, len);
      break;
    }
    case IRRangeKind::ToInclusive:
    {
      llvm::Value *hi = runtime_range->hi
                            ? runtime_range->hi
                            : llvm::ConstantInt::get(i64, 0);
      ok = builder.CreateICmpULT(hi, len);
      break;
    }
    case IRRangeKind::Exclusive:
    {
      llvm::Value *lo = runtime_range->lo
                            ? runtime_range->lo
                            : llvm::ConstantInt::get(i64, 0);
      llvm::Value *hi = runtime_range->hi ? runtime_range->hi : len;
      llvm::Value *lo_le_hi = builder.CreateICmpULE(lo, hi);
      llvm::Value *hi_le_len = builder.CreateICmpULE(hi, len);
      ok = builder.CreateAnd(lo_le_hi, hi_le_len);
      break;
    }
    case IRRangeKind::Inclusive:
    {
      llvm::Value *lo = runtime_range->lo
                            ? runtime_range->lo
                            : llvm::ConstantInt::get(i64, 0);
      llvm::Value *hi = runtime_range->hi
                            ? runtime_range->hi
                            : llvm::ConstantInt::get(i64, 0);
      llvm::Value *lo_le_hi = builder.CreateICmpULE(lo, hi);
      llvm::Value *hi_lt_len = builder.CreateICmpULT(hi, len);
      ok = builder.CreateAnd(lo_le_hi, hi_lt_len);
      break;
    }
    }
  }
  else
  {
    switch (check.range.kind)
    {
    case IRRangeKind::Full:
      break;
    case IRRangeKind::From:
    {
      llvm::Value *lo = as_u64(check.range.lo, 0);
      if (!lo)
      {
        return;
      }
      ok = builder.CreateICmpULE(lo, len);
      break;
    }
    case IRRangeKind::To:
    {
      llvm::Value *hi = as_u64(check.range.hi, *len_opt);
      if (!hi)
      {
        return;
      }
      ok = builder.CreateICmpULE(hi, len);
      break;
    }
    case IRRangeKind::ToInclusive:
    {
      llvm::Value *hi = as_u64(check.range.hi, 0);
      if (!hi)
      {
        return;
      }
      ok = builder.CreateICmpULT(hi, len);
      break;
    }
    case IRRangeKind::Exclusive:
    {
      llvm::Value *lo = as_u64(check.range.lo, 0);
      llvm::Value *hi = as_u64(check.range.hi, *len_opt);
      if (!lo || !hi)
      {
        return;
      }
      llvm::Value *lo_le_hi = builder.CreateICmpULE(lo, hi);
      llvm::Value *hi_le_len = builder.CreateICmpULE(hi, len);
      ok = builder.CreateAnd(lo_le_hi, hi_le_len);
      break;
    }
    case IRRangeKind::Inclusive:
    {
      llvm::Value *lo = as_u64(check.range.lo, 0);
      llvm::Value *hi = as_u64(check.range.hi, 0);
      if (!lo || !hi)
      {
        return;
      }
      llvm::Value *lo_le_hi = builder.CreateICmpULE(lo, hi);
      llvm::Value *hi_lt_len = builder.CreateICmpULT(hi, len);
      ok = builder.CreateAnd(lo_le_hi, hi_lt_len);
      break;
    }
    }
  }
  EmitPanicIfFalse(emitter, &builder, ok, PanicCode(PanicReason::Bounds));
}

} // namespace cursive::codegen::emit_detail
