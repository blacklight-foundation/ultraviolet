// =================================================================
// File: 05_codegen/llvm/llvm_ir_panic.cpp
// Construct: LLVM IR Panic Emission Utilities
// Spec Section: 6.12
// Spec Rules: PanicRecord, PoisonFlag
// =================================================================
//
// MIGRATED FROM: cursive-bootstrap/src/04_codegen/llvm/llvm_ir_panic.cpp
//
// This file implements panic record handling and poison flag management
// for the LLVM backend. These utilities are used during code generation
// to handle panics (runtime errors) and module initialization failures.
//
// Key functions:
// - GetOrCreatePoisonFlag: creates/gets poison flag global variable
// - SplitModulePathString: splits module path string into components
// - IsInitFunction: checks if a function is an init function
// - PoisonSetForInit: computes set of modules to poison on init failure
// - StorePanicRecord: stores panic record with code
// - ClearPanicRecord: clears panic record
// - StoreInitPanicRecord: stores panic record for init function failures
// - LoadPanicOutPtr: loads panic output pointer
// - EmitReturn: emits return instruction with default value
// - EmitPanicIfFalse: conditional panic if condition is false
// - EmitPanicReturnIfFalse: conditional panic and return
// =================================================================

#include "05_codegen/llvm/llvm_ir_panic.h"

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/intrinsics/builtins.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/llvm/llvm_emit.h"
#include "05_codegen/llvm/emit/internal_helpers.h"
#include "05_codegen/llvm/emit/llvm_emit_helpers.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include <optional>
#include <string_view>
#include <utility>

