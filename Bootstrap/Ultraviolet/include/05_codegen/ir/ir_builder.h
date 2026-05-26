#pragma once

// =============================================================================
// IR Builder API
// =============================================================================
//
// This header provides builder utilities for constructing IR nodes.
// The builder pattern simplifies IR construction and ensures validation.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.0 Codegen Model and Judgments (lines 14196-14347)
//   - ProcIR structure (line 14234)
//   - CodegenParams (line 14237)
//
// =============================================================================

#include "05_codegen/ir/ir_model.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace ultraviolet::codegen {

// =============================================================================
// Core Builder Functions
// =============================================================================

/// Create an IR node from any IR variant type.
/// This is the primary way to construct IR nodes.
///
/// Example:
///   IRPtr ir = MakeIR(IRBindVar{"x", value, type});
///   IRPtr call = MakeCall(callee, args, result);
template <typename T>
IRPtr MakeIR(T&& node) {
  static_assert(
      std::is_constructible_v<
          decltype(IR::node),
          std::decay_t<T>>,
      "T must be a valid IR node type");
  return std::make_shared<IR>(IR{std::forward<T>(node)});
}

/// Create an empty/no-op IR node.
/// Useful as a no-op marker when no IR is needed.
inline IRPtr EmptyIR() {
  return MakeIR(IROpaque{});
}

/// Create an IR node that binds a variable.
/// Shorthand for creating IRBindVar nodes.
inline IRPtr MakeBindVar(
    const std::string& name,
    const IRValue& value,
    const analysis::TypeRef& type,
    analysis::ProvenanceKind prov = analysis::ProvenanceKind::Bottom,
    const std::optional<std::string>& prov_region = std::nullopt) {
  IRBindVar bind;
  bind.name = name;
  bind.value = value;
  bind.type = type;
  bind.prov = prov;
  bind.prov_region = prov_region;
  return MakeIR(std::move(bind));
}

/// Create an IR node that stores to a variable.
inline IRPtr MakeStoreVar(const std::string& name, const IRValue& value) {
  return MakeIR(IRStoreVar{name, value});
}

/// Create an IR node that stores to a variable without dropping the old value.
inline IRPtr MakeStoreVarNoDrop(const std::string& name, const IRValue& value) {
  return MakeIR(IRStoreVarNoDrop{name, value});
}

/// Create an IR node for a procedure call.
inline IRPtr MakeCall(
    const IRValue& callee,
    std::vector<IRValue> args,
    const IRValue& result) {
  IRCall call;
  call.callee = callee;
  call.args = std::move(args);
  call.result = result;
  return MakeIR(std::move(call));
}

/// Create an IR node for a virtual table call.
inline IRPtr MakeCallVTable(
    const IRValue& base,
    std::size_t slot,
    std::vector<IRValue> args,
    const IRValue& result) {
  IRCallVTable call;
  call.base = base;
  call.slot = slot;
  call.args = std::move(args);
  call.result = result;
  return MakeIR(std::move(call));
}

/// Create an IR node for returning a value.
inline IRPtr MakeReturn(const IRValue& value) {
  return MakeIR(IRReturn{value});
}

/// Create an IR node for an if expression.
inline IRPtr MakeIf(
    const IRValue& cond,
    IRPtr then_ir,
    const IRValue& then_value,
    IRPtr else_ir,
    const IRValue& else_value,
    const IRValue& result) {
  IRIf if_node;
  if_node.cond = cond;
  if_node.then_ir = std::move(then_ir);
  if_node.then_value = then_value;
  if_node.else_ir = std::move(else_ir);
  if_node.else_value = else_value;
  if_node.result = result;
  return MakeIR(std::move(if_node));
}

/// Create an IR node for a block expression.
inline IRPtr MakeBlock(IRPtr setup, IRPtr body, const IRValue& value) {
  IRBlock block;
  block.setup = std::move(setup);
  block.body = std::move(body);
  block.value = value;
  return MakeIR(std::move(block));
}

/// Create an IR node for an infinite loop.
inline IRPtr MakeLoopInfinite(
    IRPtr body_ir,
    const IRValue& body_value,
    const IRValue& result) {
  IRLoop loop;
  loop.kind = IRLoopKind::Infinite;
  loop.body_ir = std::move(body_ir);
  loop.body_value = body_value;
  loop.result = result;
  return MakeIR(std::move(loop));
}

