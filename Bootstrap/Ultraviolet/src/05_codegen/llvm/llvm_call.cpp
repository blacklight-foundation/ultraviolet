// =============================================================================
// MIGRATION MAPPING: llvm_call.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.12.4 LLVM Call/Return Lowering (lines 17560-17600)
//   - LLVMCall-ByValue rule
//   - LLVMCall-SRet rule
//   - LLVMRetLower-SRet rule
//   - CallVTable for dynamic dispatch
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/llvm/llvm_call_abi.cpp
//   - Lines 1-100: ComputeCallABI implementation
//   - Lines 51-91: ABICall computation, error handling
//   - Lines 93-100+: Return type handling (SRet vs ByValue)
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/llvm/llvm_call.h
//   - ultraviolet/include/05_codegen/llvm/llvm_emit.h (LLVMEmitter)
//   - ultraviolet/include/05_codegen/abi/abi_calls.h (ABICall, ABICallResult)
//   - llvm/IR/DerivedTypes.h
//   - llvm/IR/Function.h
// =============================================================================

#include "05_codegen/llvm/llvm_call.h"

#include "00_core/spec_trace.h"
#include "00_core/symbols.h"
#include "04_analysis/typing/type_predicates.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/checks/checks.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/llvm/llvm_emit.h"
#include "05_codegen/llvm/emit/internal_helpers.h"
#include "05_codegen/llvm/emit/llvm_emit_helpers.h"
#include "05_codegen/llvm/llvm_ir_panic.h"
#include "05_codegen/llvm/llvm_types.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"

#include <iostream>
#include "llvm/IR/Instructions.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ultraviolet::codegen {


namespace {

using emit_detail::BuildScope;

analysis::TypeRef StripPermLocal(const analysis::TypeRef& type) {
  if (!type) {
    return type;
  }
  if (const auto* perm = std::get_if<analysis::TypePerm>(&type->node)) {
    return StripPermLocal(perm->base);
  }
  return type;
}

const char* UnwindPersonalitySymbolForModule(const llvm::Module& module) {
  const std::string triple = module.getTargetTriple().str();
  if (triple.find("windows-msvc") != std::string::npos) {
    return "__C_specific_handler";
  }
  return "__gxx_personality_v0";
}

struct CallABIParamCacheKey {
  std::optional<analysis::ParamMode> mode;
  const analysis::Type* type = nullptr;
  std::string name;

  bool operator==(const CallABIParamCacheKey& other) const {
    return mode == other.mode &&
           type == other.type &&
           name == other.name;
  }
};

struct CallABICacheKey {
  const LLVMEmitter* emitter = nullptr;
  const analysis::Sigma* sigma = nullptr;
  std::vector<std::string> module_path;
  project::TargetProfile target_profile = project::TargetProfile::X86_64SysV;
  const analysis::Type* ret_type = nullptr;
  bool use_c_abi_aggregate_sret = false;
  bool foreign_boundary_mode_independent = false;
  std::vector<CallABIParamCacheKey> params;