namespace cursive::codegen {

namespace {

using emit_detail::BuildScope;

const char* DeinitPanicSeenSlot() {
  return project::ActiveLanguageProfile().deinit_panic_seen_slot.data();
}

const char* DeinitPanicCodeSlot() {
  return project::ActiveLanguageProfile().deinit_panic_code_slot.data();
}

// Helper for byte-level GEP
llvm::Value* ByteGEP(LLVMEmitter& emitter,
                     llvm::IRBuilder<>* builder,
                     llvm::Value* base_ptr,
                     std::uint64_t offset) {
  llvm::Value* idx = llvm::ConstantInt::get(
      llvm::Type::getInt64Ty(emitter.GetContext()), offset);
  return builder->CreateGEP(
      llvm::Type::getInt8Ty(emitter.GetContext()), base_ptr, idx);
}

// Helper to store value at byte offset
void StoreAtOffset(LLVMEmitter& emitter,
                   llvm::IRBuilder<>* builder,
                   llvm::Value* base_ptr,
                   std::uint64_t offset,
                   llvm::Value* value) {
  if (!base_ptr || !value) {
    return;
  }
  llvm::Value* ptr = offset == 0 ? base_ptr : ByteGEP(emitter, builder, base_ptr, offset);
  builder->CreateStore(value, ptr);
}

// Helper to load value at byte offset
llvm::Value* LoadAtOffset(LLVMEmitter& emitter,
                          llvm::IRBuilder<>* builder,
                          llvm::Value* base_ptr,
                          std::uint64_t offset,
                          llvm::Type* type) {
  if (!base_ptr || !type) {
    return nullptr;
  }
  llvm::Value* ptr = offset == 0 ? base_ptr : ByteGEP(emitter, builder, base_ptr, offset);
  return builder->CreateLoad(type, ptr);
}

bool ContractViolationKindIs(const std::string& reason, std::string_view kind) {
  constexpr std::string_view kPrefix = "ContractViolation(";
  if (reason.size() <= kPrefix.size() + kind.size()) {
    return false;
  }
  if (reason.compare(0, kPrefix.size(), kPrefix) != 0) {
    return false;
  }
  if (reason.compare(kPrefix.size(), kind.size(), kind) != 0) {
    return false;
  }
  const char delim = reason[kPrefix.size() + kind.size()];
  return delim == ')' || delim == ',';
}

std::optional<std::pair<std::uint64_t, std::uint64_t>> PanicRecordOffsets(
    LLVMEmitter& emitter,
    const LowerCtx* ctx) {
  if (!ctx) {
    return std::nullopt;
  }

  const auto& scope = BuildScope(ctx);
  const auto layout = PanicRecordLayout(scope);
  if (!layout.has_value() || layout->offsets.size() < 2) {
    return std::nullopt;
  }
  return std::pair<std::uint64_t, std::uint64_t>{
      layout->offsets[0], layout->offsets[1]};
}

std::pair<llvm::AllocaInst*, llvm::AllocaInst*> GetOrCreateDeinitPanicSlots(
    LLVMEmitter& emitter,
    llvm::IRBuilder<>* builder) {
  if (!builder) {
    return {nullptr, nullptr};
  }

  llvm::Function* func =
      builder->GetInsertBlock() ? builder->GetInsertBlock()->getParent() : nullptr;
  if (!func) {
    return {nullptr, nullptr};
  }

  auto* seen_slot =
      llvm::dyn_cast_or_null<llvm::AllocaInst>(emitter.GetLocal(DeinitPanicSeenSlot()));
  auto* code_slot =
      llvm::dyn_cast_or_null<llvm::AllocaInst>(emitter.GetLocal(DeinitPanicCodeSlot()));
  if (seen_slot && seen_slot->getFunction() != func) {
    seen_slot = nullptr;
  }
  if (code_slot && code_slot->getFunction() != func) {
    code_slot = nullptr;
  }
  if (seen_slot && code_slot) {
    return {seen_slot, code_slot};
  }

  llvm::IRBuilder<> entry_builder(&func->getEntryBlock(),
                                  func->getEntryBlock().begin());
	if (!seen_slot) {
	  seen_slot = entry_builder.CreateAlloca(
	      llvm::Type::getInt1Ty(emitter.GetContext()), nullptr, DeinitPanicSeenSlot());
    entry_builder.CreateStore(llvm::ConstantInt::getFalse(emitter.GetContext()),
                              seen_slot);
	    emitter.SetLocal(DeinitPanicSeenSlot(), seen_slot);
  }
  if (!code_slot) {
	  code_slot = entry_builder.CreateAlloca(
	      llvm::Type::getInt32Ty(emitter.GetContext()), nullptr, DeinitPanicCodeSlot());
    entry_builder.CreateStore(
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(emitter.GetContext()), 0),
        code_slot);
	    emitter.SetLocal(DeinitPanicCodeSlot(), code_slot);
  }

  return {seen_slot, code_slot};
}

void ClearPanicRecordAt(LLVMEmitter& emitter,
                        llvm::IRBuilder<>* builder,
                        llvm::Value* panic_ptr) {
  LowerCtx* ctx = emitter.GetCurrentCtx();
  if (!ctx || !builder) {
    return;
  }
  if (!panic_ptr) {
    panic_ptr = LoadPanicOutPtr(emitter, builder);
  }
  if (!panic_ptr) {
    return;
  }

  const auto offsets = PanicRecordOffsets(emitter, ctx);
  if (!offsets.has_value()) {
    return;
  }

  llvm::Value* panic_val =
      llvm::ConstantInt::get(llvm::Type::getInt8Ty(emitter.GetContext()), 0);
  llvm::Value* code_val =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(emitter.GetContext()), 0);
  StoreAtOffset(emitter, builder, panic_ptr, offsets->first, panic_val);
  StoreAtOffset(emitter, builder, panic_ptr, offsets->second, code_val);
}

