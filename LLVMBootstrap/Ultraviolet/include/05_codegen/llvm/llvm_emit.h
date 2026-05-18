#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <system_error>
#include <vector>

#include "01_project/target_profile.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/llvm/llvm_attr.h"
#include "05_codegen/lower/lower_expr.h"

// Forward declare LLVM types to avoid pulling in all headers here
namespace llvm {
  class LLVMContext;
  class Module;
  class IRBuilderBase;
  template <typename T, typename Inserter> class IRBuilder;
  class Type;
  class AllocaInst;
  class Value;
  class Function;
  class BasicBlock;
  class FunctionType;
  class AttributeList;
  class SwitchInst;
}

namespace ultraviolet::codegen {

struct AsyncEmitState {
  const LowerCtx::AsyncProcInfo* info = nullptr;
  llvm::Value* frame_ptr = nullptr;
  llvm::Value* input_ptr = nullptr;
  llvm::SwitchInst* resume_switch = nullptr;
  std::unordered_map<std::size_t, llvm::BasicBlock*> resume_blocks;
  bool emitting_resume_prelude = false;
};


class LLVMEmitter {
public:
  // Construct with a context and target info
  LLVMEmitter(llvm::LLVMContext& ctx,
              const std::string& module_name,
              project::TargetProfile profile);
  ~LLVMEmitter();

  // §6.12.9 LLVM IR Emission Pipeline
  
  // T-LLVM-001: Set module header (triple, datalayout)
  void SetupModule();

  // T-LLVM-008: Emit the full module
  // Returns the generated LLVM module on success, or error on failure
  llvm::Module* EmitModule(const IRDecls& decls, LowerCtx& ctx);

  // T-LLVM-007: Type Mapping helpers
  llvm::Type* GetLLVMType(analysis::TypeRef type);
  llvm::Type* GetOpaquePtr(); // T-LLVM-002

  // T-LLVM-006: Runtime Declarations
  void DeclareRuntime();
  void DeclareRuntime(const std::vector<std::string>& symbols);

  // T-LLVM-009 / T-LLVM-010 Helpers
  // Evaluate an IRValue to an llvm::Value*
  llvm::Value* EvaluateIRValue(const IRValue& val);
  
  // T-LLVM-010: Bind local variable
  void EmitBindVar(const IRBindVar& bind);
  
  // T-LLVM-009: Instruction emission
  void EmitIR(const IRPtr& ir);



  // Accessors
  llvm::Module& GetModule() { return *module_; }
  llvm::LLVMContext& GetContext() { return context_; }
  project::TargetProfile GetTargetProfile() const { return target_profile_; }
  LowerCtx* GetCurrentCtx() { return current_ctx_; }
  const LowerCtx* GetCurrentCtx() const { return current_ctx_; }
  
  // Ownership transfer
  std::unique_ptr<llvm::Module> ReleaseModule();
  
  // Builder access (needs casting in impl)
  void* GetBuilderRaw() { return builder_.get(); }
  
  // Local variable management
  void SetLocal(const std::string& name, llvm::Value* val) { locals_[name] = val; }
  void RegisterLocalBindStorage(const std::string& name, llvm::Value* val);
  void SetLocalHomeStorage(const std::string& name, llvm::Value* val) {
    if (val) {
      local_home_storage_[name] = val;
    } else {
      local_home_storage_.erase(name);
    }
  }
  void SetLocalType(const std::string& name, analysis::TypeRef type) {
    if (!type) {
      return;
    }
    local_types_[name] = type;
  }
  llvm::Value* GetLocal(const std::string& name) { return locals_.count(name) ? locals_[name] : nullptr; }
  llvm::Value* GetLocalBindStorage(const std::string& name);
  llvm::Value* GetLocalHomeStorage(const std::string& name) {
    return local_home_storage_.count(name) ? local_home_storage_[name] : nullptr;
  }
  analysis::TypeRef LookupLocalType(const std::string& name) const {
    auto it = local_types_.find(name);
    return it != local_types_.end() ? it->second : nullptr;
  }
  llvm::Value* GetGlobal(const std::string& name) { return globals_.count(name) ? globals_[name] : nullptr; }
  llvm::Function* GetFunction(const std::string& name) { return functions_.count(name) ? functions_[name] : nullptr; }
  void RemoveLocal(const std::string& name) { locals_.erase(name); }
  void ClearLocals() { locals_.clear(); local_home_storage_.clear(); local_types_.clear(); }
  bool IsHostedLibraryBuild() const;
  bool RequiresHostedEnvParam(const std::string& symbol) const;
  bool HasHostedStateSlot(const std::string& symbol) const;
  llvm::Value* GetHostedCurrentEnvPtr();
  llvm::Value* GetSharedLibraryImagePanicPtr(llvm::Value* fallback_ptr = nullptr);
  llvm::Value* GetHostedStatePtr(const std::string& symbol,
                                 llvm::Type* value_ty,
                                 llvm::Value* fallback_ptr = nullptr);
  llvm::Value* GetHostedSessionPanicPtr(llvm::Value* fallback_ptr = nullptr);

