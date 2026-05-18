// =============================================================================
// File: 05_codegen/llvm/emit/module_emit.cpp
// Construct: Module Orchestration and Declaration Emission
// Spec Section: 24.1.3, 24.1.4, 24.7.8, 24.7.10, 24.7.13
// =============================================================================
#include "05_codegen/llvm/llvm_emit.h"

#include "05_codegen/llvm/emit/internal_helpers.h"

#include "00_core/spec_trace.h"
#include "00_core/symbols.h"
#include "04_analysis/resolve/scopes.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/checks/panic.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/dyn_dispatch/dyn_dispatch.h"
#include "05_codegen/globals/entrypoint.h"
#include "05_codegen/globals/globals.h"
#include "05_codegen/globals/init.h"
#include "05_codegen/globals/literal_emit.h"
#include "05_codegen/intrinsics/intrinsics_interface.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/lower/lower_module.h"
#include "05_codegen/llvm/llvm_attr.h"
#include "05_codegen/llvm/llvm_call.h"
#include "05_codegen/llvm/llvm_ir_panic.h"
#include "05_codegen/llvm/llvm_module.h"
#include "05_codegen/llvm/llvm_types.h"
#include "05_codegen/llvm/llvm_ub_safe.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace ultraviolet::codegen {

using namespace emit_detail;

  llvm::Module *LLVMEmitter::EmitModule(const IRDecls &decls, LowerCtx &ctx)
  {
    SPEC_RULE("LowerIR-Module");
    SPEC_RULE("EmitLLVM-Ok");
    current_ctx_ = &ctx;
    using Clock = std::chrono::steady_clock;

    const bool perf_enabled = EmitPerfLoggingEnabled();
    const long long slow_decl_threshold_ms =
        perf_enabled ? EmitPerfSlowDeclThresholdMs() : 0;
    const std::string perf_module_label =
        perf_enabled ? ModulePerfLabel(ctx) : std::string();
    const auto module_start = perf_enabled ? Clock::now() : Clock::time_point{};
    auto phase_start = module_start;
    long long setup_ms = 0;
    long long declare_runtime_ms = 0;
    long long declare_functions_ms = 0;
    long long emit_defs_ms = 0;
    long long entrypoint_ms = 0;
    long long debug_ir_dump_ms = 0;

    std::size_t pass1_proc_count = 0;
    std::size_t pass1_extern_count = 0;
    std::size_t pass1_reused = 0;
    std::size_t pass1_created = 0;
    std::size_t pass1_signature_mismatch = 0;
    std::array<DeclPerfBucket, static_cast<std::size_t>(DeclPerfKind::Count)>
        decl_perf{};

    IRDecls expanded_decls = ExpandIR(decls, ctx);

    if (!UniqueEmits(expanded_decls) && current_ctx_)
    {
      current_ctx_->ReportCodegenFailure();
    }

    SetupModule();
    AnchorEntrypointRules();
    AnchorInitRules();
    if (perf_enabled)
    {
      const auto now = Clock::now();
      setup_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    DeclareRuntime(RuntimeRefs(expanded_decls));
    if (perf_enabled)
    {
      const auto now = Clock::now();
      declare_runtime_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    hosted_layout_ = HostedSessionLayout{};
    hosted_env_value_ = nullptr;
    if (ctx.hosted_library)
    {
      const analysis::ScopeContext &scope = BuildScope(&ctx);
      const analysis::TypeRef context_type = analysis::MakeTypePath({"Context"});
      const analysis::TypeRef panic_type = PanicRecordType();
      const std::optional<std::uint64_t> ctx_size = ::ultraviolet::analysis::layout::SizeOf(scope, context_type);
      const std::optional<std::uint64_t> ctx_align = ::ultraviolet::analysis::layout::AlignOf(scope, context_type);
      const std::optional<std::uint64_t> panic_size = ::ultraviolet::analysis::layout::SizeOf(scope, panic_type);
      const std::optional<std::uint64_t> panic_align = ::ultraviolet::analysis::layout::AlignOf(scope, panic_type);

      hosted_layout_.active = true;
      hosted_layout_.align = 1u;
      hosted_layout_.size = 0u;

      auto reserve_slot = [&](const std::string &symbol,
                              std::uint64_t size,
                              std::uint64_t align,
                              bool zero_init,
                              std::vector<std::uint8_t> bytes) {
        HostedStateSlot slot;
        slot.align = std::max<std::uint64_t>(1u, align);
        slot.size = size;
        slot.zero_init = zero_init;
        slot.bytes = std::move(bytes);
        hosted_layout_.size = AlignUpU64(hosted_layout_.size, slot.align);
        slot.offset = hosted_layout_.size;
        hosted_layout_.size += slot.size;
        hosted_layout_.align = std::max(hosted_layout_.align, slot.align);
        hosted_layout_.slots.emplace(symbol, std::move(slot));
      };

      hosted_layout_.context_offset =
          AlignUpU64(hosted_layout_.size, std::max<std::uint64_t>(1u, ctx_align.value_or(1u)));
      hosted_layout_.size = hosted_layout_.context_offset + ctx_size.value_or(0u);
      hosted_layout_.align =
          std::max(hosted_layout_.align, std::max<std::uint64_t>(1u, ctx_align.value_or(1u)));

      hosted_layout_.panic_offset =
          AlignUpU64(hosted_layout_.size, std::max<std::uint64_t>(1u, panic_align.value_or(1u)));
      hosted_layout_.size = hosted_layout_.panic_offset + panic_size.value_or(0u);
      hosted_layout_.align =
          std::max(hosted_layout_.align, std::max<std::uint64_t>(1u, panic_align.value_or(1u)));

      std::vector<std::string> template_symbols;
      template_symbols.reserve(ctx.hosted_state_templates.size());
      for (const auto &[symbol, _tmpl] : ctx.hosted_state_templates)
      {
        template_symbols.push_back(symbol);
      }
      std::sort(template_symbols.begin(), template_symbols.end());
      for (const auto &symbol : template_symbols)
      {
        const auto tmpl_it = ctx.hosted_state_templates.find(symbol);
        if (tmpl_it == ctx.hosted_state_templates.end())
        {
          continue;
        }
        reserve_slot(symbol,
                     tmpl_it->second.size,
                     tmpl_it->second.align,
                     tmpl_it->second.zero_init,
                     tmpl_it->second.bytes);
      }

      for (const auto &module_key : ctx.hosted_project_modules)
      {
        std::vector<std::string> full = {
            std::string(project::ActiveLanguageProfile().runtime_root),
            "runtime",
            "poison"};
        const auto path = SplitModulePathKey(module_key);
        full.insert(full.end(), path.begin(), path.end());
        const std::string poison_sym = core::Mangle(core::StringOfPath(full));
        if (hosted_layout_.slots.find(poison_sym) == hosted_layout_.slots.end())
        {
          reserve_slot(poison_sym, 1u, 1u, true, {});
        }
      }

      hosted_layout_.size = AlignUpU64(hosted_layout_.size, hosted_layout_.align);
    }

    // Every linked image must own concrete poison flag definitions so
    // cross-module poison checks can resolve to the image-owned state. Hosted
    // shared libraries still use session-local poison slots while a host
    // session is active, but ordinary linked calls outside that dynamic extent
    // use the image-owned symbols.
    if (!ctx.hosted_library || ctx.shared_library_project)
    {
      (void)GetOrCreatePoisonFlag(*this, ctx.module_path);
    }

    // Pass 1: declare all functions
    for (const auto &decl : expanded_decls)
    {
      if (auto *proc = std::get_if<ProcIR>(&decl))
      {
        ProcModuleContextScope proc_module_scope(
            current_ctx_, proc->defining_module_path);
        ++pass1_proc_count;
        ABICallResult abi = ComputeProcABI(*this, proc->symbol, proc->params, proc->ret);
        llvm::GlobalValue::LinkageTypes linkage =
            ProcLLVMLinkageFor(current_ctx_, proc->symbol);
        if (!abi.valid || !abi.func_type)
        {
          if (current_ctx_)
          {
            current_ctx_->ReportCodegenFailure();
          }
          continue;
        }
        llvm::FunctionType *ft = abi.func_type;

        llvm::Function *f = module_->getFunction(proc->symbol);
        if (f)
        {
          ++pass1_reused;
          if (f->getFunctionType() != ft && current_ctx_)
          {
            ++pass1_signature_mismatch;
            std::string existing_ty_text;
            std::string expected_ty_text;
            {
              llvm::raw_string_ostream existing_os(existing_ty_text);
              f->getFunctionType()->print(existing_os);
            }
            {
              llvm::raw_string_ostream expected_os(expected_ty_text);
              ft->print(expected_os);
            }
            std::cerr << "[uv] pass1 signature mismatch kind=proc symbol="
                      << proc->symbol << " existing=" << existing_ty_text
                      << " expected=" << expected_ty_text << "\n";
            current_ctx_->ReportCodegenFailure();
          }
        }
        else
        {
          f = llvm::Function::Create(
              ft, linkage, proc->symbol, module_.get());
          ++pass1_created;
        }
        f->setLinkage(linkage);
        f->setCallingConv(CallingConvForAbi(proc->abi));
        ApplyProcFunctionAttrs(*proc, f);

        for (std::size_t idx = 0; idx < abi.llvm_param_attrs.size(); ++idx) {
          if (idx >= f->arg_size()) {
            continue;
          }
          llvm::AttrBuilder b(context_);
          AddAttrSetToBuilder(b, abi.llvm_param_attrs[idx]);
          if (b.hasAttributes()) {
            f->addParamAttrs(static_cast<unsigned>(idx), b);
          }
        }

        if (IsDropGlueSymbol(proc->symbol))
        {
          f->setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
          f->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
          if (auto *comdat = module_->getOrInsertComdat(proc->symbol))
          {
            comdat->setSelectionKind(llvm::Comdat::Any);
            f->setComdat(comdat);
          }
        }

        functions_[proc->symbol] = f;
      }
      else if (auto *ext = std::get_if<ExternProcIR>(&decl))
      {
        ++pass1_extern_count;
        // Extern procedures always cross a foreign ABI boundary. Aggregate
        // returns must therefore follow platform C ABI lowering.
        ABICallResult abi = ComputeCallABI(
            *this,
            ext->params,
            ext->ret,
            /*use_c_abi_aggregate_sret=*/true,
            /*foreign_boundary_mode_independent=*/true);
        if (!abi.valid || !abi.func_type)
        {
          if (current_ctx_)
          {
            current_ctx_->ReportCodegenFailure();
          }
          continue;
        }
        llvm::FunctionType *ft = abi.func_type;

        llvm::Function *f = module_->getFunction(ext->symbol);
        if (f)
        {
          ++pass1_reused;
          if (f->getFunctionType() != ft && current_ctx_)
          {
            ++pass1_signature_mismatch;
            std::string existing_ty_text;
            std::string expected_ty_text;
            {
              llvm::raw_string_ostream existing_os(existing_ty_text);
              f->getFunctionType()->print(existing_os);
            }
            {
              llvm::raw_string_ostream expected_os(expected_ty_text);
              ft->print(expected_os);
            }
            std::cerr << "[uv] pass1 signature mismatch kind=extern symbol="
                      << ext->symbol << " existing=" << existing_ty_text
                      << " expected=" << expected_ty_text << "\n";
            current_ctx_->ReportCodegenFailure();
          }
        }
        else
        {
          f = llvm::Function::Create(
              ft, llvm::GlobalValue::ExternalLinkage, ext->symbol, module_.get());
          ++pass1_created;
        }
        f->setCallingConv(CallingConvForAbi(ext->abi));

        for (std::size_t idx = 0; idx < abi.llvm_param_attrs.size(); ++idx) {
          if (idx >= f->arg_size()) {
            continue;
          }
          llvm::AttrBuilder b(context_);
          AddAttrSetToBuilder(b, abi.llvm_param_attrs[idx]);
          if (b.hasAttributes()) {
            f->addParamAttrs(static_cast<unsigned>(idx), b);
          }
        }

        functions_[ext->symbol] = f;
      }
    }

    if (!RuntimeDeclsOk(*module_) || !RuntimeDeclsCover(*module_, expanded_decls))
    {
      if (current_ctx_)
      {
        current_ctx_->ReportCodegenFailure();
      }
    }
    if (perf_enabled)
    {
      const auto now = Clock::now();
      declare_functions_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    // Dynamic object materialization needs vtable globals to exist before proc
    // bodies are emitted, even though the actual initializers are filled later
    // when GlobalVTable decls are visited in pass 2.
    llvm::Type *usize_ty = llvm::Type::getInt64Ty(context_);
    llvm::Type *ptr_ty = GetOpaquePtr();
    for (const auto &decl : expanded_decls)
    {
      const auto *vtable = std::get_if<GlobalVTable>(&decl);
      if (!vtable)
      {
        continue;
      }

      llvm::GlobalVariable *existing = module_->getNamedGlobal(vtable->symbol);
      if (existing)
      {
        globals_[vtable->symbol] = existing;
        continue;
      }

      std::vector<llvm::Type *> field_tys;
      field_tys.reserve(3 + vtable->slots.size());
      field_tys.push_back(usize_ty);
      field_tys.push_back(usize_ty);
      field_tys.push_back(ptr_ty);
      for (std::size_t i = 0; i < vtable->slots.size(); ++i)
      {
        field_tys.push_back(ptr_ty);
      }

      llvm::StructType *vtable_ty = llvm::StructType::get(context_, field_tys);
      auto *placeholder = new llvm::GlobalVariable(
          *module_,
          vtable_ty,
          false,
          llvm::GlobalValue::InternalLinkage,
          llvm::Constant::getNullValue(vtable_ty),
          vtable->symbol);
      globals_[vtable->symbol] = placeholder;
    }

    // Procedure bodies may read module-scope statics before their declaration
    // appears in source order. Define static globals before emitting bodies so
    // symbol reads bind to the owning definition instead of creating an
    // external declaration that later collides with the real definition.
    for (const auto &decl : expanded_decls)
    {
      if (const auto *global = std::get_if<GlobalConst>(&decl))
      {
        EmitGlobalConst(*global);
        continue;
      }
      if (const auto *global = std::get_if<GlobalZero>(&decl))
      {
        EmitGlobalZero(*global);
        continue;
      }
    }

    // Pass 2: emit definitions
    for (const auto &decl : expanded_decls)
    {
      if (std::holds_alternative<GlobalConst>(decl) ||
          std::holds_alternative<GlobalZero>(decl))
      {
        continue;
      }

      const DeclPerfKind decl_kind =
          perf_enabled ? DeclPerfKindOf(decl) : DeclPerfKind::Count;
      const auto decl_start = perf_enabled ? Clock::now() : Clock::time_point{};

      EmitDecl(decl);

      if (perf_enabled)
      {
        const auto decl_end = Clock::now();
        const long long decl_ms = ElapsedMs(decl_start, decl_end);
        auto &bucket = decl_perf[static_cast<std::size_t>(decl_kind)];
        bucket.count += 1;
        bucket.total_ms += decl_ms;

        const bool is_slowest = decl_ms > bucket.max_ms;
        const bool log_decl =
            DeclPerfKindHasBody(decl_kind) && decl_ms >= slow_decl_threshold_ms;
        std::string decl_label;
        if (is_slowest || log_decl)
        {
          decl_label = DeclPerfLabel(decl);
        }
        if (is_slowest)
        {
          bucket.max_ms = decl_ms;
          bucket.slowest_label = decl_label;
        }
        if (log_decl)
        {
          EmitPerfLogLine("module=" + perf_module_label + " stage=emit-decl kind=" +
                          DeclPerfKindName(decl_kind) + " ms=" +
                          std::to_string(decl_ms) + " label=" + decl_label);
        }
      }
    }
    if (perf_enabled)
    {
      const auto now = Clock::now();
      emit_defs_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    if (ctx.shared_library_project)
    {
      EmitLibraryEntryPoint();
      EmitPosixLibraryLifecycleHooks();
    }

    if (ctx.hosted_library && !ctx.hosted_exports.empty())
    {
      EmitHostedLifecycleExports();
      EmitHostedExportThunks();
    }

    if (ctx.main_symbol.has_value())
    {
      EmitEntryPoint();
    }
    ApplySharedLibraryDefinitionVisibility();
    if (perf_enabled)
    {
      const auto now = Clock::now();
      entrypoint_ms = ElapsedMs(phase_start, now);
      phase_start = now;
    }

    if (core::IsDebugEnabled("obj"))
    {
      // Optional IR dump for dynamic-dispatch debugging when --debug obj is enabled.
      for (auto &fn : *module_)
      {
        const std::string fn_name = fn.getName().str();
        if (core::IsDebugEnabled("obj") &&
            (fn_name.find("InvokeDescribable") != std::string::npos ||
             fn_name.find("describe_value") != std::string::npos ||
             fn_name.find("RunDynamicDispatchAlphaBeta") != std::string::npos))
        {
          std::string ir_str;
          llvm::raw_string_ostream os(ir_str);
          fn.print(os);
          os.flush();
          std::fprintf(stderr, "[llvm-ir-dump] %s:\n%s\n", fn_name.c_str(), ir_str.c_str());
        }
      }
    }
    if (perf_enabled)
    {
      const auto now = Clock::now();
      debug_ir_dump_ms = ElapsedMs(phase_start, now);
      phase_start = now;

      for (std::size_t i = 0; i < decl_perf.size(); ++i)
      {
        const auto kind = static_cast<DeclPerfKind>(i);
        const auto &bucket = decl_perf[i];
        if (bucket.count == 0)
        {
          continue;
        }
        const long long avg_ms =
            bucket.total_ms / static_cast<long long>(bucket.count);
        std::string summary = "module=" + perf_module_label +
                              " stage=emit-decls kind=" + DeclPerfKindName(kind) +
                              " count=" + std::to_string(bucket.count) +
                              " total_ms=" + std::to_string(bucket.total_ms) +
                              " avg_ms=" + std::to_string(avg_ms) +
                              " max_ms=" + std::to_string(bucket.max_ms);
        if (!bucket.slowest_label.empty())
        {
          summary += " max_label=" + bucket.slowest_label;
        }
        EmitPerfLogLine(summary);
      }

      EmitPerfLogLine(
          "module=" + perf_module_label + " stage=emit-module total_ms=" +
          std::to_string(ElapsedMs(module_start, phase_start)) + " setup_ms=" +
          std::to_string(setup_ms) + " declare_runtime_ms=" +
          std::to_string(declare_runtime_ms) + " declare_functions_ms=" +
          std::to_string(declare_functions_ms) + " emit_defs_ms=" +
          std::to_string(emit_defs_ms) + " entrypoint_ms=" +
          std::to_string(entrypoint_ms) + " debug_ir_dump_ms=" +
          std::to_string(debug_ir_dump_ms) + " decl_count=" +
          std::to_string(expanded_decls.size()) + " pass1_proc_count=" +
          std::to_string(pass1_proc_count) + " pass1_extern_count=" +
          std::to_string(pass1_extern_count) + " pass1_reused=" +
          std::to_string(pass1_reused) + " pass1_created=" +
          std::to_string(pass1_created) + " pass1_sig_mismatch=" +
          std::to_string(pass1_signature_mismatch) + " has_main_symbol=" +
          (ctx.main_symbol.has_value() ? "1" : "0"));
    }

    return module_.get();
  }

  void LLVMEmitter::EmitDecl(const IRDecl &decl)
  {
    struct Visitor
    {
      LLVMEmitter &emitter;
      void operator()(const ProcIR &proc) { emitter.EmitProc(proc); }
      void operator()(const ExternProcIR &proc) { emitter.EmitExternProc(proc); }
      void operator()(const GlobalConst &global) { emitter.EmitGlobalConst(global); }
      void operator()(const GlobalZero &global) { emitter.EmitGlobalZero(global); }
      void operator()(const GlobalVTable &vtable) { emitter.EmitVTable(vtable); }
    };
    std::visit(Visitor{*this}, decl);
    if (current_ctx_ && current_ctx_->codegen_failed)
    {
      SPEC_RULE("LowerIRDecl-Err");
    }
  }

  void LLVMEmitter::EmitExternProc(const ExternProcIR &proc)
  {
    if (!proc.raw_dylib_library_name.has_value() ||
        !proc.raw_dylib_foreign_symbol.has_value())
    {
      return;
    }

    llvm::Function *func = functions_[proc.symbol];
    if (!func || !func->empty())
    {
      return;
    }

    SPEC_RULE("CG-Item-ExternProc");

    auto *opaque_ptr_ty =
        llvm::cast<llvm::PointerType>(GetOpaquePtr());
    llvm::GlobalVariable *cache =
        module_->getGlobalVariable(proc.symbol + "__raw_dylib_cache");
    if (!cache)
    {
      cache = new llvm::GlobalVariable(
          *module_,
          opaque_ptr_ty,
          false,
          llvm::GlobalValue::InternalLinkage,
          llvm::ConstantPointerNull::get(opaque_ptr_ty),
          proc.symbol + "__raw_dylib_cache");
      cache->setAlignment(llvm::Align(8));
    }

    llvm::BasicBlock *entry =
        llvm::BasicBlock::Create(context_, "entry", func);
    llvm::BasicBlock *resolve_block =
        llvm::BasicBlock::Create(context_, "raw_dylib.resolve", func);
    llvm::BasicBlock *call_block =
        llvm::BasicBlock::Create(context_, "raw_dylib.call", func);
    llvm::BasicBlock *call_cont =
        llvm::BasicBlock::Create(context_, "raw_dylib.call.cont", func);
    llvm::BasicBlock *panic_block =
        llvm::BasicBlock::Create(context_, "raw_dylib.panic", func);

    llvm::IRBuilder<> builder(entry);
    llvm::Value *cached_ptr =
        builder.CreateLoad(opaque_ptr_ty, cache, "raw_dylib.cached");
    llvm::Value *cache_miss = builder.CreateICmpEQ(
        cached_ptr, llvm::ConstantPointerNull::get(opaque_ptr_ty));
    builder.CreateCondBr(cache_miss, resolve_block, call_block);

    builder.SetInsertPoint(resolve_block);
    llvm::Function *resolve_fn = module_->getFunction(RawDylibResolveSym());
    if (!resolve_fn)
    {
      llvm::FunctionType *resolve_ty = llvm::FunctionType::get(
          opaque_ptr_ty,
          {opaque_ptr_ty, opaque_ptr_ty},
          false);
      resolve_fn = llvm::Function::Create(
          resolve_ty,
          llvm::GlobalValue::ExternalLinkage,
          RawDylibResolveSym(),
          module_.get());
      resolve_fn->setCallingConv(llvm::CallingConv::C);
    }

    llvm::Value *dll_name = builder.CreateGlobalStringPtr(
        *proc.raw_dylib_library_name,
        proc.symbol + "__raw_dylib_dll");
    llvm::Value *foreign_symbol = builder.CreateGlobalStringPtr(
        *proc.raw_dylib_foreign_symbol,
        proc.symbol + "__raw_dylib_symbol");
    llvm::CallInst *resolved_call = builder.CreateCall(
        resolve_fn->getFunctionType(),
        resolve_fn,
        {dll_name, foreign_symbol});
    resolved_call->setCallingConv(llvm::CallingConv::C);
    builder.CreateStore(resolved_call, cache);
    builder.CreateBr(call_block);

    builder.SetInsertPoint(call_block);
    llvm::PHINode *callee_ptr = builder.CreatePHI(
        opaque_ptr_ty, 2, "raw_dylib.callee");
    callee_ptr->addIncoming(cached_ptr, entry);
    callee_ptr->addIncoming(resolved_call, resolve_block);
    llvm::Value *resolve_failed = builder.CreateICmpEQ(
        callee_ptr, llvm::ConstantPointerNull::get(opaque_ptr_ty));
    builder.CreateCondBr(resolve_failed, panic_block, call_cont);

    builder.SetInsertPoint(call_cont);
    std::vector<llvm::Value *> args;
    args.reserve(func->arg_size());
    for (llvm::Argument &arg : func->args())
    {
      args.push_back(&arg);
    }
    llvm::Value *result = EmitABICall(
        *this,
        &builder,
        callee_ptr,
        proc.params,
        proc.ret,
        args,
        /*use_c_abi_aggregate_sret=*/false,
        /*ffi_import_boundary=*/false,
        /*ffi_import_catch=*/false,
        CallingConvForAbi(proc.abi));
    if (func->getReturnType()->isVoidTy())
    {
      builder.CreateRetVoid();
    }
    else
    {
      if (!result)
      {
        result = llvm::Constant::getNullValue(func->getReturnType());
      }
      if (result->getType() != func->getReturnType())
      {
        result = CoerceValue(&builder, result, func->getReturnType());
      }
      builder.CreateRet(result);
    }

    builder.SetInsertPoint(panic_block);
    const std::string panic_sym = PanicSym();
    llvm::Function *panic_fn = module_->getFunction(panic_sym);
    if (!panic_fn)
    {
      llvm::FunctionType *panic_ty = llvm::FunctionType::get(
          llvm::Type::getVoidTy(context_),
          {llvm::Type::getInt32Ty(context_)},
          false);
      panic_fn = llvm::Function::Create(
          panic_ty,
          llvm::GlobalValue::ExternalLinkage,
          panic_sym,
          module_.get());
      panic_fn->setCallingConv(llvm::CallingConv::C);
    }
    builder.CreateCall(
        panic_fn->getFunctionType(),
        panic_fn,
        {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                PanicCode(PanicReason::Other))});
    builder.CreateUnreachable();
  }
}  // namespace ultraviolet::codegen