void StorePanicRecordValue(LLVMEmitter& emitter,
                           llvm::IRBuilder<>* builder,
                           llvm::Value* panic_ptr,
                           llvm::Value* code) {
  LowerCtx* ctx = emitter.GetCurrentCtx();
  if (!ctx || !builder || !code) {
    return;
  }
  if (!panic_ptr) {
    panic_ptr = LoadPanicOutPtr(emitter, builder);
  }
  if (!panic_ptr) {
    return;
  }

  const auto offsets = PanicRecordOffsets(emitter, ctx);
  if (!offsets.has_value()) {
    return;
  }

  llvm::Type* i32_ty = llvm::Type::getInt32Ty(emitter.GetContext());
  if (code->getType() != i32_ty) {
    if (code->getType()->isIntegerTy()) {
      const auto width = code->getType()->getIntegerBitWidth();
      if (width < 32) {
        code = builder->CreateZExt(code, i32_ty);
      } else if (width > 32) {
        code = builder->CreateTrunc(code, i32_ty);
      }
    } else {
      code = llvm::ConstantInt::get(i32_ty, 0);
    }
  }

  llvm::Value* panic_val =
      llvm::ConstantInt::get(llvm::Type::getInt8Ty(emitter.GetContext()), 1);
  StoreAtOffset(emitter, builder, panic_ptr, offsets->first, panic_val);
  StoreAtOffset(emitter, builder, panic_ptr, offsets->second, code);
}

}  // namespace

std::uint16_t PanicCodeFromString(const std::string& reason) {
  if (ContractViolationKindIs(reason, "Pre")) {
    return PanicCode(PanicReason::ContractPre);
  }
  if (ContractViolationKindIs(reason, "Post")) {
    return PanicCode(PanicReason::ContractPost);
  }
  if (ContractViolationKindIs(reason, "TypeInv")) {
    return PanicCode(PanicReason::TypeInv);
  }
  if (ContractViolationKindIs(reason, "LoopInv")) {
    return PanicCode(PanicReason::LoopInv);
  }
  if (ContractViolationKindIs(reason, "ForeignPre")) {
    return PanicCode(PanicReason::ForeignPre);
  }
  if (ContractViolationKindIs(reason, "ForeignPost")) {
    return PanicCode(PanicReason::ForeignPost);
  }
  if (reason == "ErrorExpr") return PanicCode(PanicReason::ErrorExpr);
  if (reason == "ErrorStmt") return PanicCode(PanicReason::ErrorStmt);
  if (reason == "DivZero") return PanicCode(PanicReason::DivZero);
  if (reason == "Overflow") return PanicCode(PanicReason::Overflow);
  if (reason == "Shift") return PanicCode(PanicReason::Shift);
  if (reason == "Bounds") return PanicCode(PanicReason::Bounds);
  if (reason == "Cast") return PanicCode(PanicReason::Cast);
  if (reason == "NullDeref") return PanicCode(PanicReason::NullDeref);
  if (reason == "ExpiredDeref") return PanicCode(PanicReason::ExpiredDeref);
  if (reason == "InitPanic") return PanicCode(PanicReason::InitPanic);
  if (reason == "ContractPre") return PanicCode(PanicReason::ContractPre);
  if (reason == "ContractPost") return PanicCode(PanicReason::ContractPost);
  if (reason == "AsyncFailed") return PanicCode(PanicReason::AsyncFailed);
  if (reason == "ForeignPre") return PanicCode(PanicReason::ForeignPre);
  if (reason == "ForeignPost") return PanicCode(PanicReason::ForeignPost);
  if (reason == "TypeInv") return PanicCode(PanicReason::TypeInv);
  if (reason == "LoopInv") return PanicCode(PanicReason::LoopInv);
  return PanicCode(PanicReason::Other);
}

llvm::Value* LoadPanicOutPtr(LLVMEmitter& emitter,
                             llvm::IRBuilder<>* builder) {
  llvm::Value* slot = emitter.GetLocal(std::string(kPanicOutName));
  if (!slot) {
    return emitter.GetHostedSessionPanicPtr();
  }
  if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(slot)) {
    return builder->CreateLoad(alloca->getAllocatedType(), alloca);
  }
  return builder->CreateLoad(emitter.GetOpaquePtr(), slot);
}

llvm::Value* LoadPanicCode(LLVMEmitter& emitter,
                           llvm::IRBuilder<>* builder) {
  return LoadPanicCodeValue(emitter, builder, nullptr);
}