  bool operator==(const CallABICacheKey& other) const {
    return emitter == other.emitter &&
           sigma == other.sigma &&
           module_path == other.module_path &&
           target_profile == other.target_profile &&
           ret_type == other.ret_type &&
           use_c_abi_aggregate_sret == other.use_c_abi_aggregate_sret &&
           foreign_boundary_mode_independent ==
               other.foreign_boundary_mode_independent &&
           params == other.params;
  }
};

void HashCombine(std::size_t& seed, std::size_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

struct CallABICacheKeyHash {
  std::size_t operator()(const CallABICacheKey& key) const {
    std::size_t seed = 0;
    HashCombine(seed, std::hash<const void*>{}(key.emitter));
    HashCombine(seed, std::hash<const void*>{}(key.sigma));
    HashCombine(seed, std::hash<int>{}(static_cast<int>(key.target_profile)));
    HashCombine(seed, std::hash<const void*>{}(key.ret_type));
    HashCombine(seed, std::hash<bool>{}(key.use_c_abi_aggregate_sret));
    HashCombine(seed, std::hash<bool>{}(key.foreign_boundary_mode_independent));
    for (const std::string& segment : key.module_path) {
      HashCombine(seed, std::hash<std::string>{}(segment));
    }
    for (const CallABIParamCacheKey& param : key.params) {
      HashCombine(seed, std::hash<const void*>{}(param.type));
      HashCombine(seed, std::hash<std::string>{}(param.name));
      HashCombine(seed,
                  param.mode.has_value()
                      ? std::hash<int>{}(static_cast<int>(*param.mode) + 1)
                      : 0u);
    }
    return seed;
  }
};

using CallABICache =
    std::unordered_map<CallABICacheKey, ABICallResult, CallABICacheKeyHash>;

thread_local std::unordered_map<const LLVMEmitter*, CallABICache> call_abi_caches;

CallABICacheKey MakeCallABICacheKey(
    LLVMEmitter& emitter,
    const std::vector<IRParam>& params,
    const analysis::TypeRef& ret_type,
    bool use_c_abi_aggregate_sret,
    bool foreign_boundary_mode_independent) {
  LowerCtx* current_ctx = emitter.GetCurrentCtx();
  CallABICacheKey key;
  key.emitter = &emitter;
  key.sigma = current_ctx ? current_ctx->sigma : nullptr;
  if (current_ctx) {
    key.module_path = current_ctx->module_path;
  }
  key.target_profile = emitter.GetTargetProfile();
  key.ret_type = ret_type.get();
  key.use_c_abi_aggregate_sret = use_c_abi_aggregate_sret;
  key.foreign_boundary_mode_independent = foreign_boundary_mode_independent;
  key.params.reserve(params.size());
  for (const IRParam& param : params) {
    key.params.push_back(
        CallABIParamCacheKey{param.mode, param.type.get(), param.name});
  }
  return key;
}

}  // namespace

void ClearCallABICacheForEmitter(const LLVMEmitter& emitter) {
  call_abi_caches.erase(&emitter);
}

// =============================================================================
// §6.12.9 LLVM Call Signature Lowering
// =============================================================================

bool IsValidPtrType(const analysis::TypeRef& type) {
  const auto stripped = StripPermLocal(type);
  if (!stripped) {
    return false;
  }
  if (const auto* ptr = std::get_if<analysis::TypePtr>(&stripped->node)) {
    return ptr->state.has_value() && *ptr->state == analysis::PtrState::Valid;
  }
  return false;
}

bool IsRawPtrType(const analysis::TypeRef& type) {
  const auto stripped = StripPermLocal(type);
  return stripped && std::holds_alternative<analysis::TypeRawPtr>(stripped->node);
}

bool IsClosureEnvParam(const IRParam& param) {
  return param.name == "__env" && IsRawPtrType(param.type);
}

ByRefAccessKind ByRefAccess(const analysis::TypeRef& type) {
  return PermOf(type) == analysis::Permission::Unique
             ? ByRefAccessKind::ReadWrite
             : ByRefAccessKind::ReadOnly;
}

namespace {

void AppendUniqueAttr(AttrSet& attrs, const AttrSpec& attr) {
  auto it = std::find_if(attrs.begin(),
                         attrs.end(),
                         [&](const AttrSpec& existing) {
                           return existing.kind == attr.kind &&
                                  existing.value == attr.value &&
                                  existing.type == attr.type;
                         });
  if (it == attrs.end()) {
    attrs.push_back(attr);
  }
}

void MergeUniqueAttrs(AttrSet& dst, const AttrSet& src) {
  for (const auto& attr : src) {
    AppendUniqueAttr(dst, attr);
  }
}

analysis::TypeRef LoweredByRefWrapperType(const analysis::TypeRef& type) {
  switch (ByRefAccess(type)) {
    case ByRefAccessKind::ReadOnly:
    case ByRefAccessKind::ReadWrite:
      return analysis::MakeTypePtr(
          analysis::MakeTypePerm(analysis::Permission::Const, type),
          analysis::PtrState::Valid);
  }
  return nullptr;
}

bool IsForeignAbiAggregateLLVMType(llvm::Type* ty) {
  return ty && (ty->isStructTy() || ty->isArrayTy());
}

bool IsWin64CAbiAggregateDirectSize(std::uint64_t size) {
  return size == 1 || size == 2 || size == 4 || size == 8;
}

bool ContainsFloatingLLVMType(llvm::Type* ty) {
  if (!ty) {
    return false;
  }
  if (ty->isFloatingPointTy() || ty->isVectorTy()) {
    return true;
  }
  if (auto* struct_ty = llvm::dyn_cast<llvm::StructType>(ty)) {
    for (llvm::Type* elem_ty : struct_ty->elements()) {
      if (ContainsFloatingLLVMType(elem_ty)) {
        return true;
      }
    }
    return false;
  }
  if (auto* array_ty = llvm::dyn_cast<llvm::ArrayType>(ty)) {
    return ContainsFloatingLLVMType(array_ty->getElementType());
  }
  return false;
}

llvm::Type* Win64CAbiDirectAggregateCarrier(llvm::LLVMContext& ctx,
                                            std::uint64_t size) {
  switch (size) {
    case 1:
      return llvm::Type::getInt8Ty(ctx);
    case 2:
      return llvm::Type::getInt16Ty(ctx);
    case 4:
      return llvm::Type::getInt32Ty(ctx);
    case 8:
      return llvm::Type::getInt64Ty(ctx);
    default:
      return nullptr;
  }
}

llvm::Type* SysVCAbiDirectAggregateCarrier(llvm::LLVMContext& ctx,
                                           llvm::Type* source_ty,
                                           std::uint64_t size) {
  if (size == 0 || size > 16 || ContainsFloatingLLVMType(source_ty)) {
    return nullptr;
  }
  if (size <= 8) {
    return llvm::IntegerType::get(ctx, static_cast<unsigned>(size * 8));
  }

  llvm::Type* low = llvm::Type::getInt64Ty(ctx);
  llvm::Type* high =
      llvm::IntegerType::get(ctx, static_cast<unsigned>((size - 8) * 8));
  return llvm::StructType::get(ctx, {low, high}, /*isPacked=*/size != 16);
}

llvm::Type* ForeignAbiDirectAggregateCarrier(
    project::TargetProfile profile,
    llvm::LLVMContext& ctx,
    llvm::Type* source_ty,
    std::uint64_t size) {
  if (!IsForeignAbiAggregateLLVMType(source_ty)) {
    return nullptr;
  }
  switch (profile) {
    case project::TargetProfile::X86_64Win64:
      return Win64CAbiDirectAggregateCarrier(ctx, size);
    case project::TargetProfile::X86_64SysV:
      return SysVCAbiDirectAggregateCarrier(ctx, source_ty, size);
  }
  return nullptr;
}

}  // namespace

AttrSet ComputeLoweredParamAttrs(const std::string& param_name,
                                 const analysis::TypeRef& type,
                                 PassKind pass_kind,
                                 const LowerCtx* ctx) {
  if (!type) {
    return {};
  }

  switch (pass_kind) {
    case PassKind::ByRef: {
      SPEC_RULE("LLVMArgLower-ByRef");
      AttrSet attrs;
      analysis::TypeRef wrapper = LoweredByRefWrapperType(type);
      MergeUniqueAttrs(attrs, ComputePtrAttrs(wrapper, ctx));
      MergeUniqueAttrs(attrs, ComputeArgAttrsExt(param_name, type));
      return attrs;
    }
    case PassKind::ByValue: {
      if (IsValidPtrType(type)) {
        SPEC_RULE("LLVMArgLower-ByValue-PtrValid");
        AttrSet attrs = ComputeArgAttrsExt(param_name, type);
        MergeUniqueAttrs(attrs, ComputePtrAttrs(type, ctx));
        return attrs;
      }
      SPEC_RULE("LLVMArgLower-ByValue-Other");
      return ComputeArgAttrsExt(param_name, type);
    }
    case PassKind::SRet:
      break;
  }

  return {};
}

AttrSet ComputeSRetParamAttrs(const analysis::TypeRef& ret_type,
                              llvm::Type* ret_llvm_type,
                              const LowerCtx* ctx) {
  AttrSet attrs;
  if (!ret_type) {
    return attrs;
  }

  analysis::TypeRef wrapper = analysis::MakeTypePtr(
      analysis::MakeTypePerm(analysis::Permission::Unique, ret_type),
      analysis::PtrState::Valid);
  MergeUniqueAttrs(attrs, ComputePtrAttrs(wrapper, ctx));
  AppendUniqueAttr(attrs, AttrSpec{AttrKind::StructRet, 0, ret_llvm_type});
  AppendUniqueAttr(attrs, AttrSpec{AttrKind::NoAlias, 0, nullptr});
  return attrs;
}

llvm::AllocaInst* CreateEntryAlloca(llvm::Function* func,
                                    llvm::Type* ty,
                                    const std::string& name) {
  if (!func || !ty) {
    return nullptr;
  }
  llvm::IRBuilder<> entry_builder(&func->getEntryBlock(),
                                   func->getEntryBlock().begin());
  llvm::AllocaInst* slot = entry_builder.CreateAlloca(ty, nullptr, name);
  if (llvm::Module* module = func->getParent()) {
    const llvm::Align align = module->getDataLayout().getABITypeAlign(ty);
    if (align.value() > 0) {
      slot->setAlignment(align);
    }
  }
  return slot;
}

void EnsureAllocaABIAlignment(llvm::Value* storage, llvm::Type* ty) {
  auto* alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(
      storage ? storage->stripPointerCasts() : nullptr);
  if (!alloca || !ty) {
    return;
  }
  llvm::Function* func = alloca->getFunction();
  llvm::Module* module = func ? func->getParent() : nullptr;
  if (!module) {
    return;
  }
  const llvm::Align required = module->getDataLayout().getABITypeAlign(ty);
  if (required > alloca->getAlign()) {
    alloca->setAlignment(required);
  }
}

llvm::AllocaInst* AcquireReusableEntryAlloca(llvm::Function* func,
                                             llvm::Type* ty,
                                             std::string_view name,
                                             unsigned ordinal) {
  struct ScratchBucket {
    llvm::Type* ty = nullptr;
    std::string name;
    std::vector<llvm::AllocaInst*> slots;
  };

  struct ScratchCache {
    llvm::Function* func = nullptr;
    std::vector<ScratchBucket> buckets;
  };

  thread_local ScratchCache cache;
  if (cache.func != func) {
    cache.func = func;
    cache.buckets.clear();
  }

  for (auto& bucket : cache.buckets) {
    if (bucket.ty != ty || bucket.name != name) {
      continue;
    }
    while (bucket.slots.size() <= ordinal) {
      llvm::AllocaInst* slot =
          CreateEntryAlloca(func, ty, bucket.name);
      if (!slot) {
        return nullptr;
      }
      bucket.slots.push_back(slot);
    }
    return bucket.slots[ordinal];
  }

  ScratchBucket bucket;
  bucket.ty = ty;
  bucket.name = std::string(name);
  while (bucket.slots.size() <= ordinal) {
    llvm::AllocaInst* slot = CreateEntryAlloca(func, ty, bucket.name);
    if (!slot) {
      return nullptr;
    }
    bucket.slots.push_back(slot);
  }
  llvm::AllocaInst* result = bucket.slots[ordinal];
  cache.buckets.push_back(std::move(bucket));
  return result;
}

llvm::Value* CoerceValue(llvm::IRBuilderBase* builder_base,
                         llvm::Value* value,
                         llvm::Type* target) {
  if (!builder_base || !value || !target) {
    return value;
  }

  auto* builder = static_cast<llvm::IRBuilder<>*>(builder_base);

  if (value->getType() == target) {
    return value;
  }

  // Unit/zero-sized aggregate targets are represented as empty structs in the
  // current lowering pipeline. Coercion into unit should erase the value,
  // never emit a scalar->aggregate bitcast.
  if (const auto* struct_ty = llvm::dyn_cast<llvm::StructType>(target)) {
    if (struct_ty->getNumElements() == 0) {
      return llvm::Constant::getNullValue(target);
    }
  }

  // Coerce fixed-size arrays to slices/views represented as {ptr, len}.
  // This implements Coerce-Array-Slice by materializing a pointer to the
  // array storage plus the static element count.
  if (auto* arr_ty = llvm::dyn_cast<llvm::ArrayType>(value->getType())) {
    auto* target_struct = llvm::dyn_cast<llvm::StructType>(target);
    if (target_struct &&
        target_struct->getNumElements() == 2 &&
        target_struct->getElementType(0)->isPointerTy() &&
        target_struct->getElementType(1)->isIntegerTy()) {
      llvm::Type* ptr_field_ty = target_struct->getElementType(0);
      llvm::Type* len_field_ty = target_struct->getElementType(1);

      llvm::Function* fn = builder->GetInsertBlock()
                               ? builder->GetInsertBlock()->getParent()
                               : nullptr;
      if (!fn) {
        return llvm::Constant::getNullValue(target);
      }

      llvm::Value* data_ptr = nullptr;
      const std::uint64_t elem_count = arr_ty->getNumElements();
      if (elem_count == 0) {
        data_ptr =
            llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_field_ty));
      } else {
        llvm::AllocaInst* slot = CreateEntryAlloca(fn, arr_ty, "array_to_slice");
        if (!slot) {
          return llvm::Constant::getNullValue(target);
        }
        builder->CreateStore(value, slot);

        llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(builder->getContext()), 0);
        llvm::Value* elem_ptr = builder->CreateGEP(
            arr_ty,
            slot,
            {zero, zero});
        data_ptr = CoerceValue(builder, elem_ptr, ptr_field_ty);
      }
      if (!data_ptr) {
        data_ptr =
            llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_field_ty));
      }

      llvm::Value* len_val = llvm::ConstantInt::get(len_field_ty, elem_count);
      llvm::Value* slice_val = llvm::Constant::getNullValue(target_struct);
      slice_val = builder->CreateInsertValue(slice_val, data_ptr, {0u});
      slice_val = builder->CreateInsertValue(slice_val, len_val, {1u});
      return slice_val;
    }
  }

  // Pointer to pointer (bitcast in opaque pointer world is identity)
  if (value->getType()->isPointerTy() && target->isPointerTy()) {
    return builder->CreateBitCast(value, target);
  }

  // Integer coercion
  if (value->getType()->isIntegerTy() && target->isIntegerTy()) {
    unsigned from_bits = value->getType()->getIntegerBitWidth();
    unsigned to_bits = target->getIntegerBitWidth();
    if (from_bits < to_bits) {
      return builder->CreateZExt(value, target);
    }
    if (from_bits > to_bits) {
      return builder->CreateTrunc(value, target);
    }
  }

  // Float coercion
  if (value->getType()->isFloatingPointTy() && target->isFloatingPointTy()) {
    unsigned from_bits = value->getType()->getPrimitiveSizeInBits();
    unsigned to_bits = target->getPrimitiveSizeInBits();
    if (from_bits < to_bits) {
      return builder->CreateFPExt(value, target);
    }
    if (from_bits > to_bits) {
      return builder->CreateFPTrunc(value, target);
    }
  }
  if (value->getType()->isFirstClassType() && target->isFirstClassType()) {
    llvm::BasicBlock* insert_block = builder->GetInsertBlock();
    llvm::Function* fn = insert_block ? insert_block->getParent() : nullptr;
    llvm::Module* module = fn ? fn->getParent() : nullptr;
    if (fn && module) {
      const llvm::DataLayout& layout = module->getDataLayout();
      const std::uint64_t src_bits = layout.getTypeSizeInBits(value->getType());
      const std::uint64_t dst_bits = layout.getTypeSizeInBits(target);
      if (src_bits == dst_bits && src_bits > 0 &&
          !llvm::CastInst::isBitCastable(value->getType(), target)) {
        llvm::AllocaInst* slot = CreateEntryAlloca(fn, value->getType(), "coerce_bits");
        if (slot) {
          builder->CreateStore(value, slot);
          llvm::Value* cast_ptr =
              builder->CreateBitCast(slot, llvm::PointerType::get(target, 0));
          llvm::LoadInst* loaded = builder->CreateLoad(target, cast_ptr);
          loaded->setAlignment(llvm::Align(1));
          return loaded;
        }
      }
    }
  }

  if (llvm::CastInst::isBitCastable(value->getType(), target)) {
    return builder->CreateBitCast(value, target);
  }
  return llvm::Constant::getNullValue(target);
}

