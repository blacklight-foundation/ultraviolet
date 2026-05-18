// =============================================================================
// Builtin Symbol Resolution Implementation
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.9 BuiltinSym and BuiltinModalSym
//   - Section 6.12.14 String/Bytes builtins
//   - Section 18.2 ExecutionDomain builtins
//   - Section 18.6 CancelToken builtins
//   - Section 19 Async/Reactor builtins
//
// This file provides symbol resolution for compiler-known built-in
// procedures and methods. These symbols map to runtime library functions.
//
// =============================================================================

#include "05_codegen/intrinsics/builtins.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "01_project/language_profile.h"
#include "04_analysis/modal/builtin_modal_intrinsics.h"

namespace ultraviolet::codegen {

namespace {

using BuiltinSymbolFactory = std::string (*)();

struct BuiltinSymbolEntry {
  std::string_view qualified_name;
  BuiltinSymbolFactory factory = nullptr;
};

template <std::size_t N>
std::string LookupBuiltinSymbol(
    std::string_view qualified_name,
    const std::array<BuiltinSymbolEntry, N>& entries) {
  for (const auto& entry : entries) {
    if (qualified_name == entry.qualified_name) {
      return entry.factory ? entry.factory() : std::string{};
    }
  }
  return {};
}

void AppendRuntimeSymbol(std::vector<std::string>& out, std::string symbol) {
  if (!symbol.empty()) {
    out.push_back(std::move(symbol));
  }
}

template <std::size_t N>
void AppendRuntimeSymbols(
    std::vector<std::string>& out,
    const std::array<BuiltinSymbolFactory, N>& factories) {
  for (const auto factory : factories) {
    if (!factory) {
      continue;
    }
    AppendRuntimeSymbol(out, factory());
  }
}

void SortUniqueSymbols(std::vector<std::string>& syms) {
  std::sort(syms.begin(), syms.end());
  syms.erase(std::unique(syms.begin(), syms.end()), syms.end());
}

std::string LookupBuiltinModalRuntimeSymbol(
    const analysis::TypePath& modal_path,
    std::optional<std::string_view> state,
    std::string_view member_name) {
  const auto symbol =
      analysis::LookupBuiltinModalRuntimeSymbol(modal_path, state, member_name);
  return symbol.value_or(std::string{});
}

}  // namespace

// =============================================================================
// Section 6.9 RegionLayout
// =============================================================================

struct RegionLayoutInfo {
  std::uint64_t size = 0;
  std::uint64_t align = 0;
  struct Field {
    std::string name;
    std::uint64_t offset = 0;
  };
  std::vector<Field> fields;
};

RegionLayoutInfo RegionLayout() {
  SPEC_RULE("RegionLayout");

  // Per Section 6.9 (RegionLayout):
  // ModalLayout(Region) produces size, align, disc, payload
  // RegionLayout produces size, align, [("disc", disc), ("payload", payload)]
  //
  // Region is a modal type with a discriminant and payload.
  // For Win64 x86_64-pc-windows-msvc target:
  // - Discriminant: 1 byte at offset 0
  // - Payload: pointer-sized (8 bytes) at offset 8 (aligned)
  // Total size: 16 bytes, alignment: 8

  RegionLayoutInfo layout;
  layout.size = 16;
  layout.align = 8;
  layout.fields.push_back({"disc", 0});
  layout.fields.push_back({"payload", 8});
  return layout;
}

// =============================================================================
// Section 6.9 BuiltinModalSym - Region method symbols
// =============================================================================

std::string BuiltinModalSymRegionNewScoped() {
  SPEC_RULE("BuiltinModalSym-NewScoped");
  return project::RuntimePathSig({"region", "new_scoped"});
}

std::string BuiltinModalSymRegionAlloc() {
  SPEC_RULE("BuiltinModalSym-Alloc");
  return project::RuntimePathSig({"region", "alloc"});
}

std::string BuiltinModalSymRegionMark() {
  SPEC_RULE("BuiltinModalSym-Mark");
  return project::RuntimePathSig({"region", "mark"});
}

std::string BuiltinModalSymRegionResetTo() {
  SPEC_RULE("BuiltinModalSym-ResetTo");
  return project::RuntimePathSig({"region", "reset_to"});
}

std::string BuiltinModalSymRegionResetUnchecked() {
  SPEC_RULE("BuiltinModalSym-ResetUnchecked");
  return project::RuntimePathSig({"region", "reset_unchecked"});
}

std::string BuiltinModalSymRegionFreeze() {
  SPEC_RULE("BuiltinModalSym-Freeze");
  return project::RuntimePathSig({"region", "freeze"});
}

std::string BuiltinModalSymRegionThaw() {
  SPEC_RULE("BuiltinModalSym-Thaw");
  return project::RuntimePathSig({"region", "thaw"});
}

std::string BuiltinModalSymRegionFreeUnchecked() {
  SPEC_RULE("BuiltinModalSym-FreeUnchecked");
  return project::RuntimePathSig({"region", "free_unchecked"});
}

std::string BuiltinModalSymRegionAddrIsActive() {
  SPEC_RULE("BuiltinModalSym-AddrIsActive");
  return project::RuntimePathSig({"region", "addr_is_active"});
}

std::string BuiltinModalSymRegionAddrTagFrom() {
  SPEC_RULE("BuiltinModalSym-AddrTagFrom");
  return project::RuntimePathSig({"region", "addr_tag_from"});
}

std::string BuiltinModalSymRegionScopeEnter() {
  return project::RuntimePathSig({"region", "scope_enter"});
}

std::string BuiltinModalSymRegionScopeExit() {
  return project::RuntimePathSig({"region", "scope_exit"});
}

std::string BuiltinModalSymRegionAddrTagScope() {
  return project::RuntimePathSig({"region", "addr_tag_scope"});
}

std::string BuiltinModalSym(const std::string& method) {
  SPEC_DEF("BuiltinModalSym", "");
  static const std::array<BuiltinSymbolEntry, 11> kRegionModalMethods = {{
      {"new_scoped", &BuiltinModalSymRegionNewScoped},
      {"alloc", &BuiltinModalSymRegionAlloc},
      {"mark", &BuiltinModalSymRegionMark},
      {"reset_to", &BuiltinModalSymRegionResetTo},
      {"reset_unchecked", &BuiltinModalSymRegionResetUnchecked},
      {"freeze", &BuiltinModalSymRegionFreeze},
      {"thaw", &BuiltinModalSymRegionThaw},
      {"free_unchecked", &BuiltinModalSymRegionFreeUnchecked},
      {"scope_enter", &BuiltinModalSymRegionScopeEnter},
      {"scope_exit", &BuiltinModalSymRegionScopeExit},
      {"addr_tag_scope", &BuiltinModalSymRegionAddrTagScope},
  }};
  return LookupBuiltinSymbol(method, kRegionModalMethods);
}

// =============================================================================
// Section 6.9 BuiltinSym - IO capability methods
// =============================================================================

std::string BuiltinSymIOOpenRead() {
  SPEC_RULE("BuiltinSym-IO-OpenRead");
  return project::RuntimePathSig({"io", "open_read"});
}

std::string BuiltinSymIOOpenWrite() {
  SPEC_RULE("BuiltinSym-IO-OpenWrite");
  return project::RuntimePathSig({"io", "open_write"});
}

std::string BuiltinSymIOOpenAppend() {
  SPEC_RULE("BuiltinSym-IO-OpenAppend");
  return project::RuntimePathSig({"io", "open_append"});
}

std::string BuiltinSymIOCreateWrite() {
  SPEC_RULE("BuiltinSym-IO-CreateWrite");
  return project::RuntimePathSig({"io", "create_write"});
}

std::string BuiltinSymIOReadFile() {
  SPEC_RULE("BuiltinSym-IO-ReadFile");
  return project::RuntimePathSig({"io", "read_file"});
}

std::string BuiltinSymIOReadBytes() {
  SPEC_RULE("BuiltinSym-IO-ReadBytes");
  return project::RuntimePathSig({"io", "read_bytes"});
}

std::string BuiltinSymIOWriteFile() {
  SPEC_RULE("BuiltinSym-IO-WriteFile");
  return project::RuntimePathSig({"io", "write_file"});
}

std::string BuiltinSymIOWriteStdout() {
  SPEC_RULE("BuiltinSym-IO-WriteStdout");
  return project::RuntimePathSig({"io", "write_stdout"});
}

std::string BuiltinSymIOWriteStderr() {
  SPEC_RULE("BuiltinSym-IO-WriteStderr");
  return project::RuntimePathSig({"io", "write_stderr"});
}

std::string BuiltinSymIOExists() {
  SPEC_RULE("BuiltinSym-IO-Exists");
  return project::RuntimePathSig({"io", "exists"});
}

std::string BuiltinSymIORemove() {
  SPEC_RULE("BuiltinSym-IO-Remove");
  return project::RuntimePathSig({"io", "remove"});
}

std::string BuiltinSymIOOpenDir() {
  SPEC_RULE("BuiltinSym-IO-OpenDir");
  return project::RuntimePathSig({"io", "open_dir"});
}

std::string BuiltinSymIOCreateDir() {
  SPEC_RULE("BuiltinSym-IO-CreateDir");
  return project::RuntimePathSig({"io", "create_dir"});
}

std::string BuiltinSymIOEnsureDir() {
  SPEC_RULE("BuiltinSym-IO-EnsureDir");
  return project::RuntimePathSig({"io", "ensure_dir"});
}

std::string BuiltinSymIOKind() {
  SPEC_RULE("BuiltinSym-IO-Kind");
  return project::RuntimePathSig({"io", "kind"});
}

std::string BuiltinSymIORestrict() {
  SPEC_RULE("BuiltinSym-IO-Restrict");
  return project::RuntimePathSig({"io", "restrict"});
}

// =============================================================================
// Section 6.9 BuiltinSym - Network capability methods
// =============================================================================

std::string BuiltinSymNetworkRestrictHost() {
  SPEC_RULE("BuiltinSym-Network-RestrictHost");
  return project::RuntimePathSig({"net", "restrict_to_host"});
}

// =============================================================================
// Section 6.9 BuiltinSym - HeapAllocator capability methods
// =============================================================================

std::string BuiltinSymHeapAllocatorWithQuota() {
  SPEC_RULE("BuiltinSym-HeapAllocator-WithQuota");
  return project::RuntimePathSig({"heap", "with_quota"});
}

std::string BuiltinSymHeapAllocatorAllocRaw() {
  SPEC_RULE("BuiltinSym-HeapAllocator-AllocRaw");
  return project::RuntimePathSig({"heap", "alloc_raw"});
}

std::string BuiltinSymHeapAllocatorDeallocRaw() {
  SPEC_RULE("BuiltinSym-HeapAllocator-DeallocRaw");
  return project::RuntimePathSig({"heap", "dealloc_raw"});
}

// =============================================================================
// Section 5.9.4 System builtins
// =============================================================================

std::string BuiltinSymSystemExit() {
  SPEC_RULE("BuiltinSym-System-Exit");
  return project::RuntimePathSig({"system", "exit"});
}

std::string BuiltinSymSystemGetEnv() {
  SPEC_RULE("BuiltinSym-System-GetEnv");
  return project::RuntimePathSig({"system", "get_env"});
}

std::string BuiltinSymSystemExecutablePath() {
  SPEC_RULE("BuiltinSym-System-ExecutablePath");
  return project::RuntimePathSig({"system", "executable_path"});
}

std::string BuiltinSymSystemArgumentCount() {
  SPEC_RULE("BuiltinSym-System-ArgumentCount");
  return project::RuntimePathSig({"system", "argument_count"});
}

std::string BuiltinSymSystemArgument() {
  SPEC_RULE("BuiltinSym-System-Argument");
  return project::RuntimePathSig({"system", "argument"});
}

std::string BuiltinSymSystemCurrentDirectory() {
  SPEC_RULE("BuiltinSym-System-CurrentDirectory");
  return project::RuntimePathSig({"system", "current_directory"});
}

std::string BuiltinSymSystemRun() {
  SPEC_RULE("BuiltinSym-System-Run");
  return project::RuntimePathSig({"system", "run"});
}

// =============================================================================
// Section 14.9 Time builtins
// =============================================================================

std::string BuiltinSymTimeMonotonic() {
  SPEC_RULE("BuiltinSym-Time-Monotonic");
  return project::RuntimePathSig({"time", "monotonic"});
}

std::string BuiltinSymTimeWall() {
  SPEC_RULE("BuiltinSym-Time-Wall");
  return project::RuntimePathSig({"time", "wall"});
}

std::string BuiltinSymMonotonicTimeNow() {
  SPEC_RULE("BuiltinSym-MonotonicTime-Now");
  return project::RuntimePathSig({"time", "monotonic_now"});
}

std::string BuiltinSymMonotonicTimeResolution() {
  SPEC_RULE("BuiltinSym-MonotonicTime-Resolution");
  return project::RuntimePathSig({"time", "monotonic_resolution"});
}

std::string BuiltinSymMonotonicTimeElapsed() {
  SPEC_RULE("BuiltinSym-MonotonicTime-Elapsed");
  return project::RuntimePathSig({"time", "monotonic_elapsed"});
}

std::string BuiltinSymMonotonicTimeCoarsen() {
  SPEC_RULE("BuiltinSym-MonotonicTime-Coarsen");
  return project::RuntimePathSig({"time", "monotonic_coarsen"});
}

std::string BuiltinSymWallTimeNowUtc() {
  SPEC_RULE("BuiltinSym-WallTime-NowUtc");
  return project::RuntimePathSig({"time", "wall_now_utc"});
}

std::string BuiltinSymWallTimeResolution() {
  SPEC_RULE("BuiltinSym-WallTime-Resolution");
  return project::RuntimePathSig({"time", "wall_resolution"});
}

std::string BuiltinSymWallTimeCoarsen() {
  SPEC_RULE("BuiltinSym-WallTime-Coarsen");
  return project::RuntimePathSig({"time", "wall_coarsen"});
}

// =============================================================================
// Section 18.2 ExecutionDomain builtins
// =============================================================================

std::string BuiltinSymExecutionDomainName() {
  SPEC_DEF("BuiltinSym-ExecutionDomain-Name", "Section 18.2.4");
  return project::RuntimePathSig({"execution_domain", "name"});
}

std::string BuiltinSymExecutionDomainMaxConcurrency() {
  SPEC_DEF("BuiltinSym-ExecutionDomain-MaxConcurrency", "Section 18.2.4");
  return project::RuntimePathSig({"execution_domain", "max_concurrency"});
}

// =============================================================================
// Section 18.2 Context execution domain constructors
// =============================================================================

std::string BuiltinSymContextCpu() {
  SPEC_DEF("BuiltinSym-Context-Cpu", "Section 18.2.1");
  return project::RuntimePathSig({"context", "cpu"});
}

std::string BuiltinSymContextCpuConfigured() {
  SPEC_DEF("BuiltinSym-Context-CpuConfigured", "Section 18.2.1");
  return project::RuntimePathSig({"context", "cpu_configured"});
}

std::string BuiltinSymContextGpu() {
  SPEC_DEF("BuiltinSym-Context-Gpu", "Section 18.2.2");
  return project::RuntimePathSig({"context", "gpu"});
}

std::string BuiltinSymContextInline() {
  SPEC_DEF("BuiltinSym-Context-Inline", "Section 18.2.3");
  return project::RuntimePathSig({"context", "inline"});
}

// =============================================================================
// Section 18.6 CancelToken builtins// =============================================================================
// Section 18.2 GPU intrinsics
// =============================================================================

std::string BuiltinSymGpuGlobalId() {
  SPEC_DEF("BuiltinSym-Gpu-GlobalId", "Section 18.2.2.4");
  return project::RuntimePathSig({"gpu", "global_id"});
}

std::string BuiltinSymGpuLocalId() {
  SPEC_DEF("BuiltinSym-Gpu-LocalId", "Section 18.2.2.4");
  return project::RuntimePathSig({"gpu", "local_id"});
}

std::string BuiltinSymGpuWorkgroupId() {
  SPEC_DEF("BuiltinSym-Gpu-WorkgroupId", "Section 18.2.2.4");
  return project::RuntimePathSig({"gpu", "workgroup_id"});
}

std::string BuiltinSymGpuWorkgroupSize() {
  SPEC_DEF("BuiltinSym-Gpu-WorkgroupSize", "Section 18.2.2.4");
  return project::RuntimePathSig({"gpu", "workgroup_size"});
}

std::string BuiltinSymGpuGlobalSize() {
  SPEC_DEF("BuiltinSym-Gpu-GlobalSize", "Section 18.2.2.4");
  return project::RuntimePathSig({"gpu", "global_size"});
}

std::string BuiltinSymGpuNumWorkgroups() {
  SPEC_DEF("BuiltinSym-Gpu-NumWorkgroups", "Section 18.2.2.4");
  return project::RuntimePathSig({"gpu", "num_workgroups"});
}

std::string BuiltinSymGpuLinearId() {
  SPEC_DEF("BuiltinSym-Gpu-LinearId", "Section 18.2.2.4");
  return project::RuntimePathSig({"gpu", "linear_id"});
}

std::string BuiltinSymGpuBarrier() {
  SPEC_DEF("BuiltinSym-Gpu-Barrier", "Section 18.2.2.4");
  return project::RuntimePathSig({"gpu", "barrier"});
}

std::string BuiltinSymGpuMemoryBarrier() {
  SPEC_DEF("BuiltinSym-Gpu-MemoryBarrier", "Section 18.2.2.4");
  return project::RuntimePathSig({"gpu", "memory_barrier"});
}

std::string BuiltinSymGpuWorkgroupBarrier() {
  SPEC_DEF("BuiltinSym-Gpu-WorkgroupBarrier", "Section 18.2.2.4");
  return project::RuntimePathSig({"gpu", "workgroup_barrier"});
}


// =============================================================================

std::string BuiltinSymCancelTokenNew() {
  SPEC_DEF("BuiltinSym-CancelToken-New", "Section 18.6.1");
  return core::PathSig({"CancelToken", "new"});
}

std::string BuiltinSymCancelTokenActiveCancel() {
  SPEC_DEF("BuiltinSym-CancelToken-Active-cancel", "Section 18.6.1");
  return core::PathSig({"CancelToken", "Active", "cancel"});
}

std::string BuiltinSymCancelTokenActiveIsCancelled() {
  SPEC_DEF("BuiltinSym-CancelToken-Active-is_cancelled", "Section 18.6.1");
  return core::PathSig({"CancelToken", "Active", "is_cancelled"});
}

std::string BuiltinSymCancelTokenActiveChild() {
  SPEC_DEF("BuiltinSym-CancelToken-Active-child", "Section 18.6.1");
  return core::PathSig({"CancelToken", "Active", "child"});
}

std::string BuiltinSymCancelTokenActiveWaitCancelled() {
  SPEC_DEF("BuiltinSym-CancelToken-Active-wait_cancelled", "Section 18.6.1");
  return core::PathSig({"CancelToken", "Active", "wait_cancelled"});
}

// =============================================================================
// Section 6.12.14 String/Bytes builtin symbols
// =============================================================================

std::string BuiltinSymStringFrom() {
  SPEC_DEF("BuiltinSym-string-from", "Section 6.12.14");
  return project::RuntimePathSig({"string", "from"});
}

std::string BuiltinSymStringAsView() {
  SPEC_DEF("BuiltinSym-string-as_view", "Section 6.12.14");
  return project::RuntimePathSig({"string", "as_view"});
}

std::string BuiltinSymStringSlice() {
  SPEC_DEF("BuiltinSym-string-slice", "Section 6.12.14");
  return project::RuntimePathSig({"string", "slice"});
}

std::string BuiltinSymStringToManaged() {
  SPEC_DEF("BuiltinSym-string-to_managed", "Section 6.12.14");
  return project::RuntimePathSig({"string", "to_managed"});
}

std::string BuiltinSymStringCloneWith() {
  SPEC_DEF("BuiltinSym-string-clone_with", "Section 6.12.14");
  return project::RuntimePathSig({"string", "clone_with"});
}

std::string BuiltinSymStringAppend() {
  SPEC_DEF("BuiltinSym-string-append", "Section 6.12.14");
  return project::RuntimePathSig({"string", "append"});
}

std::string BuiltinSymStringLength() {
  SPEC_DEF("BuiltinSym-string-length", "Section 6.12.14");
  return project::RuntimePathSig({"string", "length"});
}

std::string BuiltinSymStringIsEmpty() {
  SPEC_DEF("BuiltinSym-string-is_empty", "Section 6.12.14");
  return project::RuntimePathSig({"string", "is_empty"});
}

std::string BuiltinSymStringDropManaged() {
  SPEC_RULE("StringDropSym-Decl");
  SPEC_DEF("BuiltinSym-string-drop_managed", "Section 6.12.14");
  std::string sym = project::RuntimePathSig({"string", "drop_managed"});
  if (sym.empty()) {
    SPEC_RULE("StringDropSym-Err");
  }
  return sym;
}

std::string BuiltinSymBytesWithCapacity() {
  SPEC_DEF("BuiltinSym-bytes-with_capacity", "Section 6.12.14");
  return project::RuntimePathSig({"bytes", "with_capacity"});
}

std::string BuiltinSymBytesFromSlice() {
  SPEC_DEF("BuiltinSym-bytes-from_slice", "Section 6.12.14");
  return project::RuntimePathSig({"bytes", "from_slice"});
}

std::string BuiltinSymBytesAsView() {
  SPEC_DEF("BuiltinSym-bytes-as_view", "Section 6.12.14");
  return project::RuntimePathSig({"bytes", "as_view"});
}

std::string BuiltinSymBytesAsSlice() {
  SPEC_DEF("BuiltinSym-bytes-as_slice", "Section 6.12.14");
  return project::RuntimePathSig({"bytes", "as_slice"});
}

std::string BuiltinSymBytesToManaged() {
  SPEC_DEF("BuiltinSym-bytes-to_managed", "Section 6.12.14");
  return project::RuntimePathSig({"bytes", "to_managed"});
}

std::string BuiltinSymBytesView() {
  SPEC_DEF("BuiltinSym-bytes-view", "Section 6.12.14");
  return project::RuntimePathSig({"bytes", "view"});
}

std::string BuiltinSymBytesViewString() {
  SPEC_DEF("BuiltinSym-bytes-view_string", "Section 6.12.14");
  return project::RuntimePathSig({"bytes", "view_string"});
}

std::string BuiltinSymBytesAppend() {
  SPEC_DEF("BuiltinSym-bytes-append", "Section 6.12.14");
  return project::RuntimePathSig({"bytes", "append"});
}

std::string BuiltinSymBytesLength() {
  SPEC_DEF("BuiltinSym-bytes-length", "Section 6.12.14");
  return project::RuntimePathSig({"bytes", "length"});
}

std::string BuiltinSymBytesIsEmpty() {
  SPEC_DEF("BuiltinSym-bytes-is_empty", "Section 6.12.14");
  return project::RuntimePathSig({"bytes", "is_empty"});
}

std::string BuiltinSymBytesDropManaged() {
  SPEC_RULE("BytesDropSym-Decl");
  SPEC_DEF("BuiltinSym-bytes-drop_managed", "Section 6.12.14");
  std::string sym = project::RuntimePathSig({"bytes", "drop_managed"});
  if (sym.empty()) {
    SPEC_RULE("BytesDropSym-Err");
  }
  return sym;
}

// =============================================================================
// Section 14.10.6 Foundational intrinsic-call symbols
// =============================================================================

std::string BuiltinSymEqEq() {
  SPEC_DEF("BuiltinSym-Eq-eq", "Section 14.10.6");
  return project::LanguagePathSig({"intrinsic", "eq", "eq"});
}

std::string BuiltinSymStepSuccessor() {
  SPEC_DEF("BuiltinSym-Step-successor", "Section 14.10.6");
  return project::LanguagePathSig({"intrinsic", "step", "successor"});
}

std::string BuiltinSymStepPredecessor() {
  SPEC_DEF("BuiltinSym-Step-predecessor", "Section 14.10.6");
  return project::LanguagePathSig({"intrinsic", "step", "predecessor"});
}

// =============================================================================
// Section 19 Reactor builtins
// =============================================================================

std::string BuiltinSymReactorRun() {
  SPEC_DEF("BuiltinSym-Reactor-Run", "Section 19");
  return project::RuntimePathSig({"reactor", "run"});
}

std::string BuiltinSymReactorRegister() {
  SPEC_DEF("BuiltinSym-Reactor-Register", "Section 19");
  return project::RuntimePathSig({"reactor", "register"});
}

// =============================================================================
// Section 19 Async builtins
// =============================================================================

std::string BuiltinSymAsyncResume() {
  SPEC_DEF("BuiltinSym-Async-Resume", "Section 19.2.2");
  return project::RuntimePathSig({"async", "resume"});
}

std::string BuiltinSymAsyncGetDiscriminant() {
  SPEC_DEF("BuiltinSym-Async-GetDiscriminant", "Section 19");
  return project::RuntimePathSig({"async", "get_discriminant"});
}

std::string BuiltinSymAsyncGetSuspendedOutput() {
  SPEC_DEF("BuiltinSym-Async-GetSuspendedOutput", "Section 19");
  return project::RuntimePathSig({"async", "get_suspended_output"});
}

std::string BuiltinSymAsyncGetCompletedValue() {
  SPEC_DEF("BuiltinSym-Async-GetCompletedValue", "Section 19");
  return project::RuntimePathSig({"async", "get_completed_value"});
}

std::string BuiltinSymAsyncGetFailedError() {
  SPEC_DEF("BuiltinSym-Async-GetFailedError", "Section 19");
  return project::RuntimePathSig({"async", "get_failed_error"});
}

std::string BuiltinSymAsyncCreateCompleted() {
  SPEC_DEF("BuiltinSym-Async-CreateCompleted", "Section 19");
  return project::RuntimePathSig({"async", "create_completed"});
}

std::string BuiltinSymAsyncCreateFailed() {
  SPEC_DEF("BuiltinSym-Async-CreateFailed", "Section 19");
  return project::RuntimePathSig({"async", "create_failed"});
}

std::string BuiltinSymAsyncCreateSuspended() {
  SPEC_DEF("BuiltinSym-Async-CreateSuspended", "Section 19");
  return project::RuntimePathSig({"async", "create_suspended"});
}

std::string BuiltinSymAsyncAllocFrame() {
  SPEC_DEF("BuiltinSym-Async-AllocFrame", "Section 19");
  return project::RuntimePathSig({"async", "alloc_frame"});
}

std::string BuiltinSymAsyncFreeFrame() {
  SPEC_DEF("BuiltinSym-Async-FreeFrame", "Section 19");
  return project::RuntimePathSig({"async", "free_frame"});
}

std::string BuiltinSymAsyncTake() {
  SPEC_DEF("BuiltinSym-Async-Take", "Section 21.3.5");
  return project::RuntimePathSig({"async", "take"});
}

// =============================================================================
// Section 6.8 Panic symbol
// =============================================================================

std::string RuntimePanicSym() {
  SPEC_RULE("PanicSym");
  return project::RuntimePathSig({"panic"});
}

// =============================================================================
// Section 6.12.5 Context initialization symbol
// =============================================================================

std::string ContextInitSym() {
  SPEC_RULE("ContextInitSym-Decl");
  SPEC_DEF("ContextInitSym", "Section 6.12.5");
  return project::RuntimePathSig({"context_init"});
}

// =============================================================================
// Runtime conformance emission symbols
// =============================================================================

std::string RuntimeConformanceEmitSym() {
  return project::RuntimePathSig({"conformance", "emit"});
}

std::string RuntimeConformanceEmitIntSym() {
  return project::RuntimePathSig({"conformance", "emit_int"});
}

std::string RuntimeConformanceEmitBoolSym() {
  return project::RuntimePathSig({"conformance", "emit_bool"});
}

std::string RuntimeConformanceEmitFloatSym() {
  return project::RuntimePathSig({"conformance", "emit_float"});
}

std::string RuntimeConformanceEmitPtrSym() {
  return project::RuntimePathSig({"conformance", "emit_ptr"});
}

std::string RuntimeConformanceEmitStringSym() {
  return project::RuntimePathSig({"conformance", "emit_string"});
}

std::string RuntimeConformanceEmitStringManagedSym() {
  return project::RuntimePathSig({"conformance", "emit_string_managed"});
}

std::string RuntimeConformanceEmitBytesSym() {
  return project::RuntimePathSig({"conformance", "emit_bytes"});
}

std::string RuntimeConformanceEmitBytesManagedSym() {
  return project::RuntimePathSig({"conformance", "emit_bytes_managed"});
}

std::string RuntimeConformanceSetSinkSym() {
  return project::RuntimePathSig({"conformance", "set_sink"});
}

std::string RuntimeConformanceSetRootSym() {
  return project::RuntimePathSig({"conformance", "set_root"});
}

std::string RuntimeConformanceSetLogFilterSym() {
  return project::RuntimePathSig({"conformance", "set_log_filter"});
}

std::string RuntimeConformanceSetMinLevelSym() {
  return project::RuntimePathSig({"conformance", "set_min_level"});
}

std::vector<std::string> RuntimeSpecSyms() {
  std::vector<std::string> syms;

  AppendRuntimeSymbol(syms, RuntimePanicSym());
  AppendRuntimeSymbol(syms, BuiltinSymStringDropManaged());
  AppendRuntimeSymbol(syms, BuiltinSymBytesDropManaged());
  AppendRuntimeSymbol(syms, ContextInitSym());

  static const std::array<BuiltinSymbolFactory, 13> kRegionSymbols = {{
      &BuiltinModalSymRegionNewScoped,
      &BuiltinModalSymRegionAlloc,
      &BuiltinModalSymRegionMark,
      &BuiltinModalSymRegionResetTo,
      &BuiltinModalSymRegionResetUnchecked,
      &BuiltinModalSymRegionFreeze,
      &BuiltinModalSymRegionThaw,
      &BuiltinModalSymRegionFreeUnchecked,
      &BuiltinModalSymRegionAddrIsActive,
      &BuiltinModalSymRegionAddrTagFrom,
      &BuiltinSymCancelTokenNew,
      &BuiltinSymCancelTokenActiveCancel,
      &BuiltinSymCancelTokenActiveIsCancelled,
  }};
  AppendRuntimeSymbols(syms, kRegionSymbols);

  static const std::array<BuiltinSymbolFactory, 8> kStringSymbols = {{
      &BuiltinSymStringFrom,
      &BuiltinSymStringAsView,
      &BuiltinSymStringSlice,
      &BuiltinSymStringToManaged,
      &BuiltinSymStringCloneWith,
      &BuiltinSymStringAppend,
      &BuiltinSymStringLength,
      &BuiltinSymStringIsEmpty,
  }};
  AppendRuntimeSymbols(syms, kStringSymbols);

  static const std::array<BuiltinSymbolFactory, 10> kBytesSymbols = {{
      &BuiltinSymBytesWithCapacity,
      &BuiltinSymBytesFromSlice,
      &BuiltinSymBytesAsView,
      &BuiltinSymBytesToManaged,
      &BuiltinSymBytesView,
      &BuiltinSymBytesViewString,
      &BuiltinSymBytesAsSlice,
      &BuiltinSymBytesAppend,
      &BuiltinSymBytesLength,
      &BuiltinSymBytesIsEmpty,
  }};
  AppendRuntimeSymbols(syms, kBytesSymbols);

  static const std::array<BuiltinSymbolFactory, 16> kIOSymbols = {{
      &BuiltinSymIOOpenRead,
      &BuiltinSymIOOpenWrite,
      &BuiltinSymIOOpenAppend,
      &BuiltinSymIOCreateWrite,
      &BuiltinSymIOReadFile,
      &BuiltinSymIOReadBytes,
      &BuiltinSymIOWriteFile,
      &BuiltinSymIOWriteStdout,
      &BuiltinSymIOWriteStderr,
      &BuiltinSymIOExists,
      &BuiltinSymIORemove,
      &BuiltinSymIOOpenDir,
      &BuiltinSymIOCreateDir,
      &BuiltinSymIOEnsureDir,
      &BuiltinSymIOKind,
      &BuiltinSymIORestrict,
  }};
  AppendRuntimeSymbols(syms, kIOSymbols);

  static const std::array<BuiltinSymbolFactory, 1> kNetworkSymbols = {{
      &BuiltinSymNetworkRestrictHost,
  }};
  AppendRuntimeSymbols(syms, kNetworkSymbols);

  static const std::array<BuiltinSymbolFactory, 3> kHeapSymbols = {{
      &BuiltinSymHeapAllocatorWithQuota,
      &BuiltinSymHeapAllocatorAllocRaw,
      &BuiltinSymHeapAllocatorDeallocRaw,
  }};
  AppendRuntimeSymbols(syms, kHeapSymbols);

  static const std::array<BuiltinSymbolFactory, 7> kSystemSymbols = {{
      &BuiltinSymSystemExit,
      &BuiltinSymSystemGetEnv,
      &BuiltinSymSystemExecutablePath,
      &BuiltinSymSystemArgumentCount,
      &BuiltinSymSystemArgument,
      &BuiltinSymSystemCurrentDirectory,
      &BuiltinSymSystemRun,
  }};
  AppendRuntimeSymbols(syms, kSystemSymbols);

  static const std::array<BuiltinSymbolFactory, 9> kTimeSymbols = {{
      &BuiltinSymTimeMonotonic,
      &BuiltinSymTimeWall,
      &BuiltinSymMonotonicTimeNow,
      &BuiltinSymMonotonicTimeResolution,
      &BuiltinSymMonotonicTimeElapsed,
      &BuiltinSymMonotonicTimeCoarsen,
      &BuiltinSymWallTimeNowUtc,
      &BuiltinSymWallTimeResolution,
      &BuiltinSymWallTimeCoarsen,
  }};
  AppendRuntimeSymbols(syms, kTimeSymbols);

  static const std::array<BuiltinSymbolFactory, 26> kAdditionalBuiltinMethods = {{
      &BuiltinSymAsyncResume,
      &BuiltinSymAsyncAllocFrame,
      &BuiltinSymAsyncFreeFrame,
      &BuiltinSymAsyncTake,
      &BuiltinSymExecutionDomainName,
      &BuiltinSymExecutionDomainMaxConcurrency,
      &BuiltinSymContextCpu,
      &BuiltinSymContextCpuConfigured,
      &BuiltinSymContextGpu,
      &BuiltinSymContextInline,
      &BuiltinSymGpuGlobalId,
      &BuiltinSymGpuLocalId,
      &BuiltinSymGpuWorkgroupId,
      &BuiltinSymGpuWorkgroupSize,
      &BuiltinSymGpuGlobalSize,
      &BuiltinSymGpuNumWorkgroups,
      &BuiltinSymGpuLinearId,
      &BuiltinSymGpuBarrier,
      &BuiltinSymGpuMemoryBarrier,
      &BuiltinSymGpuWorkgroupBarrier,
      &BuiltinSymCancelTokenActiveChild,
      &BuiltinSymCancelTokenActiveWaitCancelled,
      &BuiltinSymReactorRun,
      &BuiltinSymCancelTokenNew,
      &BuiltinSymCancelTokenActiveCancel,
      &BuiltinSymCancelTokenActiveIsCancelled,
  }};
  AppendRuntimeSymbols(syms, kAdditionalBuiltinMethods);
  AppendRuntimeSymbol(syms, BuiltinSymReactorRegister());

  SortUniqueSymbols(syms);
  return syms;
}

std::vector<std::string> RuntimeLinkRequiredSyms() {
  std::vector<std::string> syms;
  AppendRuntimeSymbol(syms, RuntimePanicSym());
  AppendRuntimeSymbol(syms, BuiltinSymStringDropManaged());
  AppendRuntimeSymbol(syms, BuiltinSymBytesDropManaged());
  AppendRuntimeSymbol(syms, ContextInitSym());
  AppendRuntimeSymbol(syms, RuntimeConformanceEmitSym());
  AppendRuntimeSymbol(syms, RuntimeConformanceEmitIntSym());
  AppendRuntimeSymbol(syms, RuntimeConformanceEmitBoolSym());
  AppendRuntimeSymbol(syms, RuntimeConformanceEmitFloatSym());
  AppendRuntimeSymbol(syms, RuntimeConformanceEmitPtrSym());
  AppendRuntimeSymbol(syms, RuntimeConformanceEmitStringSym());
  AppendRuntimeSymbol(syms, RuntimeConformanceEmitStringManagedSym());
  AppendRuntimeSymbol(syms, RuntimeConformanceEmitBytesSym());
  AppendRuntimeSymbol(syms, RuntimeConformanceEmitBytesManagedSym());
  AppendRuntimeSymbol(syms, RuntimeConformanceSetSinkSym());
  AppendRuntimeSymbol(syms, RuntimeConformanceSetRootSym());
  AppendRuntimeSymbol(syms, RuntimeConformanceSetLogFilterSym());
  AppendRuntimeSymbol(syms, RuntimeConformanceSetMinLevelSym());

  static const std::array<BuiltinSymbolFactory, 13> kRegionSymbols = {{
      &BuiltinModalSymRegionNewScoped,
      &BuiltinModalSymRegionAlloc,
      &BuiltinModalSymRegionMark,
      &BuiltinModalSymRegionResetTo,
      &BuiltinModalSymRegionResetUnchecked,
      &BuiltinModalSymRegionFreeze,
      &BuiltinModalSymRegionThaw,
      &BuiltinModalSymRegionFreeUnchecked,
      &BuiltinModalSymRegionAddrIsActive,
      &BuiltinModalSymRegionAddrTagFrom,
      &BuiltinModalSymRegionScopeEnter,
      &BuiltinModalSymRegionScopeExit,
      &BuiltinModalSymRegionAddrTagScope,
  }};
  AppendRuntimeSymbols(syms, kRegionSymbols);

  static const std::array<BuiltinSymbolFactory, 8> kStringSymbols = {{
      &BuiltinSymStringFrom,
      &BuiltinSymStringAsView,
      &BuiltinSymStringSlice,
      &BuiltinSymStringToManaged,
      &BuiltinSymStringCloneWith,
      &BuiltinSymStringAppend,
      &BuiltinSymStringLength,
      &BuiltinSymStringIsEmpty,
  }};
  AppendRuntimeSymbols(syms, kStringSymbols);

  static const std::array<BuiltinSymbolFactory, 10> kBytesSymbols = {{
      &BuiltinSymBytesWithCapacity,
      &BuiltinSymBytesFromSlice,
      &BuiltinSymBytesAsView,
      &BuiltinSymBytesToManaged,
      &BuiltinSymBytesView,
      &BuiltinSymBytesViewString,
      &BuiltinSymBytesAsSlice,
      &BuiltinSymBytesAppend,
      &BuiltinSymBytesLength,
      &BuiltinSymBytesIsEmpty,
  }};
  AppendRuntimeSymbols(syms, kBytesSymbols);

  static const std::array<BuiltinSymbolFactory, 16> kIOSymbols = {{
      &BuiltinSymIOOpenRead,
      &BuiltinSymIOOpenWrite,
      &BuiltinSymIOOpenAppend,
      &BuiltinSymIOCreateWrite,
      &BuiltinSymIOReadFile,
      &BuiltinSymIOReadBytes,
      &BuiltinSymIOWriteFile,
      &BuiltinSymIOWriteStdout,
      &BuiltinSymIOWriteStderr,
      &BuiltinSymIOExists,
      &BuiltinSymIORemove,
      &BuiltinSymIOOpenDir,
      &BuiltinSymIOCreateDir,
      &BuiltinSymIOEnsureDir,
      &BuiltinSymIOKind,
      &BuiltinSymIORestrict,
  }};
  AppendRuntimeSymbols(syms, kIOSymbols);

  static const std::array<BuiltinSymbolFactory, 1> kNetworkSymbols = {{
      &BuiltinSymNetworkRestrictHost,
  }};
  AppendRuntimeSymbols(syms, kNetworkSymbols);

  static const std::array<BuiltinSymbolFactory, 3> kHeapSymbols = {{
      &BuiltinSymHeapAllocatorWithQuota,
      &BuiltinSymHeapAllocatorAllocRaw,
      &BuiltinSymHeapAllocatorDeallocRaw,
  }};
  AppendRuntimeSymbols(syms, kHeapSymbols);

  static const std::array<BuiltinSymbolFactory, 7> kSystemSymbols = {{
      &BuiltinSymSystemExit,
      &BuiltinSymSystemGetEnv,
      &BuiltinSymSystemExecutablePath,
      &BuiltinSymSystemArgumentCount,
      &BuiltinSymSystemArgument,
      &BuiltinSymSystemCurrentDirectory,
      &BuiltinSymSystemRun,
  }};
  AppendRuntimeSymbols(syms, kSystemSymbols);

  static const std::array<BuiltinSymbolFactory, 9> kTimeSymbols = {{
      &BuiltinSymTimeMonotonic,
      &BuiltinSymTimeWall,
      &BuiltinSymMonotonicTimeNow,
      &BuiltinSymMonotonicTimeResolution,
      &BuiltinSymMonotonicTimeElapsed,
      &BuiltinSymMonotonicTimeCoarsen,
      &BuiltinSymWallTimeNowUtc,
      &BuiltinSymWallTimeResolution,
      &BuiltinSymWallTimeCoarsen,
  }};
  AppendRuntimeSymbols(syms, kTimeSymbols);

  SortUniqueSymbols(syms);
  return syms;
}

std::vector<std::string> RuntimeBuiltinNoPanicOutSyms() {
  std::vector<std::string> syms = RuntimeLinkRequiredSyms();
  static const std::array<BuiltinSymbolFactory, 26> kAdditionalSymbols = {{
      &BuiltinSymAsyncResume,
      &BuiltinSymAsyncAllocFrame,
      &BuiltinSymAsyncFreeFrame,
      &BuiltinSymAsyncTake,
      &BuiltinSymExecutionDomainName,
      &BuiltinSymExecutionDomainMaxConcurrency,
      &BuiltinSymContextCpu,
      &BuiltinSymContextCpuConfigured,
      &BuiltinSymContextGpu,
      &BuiltinSymContextInline,
      &BuiltinSymGpuGlobalId,
      &BuiltinSymGpuLocalId,
      &BuiltinSymGpuWorkgroupId,
      &BuiltinSymGpuWorkgroupSize,
      &BuiltinSymGpuGlobalSize,
      &BuiltinSymGpuNumWorkgroups,
      &BuiltinSymGpuLinearId,
      &BuiltinSymGpuBarrier,
      &BuiltinSymGpuMemoryBarrier,
      &BuiltinSymGpuWorkgroupBarrier,
      &BuiltinSymCancelTokenNew,
      &BuiltinSymCancelTokenActiveCancel,
      &BuiltinSymCancelTokenActiveIsCancelled,
      &BuiltinSymCancelTokenActiveChild,
      &BuiltinSymCancelTokenActiveWaitCancelled,
  }};
  AppendRuntimeSymbols(syms, kAdditionalSymbols);

  static const std::array<BuiltinSymbolFactory, 2> kReactorSymbols = {{
      &BuiltinSymReactorRun,
      &BuiltinSymReactorRegister,
  }};
  AppendRuntimeSymbols(syms, kReactorSymbols);

  SortUniqueSymbols(syms);
  return syms;
}

// =============================================================================
// Dispatch function for BuiltinSym by qualified name
// =============================================================================

std::string BuiltinSym(const std::string& qualified_name) {
  SPEC_DEF("BuiltinSym", "");

  static const std::array<BuiltinSymbolEntry, 16> kIOBuiltins = {{
      {"IO::open_read", &BuiltinSymIOOpenRead},
      {"IO::open_write", &BuiltinSymIOOpenWrite},
      {"IO::open_append", &BuiltinSymIOOpenAppend},
      {"IO::create_write", &BuiltinSymIOCreateWrite},
      {"IO::read_file", &BuiltinSymIOReadFile},
      {"IO::read_bytes", &BuiltinSymIOReadBytes},
      {"IO::write_file", &BuiltinSymIOWriteFile},
      {"IO::write_stdout", &BuiltinSymIOWriteStdout},
      {"IO::write_stderr", &BuiltinSymIOWriteStderr},
      {"IO::exists", &BuiltinSymIOExists},
      {"IO::remove", &BuiltinSymIORemove},
      {"IO::open_dir", &BuiltinSymIOOpenDir},
      {"IO::create_dir", &BuiltinSymIOCreateDir},
      {"IO::ensure_dir", &BuiltinSymIOEnsureDir},
      {"IO::kind", &BuiltinSymIOKind},
      {"IO::restrict", &BuiltinSymIORestrict},
  }};
  static const std::array<BuiltinSymbolEntry, 1> kNetworkBuiltins = {{
      {"Network::restrict_to_host", &BuiltinSymNetworkRestrictHost},
  }};
  static const std::array<BuiltinSymbolEntry, 3> kHeapBuiltins = {{
      {"HeapAllocator::with_quota", &BuiltinSymHeapAllocatorWithQuota},
      {"HeapAllocator::alloc_raw", &BuiltinSymHeapAllocatorAllocRaw},
      {"HeapAllocator::dealloc_raw", &BuiltinSymHeapAllocatorDeallocRaw},
  }};
  static const std::array<BuiltinSymbolEntry, 7> kSystemBuiltins = {{
      {"System::exit", &BuiltinSymSystemExit},
      {"System::get_env", &BuiltinSymSystemGetEnv},
      {"System::executable_path", &BuiltinSymSystemExecutablePath},
      {"System::argument_count", &BuiltinSymSystemArgumentCount},
      {"System::argument", &BuiltinSymSystemArgument},
      {"System::current_directory", &BuiltinSymSystemCurrentDirectory},
      {"System::run", &BuiltinSymSystemRun},
  }};
  static const std::array<BuiltinSymbolEntry, 9> kTimeBuiltins = {{
      {"Time::monotonic", &BuiltinSymTimeMonotonic},
      {"Time::wall", &BuiltinSymTimeWall},
      {"MonotonicTime::now", &BuiltinSymMonotonicTimeNow},
      {"MonotonicTime::resolution", &BuiltinSymMonotonicTimeResolution},
      {"MonotonicTime::elapsed", &BuiltinSymMonotonicTimeElapsed},
      {"MonotonicTime::coarsen", &BuiltinSymMonotonicTimeCoarsen},
      {"WallTime::now_utc", &BuiltinSymWallTimeNowUtc},
      {"WallTime::resolution", &BuiltinSymWallTimeResolution},
      {"WallTime::coarsen", &BuiltinSymWallTimeCoarsen},
  }};
  static const std::array<BuiltinSymbolEntry, 2> kExecutionDomainBuiltins = {{
      {"ExecutionDomain::name", &BuiltinSymExecutionDomainName},
      {"ExecutionDomain::max_concurrency", &BuiltinSymExecutionDomainMaxConcurrency},
  }};
  static const std::array<BuiltinSymbolEntry, 3> kContextBuiltins = {{
      {"Context::cpu", &BuiltinSymContextCpu},
      {"Context::gpu", &BuiltinSymContextGpu},
      {"Context::inline", &BuiltinSymContextInline},
  }};
  static const std::array<BuiltinSymbolEntry, 10> kGpuBuiltins = {{
      {"gpu_global_id", &BuiltinSymGpuGlobalId},
      {"gpu_local_id", &BuiltinSymGpuLocalId},
      {"gpu_workgroup_id", &BuiltinSymGpuWorkgroupId},
      {"gpu_workgroup_size", &BuiltinSymGpuWorkgroupSize},
      {"gpu_global_size", &BuiltinSymGpuGlobalSize},
      {"gpu_num_workgroups", &BuiltinSymGpuNumWorkgroups},
      {"gpu_linear_id", &BuiltinSymGpuLinearId},
      {"gpu_barrier", &BuiltinSymGpuBarrier},
      {"gpu_memory_barrier", &BuiltinSymGpuMemoryBarrier},
      {"gpu_workgroup_barrier", &BuiltinSymGpuWorkgroupBarrier},
  }};
  static const std::array<BuiltinSymbolEntry, 5> kCancelTokenBuiltins = {{
      {"CancelToken::new", &BuiltinSymCancelTokenNew},
      {"CancelToken::Active::cancel", &BuiltinSymCancelTokenActiveCancel},
      {"CancelToken::Active::is_cancelled", &BuiltinSymCancelTokenActiveIsCancelled},
      {"CancelToken::Active::child", &BuiltinSymCancelTokenActiveChild},
      {"CancelToken::Active::wait_cancelled", &BuiltinSymCancelTokenActiveWaitCancelled},
  }};
  static const std::array<BuiltinSymbolEntry, 2> kReactorBuiltins = {{
      {"Reactor::run", &BuiltinSymReactorRun},
      {"Reactor::register", &BuiltinSymReactorRegister},
  }};
  static const std::array<BuiltinSymbolEntry, 8> kStringBuiltins = {{
      {"string::from", &BuiltinSymStringFrom},
      {"string::as_view", &BuiltinSymStringAsView},
      {"string::slice", &BuiltinSymStringSlice},
      {"string::to_managed", &BuiltinSymStringToManaged},
      {"string::clone_with", &BuiltinSymStringCloneWith},
      {"string::append", &BuiltinSymStringAppend},
      {"string::length", &BuiltinSymStringLength},
      {"string::is_empty", &BuiltinSymStringIsEmpty},
  }};
  static const std::array<BuiltinSymbolEntry, 10> kBytesBuiltins = {{
      {"bytes::with_capacity", &BuiltinSymBytesWithCapacity},
      {"bytes::from_slice", &BuiltinSymBytesFromSlice},
      {"bytes::as_view", &BuiltinSymBytesAsView},
      {"bytes::as_slice", &BuiltinSymBytesAsSlice},
      {"bytes::to_managed", &BuiltinSymBytesToManaged},
      {"bytes::view", &BuiltinSymBytesView},
      {"bytes::view_string", &BuiltinSymBytesViewString},
      {"bytes::append", &BuiltinSymBytesAppend},
      {"bytes::length", &BuiltinSymBytesLength},
      {"bytes::is_empty", &BuiltinSymBytesIsEmpty},
  }};
  static const std::array<BuiltinSymbolEntry, 3> kFoundationalBuiltins = {{
      {"Eq::eq", &BuiltinSymEqEq},
      {"Step::successor", &BuiltinSymStepSuccessor},
      {"Step::predecessor", &BuiltinSymStepPredecessor},
  }};
  static const std::array<BuiltinSymbolEntry, 8> kRegionBuiltins = {{
      {"Region::new_scoped", &BuiltinModalSymRegionNewScoped},
      {"Region::alloc", &BuiltinModalSymRegionAlloc},
      {"Region::mark", &BuiltinModalSymRegionMark},
      {"Region::reset_to", &BuiltinModalSymRegionResetTo},
      {"Region::reset_unchecked", &BuiltinModalSymRegionResetUnchecked},
      {"Region::freeze", &BuiltinModalSymRegionFreeze},
      {"Region::thaw", &BuiltinModalSymRegionThaw},
      {"Region::free_unchecked", &BuiltinModalSymRegionFreeUnchecked},
  }};

  // IO methods
  if (const auto sym = LookupBuiltinSymbol(qualified_name, kIOBuiltins);
      !sym.empty()) {
    return sym;
  }

  // Network methods
  if (const auto sym = LookupBuiltinSymbol(qualified_name, kNetworkBuiltins);
      !sym.empty()) {
    return sym;
  }

  // HeapAllocator methods
  if (const auto sym = LookupBuiltinSymbol(qualified_name, kHeapBuiltins);
      !sym.empty()) {
    return sym;
  }

  // System methods
  if (const auto sym = LookupBuiltinSymbol(qualified_name, kSystemBuiltins);
      !sym.empty()) {
    return sym;
  }

  // Time methods
  if (const auto sym = LookupBuiltinSymbol(qualified_name, kTimeBuiltins);
      !sym.empty()) {
    return sym;
  }

  // ExecutionDomain methods
  if (const auto sym = LookupBuiltinSymbol(qualified_name, kExecutionDomainBuiltins);
      !sym.empty()) {
    return sym;
  }

  // Context execution domain constructors
  if (const auto sym = LookupBuiltinSymbol(qualified_name, kContextBuiltins);
      !sym.empty()) {
    return sym;
  }

  // CancelToken builtins
  // GPU intrinsics (unqualified identifiers)
  if (const auto sym = LookupBuiltinSymbol(qualified_name, kGpuBuiltins);
      !sym.empty()) {
    return sym;
  }


  if (const auto sym = LookupBuiltinSymbol(qualified_name, kCancelTokenBuiltins);
      !sym.empty()) {
    return sym;
  }

  // Reactor builtins (Section 19)
  if (const auto sym = LookupBuiltinSymbol(qualified_name, kReactorBuiltins);
      !sym.empty()) {
    return sym;
  }

  // String builtins
  if (const auto sym = LookupBuiltinSymbol(qualified_name, kStringBuiltins);
      !sym.empty()) {
    return sym;
  }

  // Bytes builtins
  if (const auto sym = LookupBuiltinSymbol(qualified_name, kBytesBuiltins);
      !sym.empty()) {
    return sym;
  }

  // Foundational intrinsic-call methods
  if (const auto sym = LookupBuiltinSymbol(qualified_name, kFoundationalBuiltins);
      !sym.empty()) {
    return sym;
  }

  // Handle unknown string:: or bytes:: prefixed names
  if (qualified_name.rfind("string::", 0) == 0) {
    SPEC_RULE("BuiltinSym-String-Err");
    return "";
  }
  if (qualified_name.rfind("bytes::", 0) == 0) {
    SPEC_RULE("BuiltinSym-Bytes-Err");
    return "";
  }

  // Region methods (alternate qualified form)
  if (const auto sym = LookupBuiltinSymbol(qualified_name, kRegionBuiltins);
      !sym.empty()) {
    return sym;
  }

  // Unknown builtin - return empty string
  return "";
}

// =============================================================================
// Spec Rule Anchors
// =============================================================================

void AnchorBuiltinSymRules() {
  // Section 6.9 Runtime Interface
  SPEC_RULE("RegionLayout");
  SPEC_RULE("BuiltinModalSym-NewScoped");
  SPEC_RULE("BuiltinModalSym-Alloc");
  SPEC_RULE("BuiltinModalSym-Mark");
  SPEC_RULE("BuiltinModalSym-ResetTo");
  SPEC_RULE("BuiltinModalSym-ResetUnchecked");
  SPEC_RULE("BuiltinModalSym-Freeze");
  SPEC_RULE("BuiltinModalSym-Thaw");
  SPEC_RULE("BuiltinModalSym-FreeUnchecked");
  SPEC_RULE("BuiltinModalSym-AddrIsActive");
  SPEC_RULE("BuiltinModalSym-AddrTagFrom");
  SPEC_RULE("BuiltinSym-IO-OpenRead");
  SPEC_RULE("BuiltinSym-IO-OpenWrite");
  SPEC_RULE("BuiltinSym-IO-OpenAppend");
  SPEC_RULE("BuiltinSym-IO-CreateWrite");
  SPEC_RULE("BuiltinSym-IO-ReadFile");
  SPEC_RULE("BuiltinSym-IO-ReadBytes");
  SPEC_RULE("BuiltinSym-IO-WriteFile");
  SPEC_RULE("BuiltinSym-IO-WriteStdout");
  SPEC_RULE("BuiltinSym-IO-WriteStderr");
  SPEC_RULE("BuiltinSym-IO-Exists");
  SPEC_RULE("BuiltinSym-IO-Remove");
  SPEC_RULE("BuiltinSym-IO-OpenDir");
  SPEC_RULE("BuiltinSym-IO-CreateDir");
  SPEC_RULE("BuiltinSym-IO-EnsureDir");
  SPEC_RULE("BuiltinSym-IO-Kind");
  SPEC_RULE("BuiltinSym-IO-Restrict");
  SPEC_RULE("BuiltinSym-Network-RestrictHost");
  SPEC_RULE("BuiltinSym-HeapAllocator-WithQuota");
  SPEC_RULE("BuiltinSym-HeapAllocator-AllocRaw");
  SPEC_RULE("BuiltinSym-HeapAllocator-DeallocRaw");
  SPEC_RULE("BuiltinSym-System-Exit");
  SPEC_RULE("BuiltinSym-System-GetEnv");
  SPEC_RULE("BuiltinSym-System-Run");
  SPEC_RULE("BuiltinSym-Time-Monotonic");
  SPEC_RULE("BuiltinSym-Time-Wall");
  SPEC_RULE("BuiltinSym-MonotonicTime-Now");
  SPEC_RULE("BuiltinSym-MonotonicTime-Resolution");
  SPEC_RULE("BuiltinSym-MonotonicTime-Elapsed");
  SPEC_RULE("BuiltinSym-MonotonicTime-Coarsen");
  SPEC_RULE("BuiltinSym-WallTime-NowUtc");
  SPEC_RULE("BuiltinSym-WallTime-Resolution");
  SPEC_RULE("BuiltinSym-WallTime-Coarsen");

  // Section 6.8 Panic
  SPEC_RULE("PanicSym");
}

}  // namespace ultraviolet::codegen
