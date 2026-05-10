#include "05_codegen/llvm/llvm_emit.h"

#include "00_core/process_config.h"
#include "00_core/spec_trace.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/globals/literal_emit.h"
#include "05_codegen/llvm/emit/internal_helpers.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace cursive::codegen {
namespace {

bool AllZeroBytes(const std::vector<std::uint8_t>& bytes) {
  for (const std::uint8_t byte : bytes) {
    if (byte != 0) {
      return false;
    }
  }
  return true;
}

llvm::Constant* ConstantBytesAsLLVM(llvm::LLVMContext& context,
                                    llvm::Type* ty,
                                    const std::vector<std::uint8_t>& bytes) {
  if (!ty) {
    return nullptr;
  }
  if (bytes.empty()) {
    return llvm::Constant::getNullValue(ty);
  }
  if (auto* array_ty = llvm::dyn_cast<llvm::ArrayType>(ty)) {
    if (array_ty->getElementType()->isIntegerTy(8) &&
        array_ty->getNumElements() == bytes.size()) {
      return llvm::ConstantDataArray::get(
          context, llvm::ArrayRef<std::uint8_t>(bytes));
    }
  }
  if (auto* int_ty = llvm::dyn_cast<llvm::IntegerType>(ty)) {
    const unsigned bit_width = int_ty->getBitWidth();
    if (bit_width == bytes.size() * 8u) {
      std::vector<std::uint64_t> words((bit_width + 63u) / 64u, 0u);
      for (std::size_t i = 0; i < bytes.size(); ++i) {
        const std::size_t word_index = i / sizeof(std::uint64_t);
        const std::size_t byte_index = i % sizeof(std::uint64_t);
        words[word_index] |=
            static_cast<std::uint64_t>(bytes[i]) << (byte_index * 8u);
      }
      return llvm::ConstantInt::get(
          int_ty, llvm::APInt(bit_width, llvm::ArrayRef<std::uint64_t>(words)));
    }
  }
  if (ty->isHalfTy() && bytes.size() == 2u) {
    const std::uint16_t raw =
        static_cast<std::uint16_t>(bytes[0]) |
        (static_cast<std::uint16_t>(bytes[1]) << 8u);
    return llvm::ConstantFP::get(
        context, llvm::APFloat(llvm::APFloat::IEEEhalf(),
                               llvm::APInt(16, raw)));
  }
  if (ty->isFloatTy() && bytes.size() == 4u) {
    std::uint32_t raw = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      raw |= static_cast<std::uint32_t>(bytes[i]) << (i * 8u);
    }
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return llvm::ConstantFP::get(ty, static_cast<double>(value));
  }
  if (ty->isDoubleTy() && bytes.size() == 8u) {
    std::uint64_t raw = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      raw |= static_cast<std::uint64_t>(bytes[i]) << (i * 8u);
    }
    double value = 0.0;
    std::memcpy(&value, &raw, sizeof(value));
    return llvm::ConstantFP::get(ty, value);
  }
  if (ty->isPointerTy() && AllZeroBytes(bytes)) {
    return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ty));
  }
  return nullptr;
}

llvm::ArrayType* ByteArrayType(llvm::LLVMContext& context, std::uint64_t size) {
  return llvm::ArrayType::get(llvm::Type::getInt8Ty(context), size);
}

}  // namespace

void LLVMEmitter::EmitGlobalConst(const GlobalConst& global) {
  SPEC_RULE("LowerIRDecl-GlobalConst");

  analysis::TypeRef static_type = StaticTypeForConst(global, current_ctx_);
  llvm::Type* ty = static_type ? GetLLVMType(static_type) : nullptr;
  std::uint64_t align = global.align == 0 ? 1 : global.align;
  if (static_type && current_ctx_) {
    const analysis::ScopeContext& scope = emit_detail::BuildScope(current_ctx_);
    if (const auto static_align =
            ::cursive::analysis::layout::AlignOf(scope, static_type)) {
      align = *static_align;
    }
  }

  llvm::Constant* init = nullptr;
  if (ty) {
    init = ConstantBytesAsLLVM(context_, ty, global.bytes);
    if (!init) {
      if (current_ctx_) {
        current_ctx_->ReportCodegenFailure();
      }
      return;
    }
  } else {
    ty = ByteArrayType(context_, global.bytes.size());
    init = ConstantBytesAsLLVM(context_, ty, global.bytes);
    align = std::max<std::uint64_t>(align, 1);
  }
  if (!init) {
    if (current_ctx_) {
      current_ctx_->ReportCodegenFailure();
    }
    return;
  }

  const bool hosted_state_fallback = HasHostedStateSlot(global.symbol);
  auto* gv = new llvm::GlobalVariable(
      *module_,
      ty,
      !hosted_state_fallback,
      global.externally_visible ? llvm::GlobalValue::ExternalLinkage
                                : llvm::GlobalValue::InternalLinkage,
      init,
      global.symbol);
  gv->setAlignment(llvm::Align(align));

  globals_[global.symbol] = gv;
}

