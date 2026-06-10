// =============================================================================
// MIGRATION MAPPING: host_primitives.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 1.7 "Host Primitives" (lines 727-744)
//     - IOPrim: IO operations set
//       {IOOpenRead, IOOpenWrite, IOOpenAppend, IOCreateWrite, IOReadFile,
//        IOReadBytes, IOWriteFile, IOWriteStdout, IOWriteStderr, IOExists,
//        IORemove, IOOpenDir, IOCreateDir, IOEnsureDir, IOKind, IORestrict}
//     - FilePrim: file handle operations
//       {FileReadAll, FileReadAllBytes, FileWrite, FileFlush, FileClose}
//     - DirPrim: directory iterator operations
//       {DirNext, DirClose}
//     - SystemPrim: {SystemGetEnv, SystemExit, SystemRun}
//     - NetworkPrim: {NetRestrictHost}
//     - HeapPrim: {HeapWithQuota, HeapAllocRaw, HeapDeallocRaw}
//     - ReactorPrim: {ReactorRun, ReactorRegister}
//     - TimePrim: {TimeMonotonic, TimeWall, MonotonicTimeNow,
//        MonotonicTimeResolution, MonotonicTimeElapsed, MonotonicTimeCoarsen,
//        WallTimeNowUtc, WallTimeResolution, WallTimeCoarsen}
//     - CancelPrim: {CancelNew, CancelChild, CancelDoCancel, CancelIsCancelled, CancelWaitCancelled}
//     - HostPrim: union of all primitive sets
//     - HostPrimDiag: compiler diagnostic primitives
//       {ParseTOML, ReadBytes, WriteFile, ResolveTool, ResolveRuntimeLib,
//        Invoke, AssembleIR, InvokeLinker, InvokeArchiver, ArchiveMembers}
//     - HostPrimRuntime includes IOPrim, FilePrim, DirPrim, SystemPrim,
//       NetworkPrim, HeapPrim, ReactorPrim, TimePrim, and CancelPrim
//     - MapsToDiagOrRuntime(p): true if p is in HostPrimDiag or HostPrimRuntime
//     - HostPrimFail(p): failure outside mapped primitives is ill-formed
//
// SOURCE FILE: ultraviolet-bootstrap/src/00_core/host_primitives.cpp
//   - Lines 1-113 (entire file)
//
// CONTENT TO MIGRATE:
//   - IsIOPrim(prim) -> bool (lines 20-44)
//     Checks if primitive is a IO operation
//   - IsFilePrim(prim) -> bool (lines 46-59)
//     Checks if primitive is a file handle operation
//   - IsDirPrim(prim) -> bool (lines 61-71)
//     Checks if primitive is a directory operation
//   - IsHostPrimDiag(prim) -> bool (lines 73-88)
//     Checks if primitive is a compiler diagnostic operation
//   - IsHostPrimRuntime(prim) -> bool
//     Returns true for every HostPrimRuntime primitive set in spec section 1.7.
//   - MapsToDiagOrRuntime(prim) -> bool (lines 95-98)
//     Returns IsHostPrimDiag || IsHostPrimRuntime
//   - HostPrimFail(prim, failed) -> bool (lines 100-106)
//     Aborts if failed and not mapped to diag/runtime
//   - HostPrimFailureIllFormed(prim, failed) -> bool (lines 108-111)
//     Returns true if failure would be ill-formed
//
// DEPENDENCIES:
//   - ultraviolet/include/00_core/host_primitives.h (header)
//     - HostPrim enum with all primitive values
//   - ultraviolet/include/00_core/assert_spec.h
//     - SPEC_DEF macro
//   - <cstdlib> for std::abort()
//
// REFACTORING NOTES:
//   1. IsHostPrimRuntime now checks all primitive sets per spec section 1.7:
//      IOPrim, FilePrim, DirPrim, SystemPrim, NetworkPrim, HeapPrim,
//      ReactorPrim, TimePrim, CancelPrim
//   2. All SPEC_DEF traces point to "1.7"
//   3. HostPrimFail calls std::abort() for unmapped failures
//   4. Switch statements use explicit case enumeration - maintain for clarity
//   5. Added IsSystemPrim, IsHeapPrim, IsReactorPrim, IsCancelPrim helpers
//   6. HostPrim enum includes all primitives from spec section 1.7
//
// =============================================================================

#include "00_core/host_primitives.h"

#include <cstdlib>

#include "00_core/assert_spec.h"

