// =============================================================================
// File: 05_codegen/llvm/emit/ir_instruction_visitor.cpp
// Canonical owner for shared LLVM IR visitor helpers.
// =============================================================================
#include "ir_instruction_visitor.h"

namespace cursive::codegen::emit_detail {

llvm::Type *IRInstructionVisitor::ExpectedLLVMType(const IRValue &value) const
{
  analysis::TypeRef type = LookupValueType(value);
  if (!type)
  {
    return nullptr;
  }
  return emitter.GetLLVMType(type);
}

analysis::TypeRef IRInstructionVisitor::LookupValueType(const IRValue &value) const
{
  if (value.kind == IRValue::Kind::Local)
  {
    if (analysis::TypeRef local_type = emitter.LookupLocalType(value.name))
    {
      return local_type;
    }
  }

  const LowerCtx *ctx = emitter.GetCurrentCtx();
  if (!ctx)
  {
    return nullptr;
  }
  if (analysis::TypeRef type = ctx->LookupValueType(value))
  {
    return type;
  }
  if (value.kind == IRValue::Kind::Local)
  {
    if (const BindingState *state = ctx->GetBindingState(value.name))
    {
      return state->type;
    }
  }
  return nullptr;
}

llvm::Value *IRInstructionVisitor::DefaultFor(const IRValue &value) const
{
  if (llvm::Type *ty = ExpectedLLVMType(value))
  {
    return llvm::Constant::getNullValue(ty);
  }
  return llvm::ConstantInt::get(llvm::Type::getInt64Ty(emitter.GetContext()), 0);
}

llvm::Value *IRInstructionVisitor::EvaluateOrDefault(const IRValue &value) const
{
  llvm::Value *out = emitter.EvaluateIRValue(value);
  if (!out)
  {
    out = DefaultFor(value);
  }
  return out;
}

bool IRInstructionVisitor::IsAddressBackedAggregateType(llvm::Type *ty) const
{
  if (!ty)
  {
    return false;
  }
  if (ty->isArrayTy())
  {
    return true;
  }
  if (auto *struct_ty = llvm::dyn_cast<llvm::StructType>(ty))
  {
    return struct_ty->getNumElements() != 0;
  }
  return false;
}

llvm::Value *IRInstructionVisitor::ForwardedAggregateStorage(const IRValue &value) const
{
  if (value.kind != IRValue::Kind::Opaque)
  {
    return nullptr;
  }
  llvm::Type *result_ty = ExpectedLLVMType(value);
  if (!IsAddressBackedAggregateType(result_ty))
  {
    return nullptr;
  }
  llvm::Value *storage = emitter.GetAddressableStorage(value);
  if (!storage || !storage->getType()->isPointerTy())
  {
    return nullptr;
  }
  llvm::Type *expected_ptr_ty = llvm::PointerType::get(result_ty, 0);
  if (storage->getType() != expected_ptr_ty)
  {
    storage = builder.CreateBitCast(storage, expected_ptr_ty);
  }
  return storage;
}

void IRInstructionVisitor::SetForwardedOrMaterializedResult(const IRValue &value) const
{
  if (llvm::Value *storage = ForwardedAggregateStorage(value))
  {
    emitter.ForgetTempStorage(value);
    emitter.SetTempStorage(value, storage);
    return;
  }
  emitter.SetTempValue(value, EvaluateOrDefault(value));
}

std::optional<std::uint64_t> IRInstructionVisitor::StaticRangeLength(const IRRange &range,
                                               std::uint64_t base_len) const
{
  auto bound_or = [&](const std::optional<IRValue> &bound,
                      std::uint64_t default_value) -> std::optional<std::uint64_t>
  {
    if (!bound.has_value())
    {
      return default_value;
    }
    return ImmediateU64(*bound);
  };

  switch (range.kind)
  {
  case IRRangeKind::Full:
    return base_len;
  case IRRangeKind::From:
  {
    auto lo = bound_or(range.lo, 0);
    if (!lo.has_value() || *lo > base_len)
    {
      return std::nullopt;
    }
    return base_len - *lo;
  }
  case IRRangeKind::To:
  {
    auto hi = bound_or(range.hi, base_len);
    if (!hi.has_value() || *hi > base_len)
    {
      return std::nullopt;
    }
    return *hi;
  }
  case IRRangeKind::ToInclusive:
  {
    auto hi = bound_or(range.hi, 0);
    if (!hi.has_value() || *hi >= base_len)
    {
      return std::nullopt;
    }
    return *hi + 1;
  }
  case IRRangeKind::Exclusive:
  {
    auto lo = bound_or(range.lo, 0);
    auto hi = bound_or(range.hi, base_len);
    if (!lo.has_value() || !hi.has_value() || *lo > *hi || *hi > base_len)
    {
      return std::nullopt;
    }
    return *hi - *lo;
  }
  case IRRangeKind::Inclusive:
  {
    auto lo = bound_or(range.lo, 0);
    auto hi = bound_or(range.hi, 0);
    if (!lo.has_value() || !hi.has_value() || *lo > *hi || *hi >= base_len)
    {
      return std::nullopt;
    }
    return (*hi - *lo) + 1;
  }
  }
  return std::nullopt;
}

std::optional<std::uint64_t> IRInstructionVisitor::StaticLengthOf(const IRValue &value) const
{
  analysis::TypeRef type = analysis::StripPerm(LookupValueType(value));
  for (int depth = 0; type && depth < 4; ++depth)
  {
    if (auto *arr = std::get_if<analysis::TypeArray>(&type->node))
    {
      return arr->length;
    }
    if (auto *ptr = std::get_if<analysis::TypePtr>(&type->node))
    {
      type = analysis::StripPerm(ptr->element);
      continue;
    }
    if (auto *raw = std::get_if<analysis::TypeRawPtr>(&type->node))
    {
      type = analysis::StripPerm(raw->element);
      continue;
    }
    break;
  }

  const LowerCtx *ctx = emitter.GetCurrentCtx();
  if (!ctx)
  {
    return std::nullopt;
  }
  if (const DerivedValueInfo *derived = ctx->LookupDerivedValue(value))
  {
    auto loop_range_trip_count =
        [&](const IRRange &range) -> std::optional<std::uint64_t>
    {
      auto bound_or = [&](const std::optional<IRValue> &bound,
                          std::uint64_t default_value)
          -> std::optional<std::uint64_t>
      {
        if (!bound.has_value())
        {
          return default_value;
        }
        return ImmediateU64(*bound);
      };
      switch (range.kind)
      {
      case IRRangeKind::Exclusive:
      {
        auto lo = bound_or(range.lo, 0);
        auto hi = bound_or(range.hi, 0);
        if (!lo.has_value() || !hi.has_value() || *hi < *lo)
        {
          return std::nullopt;
        }
        return *hi - *lo;
      }
      case IRRangeKind::Inclusive:
      {
        auto lo = bound_or(range.lo, 0);
        auto hi = bound_or(range.hi, 0);
        if (!lo.has_value() || !hi.has_value() || *hi < *lo)
        {
          return std::nullopt;
        }
        return (*hi - *lo) + 1;
      }
      case IRRangeKind::To:
      {
        auto hi = bound_or(range.hi, 0);
        if (!hi.has_value())
        {
          return std::nullopt;
        }
        return *hi;
      }
      case IRRangeKind::ToInclusive:
      {
        auto hi = bound_or(range.hi, 0);
        if (!hi.has_value())
        {
          return std::nullopt;
        }
        return *hi + 1;
      }
      case IRRangeKind::From:
      case IRRangeKind::Full:
        return std::nullopt;
      }
      return std::nullopt;
    };
    switch (derived->kind)
    {
    case DerivedValueInfo::Kind::ArrayLit:
      return static_cast<std::uint64_t>(derived->elements.size());
    case DerivedValueInfo::Kind::ArrayRepeat:
    {
      llvm::Value *count_value = emitter.EvaluateIRValue(derived->repeat_count);
      auto *count_int = llvm::dyn_cast_or_null<llvm::ConstantInt>(count_value);
      if (!count_int)
      {
        return std::nullopt;
      }
      return count_int->getZExtValue();
    }
    case DerivedValueInfo::Kind::ArraySegments:
    {
      std::uint64_t total = 0;
      for (const auto &segment : derived->array_segments)
      {
        if (segment.kind == DerivedArraySegment::Kind::Element)
        {
          total += 1;
          continue;
        }
        if (!segment.count.has_value())
        {
          return std::nullopt;
        }
        llvm::Value *count_value = emitter.EvaluateIRValue(*segment.count);
        auto *count_int = llvm::dyn_cast_or_null<llvm::ConstantInt>(count_value);
        if (!count_int)
        {
          return std::nullopt;
        }
        total += count_int->getZExtValue();
      }
      return total;
    }
    case DerivedValueInfo::Kind::Slice:
    {
      auto base_len = StaticLengthOf(derived->base);
      if (!base_len.has_value())
      {
        return std::nullopt;
      }
      if (derived->range_value.has_value())
      {
        if (const DerivedValueInfo *range_derived =
                ctx->LookupDerivedValue(*derived->range_value))
        {
          if (range_derived->kind == DerivedValueInfo::Kind::RangeLit)
          {
            return StaticRangeLength(range_derived->range, *base_len);
          }
        }
        return std::nullopt;
      }
      return StaticRangeLength(derived->range, *base_len);
    }
    case DerivedValueInfo::Kind::RangeLit:
      return loop_range_trip_count(derived->range);
    default:
      break;
    }
  }
  return std::nullopt;
}

analysis::TypeRef IRInstructionVisitor::NormalizeValueType(const IRValue &value) const
{
  const LowerCtx *ctx = emitter.GetCurrentCtx();
  if (!ctx)
  {
    return nullptr;
  }

  analysis::TypeRef type = LookupValueType(value);
  if (!type)
  {
    return nullptr;
  }

  const analysis::ScopeContext &scope = BuildScope(ctx);
  for (int depth = 0; type && depth < 4; ++depth)
  {
    if (analysis::TypeRef stripped = analysis::StripPerm(type))
    {
      type = stripped;
    }
    if (analysis::TypeRef resolved = ResolveAliasTypeInScope(scope, type))
    {
      type = resolved;
      if (analysis::TypeRef stripped = analysis::StripPerm(type))
      {
        type = stripped;
      }
    }
    if (const auto *ptr = std::get_if<analysis::TypePtr>(&type->node))
    {
      type = ptr->element;
      continue;
    }
    if (const auto *raw = std::get_if<analysis::TypeRawPtr>(&type->node))
    {
      type = raw->element;
      continue;
    }
    break;
  }

  if (analysis::TypeRef stripped = analysis::StripPerm(type))
  {
    return stripped;
  }
  return type;
}

bool IRInstructionVisitor::IsDynamicSequenceType(const analysis::TypeRef &type) const
{
  if (!type)
  {
    return false;
  }
  if (std::holds_alternative<analysis::TypeSlice>(type->node))
  {
    return true;
  }
  if (const auto *str = std::get_if<analysis::TypeString>(&type->node))
  {
    return str->state.has_value() &&
           *str->state == analysis::StringState::View;
  }
  if (const auto *bytes = std::get_if<analysis::TypeBytes>(&type->node))
  {
    return bytes->state.has_value() &&
           *bytes->state == analysis::BytesState::View;
  }
  return false;
}

llvm::Value *IRInstructionVisitor::DynamicLengthOf(const IRValue &value) const
{
  llvm::Value *runtime_value = EvaluateOrDefault(value);
  if (!runtime_value)
  {
    return nullptr;
  }

  if (auto *struct_ty = llvm::dyn_cast<llvm::StructType>(runtime_value->getType()))
  {
    if (struct_ty->getNumElements() >= 2 &&
        struct_ty->getElementType(1)->isIntegerTy())
    {
      return builder.CreateExtractValue(runtime_value, {1u});
    }
  }

  analysis::TypeRef normalized_type = NormalizeValueType(value);
  if (!IsDynamicSequenceType(normalized_type))
  {
    return nullptr;
  }

  if (!runtime_value->getType()->isPointerTy())
  {
    return nullptr;
  }
  return EmitSliceLenFromAddr(emitter, builder, normalized_type, runtime_value);
}

std::optional<std::uint64_t> IRInstructionVisitor::ImmediateU64(const IRValue &value) const
{
  if (value.kind == IRValue::Kind::Immediate)
  {
    std::uint64_t out = 0;
    for (std::size_t i = 0; i < value.bytes.size() && i < 8; ++i)
    {
      out |= static_cast<std::uint64_t>(value.bytes[i]) << (8 * i);
    }
    return out;
  }
  return std::nullopt;
}

bool IRInstructionVisitor::IsSignedIntegerType(const analysis::TypeRef &type) const
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
  if (name == "isize")
  {
    return true;
  }
  return !name.empty() && name[0] == 'i' && name != "i1";
}