// -----------------------------------------------------------------------------
// Argument Lowering
// -----------------------------------------------------------------------------

std::optional<std::vector<llvm::Type*>> ComputeLLVMParamTypes(
    LLVMEmitter& emitter,
    const std::vector<IRParam>& params,
    const std::vector<PassKind>& param_kinds,
    bool has_sret) {

  std::vector<llvm::Type*> param_types;

  // Add sret pointer if needed
  if (has_sret) {
    param_types.push_back(emitter.GetOpaquePtr());
  }

  const analysis::ScopeContext& scope = BuildScope(emitter.GetCurrentCtx());

  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i >= param_kinds.size()) {
      break;
    }

    const auto kind = param_kinds[i];
    if (kind == PassKind::ByRef) {
      SPEC_RULE("LLVMArgLower-ByRef");
      param_types.push_back(emitter.GetOpaquePtr());
      continue;
    }

    const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, params[i].type);
    if (!size.has_value()) {
      SPEC_RULE("LLVMArgLower-Err");
      return std::nullopt;
    }

    if (*size == 0) {
      // Zero-sized types don't need a parameter
      continue;
    }

    if (IsValidPtrType(params[i].type)) {
      SPEC_RULE("LLVMArgLower-ByValue-PtrValid");
    } else {
      SPEC_RULE("LLVMArgLower-ByValue-Other");
    }

    llvm::Type* llvm_ty = emitter.GetLLVMType(params[i].type);
    if (!llvm_ty) {
      SPEC_RULE("LLVMArgLower-Err");
      return std::nullopt;
    }
    param_types.push_back(llvm_ty);
  }

  return param_types;
}

// -----------------------------------------------------------------------------
// Return Value Lowering
// -----------------------------------------------------------------------------

std::optional<llvm::Type*> ComputeLLVMReturnType(LLVMEmitter& emitter,
                                                 const analysis::TypeRef& ret_type,
                                                 PassKind ret_kind) {
  if (ret_kind == PassKind::SRet) {
    SPEC_RULE("LLVMRetLower-SRet");
    return llvm::Type::getVoidTy(emitter.GetContext());
  }

  const analysis::ScopeContext& scope = BuildScope(emitter.GetCurrentCtx());
  const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, ret_type);

  if (!size.has_value()) {
    SPEC_RULE("LLVMRetLower-Err");
    return std::nullopt;
  }

  if (*size == 0) {
    SPEC_RULE("LLVMRetLower-ByValue-ZST");
    return llvm::Type::getVoidTy(emitter.GetContext());
  }

  SPEC_RULE("LLVMRetLower-ByValue");
  llvm::Type* llvm_ty = emitter.GetLLVMType(ret_type);
  if (!llvm_ty) {
    SPEC_RULE("LLVMRetLower-Err");
    return std::nullopt;
  }

  return llvm_ty;
}

// -----------------------------------------------------------------------------
// Call Emission
// -----------------------------------------------------------------------------

