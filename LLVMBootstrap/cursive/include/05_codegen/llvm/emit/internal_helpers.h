#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "00_core/process_config.h"
#include "00_core/host/services.h"
#include "00_core/symbols.h"
#include "01_project/language_profile.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/llvm/llvm_emit.h"
#include "05_codegen/llvm/llvm_module.h"

#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

namespace cursive::codegen::emit_detail {

inline const char* LibraryEntrySym() {
  return project::ActiveLanguageProfile().library_entry_symbol.data();
}

inline const char* LibraryCtorSym() {
  return project::ActiveLanguageProfile().library_ctor_symbol.data();
}

inline const char* LibraryDtorSym() {
  return project::ActiveLanguageProfile().library_dtor_symbol.data();
}

inline const char* ImagePanicRecordSym() {
  return project::ActiveLanguageProfile().image_panic_record_symbol.data();
}

inline const char* HostAbiVersionSym() {
  return project::ActiveLanguageProfile().host_abi_version_symbol.data();
}

inline const char* HostSessionCreateSym() {
  return project::ActiveLanguageProfile().host_session_create_symbol.data();
}

inline const char* HostSessionDestroySym() {
  return project::ActiveLanguageProfile().host_session_destroy_symbol.data();
}

inline const char* HostSessionOwnerTokenSym() {
  return project::ActiveLanguageProfile().host_session_owner_token_symbol.data();
}

inline const char* HostRuntimeAllocSym() {
  return project::ActiveLanguageProfile().host_runtime_alloc_symbol.data();
}

inline const char* HostRuntimeFreeSym() {
  return project::ActiveLanguageProfile().host_runtime_free_symbol.data();
}

inline const char* HostRuntimeRegisterSym() {
  return project::ActiveLanguageProfile().host_runtime_register_symbol.data();
}

inline const char* HostRuntimeTryEnterSym() {
  return project::ActiveLanguageProfile().host_runtime_try_enter_symbol.data();
}

inline const char* HostRuntimeLeaveSym() {
  return project::ActiveLanguageProfile().host_runtime_leave_symbol.data();
}

inline const char* HostRuntimeTryRetireSym() {
  return project::ActiveLanguageProfile().host_runtime_try_retire_symbol.data();
}

inline const char* HostRuntimeAbortLiveSym() {
  return project::ActiveLanguageProfile().host_runtime_abort_live_symbol.data();
}

inline const char* HostRuntimeCurrentEnvSym() {
  return project::ActiveLanguageProfile().host_runtime_current_env_symbol.data();
}

inline const char* HostRuntimeEnterRetiredSym() {
  return project::ActiveLanguageProfile().host_runtime_enter_retired_symbol.data();
}

inline const char* HostRuntimeLeaveRetiredSym() {
  return project::ActiveLanguageProfile().host_runtime_leave_retired_symbol.data();
}

inline const char* HostRuntimeAbortRetiredSym() {
  return project::ActiveLanguageProfile().host_runtime_abort_retired_symbol.data();
}

inline const char* RawDylibResolveSym() {
  return project::ActiveLanguageProfile().raw_dylib_resolve_symbol.data();
}

inline constexpr std::uint32_t kHostAbiVersion = 1u;

inline unsigned CallingConvForAbi(const std::optional<std::string>& abi_opt) {
  if (!abi_opt.has_value()) {
    return llvm::CallingConv::C;
  }
  const std::string& abi = *abi_opt;
  if (abi == "stdcall") {
    return llvm::CallingConv::X86_StdCall;
  }
  if (abi == "fastcall") {
    return llvm::CallingConv::X86_FastCall;
  }
  if (abi == "vectorcall") {
    return llvm::CallingConv::X86_VectorCall;
  }
  return llvm::CallingConv::C;
}

inline llvm::GlobalVariable* EnsureHostedOwnerTokenGlobal(llvm::Module* module,
                                                          llvm::LLVMContext& context,
                                                          bool define_symbol) {
  if (module == nullptr) {
    return nullptr;
  }
  llvm::Type* i8_ty = llvm::Type::getInt8Ty(context);
  llvm::GlobalVariable* gv = module->getNamedGlobal(HostSessionOwnerTokenSym());
  if (!gv) {
    gv = new llvm::GlobalVariable(*module,
                                  i8_ty,
                                  false,
                                  llvm::GlobalValue::ExternalLinkage,
                                  define_symbol ? llvm::ConstantInt::get(i8_ty, 0) : nullptr,
                                  HostSessionOwnerTokenSym());
  } else if (define_symbol && gv->isDeclaration()) {
    gv->setInitializer(llvm::ConstantInt::get(i8_ty, 0));
    gv->setLinkage(llvm::GlobalValue::ExternalLinkage);
  }
  return gv;
}

inline llvm::GlobalValue::LinkageTypes LLVMLinkageFor(LinkageKind linkage) {
  switch (linkage) {
    case LinkageKind::Internal:
      return llvm::GlobalValue::InternalLinkage;
    case LinkageKind::External:
      return llvm::GlobalValue::ExternalLinkage;
  }
  return llvm::GlobalValue::ExternalLinkage;
}

inline bool IsRuntimeLifecycleSymbol(std::string_view symbol) {
  const auto& language = project::ActiveLanguageProfile();
  return symbol.rfind(language.runtime_init_mangle_prefix, 0) == 0 ||
         symbol.rfind(language.runtime_deinit_mangle_prefix, 0) == 0;
}

inline bool IsGeneratedProcSymbol(std::string_view symbol) {
  return symbol == "main" ||
         symbol == LibraryEntrySym() ||
         symbol == LibraryCtorSym() ||
         symbol == LibraryDtorSym() ||
         symbol == HostSessionCreateSym() ||
         symbol == HostSessionDestroySym() ||
         IsRuntimeLifecycleSymbol(symbol) ||
         IsDropGlueSymbol(symbol);
}

inline bool IsHostedInternalBodySymbol(const LowerCtx* ctx, std::string_view symbol) {
  if (ctx == nullptr) {
    return false;
  }
  for (const auto& info : ctx->hosted_exports) {
    if (info.internal_symbol == symbol) {
      return true;
    }
  }
  return false;
}

class ProcModuleContextScope {
 public:
  ProcModuleContextScope(LowerCtx* ctx,
                         const std::vector<std::string>& defining_module_path)
      : ctx_(ctx) {
    if (ctx_ == nullptr || defining_module_path.empty() ||
        ctx_->module_path == defining_module_path) {
      return;
    }
    saved_module_path_ = ctx_->module_path;
    ctx_->module_path = defining_module_path;
    active_ = true;
  }