bool IRInstructionVisitor::IsCharType(const analysis::TypeRef &type) const
{
  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (!stripped)
  {
    return false;
  }
  const auto *prim = std::get_if<analysis::TypePrim>(&stripped->node);
  return prim && prim->name == "char";
}

llvm::Value *IRInstructionVisitor::EmitBuiltinEqCall(llvm::IRBuilder<> &builder,
                               const analysis::TypeRef &type,
                               llvm::Value *lhs,
                               llvm::Value *rhs) const
{
  if (!lhs || !rhs || !analysis::EqType(type))
  {
    return nullptr;
  }
  return EmitTypedEq(&builder, lhs, rhs);
}

std::optional<IRInstructionVisitor::BuiltinSuccessorResult> IRInstructionVisitor::EmitBuiltinSuccessor(
    llvm::IRBuilder<> &builder,
    const analysis::TypeRef &type,
    llvm::Value *value) const
{
  if (!value || !value->getType()->isIntegerTy() ||
      !analysis::BuiltinStepType(type))
  {
    return std::nullopt;
  }

  if (IsCharType(type))
  {
    llvm::Constant *max_scalar =
        llvm::ConstantInt::get(value->getType(), 0x10FFFFu);
    llvm::Constant *one = llvm::ConstantInt::get(value->getType(), 1u);
    llvm::Constant *surrogate_start =
        llvm::ConstantInt::get(value->getType(), 0xD800u);
    llvm::Constant *surrogate_end =
        llvm::ConstantInt::get(value->getType(), 0xE000u);
    llvm::Value *has_next = builder.CreateICmpNE(value, max_scalar);
    llvm::Value *plus_one = builder.CreateAdd(value, one);
    llvm::Value *is_surrogate_start =
        builder.CreateICmpEQ(plus_one, surrogate_start);
    llvm::Value *next =
        builder.CreateSelect(is_surrogate_start, surrogate_end, plus_one);
    return BuiltinSuccessorResult{
        .has_next = has_next,
        .next = next,
    };
  }

  const unsigned width = value->getType()->getIntegerBitWidth();
  llvm::Constant *max_value = nullptr;
  if (IsSignedIntegerType(type))
  {
    max_value = llvm::ConstantInt::get(
        value->getType(), llvm::APInt::getSignedMaxValue(width));
  }
  else
  {
    max_value = llvm::ConstantInt::get(
        value->getType(), llvm::APInt::getAllOnes(width));
  }
  llvm::Value *has_next = builder.CreateICmpNE(value, max_value);
  llvm::Value *next =
      builder.CreateAdd(value, llvm::ConstantInt::get(value->getType(), 1u));
  return BuiltinSuccessorResult{
      .has_next = has_next,
      .next = next,
  };
}

