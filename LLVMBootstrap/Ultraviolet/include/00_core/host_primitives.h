#pragma once

namespace ultraviolet::core {

enum class HostPrim {
  ParseTOML,
  ReadBytes,
  WriteFile,
  ResolveTool,
  ResolveRuntimeLib,
  Invoke,
  AssembleIR,
  InvokeLinker,
  InvokeArchiver,

  IOOpenRead,
  IOOpenWrite,
  IOOpenAppend,
  IOCreateWrite,
  IOReadFile,
  IOReadBytes,
  IOWriteFile,
  IOWriteStdout,
  IOWriteStderr,
  IOExists,
  IORemove,
  IOOpenDir,
  IOCreateDir,
  IOEnsureDir,
  IOKind,
  IORestrict,

  FileReadAll,
  FileReadAllBytes,
  FileWrite,
  FileFlush,
  FileClose,

  DirNext,
  DirClose,

  // SystemPrim: system operations (section 1.7)
  SystemGetEnv,
  SystemExit,
  SystemRun,

  // HeapPrim: heap allocation operations (section 1.7)
  HeapWithQuota,
  HeapAllocRaw,
  HeapDeallocRaw,

  // ReactorPrim: reactor operations (section 1.7)
  ReactorRun,
  ReactorRegister,

  // CancelPrim: cancellation token operations (section 1.7)
  CancelNew,
  CancelChild,
  CancelDoCancel,
  CancelIsCancelled,
  CancelWaitCancelled,
};

bool IsIOPrim(HostPrim prim);
bool IsFilePrim(HostPrim prim);
bool IsDirPrim(HostPrim prim);
bool IsSystemPrim(HostPrim prim);
bool IsHeapPrim(HostPrim prim);
bool IsReactorPrim(HostPrim prim);
bool IsCancelPrim(HostPrim prim);

bool IsHostPrimDiag(HostPrim prim);
bool IsHostPrimRuntime(HostPrim prim);
bool MapsToDiagOrRuntime(HostPrim prim);

bool HostPrimFail(HostPrim prim, bool failed);
bool HostPrimFailureIllFormed(HostPrim prim, bool failed);

}  // namespace ultraviolet::core
