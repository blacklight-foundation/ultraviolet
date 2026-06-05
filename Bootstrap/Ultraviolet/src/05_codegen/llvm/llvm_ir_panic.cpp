// =================================================================
// File: 05_codegen/llvm/llvm_ir_panic.cpp
// Construct: LLVM IR Panic Emission Utilities
// Spec Section: 6.12
// Spec Rules: PanicRecord, PoisonFlag
// =================================================================
//
// MIGRATED FROM: ultraviolet-bootstrap/src/04_codegen/llvm/llvm_ir_panic.cpp
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

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ultraviolet::codegen {

namespace {

using emit_detail::BuildScope;

void RecordPanicConformanceOnce(std::once_flag& flag,
                                std::string_view rule_id,
                                std::string payload) {
  if (!core::Conformance::Enabled()) {
    return;
  }
  std::call_once(flag, [rule_id = std::string(rule_id),
                        payload = std::move(payload)]() {
    core::Conformance::Record(rule_id, std::nullopt, payload);
  });
}

void AppendPanicPayloadField(std::string& payload,
                             std::string_view key,
                             std::string_view value) {
  if (!payload.empty()) {
    payload.push_back(';');
  }
  payload.append(key);
  payload.push_back('=');
  payload.append(value);
}

void AppendPanicPayloadField(std::string& payload,
                             std::string_view key,
                             const std::string& value) {
  AppendPanicPayloadField(payload, key, std::string_view(value));
}

void AppendPanicPayloadField(std::string& payload,
                             std::string_view key,
                             const char* value) {
  AppendPanicPayloadField(payload, key, std::string_view(value ? value : "-"));
}

void AppendPanicPayloadField(std::string& payload,
                             std::string_view key,
                             bool value) {
  AppendPanicPayloadField(payload, key, std::string_view(value ? "true" : "false"));
}

void AppendPanicPayloadField(std::string& payload,
                             std::string_view key,
                             std::uint64_t value) {
  AppendPanicPayloadField(payload, key, std::to_string(value));
}

bool ContainsModulePath(const std::vector<std::string>& paths,
                        const std::string& module_name) {
  for (const auto& path : paths) {
    if (path == module_name) {
      return true;
    }
  }
  return false;
}

std::string PoisonedModulePayload(const std::string& module_name) {
  std::string payload;
  AppendPanicPayloadField(payload, "operation", "PoisonedModule");
  AppendPanicPayloadField(payload, "path", module_name);
  AppendPanicPayloadField(payload, "addr_source", "AddrOfSym(PoisonFlag(module))");
  AppendPanicPayloadField(payload, "reads_poison_flag", true);
  AppendPanicPayloadField(payload, "predicate", "flag_nonzero");
  AppendPanicPayloadField(payload, "panic_on_poison", true);
  return payload;
}

std::string PoisonedModulesPayload(const std::string& module_name,
                                   const std::vector<std::string>& paths) {
  std::string payload;
  AppendPanicPayloadField(payload, "operation", "PoisonedModules");
  AppendPanicPayloadField(payload, "source", "StoreInitPanicRecord");
  AppendPanicPayloadField(payload, "module", module_name);
  AppendPanicPayloadField(payload, "set_size",
                          static_cast<std::uint64_t>(paths.size()));
  AppendPanicPayloadField(payload, "predicate", "PoisonedModule");
  AppendPanicPayloadField(payload, "flag_value", "nonzero");
  AppendPanicPayloadField(payload, "flags_written", true);
  AppendPanicPayloadField(payload, "contains_module",
                          ContainsModulePath(paths, module_name));
  return payload;
}

std::once_flag g_poisoned_module_obligation_once;
std::once_flag g_poisoned_modules_obligation_once;
std::once_flag g_poison_judg_obligation_once;
std::once_flag g_panic_record_init_obligation_once;
std::once_flag g_panic_record_of_flag_obligation_once;
std::once_flag g_panic_record_of_code_obligation_once;
std::once_flag g_write_panic_record_clear_obligation_once;
std::once_flag g_write_panic_record_set_obligation_once;
std::once_flag g_init_panic_obligation_once;

llvm::AllocaInst* CreatePanicRuntimeAlloca(llvm::IRBuilder<>* builder,
                                           llvm::Type* type,
                                           llvm::StringRef name) {
  if (!builder || !builder->GetInsertBlock()) {
    return nullptr;
  }
  llvm::Function* function = builder->GetInsertBlock()->getParent();
  if (!function) {
    return builder->CreateAlloca(type, nullptr, name);
  }
  llvm::IRBuilder<> entry_builder(&function->getEntryBlock(),
                                  function->getEntryBlock().begin());
  return entry_builder.CreateAlloca(type, nullptr, name);
}

llvm::Value* MaterializePanicRuntimeValueRef(llvm::IRBuilder<>* builder,
                                             llvm::Value* value,
                                             llvm::Type* storage_ty,
                                             llvm::StringRef name) {
  llvm::AllocaInst* slot = CreatePanicRuntimeAlloca(builder, storage_ty, name);
  if (slot && value) {
    builder->CreateStore(value, slot);
  }
  return slot;
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

  std::string payload;
  AppendPanicPayloadField(payload, "operation", "ClearPanicRecord");
  AppendPanicPayloadField(payload, "flag_offset", offsets->first);
  AppendPanicPayloadField(payload, "code_offset", offsets->second);
  AppendPanicPayloadField(payload, "flag_value", std::uint64_t{0});
  AppendPanicPayloadField(payload, "code_value", std::uint64_t{0});
  RecordPanicConformanceOnce(
      g_panic_record_init_obligation_once,
      "def.24.PanicRecordInit",
      std::move(payload));

  llvm::Value* panic_val =
      llvm::ConstantInt::get(llvm::Type::getInt8Ty(emitter.GetContext()), 0);
  llvm::Value* code_val =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(emitter.GetContext()), 0);
  StoreAtOffset(emitter, builder, panic_ptr, offsets->first, panic_val);
  StoreAtOffset(emitter, builder, panic_ptr, offsets->second, code_val);

  std::string write_payload;
  AppendPanicPayloadField(write_payload, "operation", "WritePanicRecord");
  AppendPanicPayloadField(write_payload, "source", "ClearPanicRecordAt");
  AppendPanicPayloadField(write_payload, "panic_out_addr", "PanicOutAddr");
  AppendPanicPayloadField(write_payload, "panic_field_addr",
                          "FieldAddr(PanicRecord,PanicOutAddr,panic)");
  AppendPanicPayloadField(write_payload, "code_field_addr",
                          "FieldAddr(PanicRecord,PanicOutAddr,code)");
  AppendPanicPayloadField(write_payload, "flag_offset", offsets->first);
  AppendPanicPayloadField(write_payload, "code_offset", offsets->second);
  AppendPanicPayloadField(write_payload, "flag_value", std::uint64_t{0});
  AppendPanicPayloadField(write_payload, "code_value", std::uint64_t{0});
  AppendPanicPayloadField(write_payload, "writes", "panic,code");
  AppendPanicPayloadField(write_payload, "order", "panic_then_code");
  AppendPanicPayloadField(write_payload, "llvm_ops",
                          "StoreAtOffset,StoreAtOffset");
  AppendPanicPayloadField(write_payload, "emitted_paths",
                          "IRClearPanic,EmitCatchExportPanicReturn");
  RecordPanicConformanceOnce(
      g_write_panic_record_clear_obligation_once,
      "def.24.WritePanicRecord",
      std::move(write_payload));
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

  std::string payload;
  AppendPanicPayloadField(payload, "operation", "WritePanicRecord");
  AppendPanicPayloadField(payload, "source", "StorePanicRecordValue");
  AppendPanicPayloadField(payload, "panic_out_addr", "PanicOutAddr");
  AppendPanicPayloadField(payload, "panic_field_addr",
                          "FieldAddr(PanicRecord,PanicOutAddr,panic)");
  AppendPanicPayloadField(payload, "code_field_addr",
                          "FieldAddr(PanicRecord,PanicOutAddr,code)");
  AppendPanicPayloadField(payload, "flag_offset", offsets->first);
  AppendPanicPayloadField(payload, "code_offset", offsets->second);
  AppendPanicPayloadField(payload, "flag_value", std::uint64_t{1});
  AppendPanicPayloadField(payload, "code_value", "dynamic_i32");
  AppendPanicPayloadField(payload, "writes", "panic,code");
  AppendPanicPayloadField(payload, "order", "panic_then_code");
  AppendPanicPayloadField(payload, "llvm_ops", "StoreAtOffset,StoreAtOffset");
  AppendPanicPayloadField(payload, "emitted_paths",
                          "IRLowerPanic,EmitPanicIfFalse,"
                          "EmitPanicReturnIfFalse,EmitPoisonCheck,"
                          "CodeGen-UnwindCatch-Import");
  RecordPanicConformanceOnce(
      g_write_panic_record_set_obligation_once,
      "def.24.WritePanicRecord",
      std::move(payload));
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

  std::string payload;
  AppendPanicPayloadField(payload, "operation", "PanicRecordOf");
  AppendPanicPayloadField(payload, "source", "LoadPanicFlag");
  AppendPanicPayloadField(payload, "panic_out_addr", "PanicOutAddr");
  AppendPanicPayloadField(payload, "read_addr",
                          "FieldAddr(PanicRecord,PanicOutAddr,panic)");
  AppendPanicPayloadField(payload, "field", "panic");
  AppendPanicPayloadField(payload, "record_fields", "panic,code");
  AppendPanicPayloadField(payload, "reads", "panic");
  AppendPanicPayloadField(payload, "llvm_ops", "LoadAtOffset,ICmpNE");
  AppendPanicPayloadField(payload, "emitted_paths",
                          "IRPanicCheck,IRCleanupPanicCheck,"
                          "IRInitPanicHandle,EmitCatchExportPanicReturn,"
                          "HostedDestroyDeinitCheck");
  AppendPanicPayloadField(payload, "flag_offset", offsets->first);
  AppendPanicPayloadField(payload, "code_offset", offsets->second);
  RecordPanicConformanceOnce(
      g_panic_record_of_flag_obligation_once,
      "def.24.PanicRecordOf",
      std::move(payload));
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
  llvm::Value* code = LoadAtOffset(emitter, builder, ptr, offsets->second, code_ty);
  if (!code) {
    return nullptr;
  }

  std::string payload;
  AppendPanicPayloadField(payload, "operation", "PanicRecordOf");
  AppendPanicPayloadField(payload, "source", "LoadPanicCodeValue");
  AppendPanicPayloadField(payload, "panic_out_addr", "PanicOutAddr");
  AppendPanicPayloadField(payload, "read_addr",
                          "FieldAddr(PanicRecord,PanicOutAddr,code)");
  AppendPanicPayloadField(payload, "field", "code");
  AppendPanicPayloadField(payload, "record_fields", "panic,code");
  AppendPanicPayloadField(payload, "reads", "code");
  AppendPanicPayloadField(payload, "llvm_ops", "LoadAtOffset");
  AppendPanicPayloadField(payload, "emitted_paths",
                          "EntryStubPanicCheck,EmitReturn");
  AppendPanicPayloadField(payload, "flag_offset", offsets->first);
  AppendPanicPayloadField(payload, "code_offset", offsets->second);
  RecordPanicConformanceOnce(
      g_panic_record_of_code_obligation_once,
      "def.24.PanicRecordOf",
      std::move(payload));
  return code;
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
  SPEC_RULE("rule.24.PoisonFlag-Decl");
  core::Conformance::Record(
      "def.24.PoisonFlagStorage",
      std::nullopt,
      "storage=global_bool;linkage=external;mutable=true");
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
            ctx->shared_library_assembly_names.contains(owner_root);
        if (!imported_shared_library_data) {
          return decl;
        }
        decl->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
        return decl;
      };
  auto* bool_ty = emitter.GetLLVMType(analysis::MakeTypePrim("bool"));
  if (!bool_ty) {
    SPEC_RULE("PoisonFlag-Err");
    SPEC_RULE("rule.24.PoisonFlag-Err");
    if (ctx) {
      ctx->ReportCodegenFailure();
    }
    return nullptr;
  }
  auto define_poison_flag =
      [&](llvm::GlobalVariable* flag) -> llvm::GlobalVariable* {
        if (!flag) {
          return nullptr;
        }
        if (!flag->hasInitializer()) {
          flag->setInitializer(llvm::Constant::getNullValue(bool_ty));
        }
        flag->setConstant(false);
        flag->setLinkage(llvm::GlobalValue::ExternalLinkage);
        flag->setDLLStorageClass(llvm::GlobalValue::DefaultStorageClass);
        return flag;
      };
  if (auto* existing = emitter.GetModule().getGlobalVariable(sym, true)) {
    return define_flag ? define_poison_flag(existing)
                       : configure_imported_poison_decl(existing);
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
  std::vector<std::vector<std::size_t>> dependents(n);
  for (const auto& edge : ctx.init_eager_edges) {
    if (edge.first < n && edge.second < n) {
      dependents[edge.second].push_back(edge.first);
    }
  }

  std::vector<char> visited(n, false);
  std::vector<std::size_t> stack;
  visited[target] = true;
  stack.push_back(target);
  while (!stack.empty()) {
    const std::size_t cur = stack.back();
    stack.pop_back();
    for (const auto succ : dependents[cur]) {
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
                          llvm::IRBuilder<>* builder,
                          const std::vector<std::string>* poison_modules) {
  LowerCtx* ctx = emitter.GetCurrentCtx();
  if (!ctx) {
    return;
  }
  llvm::Function* func = builder->GetInsertBlock()->getParent();
  if (!IsInitFunction(emitter, func)) {
    return;
  }

  std::vector<std::string> computed_poison;
  if (!poison_modules) {
    computed_poison = PoisonSetForInit(*ctx);
    poison_modules = &computed_poison;
  }
  if (!poison_modules->empty()) {
    llvm::Type* bool_ty = emitter.GetLLVMType(analysis::MakeTypePrim("bool"));
    llvm::Value* val = llvm::ConstantInt::get(bool_ty, 1);
    for (const auto& module_name : *poison_modules) {
      const auto path = SplitModulePathString(module_name);
      llvm::Value* flag = GetPoisonFlagPtr(emitter, path);
      if (!flag) {
        SPEC_RULE("SetPoison-Err");
        SPEC_RULE("rule.24.SetPoison-Err");
        ctx->ReportCodegenFailure();
        return;
      }
      SPEC_RULE("rule.24.SetPoison-OnInitFail");
      builder->CreateStore(val, flag);
    }
    RecordPanicConformanceOnce(
        g_poisoned_modules_obligation_once,
        "def.PoisonedModules",
        PoisonedModulesPayload(core::StringOfPath(ctx->module_path),
                               *poison_modules));
  }

  llvm::Value* ptr = LoadPanicOutPtr(emitter, builder);
  if (!ptr) {
    return;
  }
  const auto& scope = BuildScope(ctx);
  std::vector<analysis::TypeRef> fields;
  fields.push_back(analysis::MakeTypePrim("bool"));
  fields.push_back(analysis::MakeTypePrim("u32"));
  const auto layout = ::ultraviolet::analysis::layout::RecordLayoutOf(scope, fields);
  if (!layout.has_value() || layout->offsets.size() < 2) {
    return;
  }
  llvm::LLVMContext& ctx_ll = emitter.GetContext();
  llvm::Value* panic_val = llvm::ConstantInt::get(llvm::Type::getInt8Ty(ctx_ll), 1);
  llvm::Value* code_val = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_ll),
                                                 PanicCode(PanicReason::InitPanic));
  StoreAtOffset(emitter, builder, ptr, layout->offsets[0], panic_val);
  StoreAtOffset(emitter, builder, ptr, layout->offsets[1], code_val);

  std::string payload;
  AppendPanicPayloadField(payload, "operation", "StoreInitPanicRecord");
  AppendPanicPayloadField(payload, "module", core::StringOfPath(ctx->module_path));
  AppendPanicPayloadField(payload, "panic_flag", true);
  AppendPanicPayloadField(payload,
                          "panic_code",
                          static_cast<std::uint64_t>(
                              PanicCode(PanicReason::InitPanic)));
  AppendPanicPayloadField(payload, "flag_offset", layout->offsets[0]);
  AppendPanicPayloadField(payload, "code_offset", layout->offsets[1]);
  RecordPanicConformanceOnce(
      g_init_panic_obligation_once,
      "rule.24.Init-Panic",
      std::move(payload));
}

void StorePanicRecord(LLVMEmitter& emitter,
                      llvm::IRBuilder<>* builder,
                      std::uint16_t code) {
  LowerCtx* ctx = emitter.GetCurrentCtx();
  if (!ctx) {
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
    core::Conformance::Record(
        "rule.23.CodeGen-UnwindAbort-Export",
        std::nullopt,
        "source=EmitReturn;boundary=export;mode=abort;"
        "effect=call_runtime_panic;return=unreachable");
    core::Conformance::Record(
        "requirement.23.BoundaryUnwindDynamicEffects",
        std::nullopt,
        "source=EmitReturn;boundary=export;mode=abort;"
        "dynamic_effect=abort_on_ultraviolet_panic");
    core::Conformance::Record(
        "def.23.BoundaryUnwindCodeGenerationEffects",
        std::nullopt,
        "source=EmitReturn;boundary=export;mode=abort;"
        "lowering=runtime_panic_then_unreachable");
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
                                  {emitter.GetOpaquePtr()},
                                  false);
      panic_fn = llvm::Function::Create(
          panic_ty,
          llvm::GlobalValue::ExternalLinkage,
          panic_sym,
          &emitter.GetModule());
      panic_fn->setCallingConv(llvm::CallingConv::C);
    }
    llvm::Value* panic_code_ref = MaterializePanicRuntimeValueRef(
        builder, panic_code, i32_ty, "runtime_panic_code");
    builder->CreateCall(panic_fn->getFunctionType(), panic_fn, {panic_code_ref});
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
    core::Conformance::Record(
        "rule.23.CodeGen-UnwindCatch-Export",
        std::nullopt,
        "source=EmitReturn;boundary=export;mode=catch;"
        "effect=catch_ultraviolet_panic;return=null_value");
    core::Conformance::Record(
        "requirement.23.BoundaryUnwindDynamicEffects",
        std::nullopt,
        "source=EmitReturn;boundary=export;mode=catch;"
        "dynamic_effect=catch_ultraviolet_panic");
    core::Conformance::Record(
        "def.23.BoundaryUnwindCodeGenerationEffects",
        std::nullopt,
        "source=EmitReturn;boundary=export;mode=catch;"
        "lowering=return_null_value");
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

using namespace emit_detail;

  void LLVMEmitter::EmitPoisonCheck(const std::string &module_name)
  {
    SPEC_RULE("LowerIRInstr-CheckPoison");
    SPEC_RULE("rule.24.LowerIRInstr-CheckPoison");
    SPEC_RULE("rule.24.CheckPoison-Use");
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
      SPEC_RULE("rule.24.CheckPoison-Err");
      if (current_ctx_)
      {
        current_ctx_->ReportCodegenFailure();
      }
      return;
    }

    llvm::Type *bool_ty = GetLLVMType(analysis::MakeTypePrim("bool"));
    if (!bool_ty)
    {
      SPEC_RULE("rule.24.CheckPoison-Err");
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
    core::Conformance::Record(
        "rule.24.LowerIRInstr-CheckPoison",
        std::nullopt,
        "source=EmitPoisonCheck;ir_form=CheckPoison;"
        "lower_form=poison-flag-load+conditional-branch;result=LLResult");
    RecordPanicConformanceOnce(
        g_poison_judg_obligation_once,
        "def.24.PoisonJudg",
        "judgements=PoisonFlag,CheckPoison,SetPoison;source=EmitPoisonCheck");
    core::Conformance::Record(
        "sem.24.CheckPoisonBehavior",
        std::nullopt,
        "source=EmitPoisonCheck;reads_poison_flag=true;predicate=flag_nonzero;"
        "panic_on_poison=true");
    RecordPanicConformanceOnce(g_poisoned_module_obligation_once,
                               "def.PoisonedModule",
                               PoisonedModulePayload(module_name));

    builder->SetInsertPoint(panic_bb);
    StorePanicRecord(*this, builder, PanicCode(PanicReason::InitPanic));
    if (!builder->GetInsertBlock()->getTerminator())
    {
      EmitReturn(*this, builder);
    }

    builder->SetInsertPoint(cont_bb);
  }

}  // namespace ultraviolet::codegen