std::optional<IRInstructionVisitor::BuiltinSuccessorResult> IRInstructionVisitor::EmitBuiltinPredecessor(
    llvm::IRBuilder<> &builder,
    const analysis::TypeRef &type,
    llvm::Value *value) const
{
  if (!value || !value->getType()->isIntegerTy() ||
      !analysis::BuiltinStepType(type))
  {
    return std::nullopt;
  }

  if (IsCharType(type))
  {
    llvm::Constant *min_scalar =
        llvm::ConstantInt::get(value->getType(), 0u);
    llvm::Constant *one = llvm::ConstantInt::get(value->getType(), 1u);
    llvm::Constant *surrogate_last =
        llvm::ConstantInt::get(value->getType(), 0xDFFFu);
    llvm::Constant *scalar_before_surrogates =
        llvm::ConstantInt::get(value->getType(), 0xD7FFu);
    llvm::Value *has_prev = builder.CreateICmpNE(value, min_scalar);
    llvm::Value *minus_one = builder.CreateSub(value, one);
    llvm::Value *is_surrogate_last =
        builder.CreateICmpEQ(minus_one, surrogate_last);
    llvm::Value *prev = builder.CreateSelect(
        is_surrogate_last, scalar_before_surrogates, minus_one);
    return BuiltinSuccessorResult{
        .has_next = has_prev,
        .next = prev,
    };
  }

  const unsigned width = value->getType()->getIntegerBitWidth();
  llvm::Constant *min_value = nullptr;
  if (IsSignedIntegerType(type))
  {
    min_value = llvm::ConstantInt::get(
        value->getType(), llvm::APInt::getSignedMinValue(width));
  }
  else
  {
    min_value = llvm::ConstantInt::get(value->getType(), 0u);
  }
  llvm::Value *has_prev = builder.CreateICmpNE(value, min_value);
  llvm::Value *prev =
      builder.CreateSub(value, llvm::ConstantInt::get(value->getType(), 1u));
  return BuiltinSuccessorResult{
      .has_next = has_prev,
      .next = prev,
  };
}

