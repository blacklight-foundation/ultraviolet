// =============================================================================
// MIGRATION MAPPING: llvm_module.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.12 LLVM 21 Backend Requirements (lines 17287-17650)
//   - Section 6.12.1 LLVM Module Header (lines 17289-17291)
//   - LLVMHeader = [TargetDataLayout, TargetTriple]
//   - Target: x86_64-pc-windows-msvc
//   - DataLayout: Win64 model
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/llvm/llvm_module.cpp
//   - Lines 1-55: LLVMEmitter constructor, SetupModule
//   - Lines 22-28: Constructor initializes context, module, builder
//   - Lines 35-44: SetupModule sets target triple and data layout
//   - Lines 46-100: Helper functions (BuildScope, CreateEntryAlloca, ByteGEP, etc.)
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/llvm/llvm_module.h
//   - ultraviolet/include/05_codegen/llvm/llvm_emit.h (LLVMEmitter)
//   - llvm/IR/Module.h
//   - llvm/IR/LLVMContext.h
//   - llvm/IR/IRBuilder.h
//   - llvm/TargetParser/Triple.h
//
// REFACTORING NOTES:
//   1. LLVMEmitter owns module, context ref, and IRBuilder
//   2. SetupModule configures target-specific settings
//   3. Target triple: x86_64-pc-windows-msvc
//   4. Data layout: Win64 ABI model
//   5. Helper functions for memory operations
//   6. CreateEntryAlloca for stack allocation
//   7. ByteGEP for byte-level pointer arithmetic
//   8. StoreAtOffset/LoadAtOffset for struct field access
// =============================================================================

#include "05_codegen/llvm/llvm_module.h"

#include "00_core/spec_trace.h"
#include "00_core/symbols.h"
#include "01_project/language_profile.h"
#include "05_codegen/globals/literal_emit.h"
#include "05_codegen/intrinsics/intrinsics_interface.h"
#include "05_codegen/llvm/llvm_emit.h"
#include "05_codegen/llvm/llvm_attr.h"
#include "05_codegen/llvm/llvm_call.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/TargetParser/Triple.h"

#include <mutex>
#include <string>