llvm::Value* LoadPanicFlag(LLVMEmitter& emitter,
                           llvm::IRBuilder<>* builder,
                           llvm::Value* panic_ptr) {
  LowerCtx* ctx = emitter.GetCurrentCtx();
  if (!ctx || !builder) {
    return nullptr;
  }
  llvm::Value* ptr = panic_ptr ? panic_ptr : LoadPanicOutPtr(emitter, builder);
  if (!ptr) {
    return nullptr;
  }

  const auto offsets = PanicRecordOffsets(emitter, ctx);
  if (!offsets.has_value()) {
    return nullptr;
  }

  llvm::Type* flag_ty = llvm::Type::getInt8Ty(emitter.GetContext());
  llvm::Value* flag = LoadAtOffset(emitter, builder, ptr, offsets->first, flag_ty);
  if (!flag) {
    return nullptr;
  }
  return builder->CreateICmpNE(flag, llvm::ConstantInt::get(flag_ty, 0));
}

llvm::Value* LoadPanicCodeValue(LLVMEmitter& emitter,
                                llvm::IRBuilder<>* builder,
                                llvm::Value* panic_ptr) {
  LowerCtx* ctx = emitter.GetCurrentCtx();
  if (!ctx || !builder) {
    return nullptr;
  }
  llvm::Value* ptr = panic_ptr ? panic_ptr : LoadPanicOutPtr(emitter, builder);
  if (!ptr) {
    return nullptr;
  }

  const auto offsets = PanicRecordOffsets(emitter, ctx);
  if (!offsets.has_value()) {
    return nullptr;
  }

  llvm::Type* code_ty = llvm::Type::getInt32Ty(emitter.GetContext());
  return LoadAtOffset(emitter, builder, ptr, offsets->second, code_ty);
}

bool IsInitFunction(LLVMEmitter& emitter, llvm::Function* func) {
  if (!func) {
    return false;
  }
  const std::string prefix = project::RuntimePathSig({"init"});
  return func->getName().starts_with(prefix);
}

std::vector<std::string> SplitModulePathString(const std::string& module) {
  std::vector<std::string> path;
  std::string acc;
  for (std::size_t i = 0; i < module.size();) {
    if (i + 1 < module.size() && module[i] == ':' && module[i + 1] == ':') {
      path.push_back(acc);
      acc.clear();
      i += 2;
      continue;
    }
    acc.push_back(module[i++]);
  }
  if (!acc.empty()) {
    path.push_back(acc);
  }
  return path;
}

llvm::GlobalVariable* GetOrCreatePoisonFlag(LLVMEmitter& emitter,
                                            const std::vector<std::string>& module_path) {
  SPEC_RULE("PoisonFlag-Decl");
  std::vector<std::string> full = {
      std::string(project::ActiveLanguageProfile().runtime_root),
      "runtime",
      "poison"};
  full.insert(full.end(), module_path.begin(), module_path.end());
  const std::string sym = core::Mangle(core::StringOfPath(full));
  bool define_flag = true;
  LowerCtx* ctx = emitter.GetCurrentCtx();
  if (ctx) {
    define_flag = (ctx->module_path == module_path);
  }
  auto configure_imported_poison_decl =
      [&](llvm::GlobalVariable* decl) -> llvm::GlobalVariable* {
        if (!decl || !ctx || ctx->module_path.empty() || module_path.empty()) {
          return decl;
        }
        if (project::ObjectFormatOf(emitter.GetTargetProfile()) !=
            project::ObjectFormat::Coff) {
          return decl;
        }
        const std::string& current_root = ctx->module_path.front();
        const std::string& owner_root = module_path.front();
        const bool imported_shared_library_data =
            owner_root != current_root &&
            ctx->library_assembly_names.contains(owner_root);
        if (!imported_shared_library_data) {
          return decl;
        }
        decl->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
        return decl;
      };
  if (auto* existing = emitter.GetModule().getGlobalVariable(sym, true)) {
    return define_flag ? existing : configure_imported_poison_decl(existing);
  }
  auto* bool_ty = emitter.GetLLVMType(analysis::MakeTypePrim("bool"));
  if (!bool_ty) {
    SPEC_RULE("PoisonFlag-Err");
    if (ctx) {
      ctx->ReportCodegenFailure();
    }
    return nullptr;
  }
  auto* init = define_flag ? llvm::Constant::getNullValue(bool_ty) : nullptr;
  auto* flag = new llvm::GlobalVariable(
      emitter.GetModule(),
      bool_ty,
      false,
      llvm::GlobalValue::ExternalLinkage,
      init,
      sym);
  return define_flag ? flag : configure_imported_poison_decl(flag);
}