/// Create an IR node for a conditional loop.
inline IRPtr MakeLoopConditional(
    IRPtr cond_ir,
    const IRValue& cond_value,
    IRPtr body_ir,
    const IRValue& body_value,
    const IRValue& result) {
  IRLoop loop;
  loop.kind = IRLoopKind::Conditional;
  loop.cond_ir = std::move(cond_ir);
  loop.cond_value = cond_value;
  loop.body_ir = std::move(body_ir);
  loop.body_value = body_value;
  loop.result = result;
  return MakeIR(std::move(loop));
}

/// Create an IR node for an iterator loop.
inline IRPtr MakeLoopIter(
    IRPatternPtr pattern,
    IRPtr iter_ir,
    const IRValue& iter_value,
    IRPtr body_ir,
    const IRValue& body_value,
    const IRValue& result) {
  IRLoop loop;
  loop.kind = IRLoopKind::Iter;
  loop.pattern = std::move(pattern);
  loop.iter_ir = std::move(iter_ir);
  loop.iter_value = iter_value;
  loop.body_ir = std::move(body_ir);
  loop.body_value = body_value;
  loop.result = result;
  return MakeIR(std::move(loop));
}

/// Create an IR node for if-case analysis.
inline IRPtr MakeIfCase(
    const IRValue& scrutinee,
    const analysis::TypeRef& scrutinee_type,
    std::vector<IRIfCaseClause> arms,
    const IRValue& result) {
  IRIfCase if_case;
  if_case.scrutinee = scrutinee;
  if_case.scrutinee_type = scrutinee_type;
  if_case.arms = std::move(arms);
  if_case.result = result;
  return MakeIR(std::move(if_case));
}

/// Create an IR node for a region block.
inline IRPtr MakeRegion(
    const IRValue& owner,
    const std::optional<std::string>& alias,
    IRPtr body,
    const IRValue& value) {
  IRRegion region;
  region.owner = owner;
  region.alias = alias;
  region.body = std::move(body);
  region.value = value;
  return MakeIR(std::move(region));
}

/// Create an IR node for a frame block.
inline IRPtr MakeFrame(
    const std::optional<IRValue>& region,
    IRPtr body,
    const IRValue& value) {
  IRFrame frame;
  frame.region = region;
  frame.body = std::move(body);
  frame.value = value;
  return MakeIR(std::move(frame));
}

/// Create an IR node for a unary operation.
inline IRPtr MakeUnaryOp(
    const std::string& op,
    const IRValue& operand,
    const IRValue& result) {
  return MakeIR(IRUnaryOp{op, operand, result});
}

/// Create an IR node for a fence expression.
inline IRPtr MakeFence(IRFenceOrder order, const IRValue& result) {
  return MakeIR(IRFence{order, result});
}

/// Create an IR node for a binary operation.
inline IRPtr MakeBinaryOp(
    const std::string& op,
    const IRValue& lhs,
    const IRValue& rhs,
    const IRValue& result) {
  return MakeIR(IRBinaryOp{op, lhs, rhs, result});
}

/// Create an IR node for a cast expression.
inline IRPtr MakeCast(
    const analysis::TypeRef& target,
    const IRValue& value,
    const IRValue& result) {
  return MakeIR(IRCast{target, value, result});
}

/// Create an IR node for a transmute expression.
inline IRPtr MakeTransmute(
    const analysis::TypeRef& from,
    const analysis::TypeRef& to,
    const IRValue& value,
    const IRValue& result) {
  return MakeIR(IRTransmute{from, to, value, result});
}

/// Create an IR node for allocation.
inline IRPtr MakeAlloc(
    const std::optional<IRValue>& region,
    const IRValue& value,
    const IRValue& result,
    const analysis::TypeRef& type) {
  IRAlloc alloc;
  alloc.region = region;
  alloc.value = value;
  alloc.result = result;
  alloc.type = type;
  return MakeIR(std::move(alloc));
}

/// Create an IR node for reading from a pointer.
inline IRPtr MakeReadPtr(const IRValue& ptr, const IRValue& result) {
  return MakeIR(IRReadPtr{ptr, result});
}

/// Create an IR node for writing to a pointer.
inline IRPtr MakeWritePtr(const IRValue& ptr, const IRValue& value) {
  return MakeIR(IRWritePtr{ptr, value});
}

/// Create an IR node for taking address of a place.
inline IRPtr MakeAddrOf(const IRPlace& place, const IRValue& result) {
  return MakeIR(IRAddrOf{place, result});
}

/// Create an IR node for break.
inline IRPtr MakeBreak(const std::optional<IRValue>& value = std::nullopt) {
  return MakeIR(IRBreak{value});
}

/// Create an IR node for continue.
inline IRPtr MakeContinue() {
  return MakeIR(IRContinue{});
}

/// Create an IR node for defer.
inline IRPtr MakeDefer() {
  return MakeIR(IRDefer{});
}