std::optional<IRInstructionVisitor::MaterializedRangeValue> IRInstructionVisitor::ResolveRangeValue(
    const IRValue &value,
    llvm::Type *bound_ty,
    std::optional<IRRangeKind> fallback_kind) const
{
  analysis::TypeRef range_type = NormalizeValueType(value);

  MaterializedRangeValue out;
  std::optional<unsigned> lo_index;
  std::optional<unsigned> hi_index;
  auto configure_for_kind = [&](IRRangeKind kind) -> bool
  {
    out.kind = kind;
    lo_index.reset();
    hi_index.reset();
    switch (kind)
    {
    case IRRangeKind::Full:
      return true;
    case IRRangeKind::From:
      lo_index = 0u;
      return true;
    case IRRangeKind::To:
    case IRRangeKind::ToInclusive:
      hi_index = 0u;
      return true;
    case IRRangeKind::Exclusive:
    case IRRangeKind::Inclusive:
      lo_index = 0u;
      hi_index = 1u;
      return true;
    }
    return false;
  };

  if (range_type && analysis::IsRangeType(range_type))
  {
    if (std::holds_alternative<analysis::TypeRange>(range_type->node))
    {
      if (!configure_for_kind(IRRangeKind::Exclusive))
      {
        return std::nullopt;
      }
    }
    else if (std::holds_alternative<analysis::TypeRangeInclusive>(
                 range_type->node))
    {
      if (!configure_for_kind(IRRangeKind::Inclusive))
      {
        return std::nullopt;
      }
    }
    else if (std::holds_alternative<analysis::TypeRangeFrom>(
                 range_type->node))
    {
      if (!configure_for_kind(IRRangeKind::From))
      {
        return std::nullopt;
      }
    }
    else if (std::holds_alternative<analysis::TypeRangeTo>(
                 range_type->node))
    {
      if (!configure_for_kind(IRRangeKind::To))
      {
        return std::nullopt;
      }
    }
    else if (std::holds_alternative<analysis::TypeRangeToInclusive>(
                 range_type->node))
    {
      if (!configure_for_kind(IRRangeKind::ToInclusive))
      {
        return std::nullopt;
      }
    }
    else if (std::holds_alternative<analysis::TypeRangeFull>(
                 range_type->node))
    {
      if (!configure_for_kind(IRRangeKind::Full))
      {
        return std::nullopt;
      }
    }
    else
    {
      return std::nullopt;
    }
  }
  else if (fallback_kind.has_value())
  {
    if (!configure_for_kind(*fallback_kind))
    {
      return std::nullopt;
    }
  }
  else
  {
    return std::nullopt;
  }

  if (!lo_index.has_value() && !hi_index.has_value())
  {
    return out;
  }

  llvm::Value *raw = EvaluateOrDefault(value);
  if (!raw)
  {
    return std::nullopt;
  }
  llvm::Type *range_ll = range_type ? emitter.GetLLVMType(range_type) : nullptr;

  if (raw->getType()->isPointerTy())
  {
    if (!range_ll)
    {
      return std::nullopt;
    }
    llvm::Value *typed_ptr = raw;
    llvm::Type *expected_ptr_ty = llvm::PointerType::get(range_ll, 0);
    if (typed_ptr->getType() != expected_ptr_ty)
    {
      typed_ptr = builder.CreateBitCast(typed_ptr, expected_ptr_ty);
    }
    raw = builder.CreateLoad(range_ll, typed_ptr);
  }
  else if (range_ll && raw->getType() != range_ll)
  {
    raw = CoerceTo(&builder, raw, range_ll);
  }
  if (!raw)
  {
    return std::nullopt;
  }

  auto extract_bound = [&](unsigned index) -> llvm::Value *
  {
    auto *struct_ty = llvm::dyn_cast<llvm::StructType>(raw->getType());
    if (!struct_ty || index >= struct_ty->getNumElements())
    {
      return nullptr;
    }
    llvm::Value *bound = builder.CreateExtractValue(raw, {index});
    if (!bound || !bound->getType()->isIntegerTy())
    {
      return nullptr;
    }
    llvm::Type *target_ty =
        bound_ty ? bound_ty : llvm::Type::getInt64Ty(emitter.GetContext());
    if (!target_ty->isIntegerTy())
    {
      return nullptr;
    }
    if (bound->getType() != target_ty)
    {
      bound = builder.CreateIntCast(bound, target_ty, false);
    }
    return bound;
  };

  if (lo_index.has_value())
  {
    out.lo = extract_bound(*lo_index);
    if (!out.lo)
    {
      return std::nullopt;
    }
  }
  if (hi_index.has_value())
  {
    out.hi = extract_bound(*hi_index);
    if (!out.hi)
    {
      return std::nullopt;
    }
  }
  return out;
}

} // namespace cursive::codegen::emit_detail