llvm::Value* GetPoisonFlagPtr(LLVMEmitter& emitter,
                              const std::vector<std::string>& module_path) {
  llvm::Type* bool_ty = emitter.GetLLVMType(analysis::MakeTypePrim("bool"));
  if (!bool_ty) {
    if (LowerCtx* ctx = emitter.GetCurrentCtx()) {
      ctx->ReportCodegenFailure();
    }
    return nullptr;
  }

  llvm::Value* global_flag = GetOrCreatePoisonFlag(emitter, module_path);
  if (emitter.IsHostedLibraryBuild()) {
    std::vector<std::string> full = {
        std::string(project::ActiveLanguageProfile().runtime_root),
        "runtime",
        "poison"};
    full.insert(full.end(), module_path.begin(), module_path.end());
    const std::string sym = core::Mangle(core::StringOfPath(full));
    if (llvm::Value* ptr = emitter.GetHostedStatePtr(sym, bool_ty, global_flag)) {
      return ptr;
    }
  }

  return global_flag;
}

std::vector<std::string> PoisonSetForInit(const LowerCtx& ctx) {
  const std::string module_name = core::StringOfPath(ctx.module_path);
  if (ctx.init_modules.empty()) {
    return {module_name};
  }

  std::size_t target = ctx.init_modules.size();
  for (std::size_t i = 0; i < ctx.init_modules.size(); ++i) {
    if (core::StringOfPath(ctx.init_modules[i]) == module_name) {
      target = i;
      break;
    }
  }
  if (target == ctx.init_modules.size()) {
    return {module_name};
  }

  const std::size_t n = ctx.init_modules.size();
  std::vector<std::vector<std::size_t>> outgoing(n);
  for (const auto& edge : ctx.init_eager_edges) {
    if (edge.first < n && edge.second < n) {
      outgoing[edge.first].push_back(edge.second);
    }
  }

  std::vector<char> visited(n, false);
  std::vector<std::size_t> stack;
  visited[target] = true;
  stack.push_back(target);
  while (!stack.empty()) {
    const std::size_t cur = stack.back();
    stack.pop_back();
    for (const auto succ : outgoing[cur]) {
      if (!visited[succ]) {
        visited[succ] = true;
        stack.push_back(succ);
      }
    }
  }

  std::vector<std::string> out;
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    if (visited[i]) {
      out.push_back(core::StringOfPath(ctx.init_modules[i]));
    }
  }
  if (out.empty()) {
    out.push_back(module_name);
  }
  return out;
}