namespace ultraviolet::codegen {

static_assert(LLVM_VERSION_MAJOR == 21, "Ultraviolet requires LLVM 21.1.8");
static_assert(LLVM_VERSION_MINOR == 1, "Ultraviolet requires LLVM 21.1.8");
static_assert(LLVM_VERSION_PATCH == 8, "Ultraviolet requires LLVM 21.1.8");

// =============================================================================
// §6.12.1 LLVM Module Header
// =============================================================================

void SetupModuleHeader(llvm::Module& module,
                       project::TargetProfile profile) {
  SPEC_DEF("TargetTriple", "§6.12.1");
  SPEC_DEF("TargetDataLayout", "§6.12.1");

  module.setTargetTriple(
      llvm::Triple(std::string(project::LLVMTripleOf(profile))));

  module.setDataLayout(std::string(project::LLVMDataLayoutOf(profile)));
}

// =============================================================================
// §6.12.7 LLVM Toolchain Version
// =============================================================================

bool ValidateLLVMVersion() {
  return std::string_view(LLVM_VERSION_STRING) == project::kLLVMToolchainVersion;
}

// =============================================================================
// §6.12.6 Runtime Declarations
// =============================================================================

void DeclareRuntimeFunctions(LLVMEmitter& emitter) {
  // Runtime declarations are handled by LLVMEmitter::DeclareRuntime()
  emitter.DeclareRuntime();
}

void DeclareRuntimeFunctions(LLVMEmitter& emitter,
                             const std::vector<std::string>& symbols) {
  emitter.DeclareRuntime(symbols);
}

std::string_view GetRuntimeSymbol(std::string_view operation) {
  // Map operation names to runtime symbols
  // This is a partial mapping; full coverage lives in runtime_interface.h.
  if (operation == "panic") {
    return "uv_rt_panic";
  }
  if (operation == "alloc") {
    return "uv_rt_alloc";
  }
  if (operation == "dealloc") {
    return "uv_rt_dealloc";
  }
  if (operation == "print") {
    return "uv_rt_print";
  }
  return "";
}

bool IsRuntimeSymbol(std::string_view symbol) {
  // Check if symbol starts with runtime prefix
  return symbol.find("uv_rt_") == 0 ||
         symbol.find("ultraviolet_x3a_x3aruntime") == 0;
}

// =============================================================================
// Module Creation and Management
// =============================================================================

std::unique_ptr<llvm::Module> CreateModule(llvm::LLVMContext& context,
                                           const std::string& name,
                                           project::TargetProfile profile) {
  auto module = std::make_unique<llvm::Module>(name, context);
  SetupModuleHeader(*module, profile);
  return module;
}

// =============================================================================
// Global Variable and COMDAT Management
// =============================================================================

llvm::Comdat* GetOrCreateComdat(llvm::Module& module, const std::string& name) {
  llvm::Comdat* comdat = module.getOrInsertComdat(name);
  if (comdat) {
    comdat->setSelectionKind(llvm::Comdat::Any);
  }
  return comdat;
}

llvm::GlobalVariable* CreateZeroInitGlobal(llvm::Module& module,
                                           llvm::Type* type,
                                           const std::string& name,
                                           bool is_const) {
  auto* gv = new llvm::GlobalVariable(
      module,
      type,
      is_const,
      llvm::GlobalValue::InternalLinkage,
      llvm::Constant::getNullValue(type),
      name);
  return gv;
}

// =============================================================================
// Symbol Management
// =============================================================================

bool IsDropGlueSymbol(std::string_view symbol) {
  // Check if symbol is a drop glue symbol (starts with mangled drop prefix)
  const std::string prefix = project::RuntimePathSig({"drop"});
  return symbol.size() >= prefix.size() &&
         symbol.compare(0, prefix.size(), prefix) == 0;
}

std::string_view GetDropGluePrefix() {
  struct Cache {
    project::SourceLanguage language = project::SourceLanguage::Ultraviolet;
    std::string prefix;
    bool initialized = false;
  };

  static Cache cache;
  static std::mutex cache_mu;
  std::lock_guard<std::mutex> lock(cache_mu);
  const auto active_language = project::ActiveLanguageProfile().language;
  if (!cache.initialized || cache.language != active_language) {
    cache.language = active_language;
    cache.prefix = project::RuntimePathSig({"drop"});
    cache.initialized = true;
  }
  return cache.prefix;
}

std::optional<std::string> EmitKey(const IRDecl& decl) {
  SPEC_RULE("EmitKey");
  return std::visit(
      [](const auto& node) -> std::optional<std::string> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, GlobalVTable>) {
          return std::string("vtable:") + node.symbol;
        } else if constexpr (std::is_same_v<T, ProcIR>) {
          if (IsDropGlueSymbol(node.symbol)) {
            return std::string("drop:") + node.symbol;
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, GlobalConst>) {
          if (IsLiteralSymbol(node.symbol)) {
            return std::string("lit:") + node.symbol;
          }
          return std::nullopt;
        } else {
          return std::nullopt;
        }
      },
      decl);
}

std::vector<std::string> EmitKeys(const IRDecls& decls) {
  SPEC_RULE("EmitKeys");
  std::vector<std::string> keys;
  for (const auto& decl : decls) {
    if (auto key = EmitKey(decl); key.has_value()) {
      keys.push_back(std::move(*key));
    }
  }
  return keys;
}

bool UniqueEmits(const IRDecls& decls) {
  SPEC_RULE("UniqueEmits");
  const auto keys = EmitKeys(decls);
  std::unordered_set<std::string> seen;
  seen.reserve(keys.size());
  for (const auto& key : keys) {
    if (!seen.insert(key).second) {
      return false;
    }
  }
  return true;
}

// =============================================================================
// Module Finalization
// =============================================================================

void FinalizeModule(llvm::Module& module) {
  // Add any required metadata
  // Currently a no-op, but can be extended for debug info, etc.
}

bool VerifyModule(llvm::Module& module) {
  std::string error;
  llvm::raw_string_ostream os(error);
  return !llvm::verifyModule(module, &os);
}

