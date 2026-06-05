// =============================================================================
// File: 05_codegen/llvm/emit/ir/ops/alloc.cpp
// Canonical owner for LLVM IR allocation instruction lowering.
// =============================================================================
#include "../../ir_instruction_visitor.h"

#include "00_core/assert_spec.h"

#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace ultraviolet::codegen::emit_detail {

namespace {

void RecordRegionAllocConformanceOnce(std::once_flag& flag,
                                      std::string_view rule_id,
                                      std::string payload)
{
  if (!core::Conformance::Enabled())
  {
    return;
  }
  std::call_once(flag, [rule_id = std::string(rule_id),
                        payload = std::move(payload)]() {
    core::Conformance::Record(rule_id, std::nullopt, payload);
  });
}

void AppendRegionAllocPayloadField(std::string& payload,
                                   std::string_view key,
                                   std::string_view value)
{
  if (!payload.empty())
  {
    payload += ';';
  }
  payload += key;
  payload += '=';
  payload += value;
}

void AppendRegionAllocPayloadField(std::string& payload,
                                   std::string_view key,
                                   const char* value)
{
  AppendRegionAllocPayloadField(payload,
                                key,
                                std::string_view(value ? value : "-"));
}

void AppendRegionAllocPayloadField(std::string& payload,
                                   std::string_view key,
                                   bool value)
{
  AppendRegionAllocPayloadField(payload,
                                key,
                                std::string_view(value ? "true" : "false"));
}

std::string RegionAllocResolveEntryPayload()
{
  std::string payload;
  AppendRegionAllocPayloadField(payload, "operation", "ResolveEntry");
  AppendRegionAllocPayloadField(payload, "source", "IRAllocEmission");
  AppendRegionAllocPayloadField(payload, "runtime_symbol", "Region::alloc");
  AppendRegionAllocPayloadField(payload, "runtime_resolver", "uv_region_resolve_entry");
  AppendRegionAllocPayloadField(payload, "target_source", "IRAlloc.region");
  AppendRegionAllocPayloadField(payload, "active_fast_path", true);
  AppendRegionAllocPayloadField(payload, "nearest_live_target_scan", true);
  return payload;
}

std::string RegionAllocResolveTargetAndTagPayload()
{
  std::string payload;
  AppendRegionAllocPayloadField(payload, "operation", "ResolveTargetResolveTag");
  AppendRegionAllocPayloadField(payload, "source", "IRAllocEmission");
  AppendRegionAllocPayloadField(payload, "runtime_symbol", "Region::alloc");
  AppendRegionAllocPayloadField(payload, "target", "UVRegion.handle");
  AppendRegionAllocPayloadField(payload, "tag_source", "UVRegionEntry.tag");
  AppendRegionAllocPayloadField(payload, "resolver", "uv_region_resolve_entry");
  return payload;
}

std::string FreshRuntimeAddressPayload()
{
  std::string payload;
  AppendRegionAllocPayloadField(payload, "operation", "FreshAddr");
  AppendRegionAllocPayloadField(payload, "source", "IRAllocEmission");
  AppendRegionAllocPayloadField(payload, "runtime_symbol", "Region::alloc");
  AppendRegionAllocPayloadField(payload, "allocator", "uv_region_alloc_arena");
  AppendRegionAllocPayloadField(payload, "arena_append", true);
  AppendRegionAllocPayloadField(payload, "addr_tags_before_write", "unmapped");
  AppendRegionAllocPayloadField(payload, "reuse_guard", "arena_bump_and_free_quarantine");
  return payload;
}

std::string RuntimeAddressTagsPayload()
{
  std::string payload;
  AppendRegionAllocPayloadField(payload, "operation", "AddrTags");
  AppendRegionAllocPayloadField(payload, "source", "IRAllocEmission");
  AppendRegionAllocPayloadField(payload, "runtime_symbol", "Region::alloc");
  AppendRegionAllocPayloadField(payload, "runtime_map", "UVRegionState.addr_tags");
  AppendRegionAllocPayloadField(payload, "tag_set_by", "uv_region_addr_tag_set");
  AppendRegionAllocPayloadField(payload, "tag_variant", "RegionTag");
  return payload;
}

std::once_flag g_resolve_runtime_region_entry_obligation_once;
std::once_flag g_resolve_runtime_region_target_tag_obligation_once;
std::once_flag g_fresh_runtime_address_obligation_once;
std::once_flag g_runtime_address_tags_obligation_once;

}  // namespace