void StoreInitPanicRecord(LLVMEmitter& emitter,
                          llvm::IRBuilder<>* builder) {
  LowerCtx* ctx = emitter.GetCurrentCtx();
  if (!ctx) {
    return;
  }
  llvm::Function* func = builder->GetInsertBlock()->getParent();
  if (!IsInitFunction(emitter, func)) {
    return;
  }

  const auto poison = PoisonSetForInit(*ctx);
  if (!poison.empty()) {
    llvm::Type* bool_ty = emitter.GetLLVMType(analysis::MakeTypePrim("bool"));
    llvm::Value* val = llvm::ConstantInt::get(bool_ty, 1);
    for (const auto& module_name : poison) {
      const auto path = SplitModulePathString(module_name);
      llvm::Value* flag = GetPoisonFlagPtr(emitter, path);
      if (!flag) {
        SPEC_RULE("SetPoison-Err");
        ctx->ReportCodegenFailure();
        return;
      }
      builder->CreateStore(val, flag);
    }
  }

  llvm::Value* ptr = LoadPanicOutPtr(emitter, builder);
  if (!ptr) {
    return;
  }
  const auto& scope = BuildScope(ctx);
  std::vector<analysis::TypeRef> fields;
  fields.push_back(analysis::MakeTypePrim("bool"));
  fields.push_back(analysis::MakeTypePrim("u32"));
  const auto layout = ::cursive::analysis::layout::RecordLayoutOf(scope, fields);
  if (!layout.has_value() || layout->offsets.size() < 2) {
    return;
  }
  llvm::LLVMContext& ctx_ll = emitter.GetContext();
  llvm::Value* panic_val = llvm::ConstantInt::get(llvm::Type::getInt8Ty(ctx_ll), 1);
  llvm::Value* code_val = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_ll),
                                                 PanicCode(PanicReason::InitPanic));
  StoreAtOffset(emitter, builder, ptr, layout->offsets[0], panic_val);
  StoreAtOffset(emitter, builder, ptr, layout->offsets[1], code_val);
}

void StorePanicRecord(LLVMEmitter& emitter,
                      llvm::IRBuilder<>* builder,
                      std::uint16_t code) {
  LowerCtx* ctx = emitter.GetCurrentCtx();
  if (!ctx) {
    return;
  }
  llvm::Function* func = builder->GetInsertBlock()->getParent();
  if (IsInitFunction(emitter, func)) {
    StoreInitPanicRecord(emitter, builder);
    return;
  }
  llvm::Value* ptr = LoadPanicOutPtr(emitter, builder);
  if (!ptr) {
    return;
  }
  llvm::Value* code_val =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(emitter.GetContext()), code);
  StorePanicRecordValue(emitter, builder, ptr, code_val);
}

void ClearPanicRecord(LLVMEmitter& emitter,
                      llvm::IRBuilder<>* builder) {
  ClearPanicRecordAt(emitter, builder, nullptr);
}

void EmitReturn(LLVMEmitter& emitter, llvm::IRBuilder<>* builder) {
  llvm::Function* func = builder->GetInsertBlock()->getParent();
  LowerCtx* ctx = emitter.GetCurrentCtx();
  const auto export_unwind_mode =
      (ctx && func)
          ? ctx->LookupExportUnwindMode(func->getName().str())
          : std::optional<LowerCtx::ExportUnwindMode>{};

  if (export_unwind_mode.has_value() &&
      *export_unwind_mode == LowerCtx::ExportUnwindMode::Abort) {
    llvm::Value* panic_code = LoadPanicCode(emitter, builder);
    llvm::Type* i32_ty = llvm::Type::getInt32Ty(emitter.GetContext());
    if (!panic_code) {
      panic_code = llvm::ConstantInt::get(i32_ty, 0);
    } else if (panic_code->getType() != i32_ty) {
      panic_code = builder->CreateIntCast(
          panic_code,
          i32_ty,
          /*isSigned=*/false);
    }

    const std::string panic_sym = RuntimePanicSym();
    llvm::Function* panic_fn = emitter.GetModule().getFunction(panic_sym);
    if (!panic_fn) {
      llvm::FunctionType* panic_ty =
          llvm::FunctionType::get(llvm::Type::getVoidTy(emitter.GetContext()),
                                  {i32_ty},
                                  false);
      panic_fn = llvm::Function::Create(
          panic_ty,
          llvm::GlobalValue::ExternalLinkage,
          panic_sym,
          &emitter.GetModule());
      panic_fn->setCallingConv(llvm::CallingConv::C);
    }
    builder->CreateCall(panic_fn->getFunctionType(), panic_fn, {panic_code});
    builder->CreateUnreachable();
    return;
  }

  llvm::Type* ret_ty = func->getReturnType();
  if (ret_ty->isVoidTy()) {
    builder->CreateRetVoid();
    return;
  }

  if (export_unwind_mode.has_value() &&
      *export_unwind_mode == LowerCtx::ExportUnwindMode::Catch) {
    builder->CreateRet(llvm::Constant::getNullValue(ret_ty));
    return;
  }

  if (ret_ty->isIntegerTy()) {
    if (llvm::Value* panic_code = LoadPanicCode(emitter, builder)) {
      if (panic_code->getType() != ret_ty) {
        panic_code = builder->CreateIntCast(
            panic_code,
            ret_ty,
            /*isSigned=*/false);
      }
      builder->CreateRet(panic_code);
      return;
    }
  }
  builder->CreateRet(llvm::Constant::getNullValue(ret_ty));
}

