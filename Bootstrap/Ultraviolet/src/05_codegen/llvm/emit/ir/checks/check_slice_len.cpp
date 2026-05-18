// =============================================================================
// File: 05_codegen/llvm/emit/ir/checks/check_slice_len.cpp
// Canonical owner for LLVM IR slice-length check instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

namespace ultraviolet::codegen::emit_detail {

void IRInstructionVisitor::operator()(const IRCheckSliceLen &check) const
{
  llvm::Type *i64 = llvm::Type::getInt64Ty(emitter.GetContext());
  auto length_value = [&](const IRValue &value) -> llvm::Value *
  {
    if (auto static_len = StaticLengthOf(value))
    {
      return llvm::ConstantInt::get(i64, *static_len);
    }
    if (llvm::Value *dynamic_len = DynamicLengthOf(value))
    {
      if (dynamic_len->getType()->getIntegerBitWidth() != 64)
      {
        dynamic_len = builder.CreateIntCast(dynamic_len, i64, false);
      }
      return dynamic_len;
    }
    return nullptr;
  };

  llvm::Value *base_len = length_value(check.base);
  llvm::Value *value_len = length_value(check.value);
  if (!base_len || !value_len)
  {
    return;
  }

  auto to_i64 = [&](const std::optional<IRValue> &value,
                    llvm::Value *default_value) -> llvm::Value *
  {
    if (!value.has_value())
    {
      return default_value;
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

  llvm::Value *expected_len = nullptr;
  if (runtime_range.has_value())
  {
    switch (runtime_range->kind)
    {
    case IRRangeKind::Full:
      expected_len = base_len;
      break;
    case IRRangeKind::From:
    {
      llvm::Value *lo = runtime_range->lo
                            ? runtime_range->lo
                            : llvm::ConstantInt::get(i64, 0);
      expected_len = builder.CreateSub(base_len, lo);
      break;
    }
    case IRRangeKind::To:
    {
      llvm::Value *hi = runtime_range->hi ? runtime_range->hi : base_len;
      expected_len = hi;
      break;
    }
    case IRRangeKind::ToInclusive:
    {
      llvm::Value *hi = runtime_range->hi
                            ? runtime_range->hi
                            : llvm::ConstantInt::get(i64, 0);
      expected_len = builder.CreateAdd(hi, llvm::ConstantInt::get(i64, 1));
      break;
    }
    case IRRangeKind::Exclusive:
    {
      llvm::Value *lo = runtime_range->lo
                            ? runtime_range->lo
                            : llvm::ConstantInt::get(i64, 0);
      llvm::Value *hi = runtime_range->hi ? runtime_range->hi : base_len;
      expected_len = builder.CreateSub(hi, lo);
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
      llvm::Value *span = builder.CreateSub(hi, lo);
      expected_len = builder.CreateAdd(span, llvm::ConstantInt::get(i64, 1));
      break;
    }
    }
  }
  else
  {
    switch (check.range.kind)
    {
    case IRRangeKind::Full:
      expected_len = base_len;
      break;
    case IRRangeKind::From:
    {
      llvm::Value *lo = to_i64(check.range.lo, llvm::ConstantInt::get(i64, 0));
      if (!lo)
      {
        return;
      }
      expected_len = builder.CreateSub(base_len, lo);
      break;
    }
    case IRRangeKind::To:
    {
      llvm::Value *hi = to_i64(check.range.hi, base_len);
      if (!hi)
      {
        return;
      }
      expected_len = hi;
      break;
    }
    case IRRangeKind::ToInclusive:
    {
      llvm::Value *hi = to_i64(check.range.hi, llvm::ConstantInt::get(i64, 0));
      if (!hi)
      {
        return;
      }
      expected_len = builder.CreateAdd(hi, llvm::ConstantInt::get(i64, 1));
      break;
    }
    case IRRangeKind::Exclusive:
    {
      llvm::Value *lo = to_i64(check.range.lo, llvm::ConstantInt::get(i64, 0));
      llvm::Value *hi = to_i64(check.range.hi, base_len);
      if (!lo || !hi)
      {
        return;
      }
      expected_len = builder.CreateSub(hi, lo);
      break;
    }
    case IRRangeKind::Inclusive:
    {
      llvm::Value *lo = to_i64(check.range.lo, llvm::ConstantInt::get(i64, 0));
      llvm::Value *hi = to_i64(check.range.hi, llvm::ConstantInt::get(i64, 0));
      if (!lo || !hi)
      {
        return;
      }
      llvm::Value *span = builder.CreateSub(hi, lo);
      expected_len = builder.CreateAdd(span, llvm::ConstantInt::get(i64, 1));
      break;
    }
    }
  }
  if (!expected_len)
  {
    return;
  }

  llvm::Value *ok = builder.CreateICmpEQ(value_len, expected_len);
  EmitPanicIfFalse(emitter, &builder, ok, PanicCode(PanicReason::Bounds));
}

} // namespace ultraviolet::codegen::emit_detail