void IRInstructionVisitor::operator()(const IRAlloc &alloc) const
{
  const LowerCtx *active_ctx = emitter.GetCurrentCtx();
  analysis::TypeRef value_type = alloc.type;
  if (!value_type && active_ctx)
  {
    value_type = active_ctx->LookupValueType(alloc.value);
  }

  std::optional<IRValue> target_region = alloc.region;
  if (!target_region.has_value())
  {
    if (const IRValue *current = emitter.CurrentActiveRegion())
    {
      target_region = *current;
    }
  }
  if (!target_region.has_value())
  {
    emitter.SetTempValue(alloc.result, DefaultFor(alloc.result));
    return;
  }

  emitter.PushActiveRegion(*target_region);
  llvm::Value *value = EvaluateOrDefault(alloc.value);
  emitter.PopActiveRegion();

  llvm::Type *value_ty = value_type ? emitter.GetLLVMType(value_type) : nullptr;
  if ((!value_ty || value_ty->isVoidTy()) && value)
  {
    value_ty = value->getType();
  }
  if (!value_ty || value_ty->isVoidTy())
  {
    emitter.SetTempValue(alloc.result, DefaultFor(alloc.result));
    return;
  }

  llvm::Value *region_value = EvaluateOrDefault(*target_region);
  if (!region_value)
  {
    emitter.SetTempValue(alloc.result, DefaultFor(alloc.result));
    return;
  }

  const analysis::ScopeContext &scope = BuildScope(active_ctx);
  std::uint64_t alloc_size = 0;
  std::uint64_t alloc_align = 1;
  if (value_type)
  {
    if (const auto size = ::ultraviolet::analysis::layout::SizeOf(scope, value_type))
    {
      alloc_size = *size;
    }
    if (const auto align = ::ultraviolet::analysis::layout::AlignOf(scope, value_type))
    {
      alloc_align = *align;
    }
  }
  if (alloc_align == 0)
  {
    alloc_align = 1;
  }

  const llvm::DataLayout &dl = emitter.GetModule().getDataLayout();
  if (alloc_size == 0 && !value_ty->isVoidTy())
  {
    alloc_size = static_cast<std::uint64_t>(dl.getTypeAllocSize(value_ty));
  }
  if (alloc_align == 1 && !value_ty->isVoidTy())
  {
    alloc_align = std::max<std::uint64_t>(
        alloc_align,
        static_cast<std::uint64_t>(dl.getABITypeAlign(value_ty).value()));
  }

  llvm::Type *usize_ty = llvm::Type::getInt64Ty(emitter.GetContext());
  llvm::Value *raw_ptr = nullptr;
  const std::string alloc_sym = BuiltinModalSymRegionAlloc();
  if (std::optional<RuntimeFuncInfo> alloc_info = GetRuntimeFuncInfo(alloc_sym))
  {
    llvm::Function *alloc_fn = emitter.GetModule().getFunction(alloc_sym);
    const bool runtime_c_aggregate_boundary = RuntimeUsesCAggregateABI(alloc_sym);
    const bool runtime_foreign_boundary = RuntimeUsesForeignABI(alloc_sym);
    const bool use_c_abi_aggregate_sret = runtime_c_aggregate_boundary;
    if (!alloc_fn)
    {
      ABICallResult alloc_abi = ComputeCallABI(
          emitter,
          alloc_info->params,
          alloc_info->ret,
          use_c_abi_aggregate_sret,
          /*foreign_boundary_mode_independent=*/runtime_foreign_boundary);
      if (alloc_abi.func_type)
      {
        alloc_fn = llvm::Function::Create(
            alloc_abi.func_type,
            llvm::GlobalValue::ExternalLinkage,
            alloc_sym,
            &emitter.GetModule());
        alloc_fn->setCallingConv(llvm::CallingConv::C);
      }
    }
    if (alloc_fn)
    {
      std::vector<llvm::Value *> alloc_args;
      alloc_args.reserve(3);
      alloc_args.push_back(region_value);
      alloc_args.push_back(llvm::ConstantInt::get(usize_ty, alloc_size));
      alloc_args.push_back(llvm::ConstantInt::get(usize_ty, alloc_align));
      raw_ptr = EmitABICall(
          emitter,
          &builder,
          alloc_fn,
          alloc_info->params,
          alloc_info->ret,
          alloc_args,
          use_c_abi_aggregate_sret,
          /*ffi_import_boundary=*/false,
          /*ffi_import_catch=*/false,
          std::nullopt,
          nullptr,
          nullptr,
          nullptr,
          runtime_foreign_boundary);
    }
  }
  if (!raw_ptr)
  {
    emitter.SetTempValue(alloc.result, DefaultFor(alloc.result));
    return;
  }
  RecordRegionAllocConformanceOnce(
      g_resolve_runtime_region_entry_obligation_once,
      "def.ResolveRuntimeRegionEntry",
      RegionAllocResolveEntryPayload());
  RecordRegionAllocConformanceOnce(
      g_resolve_runtime_region_target_tag_obligation_once,
      "def.ResolveRuntimeRegionTargetAndTag",
      RegionAllocResolveTargetAndTagPayload());
  RecordRegionAllocConformanceOnce(
      g_fresh_runtime_address_obligation_once,
      "def.FreshRuntimeAddress",
      FreshRuntimeAddressPayload());
  RecordRegionAllocConformanceOnce(
      g_runtime_address_tags_obligation_once,
      "def.RuntimeAddressTags",
      RuntimeAddressTagsPayload());

  analysis::TypeRef source_type = active_ctx ? active_ctx->LookupValueType(alloc.value) : nullptr;
  if (!source_type)
  {
    source_type = value_type;
  }
  if (value->getType() != value_ty)
  {
    if (value_type)
    {
      llvm::Value *coerced =
          CoerceToTyped(emitter, &builder, value, value_ty, source_type, value_type);
      value = coerced ? coerced : llvm::Constant::getNullValue(value_ty);
    }
    else
    {
      llvm::Value *coerced = CoerceTo(&builder, value, value_ty);
      value = coerced ? coerced : llvm::Constant::getNullValue(value_ty);
    }
  }

  llvm::Value *typed_ptr = builder.CreateBitCast(
      raw_ptr,
      llvm::PointerType::get(value_ty, 0));
  builder.CreateStore(value, typed_ptr);

  emitter.SetTempStorage(alloc.result, typed_ptr);
  emitter.SetTempValue(alloc.result, raw_ptr);
}

} // namespace ultraviolet::codegen::emit_detail