void EmitPanicIfFalse(LLVMEmitter& emitter,
                      llvm::IRBuilder<>* builder,
                      llvm::Value* ok,
                      std::uint16_t code) {
  if (!ok) {
    return;
  }
  llvm::Function* func = builder->GetInsertBlock()->getParent();
  llvm::BasicBlock* ok_bb = llvm::BasicBlock::Create(emitter.GetContext(), "check_ok", func);
  llvm::BasicBlock* fail_bb = llvm::BasicBlock::Create(emitter.GetContext(), "check_fail", func);
  builder->CreateCondBr(ok, ok_bb, fail_bb);

  builder->SetInsertPoint(fail_bb);
  StorePanicRecord(emitter, builder, code);
  builder->CreateBr(ok_bb);

  builder->SetInsertPoint(ok_bb);
}

void EmitPanicReturnIfFalse(LLVMEmitter& emitter,
                            llvm::IRBuilder<>* builder,
                            llvm::Value* ok,
                            std::uint16_t code) {
  if (!ok) {
    return;
  }
  llvm::Function* func = builder->GetInsertBlock()->getParent();
  llvm::BasicBlock* ok_bb = llvm::BasicBlock::Create(emitter.GetContext(), "check_ok", func);
  llvm::BasicBlock* fail_bb = llvm::BasicBlock::Create(emitter.GetContext(), "check_fail", func);
  builder->CreateCondBr(ok, ok_bb, fail_bb);

  builder->SetInsertPoint(fail_bb);
  StorePanicRecord(emitter, builder, code);
  EmitReturn(emitter, builder);

  builder->SetInsertPoint(ok_bb);
}

void HandleDeinitPanic(LLVMEmitter& emitter,
                       llvm::IRBuilder<>* builder,
                       llvm::Value* panic_ptr) {
  if (!builder || !builder->GetInsertBlock() ||
      builder->GetInsertBlock()->getTerminator()) {
    return;
  }

  llvm::Value* has_panic = LoadPanicFlag(emitter, builder, panic_ptr);
  if (!has_panic) {
    return;
  }

  auto [seen_slot, code_slot] = GetOrCreateDeinitPanicSlots(emitter, builder);
  if (!seen_slot || !code_slot) {
    return;
  }

  llvm::Function* func = builder->GetInsertBlock()->getParent();
  llvm::BasicBlock* capture_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "deinit.panic.capture", func);
  llvm::BasicBlock* cont_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "deinit.panic.cont", func);
  builder->CreateCondBr(has_panic, capture_bb, cont_bb);

  builder->SetInsertPoint(capture_bb);
  llvm::Value* seen =
      builder->CreateLoad(llvm::Type::getInt1Ty(emitter.GetContext()), seen_slot);
  llvm::Value* code = LoadPanicCodeValue(emitter, builder, panic_ptr);
  if (!code) {
    code = llvm::ConstantInt::get(llvm::Type::getInt32Ty(emitter.GetContext()), 0);
  }

  llvm::BasicBlock* store_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "deinit.panic.store", func);
  llvm::BasicBlock* clear_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "deinit.panic.clear", func);
  builder->CreateCondBr(seen, clear_bb, store_bb);

  builder->SetInsertPoint(store_bb);
  builder->CreateStore(llvm::ConstantInt::getTrue(emitter.GetContext()), seen_slot);
  builder->CreateStore(code, code_slot);
  builder->CreateBr(clear_bb);

  builder->SetInsertPoint(clear_bb);
  ClearPanicRecordAt(emitter, builder, panic_ptr);
  builder->CreateBr(cont_bb);

  builder->SetInsertPoint(cont_bb);
}

