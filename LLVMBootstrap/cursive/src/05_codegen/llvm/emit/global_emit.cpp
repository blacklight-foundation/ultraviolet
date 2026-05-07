#include "05_codegen/llvm/llvm_emit.h"

#include "00_core/process_config.h"
#include "00_core/spec_trace.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

#include <cstdio>
#include <vector>

namespace cursive::codegen {

void LLVMEmitter::EmitGlobalConst(const GlobalConst& global) {
  SPEC_RULE("LowerIRDecl-GlobalConst");

  llvm::Type* i8_ty = llvm::Type::getInt8Ty(context_);
  llvm::ArrayType* ty = llvm::ArrayType::get(i8_ty, global.bytes.size());

  llvm::Constant* init = nullptr;
  if (global.bytes.empty()) {
    init = llvm::Constant::getNullValue(ty);
  } else {
    std::vector<llvm::Constant*> byte_consts;
    byte_consts.reserve(global.bytes.size());
    for (std::uint8_t byte : global.bytes) {
      byte_consts.push_back(llvm::ConstantInt::get(i8_ty, byte));
    }
    init = llvm::ConstantArray::get(ty, byte_consts);
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

  globals_[global.symbol] = gv;
}

void LLVMEmitter::EmitGlobalZero(const GlobalZero& global) {
  SPEC_RULE("LowerIRDecl-GlobalZero");

  llvm::Type* i8_ty = llvm::Type::getInt8Ty(context_);
  llvm::ArrayType* ty = llvm::ArrayType::get(i8_ty, global.size);

  auto* gv = new llvm::GlobalVariable(
      *module_,
      ty,
      false,
      global.externally_visible ? llvm::GlobalValue::ExternalLinkage
                                : llvm::GlobalValue::InternalLinkage,
      llvm::Constant::getNullValue(ty),
      global.symbol);

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
