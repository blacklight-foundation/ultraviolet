#pragma once

#include "05_codegen/llvm/emit/llvm_emit_helpers.h"

namespace cursive::codegen::emit_detail {

struct IRInstructionVisitor
{
  LLVMEmitter &emitter;

  llvm::IRBuilder<> &builder;

  llvm::Type *ExpectedLLVMType(const IRValue &value) const;

  analysis::TypeRef LookupValueType(const IRValue &value) const;

  llvm::Value *DefaultFor(const IRValue &value) const;

  llvm::Value *EvaluateOrDefault(const IRValue &value) const;

  bool IsAddressBackedAggregateType(llvm::Type *ty) const;

  llvm::Value *ForwardedAggregateStorage(const IRValue &value) const;

  void SetForwardedOrMaterializedResult(const IRValue &value) const;

  std::optional<std::uint64_t> StaticRangeLength(const IRRange &range,
                                                 std::uint64_t base_len) const;

  std::optional<std::uint64_t> StaticLengthOf(const IRValue &value) const;

  analysis::TypeRef NormalizeValueType(const IRValue &value) const;

  bool IsDynamicSequenceType(const analysis::TypeRef &type) const;

  llvm::Value *DynamicLengthOf(const IRValue &value) const;

  std::optional<std::uint64_t> ImmediateU64(const IRValue &value) const;

  bool IsSignedIntegerType(const analysis::TypeRef &type) const;

  bool IsCharType(const analysis::TypeRef &type) const;

  llvm::Value *EmitBuiltinEqCall(llvm::IRBuilder<> &builder,
                                 const analysis::TypeRef &type,
                                 llvm::Value *lhs,
                                 llvm::Value *rhs) const;

  struct BuiltinSuccessorResult
  {
    llvm::Value *has_next = nullptr;
    llvm::Value *next = nullptr;
  };

  std::optional<BuiltinSuccessorResult> EmitBuiltinSuccessor(
      llvm::IRBuilder<> &builder,
      const analysis::TypeRef &type,
      llvm::Value *value) const;

  std::optional<BuiltinSuccessorResult> EmitBuiltinPredecessor(
      llvm::IRBuilder<> &builder,
      const analysis::TypeRef &type,
      llvm::Value *value) const;

  struct MaterializedRangeValue
  {
    IRRangeKind kind = IRRangeKind::Full;
    llvm::Value *lo = nullptr;
    llvm::Value *hi = nullptr;
  };

  std::optional<MaterializedRangeValue> ResolveRangeValue(
      const IRValue &value,
      llvm::Type *bound_ty = nullptr,
      std::optional<IRRangeKind> fallback_kind = std::nullopt) const;

  void operator()(const IROpaque &) const;

  void operator()(const IRSeq &seq) const;

  void operator()(const IRBindVar &bind) const;

  void operator()(const IRReadVar &) const;

  void operator()(const IRReadPath &read) const;

  void operator()(const IRStoreVar &store) const;

  void operator()(const IRStoreVarNoDrop &store) const;

  void operator()(const IRCall &call) const;

  void operator()(const IRUnaryOp &unary) const;

  void operator()(const IRFence &fence) const;

  void operator()(const IRBinaryOp &bin) const;

  void operator()(const IRIf &node) const;

  void operator()(const IRReturn &ret) const;

  void operator()(const IRResult &result) const;

  void operator()(const IRClearPanic &) const;

  void operator()(const IRPanicCheck &check) const;

  void operator()(const IRCleanupPanicCheck &check) const;

  void operator()(const IRLowerPanic &panic) const;

  void operator()(const IRCheckOp &check) const;

  void operator()(const IRCheckIndex &check) const;

  void operator()(const IRCheckRange &check) const;

  void operator()(const IRCheckSliceLen &check) const;

  void operator()(const IRCheckCast &check) const;

  void operator()(const IRReadPlace &) const;

  void operator()(const IRWritePlace &) const;

  void operator()(const IRAddrOf &addrof) const;

  void operator()(const IRReadPtr &read) const;

  void operator()(const IRWritePtr &write) const;

  void operator()(const IRCast &cast) const;

  void operator()(const IRTransmute &transmute) const;

  void operator()(const IRAlloc &alloc) const;

  void operator()(const IRContextBundleBuild &build) const;

  void operator()(const IRBreak &brk) const;

  void operator()(const IRContinue &) const;

  void operator()(const IRDefer &) const;

  void operator()(const IRMoveState &) const;

  void operator()(const IRBlock &block) const;

  void operator()(const IRLoop &loop) const;

  void operator()(const IRIfCase &if_case) const;

  void operator()(const IRRegion &region) const;

  void operator()(const IRFrame &frame) const;

  void operator()(const IRBranch &) const;

  void operator()(const IRPhi &) const;

  void operator()(const IRInitPanicHandle &handle) const;

  void operator()(const IRInitPanicRaise &raise) const;

  void operator()(const IRCheckPoison &check) const;

  void operator()(const IRParallel &parallel) const;

  void operator()(const IRSpawn &spawn) const;

  void operator()(const IRWait &wait) const;

  void operator()(const IRCancelCheck &check) const;

  void operator()(const IRCancelSuppress &) const;

  void operator()(const IRDispatch &dispatch) const;

  void operator()(const IRYield &y) const;

  void operator()(const IRYieldFrom &y) const;

  void operator()(const IRSpecSnapshot &spec) const;

  void operator()(const IRSpecValidate &spec) const;

  void operator()(const IRSpecCommit &spec) const;

  void operator()(const IRSpecRetry &spec) const;

  void operator()(const IRSpecFallback &spec) const;

  void operator()(const IRSpecLoop &spec) const;

  void operator()(const IRSync &s) const;

  void operator()(const IRRaceReturn &r) const;

  void operator()(const IRRaceYield &r) const;

  void operator()(const IRAll &all) const;

  void operator()(const IRAsyncComplete &async_complete) const;

  void operator()(const IRAsyncFail &async_fail) const;

  void operator()(const IRCallVTable &call) const;

  void operator()(const IRStoreGlobal &store) const;
};

} // namespace cursive::codegen::emit_detail