namespace ultraviolet::core {

static inline void SpecDefsHostPrimitives() {
  SPEC_DEF("HostPrim", "1.7");
  SPEC_DEF("HostPrimDiag", "1.7");
  SPEC_DEF("HostPrimRuntime", "1.7");
  SPEC_DEF("MapsToDiagOrRuntime", "1.7");
  SPEC_DEF("HostPrimFail", "1.7");
  SPEC_DEF("IOPrim", "1.7");
  SPEC_DEF("FilePrim", "1.7");
  SPEC_DEF("DirPrim", "1.7");
  SPEC_DEF("SystemPrim", "1.7");
  SPEC_DEF("NetworkPrim", "1.7");
  SPEC_DEF("HeapPrim", "1.7");
  SPEC_DEF("ReactorPrim", "1.7");
  SPEC_DEF("TimePrim", "1.7");
  SPEC_DEF("CancelPrim", "1.7");
}

bool IsIOPrim(HostPrim prim) {
  SpecDefsHostPrimitives();
  // IOPrim is the set of IO operations defined by section 7.7 (IOJudg/IOOp).
  switch (prim) {
    case HostPrim::IOOpenRead:
    case HostPrim::IOOpenWrite:
    case HostPrim::IOOpenAppend:
    case HostPrim::IOCreateWrite:
    case HostPrim::IOReadFile:
    case HostPrim::IOReadBytes:
    case HostPrim::IOWriteFile:
    case HostPrim::IOWriteStdout:
    case HostPrim::IOWriteStderr:
    case HostPrim::IOExists:
    case HostPrim::IORemove:
    case HostPrim::IOOpenDir:
    case HostPrim::IOCreateDir:
    case HostPrim::IOEnsureDir:
    case HostPrim::IOKind:
    case HostPrim::IORestrict:
      return true;
    default:
      return false;
  }
}

bool IsFilePrim(HostPrim prim) {
  SpecDefsHostPrimitives();
  // FilePrim is the set of file-handle operations defined by section 7.7 (FileJudg).
  switch (prim) {
    case HostPrim::FileReadAll:
    case HostPrim::FileReadAllBytes:
    case HostPrim::FileWrite:
    case HostPrim::FileFlush:
    case HostPrim::FileClose:
      return true;
    default:
      return false;
  }
}

bool IsDirPrim(HostPrim prim) {
  SpecDefsHostPrimitives();
  // DirPrim is the set of directory-iterator operations defined by section 7.7 (DirJudg).
  switch (prim) {
    case HostPrim::DirNext:
    case HostPrim::DirClose:
      return true;
    default:
      return false;
  }
}

bool IsSystemPrim(HostPrim prim) {
  SpecDefsHostPrimitives();
  // SystemPrim: {SystemGetEnv, SystemExit, SystemRun} per section 1.7
  switch (prim) {
    case HostPrim::SystemGetEnv:
    case HostPrim::SystemExit:
    case HostPrim::SystemRun:
      return true;
    default:
      return false;
  }
}

bool IsNetworkPrim(HostPrim prim) {
  SpecDefsHostPrimitives();
  switch (prim) {
    case HostPrim::NetRestrictHost:
      return true;
    default:
      return false;
  }
}

bool IsHeapPrim(HostPrim prim) {
  SpecDefsHostPrimitives();
  // HeapPrim: {HeapWithQuota, HeapAllocRaw, HeapDeallocRaw} per section 1.7
  switch (prim) {
    case HostPrim::HeapWithQuota:
    case HostPrim::HeapAllocRaw:
    case HostPrim::HeapDeallocRaw:
      return true;
    default:
      return false;
  }
}

bool IsReactorPrim(HostPrim prim) {
  SpecDefsHostPrimitives();
  // ReactorPrim: {ReactorRun, ReactorRegister} per section 1.7
  switch (prim) {
    case HostPrim::ReactorRun:
    case HostPrim::ReactorRegister:
      return true;
    default:
      return false;
  }
}

bool IsTimePrim(HostPrim prim) {
  SpecDefsHostPrimitives();
  switch (prim) {
    case HostPrim::TimeMonotonic:
    case HostPrim::TimeWall:
    case HostPrim::MonotonicTimeNow:
    case HostPrim::MonotonicTimeResolution:
    case HostPrim::MonotonicTimeElapsed:
    case HostPrim::MonotonicTimeCoarsen:
    case HostPrim::WallTimeNowUtc:
    case HostPrim::WallTimeResolution:
    case HostPrim::WallTimeCoarsen:
      return true;
    default:
      return false;
  }
}

bool IsCancelPrim(HostPrim prim) {
  SpecDefsHostPrimitives();
  // CancelPrim: {CancelNew, CancelChild, CancelDoCancel, CancelIsCancelled, CancelWaitCancelled} per section 1.7
  switch (prim) {
    case HostPrim::CancelNew:
    case HostPrim::CancelChild:
    case HostPrim::CancelDoCancel:
    case HostPrim::CancelIsCancelled:
    case HostPrim::CancelWaitCancelled:
      return true;
    default:
      return false;
  }
}

bool IsHostPrimDiag(HostPrim prim) {
  SpecDefsHostPrimitives();
  switch (prim) {
    case HostPrim::ParseTOML:
    case HostPrim::ReadBytes:
    case HostPrim::WriteFile:
    case HostPrim::ResolveTool:
    case HostPrim::ResolveRuntimeLib:
    case HostPrim::Invoke:
    case HostPrim::AssembleIR:
    case HostPrim::InvokeLinker:
    case HostPrim::InvokeArchiver:
    case HostPrim::ArchiveMembers:
      return true;
    default:
      return false;
  }
}

bool IsHostPrimRuntime(HostPrim prim) {
  SpecDefsHostPrimitives();
  // HostPrimRuntime is the union of all runtime primitive sets in section 1.7.
  return IsIOPrim(prim) || IsFilePrim(prim) || IsDirPrim(prim) ||
         IsSystemPrim(prim) || IsNetworkPrim(prim) || IsHeapPrim(prim) ||
         IsReactorPrim(prim) || IsTimePrim(prim) || IsCancelPrim(prim);
}

bool MapsToDiagOrRuntime(HostPrim prim) {
  SpecDefsHostPrimitives();
  return IsHostPrimDiag(prim) || IsHostPrimRuntime(prim);
}

bool HostPrimFail(HostPrim prim, bool failed) {
  SpecDefsHostPrimitives();
  if (failed && !MapsToDiagOrRuntime(prim)) {
    std::abort();
  }
  return failed;
}

bool HostPrimFailureIllFormed(HostPrim prim, bool failed) {
  SpecDefsHostPrimitives();
  return failed && !MapsToDiagOrRuntime(prim);
}

}  // namespace ultraviolet::core