llvm::Value* EmitABICall(LLVMEmitter& emitter,
                         llvm::IRBuilderBase* builder_base,
                         llvm::Value* callee,
                         const std::vector<IRParam>& params,
                         const analysis::TypeRef& ret_type,
                         const std::vector<llvm::Value*>& args,
                         bool use_c_abi_aggregate_sret,
                         bool ffi_import_boundary,
                         bool ffi_import_catch,
                         std::optional<unsigned> call_conv_override,
                         const std::vector<IRValue>* source_args,
                         llvm::Value** result_storage_out,
                         llvm::Value* preferred_result_storage,
                         bool foreign_boundary_mode_independent) {
  if (!builder_base || !callee) {
    return nullptr;
  }

  auto* builder = static_cast<llvm::IRBuilder<>*>(builder_base);

  const bool runtime_boundary_mode_independent =
      [&]() -> bool {
    if (auto* global = llvm::dyn_cast<llvm::GlobalValue>(callee)) {
      return IsRuntimeFunction(global->getName().str());
    }
    return false;
  }();

  ABICallResult abi = ComputeCallABI(
      emitter,
      params,
      ret_type,
      use_c_abi_aggregate_sret,
      /*foreign_boundary_mode_independent=*/
      (ffi_import_boundary || foreign_boundary_mode_independent ||
       runtime_boundary_mode_independent));
  if (!abi.valid || !abi.func_type) {
    if (LowerCtx* ctx = emitter.GetCurrentCtx()) {
      ctx->ReportCodegenFailure();
    }
    return nullptr;
  }

  llvm::Function* func = builder->GetInsertBlock()
                             ? builder->GetInsertBlock()->getParent()
                             : nullptr;
  if (!func) {
    return nullptr;
  }

  std::vector<llvm::Value*> call_args(abi.func_type->getNumParams(), nullptr);
  llvm::Value* sret_alloca = nullptr;
  struct ScratchUse {
    llvm::Type* ty = nullptr;
    std::string_view name;
    unsigned count = 0;
  };
  std::vector<ScratchUse> scratch_uses;

  auto next_scratch_ordinal =
      [&](llvm::Type* ty, std::string_view name) -> unsigned {
    for (auto& use : scratch_uses) {
      if (use.ty == ty && use.name == name) {
        return use.count++;
      }
    }
    scratch_uses.push_back(ScratchUse{ty, name, 1});
    return 0;
  };

  auto materialize_array_slice_storage =
      [&](llvm::Value* array_storage, llvm::Type* slice_ty) -> llvm::Value* {
    auto* slice_struct_ty = llvm::dyn_cast<llvm::StructType>(slice_ty);
    if (!slice_struct_ty || slice_struct_ty->getNumElements() != 2 ||
        !slice_struct_ty->getElementType(0)->isPointerTy() ||
        !slice_struct_ty->getElementType(1)->isIntegerTy()) {
      return nullptr;
    }

    llvm::Type* array_ty = nullptr;
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(array_storage)) {
      array_ty = alloca->getAllocatedType();
    } else if (auto* global = llvm::dyn_cast<llvm::GlobalVariable>(array_storage)) {
      array_ty = global->getValueType();
    }

    auto* arr_ty = llvm::dyn_cast_or_null<llvm::ArrayType>(array_ty);
    if (!arr_ty) {
      return nullptr;
    }

    const unsigned ordinal =
        next_scratch_ordinal(slice_ty, "array_to_slice_arg");
    llvm::AllocaInst* slot =
        AcquireReusableEntryAlloca(func, slice_ty, "array_to_slice_arg", ordinal);
    if (!slot) {
      return nullptr;
    }

    llvm::Type* ptr_field_ty = slice_struct_ty->getElementType(0);
    llvm::Type* len_field_ty = slice_struct_ty->getElementType(1);
    llvm::Value* data_ptr = nullptr;
    if (arr_ty->getNumElements() == 0) {
      data_ptr = llvm::ConstantPointerNull::get(
          llvm::cast<llvm::PointerType>(ptr_field_ty));
    } else {
      llvm::Type* array_ptr_ty = llvm::PointerType::get(arr_ty, 0);
      llvm::Value* typed_array_storage = array_storage;
      if (typed_array_storage->getType() != array_ptr_ty) {
        typed_array_storage = CoerceValue(builder, typed_array_storage, array_ptr_ty);
      }
      llvm::Value* zero = llvm::ConstantInt::get(
          llvm::Type::getInt64Ty(builder->getContext()), 0);
      llvm::Value* elem_ptr =
          builder->CreateGEP(arr_ty, typed_array_storage, {zero, zero});
      data_ptr = CoerceValue(builder, elem_ptr, ptr_field_ty);
    }
    if (!data_ptr) {
      data_ptr = llvm::ConstantPointerNull::get(
          llvm::cast<llvm::PointerType>(ptr_field_ty));
    }

    llvm::Value* len_val =
        llvm::ConstantInt::get(len_field_ty, arr_ty->getNumElements());
    llvm::Value* slice_val = llvm::Constant::getNullValue(slice_struct_ty);
    slice_val = builder->CreateInsertValue(slice_val, data_ptr, {0u});
    slice_val = builder->CreateInsertValue(slice_val, len_val, {1u});
    builder->CreateStore(slice_val, slot);
    return slot;
  };

  if (result_storage_out) {
    *result_storage_out = nullptr;
  }

  auto existing_arg_storage = [&](std::size_t index,
                                  llvm::Type* elem_ty) -> llvm::Value* {
    if (!source_args || index >= source_args->size()) {
      return nullptr;
    }
    llvm::Value* storage = emitter.GetAddressableStorage((*source_args)[index]);
    if (!storage || !storage->getType()->isPointerTy()) {
      return nullptr;
    }
    if (elem_ty) {
      if (llvm::Value* slice_storage =
              materialize_array_slice_storage(storage, elem_ty)) {
        return slice_storage;
      }
      if (LowerCtx* ctx = emitter.GetCurrentCtx()) {
        analysis::TypeRef stored_type = analysis::StripPerm(
            ctx->LookupValueType((*source_args)[index]));
        if (stored_type) {
          if (const auto* ptr =
                  std::get_if<analysis::TypePtr>(&stored_type->node)) {
            stored_type = analysis::StripPerm(ptr->element);
          }
        }
        llvm::Type* stored_llvm_ty =
            stored_type ? emitter.GetLLVMType(stored_type) : nullptr;
        if (stored_llvm_ty && stored_llvm_ty != elem_ty) {
          return nullptr;
        }
      }
      llvm::Type* target_ptr_ty = llvm::PointerType::get(elem_ty, 0);
      if (storage->getType() != target_ptr_ty) {
        storage = CoerceValue(builder, storage, target_ptr_ty);
      }
    }
    return storage;
  };

  auto report_codegen_failure = [&](std::string_view reason,
                                    std::size_t param_index =
                                        static_cast<std::size_t>(-1),
                                    llvm::Value* value = nullptr) -> void {
    if (LowerCtx* ctx = emitter.GetCurrentCtx()) {
      std::cerr << "[uv] call ABI materialization failed"
                << " reason=" << reason;
      if (param_index != static_cast<std::size_t>(-1)) {
        std::cerr << " param_index=" << param_index;
        if (param_index < params.size()) {
          std::cerr << " param_name=" << params[param_index].name;
        }
      }
      if (callee) {
        if (auto* fn = llvm::dyn_cast<llvm::Function>(callee)) {
          std::cerr << " callee=" << fn->getName().str();
        } else if (auto* gv = llvm::dyn_cast<llvm::GlobalValue>(callee)) {
          std::cerr << " callee=" << gv->getName().str();
        }
      }
      if (func) {
        std::cerr << " function=" << func->getName().str();
      }
      if (value && value->getType()) {
        std::string type_text;
        llvm::raw_string_ostream os(type_text);
        value->getType()->print(os);
        std::cerr << " value_type=" << os.str();
      }
      std::cerr << "\n";
      ctx->ReportCodegenFailure();
    }
  };

  auto is_null_pointer_arg = [&](llvm::Value* value) -> bool {
    if (!value || !value->getType()->isPointerTy()) {
      return false;
    }
    if (auto* constant = llvm::dyn_cast<llvm::Constant>(value)) {
      return constant->isNullValue();
    }
    return false;
  };

  auto implicit_panic_out_arg = [&]() -> llvm::Value* {
    llvm::Value* slot = emitter.GetLocal(std::string(kPanicOutName));
    if (!slot) {
      llvm::Function* current_func =
          builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
      llvm::Type* panic_out_ty = emitter.GetLLVMType(PanicOutType());
      if (llvm::Value* hosted = emitter.GetHostedSessionPanicPtr()) {
        if (!current_func || !panic_out_ty) {
          return nullptr;
        }
        llvm::IRBuilder<> entry_builder(&current_func->getEntryBlock(),
                                        current_func->getEntryBlock().begin());
        llvm::AllocaInst* panic_out_slot =
            entry_builder.CreateAlloca(panic_out_ty, nullptr, "__uv_hosted_panic_out");
        llvm::Value* stored = CoerceValue(builder, hosted, panic_out_ty);
        if (!stored && hosted->getType()->isPointerTy() && panic_out_ty->isPointerTy()) {
          stored = builder->CreateBitCast(hosted, panic_out_ty);
        }
        if (!stored) {
          return nullptr;
        }
        builder->CreateStore(stored, panic_out_slot);
        return panic_out_slot;
      }
      llvm::Type* panic_ty = emitter.GetLLVMType(PanicRecordType());
      if (!current_func || !panic_ty || !panic_out_ty) {
        return nullptr;
      }
      llvm::IRBuilder<> entry_builder(&current_func->getEntryBlock(),
                                      current_func->getEntryBlock().begin());
      llvm::AllocaInst* panic_record =
          entry_builder.CreateAlloca(panic_ty, nullptr, "__uv_implicit_panic_record");
      builder->CreateStore(llvm::Constant::getNullValue(panic_ty), panic_record);
      llvm::AllocaInst* panic_out_slot =
          entry_builder.CreateAlloca(panic_out_ty, nullptr, "__uv_implicit_panic_out");
      llvm::Value* stored = CoerceValue(builder, panic_record, panic_out_ty);
      if (!stored && panic_record->getType()->isPointerTy() && panic_out_ty->isPointerTy()) {
        stored = builder->CreateBitCast(panic_record, panic_out_ty);
      }
      if (!stored) {
        return nullptr;
      }
      builder->CreateStore(stored, panic_out_slot);
      return panic_out_slot;
    }
    if (!slot->getType()->isPointerTy()) {
      return nullptr;
    }
    return slot;
  };

  auto recover_pointer_value_arg = [&](std::size_t index,
                                       llvm::Type* target_ty) -> llvm::Value* {
    if (!source_args || index >= source_args->size()) {
      return nullptr;
    }
    llvm::Value* storage = emitter.GetAddressableStorage((*source_args)[index]);
    if (!storage || !storage->getType()->isPointerTy()) {
      return nullptr;
    }
    if (target_ty && storage->getType() != target_ty) {
      storage = CoerceValue(builder, storage, target_ty);
    }
    return storage;
  };

  auto is_slice_param_type = [](analysis::TypeRef type) -> bool {
    type = analysis::StripPerm(type);
    return type && std::holds_alternative<analysis::TypeSlice>(type->node);
  };

  auto source_arg_slice_type = [&](std::size_t index) -> analysis::TypeRef {
    if (!source_args || index >= source_args->size()) {
      return nullptr;
    }
    LowerCtx* ctx = emitter.GetCurrentCtx();
    if (!ctx) {
      return nullptr;
    }
    analysis::TypeRef type = analysis::StripPerm(
        ctx->LookupValueType((*source_args)[index]));
    if (!type) {
      return nullptr;
    }
    if (const auto* ptr = std::get_if<analysis::TypePtr>(&type->node)) {
      analysis::TypeRef element = analysis::StripPerm(ptr->element);
      if (element && std::holds_alternative<analysis::TypeSlice>(element->node)) {
        return element;
      }
    }
    if (std::holds_alternative<analysis::TypeSlice>(type->node)) {
      return type;
    }
    return nullptr;
  };

  auto materialize_slice_arg =
      [&](std::size_t index,
          const analysis::TypeRef& slice_arg_type,
          llvm::Type* target_ty,
          bool prefer_storage,
          llvm::Value* fallback_arg = nullptr) -> llvm::Value* {
    if (!slice_arg_type) {
      return nullptr;
    }
    llvm::Type* slice_ty = emitter.GetLLVMType(slice_arg_type);
    if (!slice_ty) {
      return nullptr;
    }
    llvm::Value* storage = existing_arg_storage(index, slice_ty);
    if (!storage && fallback_arg && fallback_arg->getType()->isPointerTy()) {
      storage = materialize_array_slice_storage(fallback_arg, slice_ty);
    }
    if (!storage) {
      return nullptr;
    }
    if (prefer_storage || (target_ty && target_ty->isPointerTy())) {
      return storage;
    }
    if (!storage->getType()->isPointerTy()) {
      return storage;
    }
    llvm::LoadInst* loaded = builder->CreateLoad(slice_ty, storage);
    loaded->setAlignment(llvm::Align(1));
    return loaded;
  };

  auto source_arg_value_type = [&](std::size_t index) -> analysis::TypeRef {
    if (!source_args || index >= source_args->size()) {
      return nullptr;
    }
    LowerCtx* ctx = emitter.GetCurrentCtx();
    if (!ctx) {
      return nullptr;
    }
    analysis::TypeRef type =
        analysis::StripPerm(ctx->LookupValueType((*source_args)[index]));
    if (!type) {
      return nullptr;
    }
    if (const auto* ptr = std::get_if<analysis::TypePtr>(&type->node)) {
      return analysis::StripPerm(ptr->element);
    }
    return type;
  };

  auto is_function_value_type = [&](analysis::TypeRef type) -> bool {
    type = analysis::StripPerm(type);
    if (!type) {
      return false;
    }
    LowerCtx* ctx = emitter.GetCurrentCtx();
    const analysis::ScopeContext scope = BuildScope(ctx);
    if (analysis::TypeRef resolved =
            emit_detail::ResolveAliasTypeInScope(scope, type)) {
      type = analysis::StripPerm(resolved);
      if (!type) {
        type = resolved;
      }
    }
    return type && std::holds_alternative<analysis::TypeFunc>(type->node);
  };

  auto materialize_mismatched_pointer_arg =
      [&](std::size_t index,
          llvm::Value* ptr_arg,
          llvm::Type* target_elem_ty,
          const analysis::TypeRef& target_source_type,
          std::string_view scratch_name) -> llvm::Value* {
    if (!ptr_arg || !ptr_arg->getType()->isPointerTy() || !target_elem_ty) {
      return nullptr;
    }

    llvm::Type* source_elem_ty = nullptr;
    analysis::TypeRef source_type = source_arg_value_type(index);
    if (source_type) {
      source_elem_ty = emitter.GetLLVMType(source_type);
    }
    if (!source_elem_ty) {
      if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(ptr_arg)) {
        source_elem_ty = alloca->getAllocatedType();
      }
    }
    if (!source_elem_ty || source_elem_ty == target_elem_ty) {
      return nullptr;
    }

    llvm::Value* typed_source = ptr_arg;
    llvm::Type* source_ptr_ty = llvm::PointerType::get(source_elem_ty, 0);
    if (typed_source->getType() != source_ptr_ty) {
      typed_source = CoerceValue(builder, typed_source, source_ptr_ty);
    }
    if (!typed_source) {
      report_codegen_failure("byref-source-pointer-type", index, ptr_arg);
      return nullptr;
    }

    llvm::Value* loaded = builder->CreateLoad(source_elem_ty, typed_source);
    llvm::Value* stored =
        emit_detail::CoerceToTyped(emitter,
                                   builder,
                                   loaded,
                                   target_elem_ty,
                                   source_type,
                                   target_source_type);
    if (!stored) {
      stored = CoerceValue(builder, loaded, target_elem_ty);
    }
    if (!stored) {
      report_codegen_failure("byref-pointer-coercion", index, ptr_arg);
      return nullptr;
    }

    const unsigned ordinal = next_scratch_ordinal(target_elem_ty, scratch_name);
    llvm::AllocaInst* slot =
        AcquireReusableEntryAlloca(func, target_elem_ty, scratch_name, ordinal);
    if (!slot) {
      report_codegen_failure("byref-pointer-scratch-allocation", index, ptr_arg);
      return nullptr;
    }
    builder->CreateStore(stored, slot);
    return slot;
  };

  // Handle sret parameter
  if (abi.has_sret) {
    llvm::Type* ret_ty = emitter.GetLLVMType(ret_type);
    if (preferred_result_storage && ret_ty &&
        preferred_result_storage->getType()->isPointerTy()) {
      llvm::Type* target_ptr_ty = llvm::PointerType::get(ret_ty, 0);
      sret_alloca = preferred_result_storage;
      if (sret_alloca->getType() != target_ptr_ty) {
        sret_alloca = CoerceValue(builder, sret_alloca, target_ptr_ty);
      }
    } else if (result_storage_out) {
      // Published aggregate results must not alias other still-live call
      // results, but they can reuse previously released aggregate temp
      // storage once the prior owner is dead.
      sret_alloca = emitter.AcquireReusableAggregateStorage(func, ret_ty, "sret");
    } else {
      const unsigned ordinal = next_scratch_ordinal(ret_ty, "sret");
      sret_alloca = AcquireReusableEntryAlloca(func, ret_ty, "sret", ordinal);
    }
    call_args[0] = sret_alloca;
    EnsureAllocaABIAlignment(sret_alloca, ret_ty);
    if (result_storage_out) {
      *result_storage_out = sret_alloca;
    }
  }

  // Map source arguments onto ABI parameters. Hidden panic-out participates in
  // the lowered procedure signature, but it is not a source argument. Keeping a
  // separate source-argument cursor prevents hidden panic-out from shifting any
  // ordinary argument that is passed later in the platform ABI stack area.
  std::size_t source_arg_index = 0;

  // Map arguments according to ABI
  for (std::size_t i = 0; i < params.size(); ++i) {
    const bool is_panic_out_param =
        params[i].name == std::string(kPanicOutName);
    if (i >= abi.param_indices.size()) {
      break;
    }
    if (!abi.param_indices[i].has_value()) {
      if (!is_panic_out_param && source_arg_index < args.size()) {
        ++source_arg_index;
      }
      continue;
    }

    unsigned idx = *abi.param_indices[i];
    if (idx >= call_args.size()) {
      continue;
    }

    std::size_t arg_lookup_index = source_arg_index;
    llvm::Value* arg = nullptr;
    if (is_panic_out_param) {
      arg = implicit_panic_out_arg();
    } else {
      arg = source_arg_index < args.size() ? args[source_arg_index] : nullptr;
      arg_lookup_index = source_arg_index;
      ++source_arg_index;
    }
    if (!arg) {
      continue;
    }

    const ABIArgCarrierKind carrier =
        i < abi.param_carriers.size() ? abi.param_carriers[i]
                                      : ABIArgCarrierKind::Direct;

    if (abi.param_kinds[i] == PassKind::ByRef) {
      // Need to pass by reference - create temporary if not already a pointer
      llvm::Type* elem_ty = emitter.GetLLVMType(params[i].type);
      bool materialized_slice_storage = false;
      if (analysis::TypeRef slice_arg_type = is_slice_param_type(params[i].type)
                                                ? params[i].type
                                                : source_arg_slice_type(arg_lookup_index)) {
        if (llvm::Value* slice_storage =
                materialize_slice_arg(arg_lookup_index,
                                      slice_arg_type,
                                      abi.param_types[idx],
                                      /*prefer_storage=*/true,
                                      arg)) {
          arg = slice_storage;
          materialized_slice_storage = true;
        }
      }
      if (!materialized_slice_storage && is_function_value_type(params[i].type)) {
        if (llvm::Function* fn = emit_detail::FunctionFromLLVMValue(arg)) {
          const unsigned ordinal =
              next_scratch_ordinal(elem_ty, "function_ref_arg");
          llvm::AllocaInst* slot =
              AcquireReusableEntryAlloca(func, elem_ty, "function_ref_arg", ordinal);
          if (!slot) {
            report_codegen_failure("function-ref-scratch-allocation", i, arg);
            continue;
          }
          llvm::Value* stored = CoerceValue(builder, fn, elem_ty);
          if (!stored) {
            report_codegen_failure("function-ref-coercion", i, arg);
            continue;
          }
          builder->CreateStore(stored, slot);
          call_args[idx] = slot;
          continue;
        }
      }
      if (is_panic_out_param) {
        llvm::Type* target_ty = abi.param_types[idx];
        if (target_ty && arg->getType() != target_ty) {
          arg = CoerceValue(builder, arg, target_ty);
        }
        call_args[idx] = arg;
        continue;
      }
      if (!materialized_slice_storage && IsRawPtrType(params[i].type) &&
          elem_ty && elem_ty->isPointerTy()) {
        llvm::Value* raw_ptr_value = nullptr;
        const analysis::TypeRef source_type =
            source_arg_value_type(arg_lookup_index);
        if (IsRawPtrType(source_type)) {
          if (llvm::Value* storage = existing_arg_storage(arg_lookup_index, elem_ty)) {
            call_args[idx] = storage;
            continue;
          }
          raw_ptr_value = arg;
        } else if (arg->getType()->isPointerTy() && !is_null_pointer_arg(arg)) {
          raw_ptr_value = arg;
        }
        if (raw_ptr_value) {
          if (raw_ptr_value->getType() != elem_ty) {
            raw_ptr_value = CoerceValue(builder, raw_ptr_value, elem_ty);
          }
          if (!raw_ptr_value) {
            report_codegen_failure("rawptr-byref-address-coercion", i, arg);
            continue;
          }
          const unsigned ordinal = next_scratch_ordinal(elem_ty, "rawptr_arg");
          llvm::AllocaInst* slot =
              AcquireReusableEntryAlloca(func, elem_ty, "rawptr_arg", ordinal);
          if (!slot) {
            report_codegen_failure("rawptr-byref-scratch-allocation", i, arg);
            continue;
          }
          builder->CreateStore(raw_ptr_value, slot);
          call_args[idx] = slot;
          continue;
        }
      }
      if (!arg->getType()->isPointerTy() || is_null_pointer_arg(arg)) {
        llvm::Value* storage = existing_arg_storage(arg_lookup_index, elem_ty);
        if (storage) {
          call_args[idx] = storage;
          continue;
        }
        if (arg->getType()->isPointerTy()) {
          report_codegen_failure("byref-null-pointer", i, arg);
          continue;
        }
        const unsigned ordinal = next_scratch_ordinal(elem_ty, "byref_arg");
        llvm::AllocaInst* slot =
            AcquireReusableEntryAlloca(func, elem_ty, "byref_arg", ordinal);
        if (slot) {
          llvm::Value* stored = CoerceValue(builder, arg, elem_ty);
          builder->CreateStore(stored, slot);
          call_args[idx] = slot;
        } else {
          report_codegen_failure("byref-scratch-allocation", i, arg);
        }
        continue;
      }
      if (!materialized_slice_storage) {
        if (llvm::Value* storage =
                materialize_mismatched_pointer_arg(arg_lookup_index,
                                                   arg,
                                                   elem_ty,
                                                   params[i].type,
                                                   "byref_arg")) {
          call_args[idx] = storage;
          continue;
        }
      }
      llvm::Type* target_ty = abi.param_types[idx];
      if (target_ty && arg->getType() != target_ty) {
        arg = CoerceValue(builder, arg, target_ty);
      }
      call_args[idx] = arg;
      continue;
    }

    if (carrier == ABIArgCarrierKind::Indirect) {
      llvm::Type* elem_ty = emitter.GetLLVMType(params[i].type);
      llvm::Value* ptr_arg = arg;
      bool materialized_slice_storage = false;
      if (analysis::TypeRef slice_arg_type = is_slice_param_type(params[i].type)
                                                ? params[i].type
                                                : source_arg_slice_type(arg_lookup_index)) {
        if (llvm::Value* slice_storage =
                materialize_slice_arg(arg_lookup_index,
                                      slice_arg_type,
                                      abi.param_types[idx],
                                      /*prefer_storage=*/true,
                                      arg)) {
          ptr_arg = slice_storage;
          materialized_slice_storage = true;
        }
      }
      if (!ptr_arg->getType()->isPointerTy() || is_null_pointer_arg(ptr_arg)) {
        if (llvm::Value* storage = existing_arg_storage(arg_lookup_index, elem_ty)) {
          ptr_arg = storage;
        } else if (ptr_arg->getType()->isPointerTy()) {
          report_codegen_failure("indirect-null-pointer", i, ptr_arg);
          continue;
        } else {
          const unsigned ordinal = next_scratch_ordinal(elem_ty, "indirect_arg");
          llvm::AllocaInst* slot =
              AcquireReusableEntryAlloca(func, elem_ty, "indirect_arg", ordinal);
          if (slot) {
            llvm::Value* stored = CoerceValue(builder, arg, elem_ty);
            builder->CreateStore(stored, slot);
            ptr_arg = slot;
          } else {
            report_codegen_failure("indirect-scratch-allocation", i, arg);
            continue;
          }
        }
      }
      if (!materialized_slice_storage) {
        if (llvm::Value* storage =
                materialize_mismatched_pointer_arg(arg_lookup_index,
                                                   ptr_arg,
                                                   elem_ty,
                                                   params[i].type,
                                                   "indirect_arg")) {
          ptr_arg = storage;
        }
      }
      llvm::Type* target_ty = abi.param_types[idx];
      if (target_ty && ptr_arg->getType() != target_ty) {
        ptr_arg = CoerceValue(builder, ptr_arg, target_ty);
      }
      call_args[idx] = ptr_arg;
      continue;
    }

    // By value
    llvm::Type* target_ty = abi.param_types[idx];
    analysis::TypeRef slice_arg_type = nullptr;
    if (is_slice_param_type(params[i].type)) {
      slice_arg_type = params[i].type;
    } else {
      slice_arg_type = source_arg_slice_type(arg_lookup_index);
    }
    if (llvm::Value* slice_arg =
            materialize_slice_arg(arg_lookup_index,
                                  slice_arg_type,
                                  target_ty,
                                  /*prefer_storage=*/false,
                                  arg)) {
      arg = slice_arg;
    }
    if (target_ty && !target_ty->isPointerTy() &&
        arg->getType()->isPointerTy()) {
      llvm::Value* storage = existing_arg_storage(arg_lookup_index, target_ty);
      if (!storage) {
        storage = arg;
      }
      llvm::Type* target_ptr_ty = llvm::PointerType::get(target_ty, 0);
      if (storage->getType() != target_ptr_ty) {
        storage = CoerceValue(builder, storage, target_ptr_ty);
      }
      if (storage) {
        arg = builder->CreateLoad(target_ty, storage);
      }
    }
    const analysis::TypeRef source_value_type =
        source_arg_value_type(arg_lookup_index);
    const bool is_pointer_value_arg =
        (IsRawPtrType(params[i].type) && IsRawPtrType(source_value_type)) ||
        (is_function_value_type(params[i].type) &&
         is_function_value_type(source_value_type));
    if (target_ty && target_ty->isPointerTy() && is_pointer_value_arg &&
        arg->getType()->isPointerTy()) {
      if (llvm::Value* storage = existing_arg_storage(arg_lookup_index, target_ty)) {
        arg = builder->CreateLoad(target_ty, storage);
      }
    }
    if (IsValidPtrType(params[i].type) && is_null_pointer_arg(arg)) {
      if (llvm::Value* recovered = recover_pointer_value_arg(arg_lookup_index, target_ty)) {
        arg = recovered;
      } else {
        report_codegen_failure("valid-pointer-null-value", i, arg);
        continue;
      }
    }
    if (target_ty && arg->getType() != target_ty) {
      arg = CoerceValue(builder, arg, target_ty);
    }
    call_args[idx] = arg;
  }

  // Every emitted LLVM parameter must be materialized explicitly. If a slot is
  // still missing here, the lowered call signature is undefined.
  for (std::size_t i = 0; i < call_args.size(); ++i) {
    if (!call_args[i]) {
      for (std::size_t param_index = 0; param_index < params.size(); ++param_index) {
        if (param_index >= abi.param_indices.size() ||
            !abi.param_indices[param_index].has_value() ||
            *abi.param_indices[param_index] != i ||
            params[param_index].name != std::string(kPanicOutName)) {
          continue;
        }
        call_args[i] = implicit_panic_out_arg();
        break;
      }
    }
    if (!call_args[i]) {
      report_codegen_failure("missing-call-arg", i);
      return nullptr;
    }
  }

  auto release_consumed_move_arg_temps = [&]() -> void {
    if (!source_args) {
      return;
    }

    const std::size_t released_count =
        std::min(source_args->size(), params.size());
    for (std::size_t i = 0; i < released_count; ++i) {
      if (!params[i].mode.has_value()) {
        continue;
      }

      emitter.ReleaseMoveConsumedStorage((*source_args)[i]);
    }
  };

  llvm::Instruction* call_like_inst = nullptr;
  llvm::Value* direct_result = nullptr;
  llvm::BasicBlock* normal_block = nullptr;
  llvm::BasicBlock* unwind_block = nullptr;
  unsigned call_conv = llvm::CallingConv::C;
  if (call_conv_override.has_value()) {
    call_conv = *call_conv_override;
  }
  if (auto* callee_fn = llvm::dyn_cast<llvm::Function>(callee)) {
    call_conv = callee_fn->getCallingConv();
  }

  if (ffi_import_boundary) {
    if (ffi_import_catch) {
      SPEC_RULE("CodeGen-UnwindCatch-Import");
    } else {
      SPEC_RULE("CodeGen-UnwindAbort-Import");
    }

    llvm::Type* i32_ty = llvm::Type::getInt32Ty(emitter.GetContext());
    llvm::FunctionType* personality_ty =
        llvm::FunctionType::get(i32_ty, /*isVarArg=*/true);
    llvm::FunctionCallee personality =
        emitter.GetModule().getOrInsertFunction(
            UnwindPersonalitySymbolForModule(emitter.GetModule()),
            personality_ty);
    func->setPersonalityFn(llvm::cast<llvm::Constant>(personality.getCallee()));

    normal_block = llvm::BasicBlock::Create(emitter.GetContext(),
                                            "ffi.invoke.cont",
                                            func);
    unwind_block = llvm::BasicBlock::Create(emitter.GetContext(),
                                            "ffi.invoke.unwind",
                                            func);

    llvm::InvokeInst* invoke_inst =
        builder->CreateInvoke(abi.func_type,
                              callee,
                              normal_block,
                              unwind_block,
                              call_args);
    invoke_inst->setCallingConv(call_conv);
    call_like_inst = invoke_inst;

    builder->SetInsertPoint(unwind_block);

    llvm::Type* i8_ty = llvm::Type::getInt8Ty(emitter.GetContext());
    auto* i8_ptr_ty = llvm::PointerType::get(i8_ty, 0);
    auto* lpad_ty = llvm::StructType::get(i8_ptr_ty, i32_ty);
    llvm::LandingPadInst* landing_pad =
        builder->CreateLandingPad(lpad_ty, 1, "ffi_lpad");
    landing_pad->setCleanup(true);
    landing_pad->addClause(llvm::ConstantPointerNull::get(i8_ptr_ty));

    if (ffi_import_catch) {
      const std::uint16_t panic_code = PanicCode(PanicReason::Other);
      StorePanicRecord(emitter, builder, panic_code);

      if (abi.has_sret && sret_alloca) {
        llvm::Type* ret_ty = emitter.GetLLVMType(ret_type);
        if (ret_ty) {
          builder->CreateStore(llvm::Constant::getNullValue(ret_ty), sret_alloca);
        }
      }

      builder->CreateBr(normal_block);
    } else {
      const std::uint16_t panic_code = PanicCode(PanicReason::Other);
      const std::string panic_sym = PanicSym();
      llvm::Function* panic_fn = emitter.GetModule().getFunction(panic_sym);
      if (!panic_fn) {
        llvm::FunctionType* panic_ty = llvm::FunctionType::get(
            llvm::Type::getVoidTy(emitter.GetContext()),
            {i32_ty},
            false);
        panic_fn = llvm::Function::Create(
            panic_ty,
            llvm::GlobalValue::ExternalLinkage,
            panic_sym,
            &emitter.GetModule());
        panic_fn->setCallingConv(llvm::CallingConv::C);
      }
      llvm::Value* panic_arg = llvm::ConstantInt::get(i32_ty, panic_code);
      builder->CreateCall(panic_fn->getFunctionType(), panic_fn, {panic_arg});
      builder->CreateUnreachable();
    }

    builder->SetInsertPoint(normal_block);

    if (!invoke_inst->getType()->isVoidTy()) {
      if (ffi_import_catch) {
        llvm::PHINode* result_phi =
            builder->CreatePHI(invoke_inst->getType(), 2, "ffi_invoke_result");
        result_phi->addIncoming(invoke_inst, invoke_inst->getParent());
        result_phi->addIncoming(llvm::Constant::getNullValue(invoke_inst->getType()),
                                unwind_block);
        direct_result = result_phi;
      } else {
        direct_result = invoke_inst;
      }
    }
  } else {
    llvm::CallInst* call_inst =
        builder->CreateCall(abi.func_type, callee, call_args);
    call_inst->setCallingConv(call_conv);
    call_like_inst = call_inst;
    if (!call_inst->getType()->isVoidTy()) {
      direct_result = call_inst;
    }
  }

  release_consumed_move_arg_temps();

  // Handle return value
  if (abi.has_sret && sret_alloca) {
    if (!result_storage_out) {
      return builder->CreateLoad(emitter.GetLLVMType(ret_type), sret_alloca);
    }
    return nullptr;
  }

  if (direct_result) {
    if (ret_type) {
      llvm::Type* expected = emitter.GetLLVMType(ret_type);
      if (expected && direct_result->getType() != expected) {
        direct_result = CoerceValue(builder, direct_result, expected);
      }
    }
    return direct_result;
  }

  if (call_like_inst && !call_like_inst->getType()->isVoidTy()) {
    return call_like_inst;
  }

  if (ret_type) {
    return llvm::Constant::getNullValue(emitter.GetLLVMType(ret_type));
  }

  return nullptr;
}