// =============================================================================
// Structured Concurrency Builders (§18)
// =============================================================================

/// Create an IR node for a parallel block.
inline IRPtr MakeParallel(
    const IRValue& domain,
    IRPtr body,
    const IRValue& result,
    const std::optional<IRValue>& cancel_token = std::nullopt,
    const std::string& name = "") {
  IRParallel parallel;
  parallel.domain = domain;
  parallel.body = std::move(body);
  parallel.result = result;
  parallel.cancel_token = cancel_token;
  parallel.name = name;
  return MakeIR(std::move(parallel));
}

/// Create an IR node for spawn expression.
inline IRPtr MakeSpawn(
    IRPtr captured_env,
    IRPtr body,
    const IRValue& body_result,
    const IRValue& result,
    const IRValue& env_ptr,
    const IRValue& env_size,
    const IRValue& body_fn,
    const IRValue& result_size,
    const std::string& name = "") {
  IRSpawn spawn;
  spawn.captured_env = std::move(captured_env);
  spawn.body = std::move(body);
  spawn.body_result = body_result;
  spawn.result = result;
  spawn.env_ptr = env_ptr;
  spawn.env_size = env_size;
  spawn.body_fn = body_fn;
  spawn.result_size = result_size;
  spawn.name = name;
  return MakeIR(std::move(spawn));
}

/// Create an IR node for wait expression.
inline IRPtr MakeWait(const IRValue& handle, const IRValue& result) {
  return MakeIR(IRWait{handle, result});
}

// =============================================================================
// Async Operation Builders (§19)
// =============================================================================

/// Create an IR node for yield expression.
inline IRPtr MakeYield(
    bool release,
    const IRValue& value,
    const IRValue& result,
    const IRValue& keys_record,
    std::size_t state_index) {
  IRYield yield;
  yield.release = release;
  yield.value = value;
  yield.result = result;
  yield.keys_record = keys_record;
  yield.state_index = state_index;
  return MakeIR(std::move(yield));
}

/// Create an IR node for yield-from expression.
inline IRPtr MakeYieldFrom(
    bool release,
    const IRValue& source,
    const IRValue& result,
    const analysis::TypeRef& source_type,
    std::size_t state_index) {
  IRYieldFrom yf;
  yf.release = release;
  yf.source = source;
  yf.result = result;
  yf.source_type = source_type;
  yf.state_index = state_index;
  return MakeIR(std::move(yf));
}

/// Create an IR node for sync expression.
inline IRPtr MakeSync(
    const IRValue& async_value,
    const IRValue& result,
    const analysis::TypeRef& async_type,
    const analysis::TypeRef& result_type,
    const analysis::TypeRef& error_type) {
  IRSync sync;
  sync.async_value = async_value;
  sync.result = result;
  sync.async_type = async_type;
  sync.result_type = result_type;
  sync.error_type = error_type;
  return MakeIR(std::move(sync));
}

// =============================================================================
// Check/Validation Builders
// =============================================================================

/// Create an IR node for index bounds checking.
inline IRPtr MakeCheckIndex(const IRValue& base, const IRValue& index) {
  return MakeIR(IRCheckIndex{base, index});
}

/// Create an IR node for range bounds checking.
inline IRPtr MakeCheckRange(const IRValue& base, const IRRange& range) {
  return MakeIR(IRCheckRange{base, range});
}

/// Create an IR node for a runtime check operation.
inline IRPtr MakeCheckOp(
    const std::string& op,
    const std::string& reason,
    const IRValue& lhs,
    const std::optional<IRValue>& rhs = std::nullopt) {
  return MakeIR(IRCheckOp{op, reason, lhs, rhs});
}

/// Create an IR node for cast validation.
inline IRPtr MakeCheckCast(
    const analysis::TypeRef& target,
    const IRValue& value) {
  return MakeIR(IRCheckCast{target, value});
}

// =============================================================================
// Panic Handling Builders
// =============================================================================

/// Create an IR node to clear panic state.
inline IRPtr MakeClearPanic() {
  return MakeIR(IRClearPanic{});
}

/// Create an IR node to check for panic.
inline IRPtr MakePanicCheck() {
  return MakeIR(IRPanicCheck{});
}

inline IRPtr MakeCleanupPanicCheck(IRPtr cleanup_ir) {
  IRCleanupPanicCheck check;
  check.cleanup_ir = std::move(cleanup_ir);
  return MakeIR(std::move(check));
}