void LLVMEmitter::EmitGlobalZero(const GlobalZero& global) {
  SPEC_RULE("LowerIRDecl-GlobalZero");

  analysis::TypeRef static_type =
      current_ctx_ ? current_ctx_->LookupStaticType(global.symbol) : nullptr;
  llvm::Type* ty = static_type ? GetLLVMType(static_type) : nullptr;
  std::uint64_t align = global.align == 0 ? 1 : global.align;
  if (static_type && current_ctx_) {
    const analysis::ScopeContext& scope = emit_detail::BuildScope(current_ctx_);
    if (const auto static_size =
            ::cursive::analysis::layout::SizeOf(scope, static_type)) {
      if (*static_size != global.size) {
        current_ctx_->ReportCodegenFailure();
        return;
      }
    }
    if (const auto static_align =
            ::cursive::analysis::layout::AlignOf(scope, static_type)) {
      align = *static_align;
    }
  }
  if (!ty) {
    ty = ByteArrayType(context_, global.size);
  }

  auto* gv = new llvm::GlobalVariable(
      *module_,
      ty,
      false,
      global.externally_visible ? llvm::GlobalValue::ExternalLinkage
                                : llvm::GlobalValue::InternalLinkage,
      llvm::Constant::getNullValue(ty),
      global.symbol);
  gv->setAlignment(llvm::Align(align));

  globals_[global.symbol] = gv;
}

void LLVMEmitter::EmitVTable(const GlobalVTable& vtable) {
  SPEC_RULE("LowerIRDecl-VTable");

  const bool debug_vtable = core::IsDebugEnabled("obj");
  if (debug_vtable) {
    std::fprintf(stderr, "[emit-vtable] symbol=%s slots=%zu\n",
                 vtable.symbol.c_str(), vtable.slots.size());
  }

  auto* usize_ty = llvm::Type::getInt64Ty(context_);
  auto* ptr_ty = GetOpaquePtr();

  auto resolve_ptr_const = [&](const std::string& symbol,
                               std::size_t slot_index,
                               bool drop_entry) -> llvm::Constant* {
    if (symbol.empty()) {
      return llvm::ConstantPointerNull::get(
          llvm::cast<llvm::PointerType>(ptr_ty));
    }

    if (llvm::Function* func = functions_[symbol]) {
      return llvm::ConstantExpr::getBitCast(func, ptr_ty);
    }
    if (llvm::Function* func = module_->getFunction(symbol)) {
      return llvm::ConstantExpr::getBitCast(func, ptr_ty);
    }
    if (llvm::GlobalVariable* global = module_->getNamedGlobal(symbol)) {
      return llvm::ConstantExpr::getBitCast(global, ptr_ty);
    }

    if (debug_vtable) {
      std::fprintf(stderr,
                   "[emit-vtable]   %s[%zu]=%s -> NULL (not found)\n",
                   drop_entry ? "drop" : "slot",
                   slot_index,
                   symbol.c_str());
    }
    return llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptr_ty));
  };

  std::vector<llvm::Type*> field_tys;
  field_tys.reserve(3 + vtable.slots.size());
  field_tys.push_back(usize_ty);
  field_tys.push_back(usize_ty);
  field_tys.push_back(ptr_ty);
  for (std::size_t i = 0; i < vtable.slots.size(); ++i) {
    field_tys.push_back(ptr_ty);
  }
  llvm::StructType* vtable_ty = llvm::StructType::get(context_, field_tys);

  std::vector<llvm::Constant*> fields;
  fields.reserve(3 + vtable.slots.size());
  fields.push_back(llvm::ConstantInt::get(usize_ty, vtable.header.size));
  fields.push_back(llvm::ConstantInt::get(usize_ty, vtable.header.align));
  fields.push_back(resolve_ptr_const(vtable.header.drop_sym, 0, true));

  for (std::size_t i = 0; i < vtable.slots.size(); ++i) {
    const auto& slot = vtable.slots[i];
    llvm::Constant* entry = resolve_ptr_const(slot, i, false);
    fields.push_back(entry);
    if (debug_vtable && !llvm::isa<llvm::ConstantPointerNull>(entry)) {
      std::fprintf(stderr, "[emit-vtable]   slot[%zu]=%s -> FOUND\n", i,
                   slot.c_str());
    }
  }

  llvm::Constant* init = llvm::ConstantStruct::get(vtable_ty, fields);

  llvm::GlobalVariable* gv = module_->getNamedGlobal(vtable.symbol);
  if (!gv) {
    gv = new llvm::GlobalVariable(
        *module_,
        vtable_ty,
        true,
        llvm::GlobalValue::InternalLinkage,
        init,
        vtable.symbol);
  } else {
    if (gv->getValueType() != vtable_ty) {
      if (current_ctx_) {
        current_ctx_->ReportCodegenFailure();
      }
      return;
    }
    gv->setInitializer(init);
    gv->setConstant(true);
    gv->setLinkage(llvm::GlobalValue::InternalLinkage);
  }

  globals_[vtable.symbol] = gv;
}

}  // namespace cursive::codegen
