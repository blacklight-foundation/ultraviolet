#pragma once

// =============================================================================
// Intrinsic Interface - Runtime Function Declarations
// =============================================================================
//
// This file provides the interface for declaring and emitting runtime
// function declarations. These declarations are emitted at the start of
// LLVM IR generation to ensure all runtime functions are available.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.9 Runtime Interface
//   - Section 6.12 Codegen Parameters
//
// The runtime interface includes:
//   - Panic handling functions
//   - Region memory management
//   - String/bytes built-in methods
//   - IO capability methods
//   - HeapAllocator capability methods
//   - ExecutionDomain methods
//   - Async runtime functions
//   - Structured concurrency runtime
//
// =============================================================================

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "04_analysis/typing/types.h"
#include "05_codegen/ir/ir_model.h"

namespace ultraviolet::codegen {

// Forward declarations
struct LowerCtx;

// =============================================================================
// Runtime Symbol Constants
// =============================================================================

// =============================================================================
// Runtime Symbol Categories
// =============================================================================

/// Category of runtime symbol for dispatch purposes.
enum class RuntimeSymbolCategory {
  /// Core runtime (panic, context init)
  Core,
  /// Region memory management
  Region,
  /// String built-in methods
  String,
  /// Bytes built-in methods
  Bytes,
  /// IO capability
  IO,
  /// Network capability
  Network,
  /// HeapAllocator capability
  HeapAllocator,
  /// System capability
  System,
  /// Time capability
  Time,
  /// ExecutionDomain capability
  ExecutionDomain,
  /// CancelToken modal
  CancelToken,
  /// Async runtime
  Async,
  /// Structured concurrency
  Concurrency,
  /// Runtime conformance trace/logging
  Conformance,
  /// Unknown category
  Unknown,
};

/// Determine the category of a runtime symbol.
RuntimeSymbolCategory CategorizeRuntimeSymbol(const std::string& symbol);

/// Collect referenced symbols from IR.
std::vector<std::string> RefSyms(const IRPtr& ir);
std::vector<std::string> RefSyms(const IRDecl& decl);
std::vector<std::string> RefSyms(const IRDecls& decls);

/// Enumerate the known runtime symbol surface.
std::vector<std::string> RuntimeSyms();

/// Enumerate the broader runtime declaration set needed by the backend.
std::vector<std::string> RuntimeDeclSyms();

/// Collect runtime function references from IR.
std::vector<std::string> RuntimeRefs(const IRPtr& ir);
std::vector<std::string> RuntimeRefs(const IRDecls& decls);

// =============================================================================
// Runtime Function Info
// =============================================================================

/// Information about a runtime function.
struct RuntimeFuncInfo {
  /// The mangled symbol name.
  std::string symbol;

  /// Parameter types.
  std::vector<IRParam> params;

  /// Return type.
  analysis::TypeRef ret;

  /// Whether the function never returns.
  bool noreturn = false;

  /// Whether the function may unwind.
  bool may_unwind = false;

  /// Whether the C runtime entry writes its UV result through a leading out pointer.
  bool returns_via_out_param = false;