void RestoreDeinitPanicIfAny(LLVMEmitter& emitter,
                             llvm::IRBuilder<>* builder,
                             llvm::Value* panic_ptr) {
  if (!builder || !builder->GetInsertBlock() ||
      builder->GetInsertBlock()->getTerminator()) {
    return;
  }

  auto [seen_slot, code_slot] = GetOrCreateDeinitPanicSlots(emitter, builder);
  if (!seen_slot || !code_slot) {
    return;
  }

  llvm::Function* func = builder->GetInsertBlock()->getParent();
  llvm::Value* seen =
      builder->CreateLoad(llvm::Type::getInt1Ty(emitter.GetContext()), seen_slot);
  llvm::BasicBlock* restore_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "deinit.panic.restore", func);
  llvm::BasicBlock* cont_bb =
      llvm::BasicBlock::Create(emitter.GetContext(), "deinit.panic.done", func);
  builder->CreateCondBr(seen, restore_bb, cont_bb);

  builder->SetInsertPoint(restore_bb);
  llvm::Value* code =
      builder->CreateLoad(llvm::Type::getInt32Ty(emitter.GetContext()), code_slot);
  StorePanicRecordValue(emitter, builder, panic_ptr, code);
  builder->CreateBr(cont_bb);

  builder->SetInsertPoint(cont_bb);
}

using namespace emit_detail;

  void LLVMEmitter::EmitPoisonCheck(const std::string &module_name)
  {
    SPEC_RULE("LowerIRDecl-PoisonCheck");
    auto *builder = static_cast<llvm::IRBuilder<> *>(builder_.get());
    if (!builder || !builder->GetInsertBlock() ||
        builder->GetInsertBlock()->getTerminator())
    {
      return;
    }

    const auto module_path = SplitModulePathString(module_name);
    llvm::Value *flag_ptr = GetPoisonFlagPtr(*this, module_path);
    if (!flag_ptr)
    {
      if (current_ctx_)
      {
        current_ctx_->ReportCodegenFailure();
      }
      return;
    }

    llvm::Type *bool_ty = GetLLVMType(analysis::MakeTypePrim("bool"));
    if (!bool_ty)
    {
      if (current_ctx_)
      {
        current_ctx_->ReportCodegenFailure();
      }
      return;
    }

    llvm::Value *poisoned = builder->CreateLoad(bool_ty, flag_ptr);
    llvm::Function *func = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock *panic_bb =
        llvm::BasicBlock::Create(context_, "poison.take", func);
    llvm::BasicBlock *cont_bb =
        llvm::BasicBlock::Create(context_, "poison.cont", func);
    builder->CreateCondBr(AsBool(builder, poisoned), panic_bb, cont_bb);

    builder->SetInsertPoint(panic_bb);
    StorePanicRecord(*this, builder, PanicCode(PanicReason::InitPanic));
    if (current_ctx_)
    {
      CleanupPlan cleanup_plan = ComputeCleanupPlanToFunctionRoot(*current_ctx_);
      IRPtr cleanup_ir = EmitCleanupOnPanic(cleanup_plan, *current_ctx_);
      if (cleanup_ir)
      {
        EmitIR(cleanup_ir);
      }
    }
    if (!builder->GetInsertBlock()->getTerminator())
    {
      EmitReturn(*this, builder);
    }

    builder->SetInsertPoint(cont_bb);
  }

}  // namespace cursive::codegen