// -----------------------------------------------------------------------------
// Calling Convention
// -----------------------------------------------------------------------------

unsigned GetUltravioletCallingConv() {
  // Ultraviolet uses C calling convention on Windows
  return llvm::CallingConv::C;
}

bool IsCCallingConv(std::string_view symbol) {
  // Check for extern "C" symbols
  return IsExternC(symbol);
}

bool IsExternC(std::string_view symbol) {
  // Extern C symbols don't have mangled names
  // Check if symbol starts with mangling prefix
  if (symbol.empty()) {
    return false;
  }
  // If it starts with underscore and a capital letter, likely extern C
  // This is a heuristic - actual determination would need symbol table lookup
  if (symbol[0] == '_' && symbol.size() > 1) {
    char second = symbol[1];
    // Mangled names typically start with _C or _Z
    if (second == 'C' || second == 'Z') {
      return false;  // Mangled
    }
    return true;  // Likely extern C
  }
  // Simple names are extern C
  return symbol.find("::") == std::string_view::npos;
}

// -----------------------------------------------------------------------------
// Procedure ABI Lowering
// -----------------------------------------------------------------------------

ABICallResult ComputeCallABI(LLVMEmitter& emitter,
                             const std::vector<IRParam>& params,
                             analysis::TypeRef ret_type,
                             bool use_c_abi_aggregate_sret,
                             bool foreign_boundary_mode_independent) {
  SPEC_RULE("LLVMCall-ByValue");
  SPEC_RULE("LLVMCall-SRet");

  ABICallResult result;
  LowerCtx* current_ctx = emitter.GetCurrentCtx();
  const bool use_cache = !core::Conformance::Enabled();
  const CallABICacheKey cache_key =
      use_cache
          ? MakeCallABICacheKey(emitter,
                                params,
                                ret_type,
                                use_c_abi_aggregate_sret,
                                foreign_boundary_mode_independent)
          : CallABICacheKey{};
  if (use_cache)
  {
    CallABICache& cache = call_abi_caches[&emitter];
    const auto cached = cache.find(cache_key);
    if (cached != cache.end())
    {
      return cached->second;
    }
  }
  const analysis::ScopeContext& scope = BuildScope(current_ctx);
  auto invalidate = [&]() -> ABICallResult {
    if (current_ctx) {
      current_ctx->ReportCodegenFailure();
    }
    return result;
  };

  std::vector<std::pair<std::optional<analysis::ParamMode>, analysis::TypeRef>>
      abi_params;
  abi_params.reserve(params.size());
  for (const auto& param : params) {
    abi_params.push_back({param.mode, param.type});
  }

  const auto param_policy =
      foreign_boundary_mode_independent
          ? ABIParamPolicy::ForeignBoundary
          : ABIParamPolicy::ModeAware;
  const auto call_info = ABICall(scope, abi_params, ret_type, param_policy);
  if (!call_info.has_value()) {
    SPEC_RULE("LLVMCall-Err");
    llvm::Function* debug_func = nullptr;
    auto* debug_builder =
        static_cast<llvm::IRBuilder<>*>(emitter.GetBuilderRaw());
    if (debug_builder && debug_builder->GetInsertBlock()) {
      debug_func = debug_builder->GetInsertBlock()->getParent();
    }
    std::cerr << "[uv] ABICall failed in "
              << (debug_func ? debug_func->getName().str()
                             : std::string("<no-func>"))
              << " ret_null=" << (ret_type ? 0 : 1)
              << " param_count=" << params.size()
              << " ret_type="
              << (ret_type ? analysis::TypeToString(ret_type)
                           : std::string("<null>"))
              << "\n";
    for (std::size_t i = 0; i < params.size(); ++i) {
      const int mode_tag =
          params[i].mode.has_value() ? static_cast<int>(*params[i].mode) : -1;
      std::cerr << "[uv]   param[" << i << "] mode=" << mode_tag
                << " type_null=" << (params[i].type ? 0 : 1)
                << " type="
                << (params[i].type ? analysis::TypeToString(params[i].type)
                                   : std::string("<null>"))
                << "\n";
    }
    if (current_ctx) {
      current_ctx->ReportCodegenFailure();
    }
    return result;
  }

  result.param_kinds = call_info->param_kinds;
  result.param_carriers.assign(params.size(), ABIArgCarrierKind::Direct);

  const bool win64_foreign_abi =
      use_c_abi_aggregate_sret &&
      emitter.GetTargetProfile() == project::TargetProfile::X86_64Win64;
  const bool foreign_c_abi = use_c_abi_aggregate_sret;

  bool c_abi_sret = false;
  if (win64_foreign_abi && ret_type) {
    const auto ret_size = ::ultraviolet::analysis::layout::SizeOf(scope, ret_type);
    llvm::Type* ret_ll = emitter.GetLLVMType(ret_type);
    if (ret_size.has_value() && *ret_size > 0 &&
        IsForeignAbiAggregateLLVMType(ret_ll) &&
        !IsWin64CAbiAggregateDirectSize(*ret_size)) {
      c_abi_sret = true;
    }
  }

  result.has_sret = call_info->has_sret || c_abi_sret;
  llvm::Type* sret_storage_ty =
      ret_type ? emitter.GetLLVMType(ret_type) : nullptr;

  if (result.has_sret) {
    SPEC_RULE("LLVMRetLower-SRet");
    if (!sret_storage_ty || sret_storage_ty->isVoidTy()) {
      SPEC_RULE("LLVMRetLower-Err");
      return invalidate();
    }
    result.ret_type = llvm::Type::getVoidTy(emitter.GetContext());
  } else {
    const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, ret_type);
    if (!size.has_value()) {
      SPEC_RULE("LLVMRetLower-Err");
      return invalidate();
    }
    if (*size == 0) {
      SPEC_RULE("LLVMRetLower-ByValue-ZST");
      result.ret_type = llvm::Type::getVoidTy(emitter.GetContext());
    } else {
      SPEC_RULE("LLVMRetLower-ByValue");
      result.ret_type = emitter.GetLLVMType(ret_type);
      if (win64_foreign_abi && ret_type) {
        const auto ret_size =
            ::ultraviolet::analysis::layout::SizeOf(scope, ret_type);
        if (ret_size.has_value() && *ret_size > 0 &&
            IsForeignAbiAggregateLLVMType(result.ret_type)) {
          if (llvm::Type* carrier =
                  Win64CAbiDirectAggregateCarrier(emitter.GetContext(),
                                                  *ret_size)) {
            result.ret_type = carrier;
          } else {
            result.ret_type = llvm::Type::getVoidTy(emitter.GetContext());
          }
        }
      }
      if (foreign_c_abi && ret_type &&
          emitter.GetTargetProfile() == project::TargetProfile::X86_64SysV &&
          IsForeignAbiAggregateLLVMType(result.ret_type)) {
        const auto ret_size =
            ::ultraviolet::analysis::layout::SizeOf(scope, ret_type);
        if (ret_size.has_value() && *ret_size > 0) {
          if (llvm::Type* carrier = ForeignAbiDirectAggregateCarrier(
                  emitter.GetTargetProfile(),
                  emitter.GetContext(),
                  result.ret_type,
                  *ret_size)) {
            result.ret_type = carrier;
          }
        }
      }
      if (!result.ret_type) {
        SPEC_RULE("LLVMRetLower-Err");
        return invalidate();
      }
    }
  }

  result.param_indices.assign(params.size(), std::nullopt);

  if (result.has_sret) {
    result.param_types.push_back(emitter.GetOpaquePtr());
    result.llvm_param_attrs.push_back(
        ComputeSRetParamAttrs(ret_type, sret_storage_ty, current_ctx));
  }

  unsigned llvm_index = result.has_sret ? 1u : 0u;
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i >= result.param_kinds.size()) {
      break;
    }

    auto kind = result.param_kinds[i];
    if (IsClosureEnvParam(params[i])) {
      kind = PassKind::ByValue;
      result.param_kinds[i] = kind;
    }
    if (kind == PassKind::ByRef) {
      SPEC_RULE("LLVMArgLower-ByRef");
      result.param_types.push_back(emitter.GetOpaquePtr());
      result.llvm_param_attrs.push_back(
          ComputeLoweredParamAttrs(params[i].name,
                                   params[i].type,
                                   kind,
                                   current_ctx));
      result.param_indices[i] = llvm_index++;
      result.param_carriers[i] = ABIArgCarrierKind::Indirect;
      continue;
    }

    const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, params[i].type);
    if (!size.has_value()) {
      SPEC_RULE("LLVMArgLower-Err");
      return invalidate();
    }
    if (*size == 0) {
      result.param_indices[i] = std::nullopt;
      continue;
    }

    llvm::Type* llvm_ty = emitter.GetLLVMType(params[i].type);
    if (win64_foreign_abi && llvm_ty &&
        IsForeignAbiAggregateLLVMType(llvm_ty)) {
      if (llvm::Type* carrier =
              Win64CAbiDirectAggregateCarrier(emitter.GetContext(), *size)) {
        llvm_ty = carrier;
      } else {
        llvm_ty = emitter.GetOpaquePtr();
        result.param_carriers[i] = ABIArgCarrierKind::Indirect;
      }
    }
    if (foreign_c_abi &&
        emitter.GetTargetProfile() == project::TargetProfile::X86_64SysV &&
        llvm_ty && IsForeignAbiAggregateLLVMType(llvm_ty)) {
      if (llvm::Type* carrier = ForeignAbiDirectAggregateCarrier(
              emitter.GetTargetProfile(),
              emitter.GetContext(),
              llvm_ty,
              *size)) {
        llvm_ty = carrier;
      }
    }
    if (!llvm_ty) {
      SPEC_RULE("LLVMArgLower-Err");
      return invalidate();
    }
    result.param_types.push_back(llvm_ty);
    result.llvm_param_attrs.push_back(
        ComputeLoweredParamAttrs(params[i].name, params[i].type, kind, current_ctx));
    result.param_indices[i] = llvm_index++;
  }

  result.func_type =
      llvm::FunctionType::get(result.ret_type, result.param_types, false);
  result.valid = true;
  if (use_cache) {
    call_abi_caches[&emitter].emplace(cache_key, result);
  }
  return result;
}