  // Temporary IRValue materialization cache
  void SetTempValue(const IRValue& value, llvm::Value* llvm_value) {
    if (value.kind == IRValue::Kind::Opaque) {
      values_[value.name] = llvm_value;
    }
  }
  llvm::Value* GetTempValue(const IRValue& value) const {
    if (value.kind != IRValue::Kind::Opaque) {
      return nullptr;
    }
    auto it = values_.find(value.name);
    return it != values_.end() ? it->second : nullptr;
  }
  void SetTempStorage(const IRValue& value, llvm::Value* llvm_storage) {
    if (value.kind == IRValue::Kind::Opaque) {
      storage_values_[value.name] = llvm_storage;
    }
  }
  llvm::Value* GetTempStorage(const IRValue& value) const {
    if (value.kind != IRValue::Kind::Opaque) {
      return nullptr;
    }
    auto it = storage_values_.find(value.name);
    return it != storage_values_.end() ? it->second : nullptr;
  }
  llvm::Value* GetAddressableStorage(const IRValue& value);
  struct FlowStateSnapshot {
    std::unordered_map<std::string, llvm::Value*> locals;
    std::unordered_map<std::string, llvm::Value*> local_home_storage;
    std::unordered_map<std::string, analysis::TypeRef> local_types;
    std::unordered_map<std::string, llvm::Value*> values;
    std::unordered_map<std::string, llvm::Value*> storage_values;
    std::unordered_map<std::string, llvm::Value*> preferred_result_storage;
  };
  FlowStateSnapshot SaveFlowState() const;
  void RestoreFlowState(const FlowStateSnapshot& snapshot);
  void SetPreferredResultStorage(const IRValue& value, llvm::Value* storage) {
    if (value.kind == IRValue::Kind::Opaque && storage) {
      preferred_result_storage_[value.name] = storage;
    }
  }
  llvm::Value* TakePreferredResultStorage(const IRValue& value) {
    if (value.kind != IRValue::Kind::Opaque) {
      return nullptr;
    }
    auto it = preferred_result_storage_.find(value.name);
    if (it == preferred_result_storage_.end()) {
      return nullptr;
    }
    llvm::Value* storage = it->second;
    preferred_result_storage_.erase(it);
    return storage;
  }
  llvm::AllocaInst* AcquireReusableAggregateStorage(llvm::Function* func,
                                                    llvm::Type* ty,
                                                    std::string_view name);
  void ReleaseReusableAggregateStorage(llvm::Value* storage);
  void ForgetTempStorage(const IRValue& value);
  void ReleaseTempStorage(const IRValue& value);
  void ReleaseMoveConsumedStorage(const IRValue& value);
  void ClearTempValues() {
    values_.clear();
    storage_values_.clear();
    preferred_result_storage_.clear();
  }

  // Async lowering state (active only while emitting async resume proc)
  void SetAsyncState(AsyncEmitState* state) { async_state_ = state; }
  AsyncEmitState* GetAsyncState() { return async_state_; }
  const AsyncEmitState* GetAsyncState() const { return async_state_; }

  // Symbol aliases for IRReadPath resolution
  void SetSymbolAlias(const std::string& name, const std::string& symbol) {
    symbol_aliases_[name] = symbol;
  }
  std::optional<std::string> LookupSymbolAlias(const std::string& name) const {
    auto it = symbol_aliases_.find(name);
    if (it == symbol_aliases_.end()) {
      return std::nullopt;
    }
    return it->second;
  }
  void ClearSymbolAliases() { symbol_aliases_.clear(); }

  // Active region tracking for implicit region allocation
  void PushActiveRegion(const IRValue& region) { active_regions_.push_back(region); }
  void PopActiveRegion() { if (!active_regions_.empty()) active_regions_.pop_back(); }
  const IRValue* CurrentActiveRegion() const {
    return active_regions_.empty() ? nullptr : &active_regions_.back();
  }