std::vector<std::string> DeclSyms(const llvm::Module& module) {
  SPEC_RULE("DeclSyms");
  std::vector<std::string> syms;
  syms.reserve(module.size());
  for (const auto& fn : module.functions()) {
    if (!fn.getName().empty()) {
      syms.push_back(std::string(fn.getName()));
    }
  }
  std::sort(syms.begin(), syms.end());
  syms.erase(std::unique(syms.begin(), syms.end()), syms.end());
  return syms;
}

static AttrSet ActualDeclAttrs(const llvm::Function& fn) {
  AttrSet attrs;
  if (fn.hasFnAttribute(llvm::Attribute::NoReturn)) {
    attrs.push_back({AttrKind::NoReturn, 0});
  }
  if (fn.hasFnAttribute(llvm::Attribute::NoUnwind)) {
    attrs.push_back({AttrKind::NoUnwind, 0});
  }
  return attrs;
}

bool RuntimeDeclsOk(const llvm::Module& module) {
  SPEC_RULE("RuntimeDeclsOk");
  for (const auto& fn : module.functions()) {
    const std::string symbol(fn.getName());
    if (!IsRuntimeFunction(symbol)) {
      continue;
    }
    if (!DeclAttrsOk(symbol, ActualDeclAttrs(fn))) {
      return false;
    }
  }
  return true;
}

bool RuntimeDeclsCover(const llvm::Module& module, const IRDecls& decls) {
  SPEC_RULE("RuntimeDeclsCover");
  const auto refs = RuntimeRefs(decls);
  const auto decl_syms = DeclSyms(module);
  const std::unordered_set<std::string> decl_set(decl_syms.begin(), decl_syms.end());
  for (const auto& symbol : refs) {
    if (!decl_set.contains(symbol)) {
      return false;
    }
  }
  return true;
}

// -----------------------------------------------------------------------------
// LLVMEmitter Module Setup Wrappers
// -----------------------------------------------------------------------------

  void LLVMEmitter::SetupModule()
  {
    SetupModuleHeader(*module_, target_profile_);
  }
  // T-LLVM-006: Runtime Declarations
  void LLVMEmitter::DeclareRuntime()
  {
    DeclareRuntime(RuntimeDeclSyms());
  }

  void LLVMEmitter::DeclareRuntime(const std::vector<std::string>& symbols)
  {
    AnchorRuntimeInterfaceRules();

    for (const auto& symbol : symbols) {
      if (module_->getFunction(symbol) != nullptr) {
        continue;
      }

      const auto info = GetRuntimeFuncInfo(symbol);
      if (!info.has_value()) {
        continue;
      }

      const bool runtime_foreign_boundary = RuntimeUsesForeignABI(symbol);
      ABICallResult abi = ComputeCallABI(
          *this,
          info->params,
          info->ret,
          /*use_c_abi_aggregate_sret=*/runtime_foreign_boundary,
          /*foreign_boundary_mode_independent=*/false);
      if (!abi.valid || !abi.func_type) {
        if (current_ctx_) {
          current_ctx_->ReportCodegenFailure();
        }
        continue;
      }
      llvm::FunctionType* ft = abi.func_type;

      llvm::Function* fn = llvm::Function::Create(
          ft, llvm::GlobalValue::ExternalLinkage, symbol, module_.get());
      fn->setCallingConv(llvm::CallingConv::C);

      for (std::size_t idx = 0; idx < abi.llvm_param_attrs.size(); ++idx) {
        if (idx >= fn->arg_size()) {
          continue;
        }
        llvm::AttrBuilder param_builder(context_);
        AddAttrSetToBuilder(param_builder, abi.llvm_param_attrs[idx]);
        if (param_builder.hasAttributes()) {
          fn->addParamAttrs(static_cast<unsigned>(idx), param_builder);
        }
      }

      AttrSet decl_attrs = DeclAttrs(symbol);
      llvm::AttrBuilder builder(context_);
      AddAttrSetToBuilder(builder, decl_attrs);
      if (builder.hasAttributes()) {
        fn->addFnAttrs(builder);
      }

      functions_[symbol] = fn;
    }
  }
}  // namespace ultraviolet::codegen