std::vector<IRParam> BuildProcABIParams(LLVMEmitter& emitter,
                                        const std::string& symbol,
                                        const std::vector<IRParam>& params) {
  std::vector<IRParam> augmented = params;
  if (emitter.RequiresHostedEnvParam(symbol) &&
      !emit_detail::HasLeadingHostedEnvParam(augmented))
  {
    augmented.insert(augmented.begin(), HostedEnvParam());
  }
  LowerCtx* current_ctx = emitter.GetCurrentCtx();
  const bool needs_panic_out =
      current_ctx ? current_ctx->NeedsPanicOutForSymbol(symbol)
                   : NeedsPanicOut(symbol);
  if (needs_panic_out &&
      (augmented.empty() || augmented.back().name != std::string(kPanicOutName)))
  {
    augmented.push_back(PanicOutParam());
  }
  return augmented;
}

ABICallResult ComputeProcABI(
      LLVMEmitter& emitter,
      const std::string &symbol,
      const std::vector<IRParam> &params,
      analysis::TypeRef ret_type,
      bool use_c_abi_aggregate_sret,
      bool foreign_boundary_mode_independent)
  {
    std::vector<IRParam> augmented = BuildProcABIParams(emitter, symbol, params);
    LowerCtx* current_ctx = emitter.GetCurrentCtx();

    analysis::TypeRef abi_ret = ret_type;
    if (const LowerCtx::AsyncProcInfo *async_info =
            current_ctx ? current_ctx->LookupAsyncProc(symbol) : nullptr;
        async_info && async_info->is_resume &&
        emit_detail::HasNamedParam(augmented, kAsyncOutParamName))
    {
      abi_ret = analysis::MakeTypePrim("()");
    }

    if (!use_c_abi_aggregate_sret && current_ctx)
    {
      if (const auto *sig = current_ctx->LookupProcSig(symbol);
          sig && sig->abi.has_value())
      {
        use_c_abi_aggregate_sret = true;
        foreign_boundary_mode_independent = true;
      }
    }

    return ComputeCallABI(emitter,
                          augmented,
                          abi_ret,
                          use_c_abi_aggregate_sret,
                          foreign_boundary_mode_independent);
  }
}  // namespace ultraviolet::codegen