  // UVX Extension: Parallel context tracking for spawn/dispatch (§18)
  void PushParallelContext(const IRValue& ctx) { parallel_contexts_.push_back(ctx); }
  void PopParallelContext() { if (!parallel_contexts_.empty()) parallel_contexts_.pop_back(); }
  const IRValue* CurrentParallelContext() const {
    return parallel_contexts_.empty() ? nullptr : &parallel_contexts_.back();
  }

  // Loop control targets for IRBreak / IRContinue lowering.
  void PushLoopTargets(llvm::BasicBlock* break_target,
                       llvm::BasicBlock* continue_target,
                       llvm::Value* break_value_slot = nullptr,
                       analysis::TypeRef break_result_type = nullptr);
  void PopLoopTargets();
  llvm::BasicBlock* CurrentLoopBreakTarget() const;
  llvm::BasicBlock* CurrentLoopContinueTarget() const;
  llvm::Value* CurrentLoopBreakValueSlot() const;
  analysis::TypeRef CurrentLoopBreakResultType() const;

private:
  llvm::LLVMContext& context_;
  std::unique_ptr<llvm::Module> module_;
  std::unique_ptr<llvm::IRBuilderBase> builder_; // Use base class to hide template
  project::TargetProfile target_profile_;
  
  LowerCtx* current_ctx_ = nullptr;

  // Track declarations

  std::unordered_map<std::string, llvm::Function*> functions_;
  std::unordered_map<std::string, llvm::Value*> globals_;
  std::unordered_map<std::string, llvm::Value*> locals_;
  std::unordered_map<std::string, llvm::Value*> local_home_storage_;
  std::unordered_map<std::string, analysis::TypeRef> local_types_;

  std::unordered_map<std::string, llvm::Value*> values_;
  std::unordered_map<std::string, llvm::Value*> storage_values_;
  std::unordered_map<std::string, llvm::Value*> preferred_result_storage_;
  std::unordered_map<llvm::Function*,
                     std::unordered_map<llvm::Type*, std::vector<llvm::AllocaInst*>>> reusable_aggregate_storage_;
  AsyncEmitState* async_state_ = nullptr;
  std::unordered_map<std::string, std::string> symbol_aliases_;
  std::vector<IRValue> active_regions_;
  std::vector<IRValue> parallel_contexts_;  // UVX Extension: §18 parallel context stack
  std::vector<llvm::BasicBlock*> loop_break_targets_;
  std::vector<llvm::BasicBlock*> loop_continue_targets_;
  std::vector<llvm::Value*> loop_break_value_slots_;
  std::vector<analysis::TypeRef> loop_break_result_types_;


  // Type cache
  std::unordered_map<analysis::TypeRef, llvm::Type*> type_cache_;
  struct HostedStateSlot {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint64_t align = 1;
    bool zero_init = false;
    std::vector<std::uint8_t> bytes;
  };
  struct HostedSessionLayout {
    bool active = false;
    std::uint64_t size = 0;
    std::uint64_t align = 1;
    std::uint64_t context_offset = 0;
    std::uint64_t panic_offset = 0;
    std::unordered_map<std::string, HostedStateSlot> slots;
  };
  HostedSessionLayout hosted_layout_;
  llvm::Value* hosted_env_value_ = nullptr;

  // Internal helpers
public:
  void EmitDecl(const IRDecl& decl);
  void EmitProc(const ProcIR& proc);
  void EmitExternProc(const ExternProcIR& proc);
  void EmitGlobalConst(const GlobalConst& global);
  void EmitGlobalZero(const GlobalZero& global);
  void EmitVTable(const GlobalVTable& vtable);
  
  // T-LLVM-015: Entrypoint Generation
  void EmitEntryPoint();
  void EmitLibraryEntryPoint();
  void EmitPosixLibraryLifecycleHooks();
  void EmitHostedLifecycleExports();
  void EmitHostedExportThunks();
  void ApplySharedLibraryDefinitionVisibility();
  
  // T-LLVM-016: Poison Instrumentation
  void EmitPoisonCheck(const std::string& module_name);


  // Attribute helpers (T-LLVM-003)
  void AddPtrAttributes(llvm::Function* func, unsigned arg_idx, analysis::TypeRef type);
  
};
  


// Main entry point for T-LLVM-008
llvm::Module* EmitLLVM(const IRDecls& decls,
                       LowerCtx& ctx,
                       llvm::LLVMContext& llvm_ctx,
                       project::TargetProfile profile);

} // namespace ultraviolet::codegen