  ProcModuleContextScope(const ProcModuleContextScope&) = delete;
  ProcModuleContextScope& operator=(const ProcModuleContextScope&) = delete;

  ~ProcModuleContextScope() {
    if (active_ && ctx_ != nullptr) {
      ctx_->module_path = std::move(saved_module_path_);
    }
  }

 private:
  LowerCtx* ctx_ = nullptr;
  std::vector<std::string> saved_module_path_;
  bool active_ = false;
};

inline llvm::GlobalValue::LinkageTypes ProcLLVMLinkageFor(const LowerCtx* ctx,
                                                          std::string_view symbol) {
  if (IsRuntimeLifecycleSymbol(symbol) || IsHostedInternalBodySymbol(ctx, symbol)) {
    return llvm::GlobalValue::ExternalLinkage;
  }
  if (ctx != nullptr) {
    if (const auto proc_linkage = ctx->LookupProcLinkage(std::string(symbol));
        proc_linkage.has_value()) {
      return LLVMLinkageFor(*proc_linkage);
    }
  }
  return llvm::GlobalValue::ExternalLinkage;
}

inline bool IsProjectEntryModule(const LowerCtx* ctx) {
  if (ctx == nullptr) {
    return false;
  }
  if (ctx->project_entry_module.has_value()) {
    return core::StringOfPath(ctx->module_path) == *ctx->project_entry_module;
  }
  return true;
}

inline void ApplyProcFunctionAttrs(const ProcIR& proc, llvm::Function* fn) {
  if (!fn) {
    return;
  }
  switch (proc.inline_mode) {
    case IRInlineMode::Always:
      fn->addFnAttr(llvm::Attribute::AlwaysInline);
      break;
    case IRInlineMode::Never:
      fn->addFnAttr(llvm::Attribute::NoInline);
      break;
    case IRInlineMode::Default:
      break;
  }
  if (proc.cold) {
    fn->addFnAttr(llvm::Attribute::Cold);
  }
}

inline bool IsDisabledPerfValue(const char* value) {
  if (value == nullptr || *value == '\0') {
    return true;
  }
  std::string lowered;
  for (const char* p = value; *p != '\0'; ++p) {
    lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
  }
  return lowered == "0" || lowered == "false" || lowered == "off" || lowered == "no";
}

inline bool EmitPerfLoggingEnabled() {
  static const bool enabled = [] {
    if (const auto env = core::HostGetEnvUtf8("CURSIVE_EMIT_PERF");
        env.has_value() && !env->empty()) {
      return !IsDisabledPerfValue(env->c_str());
    }
    return core::IsDebugEnabled("codegen") || core::IsDebugEnabled("pipeline");
  }();
  return enabled;
}

inline long long ElapsedMs(std::chrono::steady_clock::time_point start,
                           std::chrono::steady_clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

inline std::string DeclPerfLabel(const IRDecl& decl) {
  struct Visitor {
    std::string operator()(const ProcIR& proc) const { return "proc:" + proc.symbol; }
    std::string operator()(const ExternProcIR& proc) const { return "extern:" + proc.symbol; }
    std::string operator()(const GlobalConst& global) const { return "global-const:" + global.symbol; }
    std::string operator()(const GlobalZero& global) const { return "global-zero:" + global.symbol; }
    std::string operator()(const GlobalVTable& vtable) const { return "global-vtable:" + vtable.symbol; }
  };
  return std::visit(Visitor{}, decl);
}

inline std::string ModulePerfLabel(const LowerCtx& ctx) {
  const std::string key = core::StringOfPath(ctx.module_path);
  if (key.empty()) {
    return "<root>";
  }
  return key;
}

inline long long EmitPerfSlowDeclThresholdMs() {
  static const long long threshold = [] {
    if (const auto env = core::HostGetEnvUtf8("CURSIVE_EMIT_PERF_SLOW_DECL_MS");
        env.has_value() && !env->empty()) {
      char* end = nullptr;
      const long parsed = std::strtol(env->c_str(), &end, 10);
      if (end != env->c_str() && parsed >= 0) {
        return static_cast<long long>(parsed);
      }
    }
    return 5LL;
  }();
  return threshold;
}

inline long long EmitPerfSlowProcThresholdMs() {
  static const long long threshold = [] {
    if (const auto env = core::HostGetEnvUtf8("CURSIVE_EMIT_PERF_SLOW_PROC_MS");
        env.has_value() && !env->empty()) {
      char* end = nullptr;
      const long parsed = std::strtol(env->c_str(), &end, 10);
      if (end != env->c_str() && parsed >= 0) {
        return static_cast<long long>(parsed);
      }
    }
    return 25LL;
  }();
  return threshold;
}

inline bool EmitPerfLogAllProcs() {
  static const bool enabled = [] {
    if (const auto env = core::HostGetEnvUtf8("CURSIVE_EMIT_PERF_PROC_ALL");
        env.has_value() && !env->empty()) {
      return !IsDisabledPerfValue(env->c_str());
    }
    return false;
  }();
  return enabled;
}

inline std::uint64_t AlignUpU64(std::uint64_t value, std::uint64_t align) {
  if (align == 0u) {
    return value;
  }
  const std::uint64_t rem = value % align;
  return rem == 0u ? value : (value + (align - rem));
}

inline std::vector<std::string> SplitModulePathKey(std::string_view key) {
  std::vector<std::string> parts;
  std::string current;
  for (std::size_t i = 0; i < key.size();) {
    if (i + 1 < key.size() && key[i] == ':' && key[i + 1] == ':') {
      parts.push_back(current);
      current.clear();
      i += 2;
      continue;
    }
    current.push_back(static_cast<char>(key[i]));
    ++i;
  }
  if (!current.empty()) {
    parts.push_back(std::move(current));
  }
  return parts;
}

inline void EmitPerfLogLine(const std::string& line) {
  static std::mutex log_mu;
  std::lock_guard<std::mutex> lock(log_mu);
  std::fprintf(stderr, "[emit-perf] %s\n", line.c_str());
  std::fflush(stderr);
}

enum class DeclPerfKind : std::size_t {
  Proc = 0,
  ExternProc,
  GlobalConst,
  GlobalZero,
  GlobalVTable,
  Count,
};

inline DeclPerfKind DeclPerfKindOf(const IRDecl& decl) {
  struct Visitor {
    DeclPerfKind operator()(const ProcIR&) const { return DeclPerfKind::Proc; }
    DeclPerfKind operator()(const ExternProcIR&) const { return DeclPerfKind::ExternProc; }
    DeclPerfKind operator()(const GlobalConst&) const { return DeclPerfKind::GlobalConst; }
    DeclPerfKind operator()(const GlobalZero&) const { return DeclPerfKind::GlobalZero; }
    DeclPerfKind operator()(const GlobalVTable&) const { return DeclPerfKind::GlobalVTable; }
  };
  return std::visit(Visitor{}, decl);
}

inline const char* DeclPerfKindName(DeclPerfKind kind) {
  switch (kind) {
    case DeclPerfKind::Proc:
      return "proc";
    case DeclPerfKind::ExternProc:
      return "extern";
    case DeclPerfKind::GlobalConst:
      return "global-const";
    case DeclPerfKind::GlobalZero:
      return "global-zero";
    case DeclPerfKind::GlobalVTable:
      return "global-vtable";
    case DeclPerfKind::Count:
      break;
  }
  return "unknown";
}

inline bool DeclPerfKindHasBody(DeclPerfKind kind) {
  return kind != DeclPerfKind::ExternProc;
}

struct DeclPerfBucket {
  std::size_t count = 0;
  long long total_ms = 0;
  long long max_ms = 0;
  std::string slowest_label;
};

inline const analysis::ScopeContext& BuildScope(const LowerCtx* ctx) {
  static const analysis::ScopeContext kEmptyScope{};

  struct ScopeCache {
    const LowerCtx* ctx = nullptr;
    const analysis::Sigma* sigma = nullptr;
    std::vector<std::string> module_path;
    std::optional<project::TargetProfile> target_profile;
    analysis::ScopeContext scope;
  };

  thread_local ScopeCache cache;
  if (!ctx || !ctx->sigma) {
    return kEmptyScope;
  }

  if (cache.ctx != ctx || cache.sigma != ctx->sigma ||
      cache.module_path != ctx->module_path ||
      cache.target_profile != ctx->target_profile) {
    cache.ctx = ctx;
    cache.sigma = ctx->sigma;
    cache.module_path = ctx->module_path;
    cache.target_profile = ctx->target_profile;
    cache.scope = analysis::ScopeContext{};
    cache.scope.sigma = *ctx->sigma;
    cache.scope.sigma_source = ctx->sigma;
    cache.scope.current_module = ctx->module_path;
    cache.scope.target_profile = ctx->target_profile;
  }

  return cache.scope;
}

inline bool HasLeadingHostedEnvParam(const std::vector<IRParam>& params) {
  return !params.empty() && params.front().name == kHostedEnvParamName;
}

inline bool HasNamedParam(const std::vector<IRParam>& params, std::string_view name) {
  return std::find_if(params.begin(),
                      params.end(),
                      [&](const IRParam& param) { return param.name == name; }) != params.end();
}

}  // namespace cursive::codegen::emit_detail
