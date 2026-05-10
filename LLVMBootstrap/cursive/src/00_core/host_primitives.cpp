// =============================================================================
// MIGRATION MAPPING: host_primitives.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 1.7 "Host Primitives" (lines 727-744)
//     - FSPrim: filesystem operations set
//       {FSOpenRead, FSOpenWrite, FSOpenAppend, FSCreateWrite, FSReadFile,
//        FSReadBytes, FSWriteFile, FSWriteStdout, FSWriteStderr, FSExists,
//        FSRemove, FSOpenDir, FSCreateDir, FSEnsureDir, FSKind, FSRestrict}
//     - FilePrim: file handle operations
//       {FileReadAll, FileReadAllBytes, FileWrite, FileFlush, FileClose}
//     - DirPrim: directory iterator operations
//       {DirNext, DirClose}
//     - SystemPrim: {SystemGetEnv, SystemExit, SystemRun}
//     - HeapPrim: {HeapWithQuota, HeapAllocRaw, HeapDeallocRaw}
//     - ReactorPrim: {ReactorRun, ReactorRegister}
//     - CancelPrim: {CancelNew, CancelChild, CancelDoCancel, CancelIsCancelled, CancelWaitCancelled}
//     - HostPrim: union of all primitive sets
//     - HostPrimDiag: compiler diagnostic primitives
//       {ParseTOML, ReadBytes, WriteFile, ResolveTool, ResolveRuntimeLib,
//        Invoke, AssembleIR, InvokeLinker, InvokeArchiver}
//     - HostPrimRuntime: FSPrim ∪ FilePrim ∪ DirPrim ∪ SystemPrim ∪ HeapPrim ∪ ReactorPrim ∪ CancelPrim
//     - MapsToDiagOrRuntime(p): true if p in HostPrimDiag ∪ HostPrimRuntime
//     - HostPrimFail(p): failure outside mapped primitives is ill-formed
//
// SOURCE FILE: cursive-bootstrap/src/00_core/host_primitives.cpp
//   - Lines 1-113 (entire file)
//
// CONTENT TO MIGRATE:
//   - IsFSPrim(prim) -> bool (lines 20-44)
//     Checks if primitive is a filesystem operation
//   - IsFilePrim(prim) -> bool (lines 46-59)
//     Checks if primitive is a file handle operation
//   - IsDirPrim(prim) -> bool (lines 61-71)
//     Checks if primitive is a directory operation
//   - IsHostPrimDiag(prim) -> bool (lines 73-88)
//     Checks if primitive is a compiler diagnostic operation
//   - IsHostPrimRuntime(prim) -> bool
//     Returns IsFSPrim || IsFilePrim || IsDirPrim || IsSystemPrim || IsHeapPrim || IsReactorPrim || IsCancelPrim
//     Per spec section 1.7: HostPrimRuntime = FSPrim ∪ FilePrim ∪ DirPrim ∪ SystemPrim ∪ HeapPrim ∪ ReactorPrim ∪ CancelPrim
//   - MapsToDiagOrRuntime(prim) -> bool (lines 95-98)
//     Returns IsHostPrimDiag || IsHostPrimRuntime
//   - HostPrimFail(prim, failed) -> bool (lines 100-106)
//     Aborts if failed and not mapped to diag/runtime
//   - HostPrimFailureIllFormed(prim, failed) -> bool (lines 108-111)
//     Returns true if failure would be ill-formed
//
// DEPENDENCIES:
//   - cursive/include/00_core/host_primitives.h (header)
//     - HostPrim enum with all primitive values
//   - cursive/include/00_core/assert_spec.h
//     - SPEC_DEF macro
//   - <cstdlib> for std::abort()
//
// REFACTORING NOTES:
//   1. IsHostPrimRuntime now checks all primitive sets per spec section 1.7:
//      FSPrim, FilePrim, DirPrim, SystemPrim, HeapPrim, ReactorPrim, CancelPrim
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

namespace cursive::core {

static inline void SpecDefsHostPrimitives() {
  SPEC_DEF("HostPrim", "1.7");
  SPEC_DEF("HostPrimDiag", "1.7");
  SPEC_DEF("HostPrimRuntime", "1.7");
  SPEC_DEF("MapsToDiagOrRuntime", "1.7");
  SPEC_DEF("HostPrimFail", "1.7");
  SPEC_DEF("FSPrim", "1.7");
  SPEC_DEF("FilePrim", "1.7");
  SPEC_DEF("DirPrim", "1.7");
  SPEC_DEF("SystemPrim", "1.7");
  SPEC_DEF("HeapPrim", "1.7");
  SPEC_DEF("ReactorPrim", "1.7");
  SPEC_DEF("CancelPrim", "1.7");
}

bool IsFSPrim(HostPrim prim) {
  SpecDefsHostPrimitives();
  // FSPrim is the set of FS operations defined by section 7.7 (FSJudg/FSOp).
  switch (prim) {
    case HostPrim::FSOpenRead:
    case HostPrim::FSOpenWrite:
    case HostPrim::FSOpenAppend:
    case HostPrim::FSCreateWrite:
    case HostPrim::FSReadFile:
    case HostPrim::FSReadBytes:
    case HostPrim::FSWriteFile:
    case HostPrim::FSWriteStdout:
    case HostPrim::FSWriteStderr:
    case HostPrim::FSExists:
    case HostPrim::FSRemove:
    case HostPrim::FSOpenDir:
    case HostPrim::FSCreateDir:
    case HostPrim::FSEnsureDir:
    case HostPrim::FSKind:
    case HostPrim::FSRestrict:
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
      return true;
    default:
      return false;
  }
}

bool IsHostPrimRuntime(HostPrim prim) {
  SpecDefsHostPrimitives();
  // HostPrimRuntime = FSPrim ∪ FilePrim ∪ DirPrim ∪ SystemPrim ∪ HeapPrim ∪ ReactorPrim ∪ CancelPrim
  return IsFSPrim(prim) || IsFilePrim(prim) || IsDirPrim(prim) ||
         IsSystemPrim(prim) || IsHeapPrim(prim) || IsReactorPrim(prim) ||
         IsCancelPrim(prim);
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

}  // namespace cursive::core
