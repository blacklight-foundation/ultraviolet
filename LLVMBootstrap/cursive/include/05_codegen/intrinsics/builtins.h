#pragma once

#include <string>
#include <vector>

namespace cursive::codegen {

// =============================================================================
// Section 6.9 BuiltinSym - Builtin symbol resolution
// =============================================================================
//
// Returns the mangled symbol for a builtin by its qualified name
// e.g., "FileSystem::open_read" -> PathSig(["cursive", "runtime", "fs", "open_read"])
// e.g., "Context::cpu" -> PathSig(["cursive", "runtime", "context", "cpu"])
//
// Returns empty string if the qualified name is not a known builtin.

std::string BuiltinSym(const std::string& qualified_name);

// =============================================================================
// Section 6.9 BuiltinModalSym - Region method symbol resolution
// =============================================================================

std::string BuiltinModalSym(const std::string& method);

// Individual region symbols
std::string BuiltinModalSymRegionNewScoped();
std::string BuiltinModalSymRegionAlloc();
std::string BuiltinModalSymRegionMark();
std::string BuiltinModalSymRegionResetTo();
std::string BuiltinModalSymRegionResetUnchecked();
std::string BuiltinModalSymRegionFreeze();
std::string BuiltinModalSymRegionThaw();
std::string BuiltinModalSymRegionFreeUnchecked();
std::string BuiltinModalSymRegionAddrIsActive();
std::string BuiltinModalSymRegionAddrTagFrom();
std::string BuiltinModalSymRegionScopeEnter();
std::string BuiltinModalSymRegionScopeExit();
std::string BuiltinModalSymRegionAddrTagScope();

// =============================================================================
// Section 6.9 FileSystem builtin symbols
// =============================================================================

std::string BuiltinSymFileSystemOpenRead();
std::string BuiltinSymFileSystemOpenWrite();
std::string BuiltinSymFileSystemOpenAppend();
std::string BuiltinSymFileSystemCreateWrite();
std::string BuiltinSymFileSystemReadFile();
std::string BuiltinSymFileSystemReadBytes();
std::string BuiltinSymFileSystemWriteFile();
std::string BuiltinSymFileSystemWriteStdout();
std::string BuiltinSymFileSystemWriteStderr();
std::string BuiltinSymFileSystemExists();
std::string BuiltinSymFileSystemRemove();
std::string BuiltinSymFileSystemOpenDir();
std::string BuiltinSymFileSystemCreateDir();
std::string BuiltinSymFileSystemEnsureDir();
std::string BuiltinSymFileSystemKind();
std::string BuiltinSymFileSystemRestrict();

// =============================================================================
// Section 6.9 Network builtin symbols
// =============================================================================

std::string BuiltinSymNetworkRestrictHost();

// =============================================================================
// Section 6.9 HeapAllocator builtin symbols
// =============================================================================

std::string BuiltinSymHeapAllocatorWithQuota();
std::string BuiltinSymHeapAllocatorAllocRaw();
std::string BuiltinSymHeapAllocatorDeallocRaw();

// =============================================================================
// Section 5.9.4 System builtin symbols
// =============================================================================

std::string BuiltinSymSystemExit();
std::string BuiltinSymSystemGetEnv();
std::string BuiltinSymSystemExecutablePath();
std::string BuiltinSymSystemArgumentCount();
std::string BuiltinSymSystemArgument();
std::string BuiltinSymSystemCurrentDirectory();
std::string BuiltinSymSystemRun();

// =============================================================================
// Section 14.9 Time builtin symbols
// =============================================================================

std::string BuiltinSymTimeMonotonic();
std::string BuiltinSymTimeWall();
std::string BuiltinSymMonotonicTimeNow();
std::string BuiltinSymMonotonicTimeResolution();
std::string BuiltinSymMonotonicTimeElapsed();
std::string BuiltinSymMonotonicTimeCoarsen();
std::string BuiltinSymWallTimeNowUtc();
std::string BuiltinSymWallTimeResolution();
std::string BuiltinSymWallTimeCoarsen();

// =============================================================================
// Section 18.2 ExecutionDomain builtin symbols
// =============================================================================

std::string BuiltinSymExecutionDomainName();
std::string BuiltinSymExecutionDomainMaxConcurrency();

// =============================================================================
// Section 18.2 Context execution domain constructors
// =============================================================================

std::string BuiltinSymContextCpu();
std::string BuiltinSymContextCpuConfigured();
std::string BuiltinSymContextGpu();
std::string BuiltinSymContextInline();

// =============================================================================
// Section 18.2 GPU intrinsic symbols
// =============================================================================

std::string BuiltinSymGpuGlobalId();
std::string BuiltinSymGpuLocalId();
std::string BuiltinSymGpuWorkgroupId();
std::string BuiltinSymGpuWorkgroupSize();
std::string BuiltinSymGpuGlobalSize();
std::string BuiltinSymGpuNumWorkgroups();
std::string BuiltinSymGpuLinearId();
std::string BuiltinSymGpuBarrier();
std::string BuiltinSymGpuMemoryBarrier();
std::string BuiltinSymGpuWorkgroupBarrier();

// =============================================================================
// Section 18.6 CancelToken builtin symbols
// =============================================================================

std::string BuiltinSymCancelTokenNew();
std::string BuiltinSymCancelTokenActiveCancel();
std::string BuiltinSymCancelTokenActiveIsCancelled();
std::string BuiltinSymCancelTokenActiveChild();
std::string BuiltinSymCancelTokenActiveWaitCancelled();

// =============================================================================
// Section 19 Reactor builtin symbols
// =============================================================================

std::string BuiltinSymReactorRun();
std::string BuiltinSymReactorRegister();

// =============================================================================
// Section 19 Async builtin symbols
// =============================================================================

std::string BuiltinSymAsyncResume();
std::string BuiltinSymAsyncGetDiscriminant();
std::string BuiltinSymAsyncGetSuspendedOutput();
std::string BuiltinSymAsyncGetCompletedValue();
std::string BuiltinSymAsyncGetFailedError();
std::string BuiltinSymAsyncCreateCompleted();
std::string BuiltinSymAsyncCreateFailed();
std::string BuiltinSymAsyncCreateSuspended();
std::string BuiltinSymAsyncAllocFrame();
std::string BuiltinSymAsyncFreeFrame();
std::string BuiltinSymAsyncTake();

// =============================================================================
// Section 6.12.14 String/Bytes builtin symbols
// =============================================================================

std::string BuiltinSymStringFrom();
std::string BuiltinSymStringAsView();
std::string BuiltinSymStringSlice();
std::string BuiltinSymStringToManaged();
std::string BuiltinSymStringCloneWith();
std::string BuiltinSymStringAppend();
std::string BuiltinSymStringLength();
std::string BuiltinSymStringIsEmpty();
std::string BuiltinSymStringDropManaged();

std::string BuiltinSymBytesWithCapacity();
std::string BuiltinSymBytesFromSlice();
std::string BuiltinSymBytesAsView();
std::string BuiltinSymBytesAsSlice();
std::string BuiltinSymBytesToManaged();
std::string BuiltinSymBytesView();
std::string BuiltinSymBytesViewString();
std::string BuiltinSymBytesAppend();
std::string BuiltinSymBytesLength();
std::string BuiltinSymBytesIsEmpty();
std::string BuiltinSymBytesDropManaged();

// =============================================================================
// Section 14.10.6 Foundational intrinsic-call symbols
// =============================================================================

std::string BuiltinSymEqEq();
std::string BuiltinSymStepSuccessor();
std::string BuiltinSymStepPredecessor();

// =============================================================================
// Section 6.8 Panic symbol
// =============================================================================

std::string RuntimePanicSym();

// =============================================================================
// Section 6.12.5 Context initialization symbol
// =============================================================================

std::string ContextInitSym();

// =============================================================================
// Runtime conformance emission symbols
// =============================================================================

std::string RuntimeConformanceEmitSym();
std::string RuntimeConformanceEmitIntSym();
std::string RuntimeConformanceEmitBoolSym();
std::string RuntimeConformanceEmitFloatSym();
std::string RuntimeConformanceEmitPtrSym();
std::string RuntimeConformanceEmitStringSym();
std::string RuntimeConformanceEmitStringManagedSym();
std::string RuntimeConformanceEmitBytesSym();
std::string RuntimeConformanceEmitBytesManagedSym();
std::string RuntimeConformanceSetSinkSym();
std::string RuntimeConformanceSetRootSym();
std::string RuntimeConformanceSetLogFilterSym();
std::string RuntimeConformanceSetMinLevelSym();

// =============================================================================
// Runtime symbol sets used by linker compatibility and panic-out lowering
// =============================================================================

/// Spec-defined Chapter 24 runtime symbol surface.
std::vector<std::string> RuntimeSpecSyms();

/// Runtime symbols required by project link-time compatibility checks.
std::vector<std::string> RuntimeLinkRequiredSyms();

/// Runtime symbols that never require panic out-parameters.
std::vector<std::string> RuntimeBuiltinNoPanicOutSyms();

// =============================================================================
// Anchor function for SPEC_RULE markers
// =============================================================================

void AnchorBuiltinSymRules();

}  // namespace cursive::codegen