  /// ABI string (e.g., "C", "C-unwind").
  std::string abi = "C";
};

// =============================================================================
// Runtime Declaration Interface
// =============================================================================

/// Check if a symbol is a known runtime function.
bool IsRuntimeFunction(const std::string& symbol);

/// Check if a runtime symbol must use C aggregate return lowering.
bool RuntimeUsesCAggregateABI(const std::string& symbol);

/// Check if a runtime symbol returns through an explicit leading out parameter.
bool RuntimeUsesExplicitOutResultABI(const std::string& symbol);

/// Check if a runtime symbol is an implementation helper with a C ABI boundary.
bool RuntimeUsesForeignABI(const std::string& symbol);

/// Get information about a runtime function.
/// Returns nullopt if the symbol is not a known runtime function.
std::optional<RuntimeFuncInfo> GetRuntimeFuncInfo(const std::string& symbol);

// =============================================================================
// Structured Concurrency Runtime Symbols
// =============================================================================

/// Get symbol for parallel_begin runtime call.
std::string ConcurrencySymParallelBegin();

/// Get symbol for parallel_join runtime call.
std::string ConcurrencySymParallelJoin();

/// Get symbol for spawn_create runtime call.
std::string ConcurrencySymSpawnCreate();

/// Get symbol for spawn_wait runtime call.
std::string ConcurrencySymSpawnWait();

/// Get symbol for dispatch_run runtime call.
std::string ConcurrencySymDispatchRun();

/// Get symbol for cancel_token_new runtime call.
std::string ConcurrencySymCancelTokenNew();

/// Get symbol for cancel_token_cancel runtime call.
std::string ConcurrencySymCancelTokenCancel();

/// Get symbol for cancel_token_is_cancelled runtime call.
std::string ConcurrencySymCancelTokenIsCancelled();

/// Get symbol for parallel_work_panic runtime call.
std::string ConcurrencySymParallelWorkPanic();

/// Get symbol for key scope enter runtime call.
std::string ConcurrencySymKeyScopeEnter();

/// Get symbol for key scope exit runtime call.
std::string ConcurrencySymKeyScopeExit();

/// Get symbol for key acquire runtime call.
std::string ConcurrencySymKeyAcquire();

/// Get symbol for key conflict-check runtime call.
std::string ConcurrencySymKeyCheckConflict();

/// Get symbol for releasing a specific held key from a scope.
std::string ConcurrencySymKeyReleaseOne();

/// Get symbol for key release-all runtime call.
std::string ConcurrencySymKeyReleaseAll();

/// Get symbol for reacquiring a single previously released key.
std::string ConcurrencySymKeyReacquireOne();

/// Get symbol for key reacquire runtime call.
std::string ConcurrencySymKeyReacquire();

/// Get symbol for key-release snapshot discard runtime call.
std::string ConcurrencySymKeyReleaseSnapshotDiscard();

// =============================================================================
// LLVM Intrinsic Mapping
// =============================================================================

/// LLVM intrinsic kind for mapping Ultraviolet operations to LLVM intrinsics.
enum class LLVMIntrinsicKind {
  /// No LLVM intrinsic - use regular codegen.
  None,
  /// llvm.sadd.with.overflow
  SAddWithOverflow,
  /// llvm.uadd.with.overflow
  UAddWithOverflow,
  /// llvm.ssub.with.overflow
  SSubWithOverflow,
  /// llvm.usub.with.overflow
  USubWithOverflow,
  /// llvm.smul.with.overflow
  SMulWithOverflow,
  /// llvm.umul.with.overflow
  UMulWithOverflow,
  /// llvm.memcpy
  Memcpy,
  /// llvm.memmove
  Memmove,
  /// llvm.memset
  Memset,
  /// llvm.sqrt
  Sqrt,
  /// llvm.pow
  Pow,
  /// llvm.fabs
  Fabs,
  /// llvm.floor
  Floor,
  /// llvm.ceil
  Ceil,
  /// llvm.round
  Round,
  /// llvm.trunc
  Trunc,
  /// llvm.atomicrmw
  AtomicRMW,
  /// llvm.cmpxchg
  CmpXchg,
};

/// Get the LLVM intrinsic kind for an operation, if any.
LLVMIntrinsicKind GetLLVMIntrinsic(const std::string& op, const analysis::TypeRef& type);

/// Get the LLVM intrinsic name for a kind.
std::string GetLLVMIntrinsicName(LLVMIntrinsicKind kind, const analysis::TypeRef& type);

// =============================================================================
// Spec Rule Anchors
// =============================================================================

/// Emit SPEC_RULE anchors for runtime interface rules.
void AnchorRuntimeInterfaceRules();

}  // namespace ultraviolet::codegen