/// Create an IR node to initialize panic handling.
inline IRPtr MakeInitPanicHandle(
    const std::string& module,
    std::vector<std::string> poison_modules,
    IRPtr cleanup_ir = nullptr) {
  IRInitPanicHandle init;
  init.module = module;
  init.poison_modules = std::move(poison_modules);
  init.cleanup_ir = std::move(cleanup_ir);
  return MakeIR(std::move(init));
}

inline IRPtr MakeInitPanicRaise(
    const std::string& module,
    std::vector<std::string> poison_modules,
    IRPtr cleanup_ir = nullptr) {
  IRInitPanicRaise init;
  init.module = module;
  init.poison_modules = std::move(poison_modules);
  init.cleanup_ir = std::move(cleanup_ir);
  return MakeIR(std::move(init));
}

/// Create an IR node to check for poison.
inline IRPtr MakeCheckPoison(const std::string& module) {
  IRCheckPoison check;
  check.module = module;
  return MakeIR(std::move(check));
}

/// Create an IR node to lower a panic.
inline IRPtr MakeLowerPanic(const std::string& reason, IRPtr cleanup_ir = nullptr) {
  IRLowerPanic panic;
  panic.reason = reason;
  panic.cleanup_ir = std::move(cleanup_ir);
  return MakeIR(std::move(panic));
}

// =============================================================================
// IRValue Builders
// =============================================================================

/// Create a local IRValue.
inline IRValue MakeLocalValue(const std::string& name) {
  IRValue v;
  v.kind = IRValue::Kind::Local;
  v.name = name;
  return v;
}

/// Create a symbol IRValue.
inline IRValue MakeSymbolValue(const std::string& name) {
  IRValue v;
  v.kind = IRValue::Kind::Symbol;
  v.name = name;
  return v;
}

/// Create an opaque IRValue.
inline IRValue MakeOpaqueValue() {
  IRValue v;
  v.kind = IRValue::Kind::Opaque;
  return v;
}

/// Create an immediate IRValue with bytes.
inline IRValue MakeImmediateValue(
    const std::string& name,
    std::vector<std::uint8_t> bytes) {
  IRValue v;
  v.kind = IRValue::Kind::Immediate;
  v.name = name;
  v.bytes = std::move(bytes);
  return v;
}

// =============================================================================
// IRPlace Builders
// =============================================================================

/// Create an IRPlace from a representation string.
inline IRPlace MakePlace(const std::string& repr) {
  return IRPlace{repr};
}

// =============================================================================
// Declaration Builders
// =============================================================================

/// Create a ProcIR declaration.
inline ProcIR MakeProcIR(
    const std::string& symbol,
    std::vector<IRParam> params,
    const analysis::TypeRef& ret,
    IRPtr body) {
  ProcIR proc;
  proc.symbol = symbol;
  proc.params = std::move(params);
  proc.ret = ret;
  proc.body = std::move(body);
  return proc;
}

/// Create a GlobalConst declaration.
inline GlobalConst MakeGlobalConst(
    const std::string& symbol,
    std::vector<std::uint8_t> bytes,
    std::uint64_t align = 1) {
  GlobalConst gc;
  gc.symbol = symbol;
  gc.bytes = std::move(bytes);
  gc.align = align;
  return gc;
}

/// Create a GlobalZero declaration.
inline GlobalZero MakeGlobalZero(
    const std::string& symbol,
    std::uint64_t size,
    std::uint64_t align = 1) {
  GlobalZero gz;
  gz.symbol = symbol;
  gz.size = size;
  gz.align = align;
  return gz;
}

/// Create a GlobalVTable declaration.
inline GlobalVTable MakeGlobalVTable(
    const std::string& symbol,
    const VTableHeader& header,
    std::vector<std::string> slots) {
  GlobalVTable vt;
  vt.symbol = symbol;
  vt.header = header;
  vt.slots = std::move(slots);
  return vt;
}

/// Create an ExternProcIR declaration.
inline ExternProcIR MakeExternProcIR(
    const std::string& symbol,
    std::vector<IRParam> params,
    const analysis::TypeRef& ret,
    const std::optional<std::string>& abi = std::nullopt,
    const std::optional<std::string>& raw_dylib_library_name = std::nullopt,
    const std::optional<std::string>& raw_dylib_foreign_symbol = std::nullopt,
    bool raw_dylib_catch_unwind = false) {
  ExternProcIR ext;
  ext.symbol = symbol;
  ext.params = std::move(params);
  ext.ret = ret;
  ext.abi = abi;
  ext.raw_dylib_library_name = raw_dylib_library_name;
  ext.raw_dylib_foreign_symbol = raw_dylib_foreign_symbol;
  ext.raw_dylib_catch_unwind = raw_dylib_catch_unwind;
  return ext;
}

}  // namespace ultraviolet::codegen
