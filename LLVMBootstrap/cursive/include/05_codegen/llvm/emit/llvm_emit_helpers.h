#pragma once

#include "00_core/process_config.h"
#include "00_core/host/services.h"
#include "00_core/spec_trace.h"
#include "00_core/symbols.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/layout/layout.h"
#include "04_analysis/modal/builtin_modal_intrinsics.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/modal/modal_widen.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_lookup.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_predicates.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/checks/panic.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/dyn_dispatch/dyn_dispatch.h"
#include "05_codegen/globals/binding_storage.h"
#include "05_codegen/globals/entrypoint.h"
#include "05_codegen/globals/globals.h"
#include "05_codegen/globals/init.h"
#include "05_codegen/globals/literal_emit.h"
#include "05_codegen/intrinsics/async_frame.h"
#include "05_codegen/intrinsics/builtins.h"
#include "05_codegen/intrinsics/intrinsics_interface.h"
#include "05_codegen/llvm/emit/internal_helpers.h"
#include "05_codegen/llvm/llvm_attr.h"
#include "05_codegen/llvm/llvm_call.h"
#include "05_codegen/llvm/llvm_ir_panic.h"
#include "05_codegen/llvm/llvm_module.h"
#include "05_codegen/llvm/llvm_types.h"
#include "05_codegen/llvm/llvm_ub_safe.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace cursive::codegen::emit_detail {



    using AsyncCombinatorKind = analysis::BuiltinAsyncCombinatorKind;
    std::optional<AsyncCombinatorKind> AsyncCombinatorKindFromSymbol(
        std::string_view symbol)
    ;

    llvm::Value *EmitSliceLenFromAddr(LLVMEmitter &emitter,
                                      llvm::IRBuilder<> &builder,
                                      const analysis::TypeRef &type,
                                      llvm::Value *addr)
    ;





    llvm::Value *EmitIndexLenFromAddr(LLVMEmitter &emitter,
                                      llvm::IRBuilder<> &builder,
                                      const analysis::TypeRef &type,
                                      llvm::Value *addr)
    ;




    llvm::Value *EmitSequenceDataPtrFromAddr(LLVMEmitter &emitter,
                                             llvm::IRBuilder<> &builder,
                                             const analysis::TypeRef &type,
                                             llvm::Value *addr)
    ;





    struct IndexedSequenceIterState
    {
      analysis::TypeRef element_type = nullptr;
      llvm::Value *length = nullptr;
      llvm::Value *array_ptr = nullptr;
      llvm::ArrayType *array_type = nullptr;
      llvm::Value *data_ptr = nullptr;
    };

    struct IndexedSequenceLoweredIterState
    {
      IndexedSequenceIterState iter;
      llvm::AllocaInst *idx_slot = nullptr;
      llvm::AllocaInst *elem_slot = nullptr;
    };

    bool PrepareIndexedSequenceIter(LLVMEmitter &emitter,
                                    llvm::IRBuilder<> &entry_builder,
                                    llvm::IRBuilder<> &builder,
                                    const analysis::TypeRef &type,
                                    llvm::Value *value,
                                    IndexedSequenceIterState &out)
    ;













    llvm::Value *EmitIndexedSequenceElem(LLVMEmitter &emitter,
                                         llvm::IRBuilder<> &builder,
                                         const IndexedSequenceIterState &iter,
                                         llvm::Value *index);

    bool EmitSeqIterInit(LLVMEmitter &emitter,
                         llvm::IRBuilder<> &entry_builder,
                         llvm::IRBuilder<> &builder,
                         const analysis::TypeRef &type,
                         llvm::Value *value,
                         IndexedSequenceLoweredIterState &out)
    ;



    llvm::Value *EmitSeqIterNext(LLVMEmitter &emitter,
                                 llvm::IRBuilder<> &builder,
                                 const IndexedSequenceLoweredIterState &iter)
    ;








    llvm::Value *LoadSeqIterElem(llvm::IRBuilder<> &builder,
                                 const IndexedSequenceLoweredIterState &iter)
    ;

    llvm::Value *EmitIndexedSequenceElem(LLVMEmitter &emitter,
                                         llvm::IRBuilder<> &builder,
                                         const IndexedSequenceIterState &iter,
                                         llvm::Value *index)
    ;






    struct IRNodePerfBucket
    {
      std::size_t count = 0;
      long long total_self_ms = 0;
      long long max_self_ms = 0;
    };

    struct IRNodePerfFrame
    {
      std::size_t kind_index = 0;
      std::chrono::steady_clock::time_point start;
      long long child_ms = 0;
    };

    inline constexpr std::size_t kIRNodePerfKindCount = 63;

    struct IRProcPerfContext
    {
      std::array<IRNodePerfBucket, kIRNodePerfKindCount> buckets{};
      std::vector<IRNodePerfFrame> stack;
    };

    extern thread_local IRProcPerfContext *g_ir_proc_perf_ctx;

    const char *IRNodePerfKindName(std::size_t index)
    ;

    long long IRProcPerfTotalSelfMs(const IRProcPerfContext &ctx)
    ;

    void AppendTopIRNodePerf(std::string &line, const IRProcPerfContext &ctx)
    ;






    bool IsClosurePairLLVMType(llvm::Type *ty)
    ;

    llvm::Function *FunctionFromLLVMValue(llvm::Value *value)
    ;

    std::uint64_t AlignUpBytes(std::uint64_t value, std::uint64_t align)
    ;

    std::vector<ast::ModulePath> ComputeEntryInitOrder(const LowerCtx &ctx)
    ;








    llvm::Value *CreateTaggedPayloadI8Ptr(LLVMEmitter &emitter,
                                          llvm::IRBuilder<> *builder,
                                          llvm::StructType *tagged_ty,
                                          llvm::Value *tagged_slot,
                                          std::uint64_t payload_align)
    ;



    bool IsUnitTypeRef(const analysis::TypeRef &type)
    ;

    bool IsNeverTypeRef(const analysis::TypeRef &type)
    ;

    bool IsRuntimeHandleModalPath(const analysis::TypePath &path)
    ;

    std::uint64_t AsyncStateIndexOrDefault(const analysis::ScopeContext &scope,
                                           std::string_view state_name,
                                           std::uint64_t fallback)
    ;

    struct AsyncStateDiscs
    {
      std::uint64_t suspended = 0;
      std::uint64_t completed = 1;
      std::optional<std::uint64_t> failed;
    };

    AsyncStateDiscs LoweredAsyncStateDiscs(
        const analysis::ScopeContext &scope,
        const std::optional<::cursive::analysis::layout::LoweredAsyncType> &lowered)
    ;


    AsyncStateDiscs LoweredAsyncStateDiscs(
        const analysis::ScopeContext &scope,
        const analysis::TypeRef &async_type)
    ;

    AsyncStateDiscs LoweredAsyncStateDiscs(
        const analysis::ScopeContext &scope,
        const analysis::AsyncSig &sig)
    ;

    inline constexpr std::uint64_t kAsyncPayloadFramePtrOffset = 8;

    llvm::Value *AsyncFrameAddr(LLVMEmitter &emitter,
                                llvm::IRBuilder<> *builder,
                                llvm::Value *frame_ptr,
                                std::uint64_t offset)
    ;

    llvm::Value *AsyncFrameTypedPtr(LLVMEmitter &emitter,
                                    llvm::IRBuilder<> *builder,
                                    llvm::Value *frame_ptr,
                                    std::uint64_t offset,
                                    llvm::Type *pointee)
    ;

    llvm::Value *NullOpaquePtr(LLVMEmitter &emitter)
    ;

    llvm::Value *CoerceTo(llvm::IRBuilder<> *builder,
                          llvm::Value *value,
                          llvm::Type *target_ty);

    llvm::Value *CoerceBoolTo(llvm::IRBuilder<> *builder,
                              llvm::Value *value,
                              llvm::Type *target_ty);

    llvm::Value *CoerceToTyped(LLVMEmitter &emitter,
                               llvm::IRBuilder<> *builder,
                               llvm::Value *value,
                               llvm::Type *target_ty,
                               const analysis::TypeRef &source_type,
                               const analysis::TypeRef &target_type);

    llvm::Value *CoerceOrNullOpaquePtr(LLVMEmitter &emitter,
                                       llvm::IRBuilder<> *builder,
                                       llvm::Value *value)
    ;

    llvm::Value *EmitRuntimeCallBySymbol(LLVMEmitter &emitter,
                                         llvm::IRBuilder<> *builder,
                                         const std::string &symbol,
                                         const std::vector<llvm::Value *> &args)
    ;

      inline constexpr bool kUseCAbiAggregateSRet = true;


    llvm::Value *EmitAsyncResumeRuntimeCall(LLVMEmitter &emitter,
                                            llvm::IRBuilder<> *builder,
                                            llvm::Value *suspended,
                                            llvm::Value *input,
                                            llvm::Value *panic_out)
    ;

    void StoreAsyncFrameKeySnapshot(LLVMEmitter &emitter,
                                    llvm::IRBuilder<> *builder,
                                    llvm::Value *frame_ptr,
                                    llvm::Value *released_handle)
    ;

    void StoreAsyncFrameHostedEnv(LLVMEmitter &emitter,
                                  llvm::IRBuilder<> *builder,
                                  llvm::Value *frame_ptr,
                                  llvm::Value *hosted_env)
    ;

    llvm::Value *LoadAsyncFrameKeySnapshot(LLVMEmitter &emitter,
                                           llvm::IRBuilder<> *builder,
                                           llvm::Value *frame_ptr)
    ;

    llvm::Value *EmitKeyReleaseAll(LLVMEmitter &emitter,
                                   llvm::IRBuilder<> *builder)
    ;

    void EmitKeyReacquire(LLVMEmitter &emitter,
                          llvm::IRBuilder<> *builder,
                          llvm::Value *released_handle)
    ;

    llvm::Value *LoadLocalValue(LLVMEmitter &emitter,
                                llvm::IRBuilder<> *builder,
                                const std::string &name)
    ;

    bool StoreProcedureOutValue(LLVMEmitter &emitter,
                                llvm::IRBuilder<> *builder,
                                llvm::Function *func,
                                const std::string &symbol,
                                const LowerCtx::ProcSigInfo *sig,
                                llvm::Value *value,
                                const analysis::TypeRef &source_type)
    ;







    llvm::Value *ResolveProcedureOutPtr(LLVMEmitter &emitter,
                                        llvm::IRBuilder<> *builder,
                                        llvm::Function *func,
                                        const std::string &symbol,
                                        const LowerCtx::ProcSigInfo *sig)
    ;








    std::optional<std::size_t> ParseTupleFieldIndex(std::string_view text)
    ;

    struct FieldAccessMeta
    {
      std::size_t index = 0;
      analysis::TypeRef aggregate_type;
      analysis::TypeRef field_type;
      std::vector<analysis::TypeRef> aggregate_fields;
      ::cursive::analysis::layout::RecordLayoutOptions layout_options{};
    };

    analysis::TypeRef ResolveAliasTypeInScope(const analysis::ScopeContext &scope,
                                              const analysis::TypeRef &type,
                                              std::size_t depth = 0)
    ;






    std::optional<FieldAccessMeta> ResolveFieldAccessMeta(
        const analysis::ScopeContext &scope,
        const analysis::TypeRef &base_type,
        std::string_view field_name)
    ;











    llvm::Value *CoerceTo(llvm::IRBuilder<> *builder,
                          llvm::Value *value,
                          llvm::Type *target_ty)
    ;

    llvm::Value *CoerceBoolTo(llvm::IRBuilder<> *builder,
                              llvm::Value *value,
                              llvm::Type *target_ty)
    ;

    analysis::TypeRef StripPermType(const analysis::TypeRef &type)
    ;

    analysis::TypeRef ResolveAliasType(const LowerCtx *ctx,
                                       const analysis::TypeRef &type,
                                       std::size_t depth = 0)
    ;

    bool IsUnitType(const analysis::TypeRef &type)
    ;

    bool IsBoolType(const analysis::TypeRef &type)
    ;

    bool IsBoolBinOp(std::string_view op)
    ;

    bool IsNeverType(const analysis::TypeRef &type)
    ;

    std::optional<std::size_t> FindUnionMemberIndex(
        const std::vector<analysis::TypeRef> &members,
        const analysis::TypeRef &member_type)
    ;

    std::optional<std::size_t> InferUnionMemberIndexFromValue(
        LLVMEmitter &emitter,
        llvm::Value *source_value,
        const std::vector<analysis::TypeRef> &members)
    ;








    bool UnionDebugEnabled()
    ;

    llvm::Value *PackUnionFromMember(LLVMEmitter &emitter,
                                     llvm::IRBuilder<> *builder,
                                     llvm::Value *source_value,
                                     llvm::Type *target_ty,
                                     const analysis::TypeRef &source_type,
                                     const analysis::TypeRef &target_type)
    ;













    llvm::Value *CoerceToTyped(LLVMEmitter &emitter,
                               llvm::IRBuilder<> *builder,
                               llvm::Value *value,
                               llvm::Type *target_ty,
                               const analysis::TypeRef &source_type,
                               const analysis::TypeRef &target_type)
    ;







    llvm::Value *AsBool(llvm::IRBuilder<> *builder, llvm::Value *value)
    ;

    llvm::Value *EmitTypedEq(llvm::IRBuilder<> *builder,
                             llvm::Value *lhs,
                             llvm::Value *rhs);

    std::string LLVMValueRepr(llvm::Value *value)
    ;

    llvm::Value *EmitAggregateEq(llvm::IRBuilder<> *builder,
                                 llvm::Value *lhs,
                                 llvm::Value *rhs)
    ;




    llvm::Value *EmitTypedEq(llvm::IRBuilder<> *builder,
                             llvm::Value *lhs,
                             llvm::Value *rhs)
    ;



    std::string BasePlaceIdentifier(const std::string &repr)
    ;


}  // namespace cursive::codegen::emit_detail
