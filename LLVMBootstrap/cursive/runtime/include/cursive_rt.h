#ifndef CURSIVE_RT_H
#define CURSIVE_RT_H

#include <stdint.h>
#include <stddef.h>

#include "cursive_rt_language_symbols.h"

#if defined(_MSC_VER)
#define C0_ALIGNED_STRUCT(name, alignment) __declspec(align(alignment)) struct name
#else
#define C0_ALIGNED_STRUCT(name, alignment) \
  struct __attribute__((aligned(alignment))) name
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Basic view/managed string and bytes types
typedef struct C0StringView {
  const uint8_t* data;
  uint64_t len;
} C0StringView;

void* cursive_raw_dylib_resolve(const char* dll_name,
                                const char* symbol_name);

typedef struct C0StringManaged {
  uint8_t* data;
  uint64_t len;
  uint64_t cap;
} C0StringManaged;

typedef struct C0BytesView {
  const uint8_t* data;
  uint64_t len;
} C0BytesView;

typedef struct C0BytesManaged {
  uint8_t* data;
  uint64_t len;
  uint64_t cap;
} C0BytesManaged;

// Dynamic object layout (data pointer + vtable pointer)
typedef struct C0DynObject {
  void* data;
  void* vtable;
} C0DynObject;

// Context record. The System field is zero-sized in the runtime layout.
typedef struct C0Context {
  C0DynObject fs;
  C0DynObject net;
  C0DynObject heap;
  C0DynObject reactor;
  C0DynObject time;
} C0Context;

typedef enum {
  C0_DOMAIN_CPU = 0,
  C0_DOMAIN_GPU = 1,
  C0_DOMAIN_INLINE = 2,
} C0DomainKind;

typedef struct C0ExecutionDomain {
  uint8_t kind;
  uint8_t _pad[3];
  int32_t priority_hint;
  uint64_t max_concurrency;
  uint64_t affinity_mask;
} C0ExecutionDomain;

typedef struct C0Usize3 {
  uint64_t x;
  uint64_t y;
  uint64_t z;
} C0Usize3;


// Modal string layout (string with unspecified state)
typedef struct C0StringModal {
  uint8_t disc;
  uint8_t _pad[7];
  C0StringManaged payload;
} C0StringModal;

// RegionOptions record: { stack_size: usize, name: string }
typedef struct C0RegionOptions {
  uint64_t stack_size;
  C0StringModal name;
} C0RegionOptions;

// Region modal layout (disc + handle payload)
typedef struct C0Region {
  uint8_t disc;
  uint8_t _pad[7];
  uint64_t handle;
} C0Region;

// Async<(), (), (), !> layout used by CancelToken::wait_cancelled
typedef struct C0AsyncUnitUnitUnitNever {
  uint8_t disc;
  uint8_t _pad0[7];
  uint8_t payload[8];
} C0AsyncUnitUnitUnitNever;

// Async<Out, In, Result, E> resume value used by current codegen paths
// (disc + payload with frame-carrying suspended state).
typedef struct C0AsyncResumeValue {
  uint8_t disc;
  uint8_t _pad0[7];
  uint8_t payload[16];
} C0AsyncResumeValue;

// Range layout (tag, lo, hi)
typedef struct C0Range {
  uint8_t tag;
  uint8_t _pad[7];
  uint64_t lo;
  uint64_t hi;
} C0Range;

// Slice layout for [u8]
typedef struct C0SliceU8 {
  const uint8_t* data;
  uint64_t len;
} C0SliceU8;

// File/DirIter state payloads (handle: usize)
typedef struct C0FileHandle {
  uint64_t handle;
} C0FileHandle;

typedef struct C0DirIterHandle {
  uint64_t handle;
} C0DirIterHandle;

// IoError enum (u8)
typedef uint8_t C0IoError;

enum {
  C0_IO_NOTFOUND = 0,
  C0_IO_PERMISSION_DENIED = 1,
  C0_IO_ALREADY_EXISTS = 2,
  C0_IO_INVALID_PATH = 3,
  C0_IO_BUSY = 4,
  C0_IO_FAILURE = 5,
};

// FileKind enum (u8)
typedef uint8_t C0FileKind;

enum {
  C0_FILE_KIND_FILE = 0,
  C0_FILE_KIND_DIR = 1,
  C0_FILE_KIND_OTHER = 2,
};

// AllocationError enum with payload (u8 disc + usize payload)
typedef struct C0AllocationError {
  uint8_t disc;
  uint8_t _pad[7];
  uint64_t size;
} C0AllocationError;

enum {
  C0_ALLOC_OUT_OF_MEMORY = 0,
  C0_ALLOC_QUOTA_EXCEEDED = 1,
};

typedef C0_ALIGNED_STRUCT(C0U128, 16) {
  uint64_t lo;
  uint64_t hi;
} C0U128;

typedef C0U128 C0I128;

typedef struct C0Duration {
  C0U128 nanoseconds;
} C0Duration;

typedef C0_ALIGNED_STRUCT(C0MonotonicInstant, 16) {
  uint64_t domain;
  uint8_t _pad[8];
  C0U128 ticks;
} C0MonotonicInstant;

typedef struct C0UtcInstant {
  C0I128 unix_nanoseconds;
} C0UtcInstant;

typedef uint8_t C0TimeError;

enum {
  C0_TIME_UNSUPPORTED = 0,
  C0_TIME_CLOCK_UNAVAILABLE = 1,
  C0_TIME_OUT_OF_RANGE = 2,
  C0_TIME_INVALID_RESOLUTION = 3,
  C0_TIME_CLOCK_MISMATCH = 4,
};

// DirEntry record: { name: string@Managed, path: string@Managed, kind: FileKind }
typedef struct C0DirEntry {
  C0StringManaged name;
  C0StringManaged path;
  uint8_t kind;
  uint8_t _pad[7];
} C0DirEntry;

// Union layouts (tagged) used by runtime APIs

typedef struct C0Union_StringManaged_AllocError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    C0AllocationError alloc_error;
    C0StringManaged value;
  } payload;
} C0Union_StringManaged_AllocError;

typedef struct C0Union_BytesManaged_AllocError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    C0AllocationError alloc_error;
    C0BytesManaged value;
  } payload;
} C0Union_BytesManaged_AllocError;

typedef struct C0Union_Unit_AllocError {
  uint8_t disc;
  uint8_t _pad[7];
  C0AllocationError error;
} C0Union_Unit_AllocError;

typedef struct C0Union_StringManaged_IoError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    uint8_t io_error;
    C0StringManaged value;
  } payload;
} C0Union_StringManaged_IoError;

typedef struct C0Union_BytesManaged_IoError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    uint8_t io_error;
    C0BytesManaged value;
  } payload;
} C0Union_BytesManaged_IoError;

typedef struct C0Union_File_IoError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    uint8_t io_error;
    uint64_t handle;
  } payload;
} C0Union_File_IoError;

typedef struct C0Union_DirIter_IoError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    uint8_t io_error;
    uint64_t handle;
  } payload;
} C0Union_DirIter_IoError;

typedef struct C0Union_Unit_IoError {
  uint8_t disc;
  uint8_t payload;
} C0Union_Unit_IoError;

typedef struct C0Union_FileKind_IoError {
  uint8_t disc;
  uint8_t payload;
} C0Union_FileKind_IoError;

typedef C0_ALIGNED_STRUCT(C0Union_Duration_TimeError, 16) {
  uint8_t disc;
  uint8_t _pad[15];
  union {
    C0TimeError time_error;
    C0Duration value;
  } payload;
} C0Union_Duration_TimeError;

typedef struct C0Union_DynObject_TimeError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    C0TimeError time_error;
    C0DynObject value;
  } payload;
} C0Union_DynObject_TimeError;

typedef C0_ALIGNED_STRUCT(C0Union_UtcInstant_TimeError, 16) {
  uint8_t disc;
  uint8_t _pad[15];
  union {
    C0TimeError time_error;
    C0UtcInstant value;
  } payload;
} C0Union_UtcInstant_TimeError;

typedef struct C0Union_DirEntry_Unit {
  uint8_t disc;
  uint8_t _pad[7];
  C0DirEntry entry;
} C0Union_DirEntry_Unit;

typedef struct C0Union_DirEntry_Unit_IoError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    C0Union_DirEntry_Unit value;
    uint8_t io_error;
  } payload;
} C0Union_DirEntry_Unit_IoError;

// -----------------------------------------------------------------------------
// Runtime functions (mangled symbol names)
// -----------------------------------------------------------------------------

// Panic
void cursive_x3a_x3aruntime_x3a_x3apanic(uint32_t code);

// Runtime conformance trace
void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit(
  const C0StringView* rule_id,
  const C0StringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const C0StringView* payload);
void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fint(
  const C0StringView* rule_id,
  const C0StringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const C0StringView* payload_prefix,
  uint64_t raw,
  uint8_t bits,
  uint8_t is_signed);
void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fbool(
  const C0StringView* rule_id,
  const C0StringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const C0StringView* payload_prefix,
  uint8_t actual);
void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5ffloat(
  const C0StringView* rule_id,
  const C0StringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const C0StringView* payload_prefix,
  double actual,
  uint8_t bits);
void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fptr(
  const C0StringView* rule_id,
  const C0StringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const C0StringView* payload_prefix,
  const void* actual);
void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fstring(
  const C0StringView* rule_id,
  const C0StringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const C0StringView* payload_prefix,
  const C0StringView* actual);
void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fstring_x5fmanaged(
  const C0StringView* rule_id,
  const C0StringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const C0StringView* payload_prefix,
  const C0StringManaged* actual);
void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fbytes(
  const C0StringView* rule_id,
  const C0StringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const C0StringView* payload_prefix,
  const C0BytesView* actual);
void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fbytes_x5fmanaged(
  const C0StringView* rule_id,
  const C0StringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const C0StringView* payload_prefix,
  const C0BytesManaged* actual);
void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aset_x5fsink(
  uint8_t sink_kind,
  const uint8_t* path_utf8,
  uint64_t path_len);
// Sink semantics:
//   0 -> console
//   1 -> file
//   2 -> console + file
void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aset_x5froot(
  const uint8_t* path_utf8,
  uint64_t path_len);
// Filter semantics:
//   bit0=log, bit1=diagnostic, bit2=runtime
//   0 is treated as "all"
void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aset_x5flog_x5ffilter(
  uint8_t mask_bits);
// Level semantics:
//   0 -> trace, 1 -> info, 2 -> warning, 3 -> error
void cursive_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aset_x5fmin_x5flevel(
  uint8_t level);

// Context initialization
void cursive_x3a_x3aruntime_x3a_x3acontext_x5finit(C0Context* out);

// String/bytes drop
void cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3adrop_x5fmanaged(C0StringManaged* value);
void cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3adrop_x5fmanaged(C0BytesManaged* value);

// Region procs
C0Region cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3anew_x5fscoped(const C0RegionOptions* options);
void* cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3aalloc(const C0Region* self, uint64_t size, uint64_t align);
uint64_t cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3amark(const C0Region* self);
void cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3areset_x5fto(const C0Region* self, uint64_t mark);
C0Region cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3areset_x5funchecked(const C0Region* self);
C0Region cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3afreeze(const C0Region* self);
C0Region cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3athaw(const C0Region* self);
C0Region cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3afree_x5funchecked(const C0Region* self);
uint8_t cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3aaddr_x5fis_x5factive(const void* addr);
void cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3aaddr_x5ftag_x5ffrom(const void* addr, const void* base);
void cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fenter(uint64_t scope_id);
void cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fexit(uint64_t scope_id);
void cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3aaddr_x5ftag_x5fscope(const void* addr, uint64_t scope_id);

// Async frame allocation
void* cursive_x3a_x3aruntime_x3a_x3aasync_x3a_x3aalloc_x5fframe(uint64_t size, uint64_t align);
void cursive_x3a_x3aruntime_x3a_x3aasync_x3a_x3afree_x5fframe(void* frame);
C0AsyncResumeValue cursive_x3a_x3aruntime_x3a_x3aasync_x3a_x3aresume(
    const C0AsyncResumeValue* suspended,
    const void* input,
    void* panic_out);
C0AsyncResumeValue cursive_x3a_x3aruntime_x3a_x3aasync_x3a_x3atake(
    const C0AsyncResumeValue* source,
    uint64_t count,
    void* panic_out);

// String builtins
void cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3afrom(
  C0Union_StringManaged_AllocError* out,
  const C0StringView* source,
  const C0DynObject* heap);

C0StringView cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3aas_x5fview(
  const C0StringManaged* self);

C0StringView cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3aslice(
  const C0StringView* self,
  const uint64_t* start,
  const uint64_t* end);

void cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3ato_x5fmanaged(
  C0Union_StringManaged_AllocError* out,
  const C0StringView* self,
  const C0DynObject* heap);

void cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3aclone_x5fwith(
  C0Union_StringManaged_AllocError* out,
  const C0StringManaged* self,
  const C0DynObject* heap);

void cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3aappend(
  C0Union_Unit_AllocError* out,
  C0StringManaged* self,
  const C0StringView* data,
  const C0DynObject* heap);

uint64_t cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3alength(
  const C0StringView* self);

uint8_t cursive_x3a_x3aruntime_x3a_x3astring_x3a_x3ais_x5fempty(
  const C0StringView* self);

// Bytes builtins
void cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3awith_x5fcapacity(
  C0Union_BytesManaged_AllocError* out,
  const uint64_t* cap,
  const C0DynObject* heap);

void cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3afrom_x5fslice(
  C0Union_BytesManaged_AllocError* out,
  const C0SliceU8* data,
  const C0DynObject* heap);

C0BytesView cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3aas_x5fview(
  const C0BytesManaged* self);

void cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3ato_x5fmanaged(
  C0Union_BytesManaged_AllocError* out,
  const C0BytesView* self,
  const C0DynObject* heap);

C0BytesView cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3aview(
  const C0SliceU8* data);

C0SliceU8 cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3aas_x5fslice(
  const C0BytesView* self);

C0BytesView cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3aview_x5fstring(
  const C0StringView* data);

void cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3aappend(
  C0Union_Unit_AllocError* out,
  C0BytesManaged* self,
  const C0BytesView* data,
  const C0DynObject* heap);

uint64_t cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3alength(
  const C0BytesView* self);

uint8_t cursive_x3a_x3aruntime_x3a_x3abytes_x3a_x3ais_x5fempty(
  const C0BytesView* self);

// FileSystem builtins
C0Union_File_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fread(
  const C0DynObject* self,
  const C0StringView* path);

C0Union_File_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fwrite(
  const C0DynObject* self,
  const C0StringView* path);

C0Union_File_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fappend(
  const C0DynObject* self,
  const C0StringView* path);

C0Union_File_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3acreate_x5fwrite(
  const C0DynObject* self,
  const C0StringView* path);

void cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aread_x5ffile(
  C0Union_StringManaged_IoError* out,
  const C0DynObject* self,
  const C0StringView* path);

void cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aread_x5fbytes(
  C0Union_BytesManaged_IoError* out,
  const C0DynObject* self,
  const C0StringView* path);

C0Union_Unit_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3awrite_x5ffile(
  const C0DynObject* self,
  const C0StringView* path,
  const C0BytesView* data);

C0Union_Unit_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3awrite_x5fstdout(
  const C0DynObject* self,
  const C0StringView* data);

C0Union_Unit_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3awrite_x5fstderr(
  const C0DynObject* self,
  const C0StringView* data);

uint8_t cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aexists(
  const C0DynObject* self,
  const C0StringView* path);

C0Union_Unit_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aremove(
  const C0DynObject* self,
  const C0StringView* path);

C0Union_DirIter_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fdir(
  const C0DynObject* self,
  const C0StringView* path);

C0Union_Unit_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3acreate_x5fdir(
  const C0DynObject* self,
  const C0StringView* path);

C0Union_Unit_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aensure_x5fdir(
  const C0DynObject* self,
  const C0StringView* path);

C0Union_FileKind_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3akind(
  const C0DynObject* self,
  const C0StringView* path);

C0DynObject cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3arestrict(
  const C0DynObject* self,
  const C0StringView* path);

// Network builtins
C0DynObject cursive_x3a_x3aruntime_x3a_x3anet_x3a_x3arestrict_x5fto_x5fhost(
  const C0DynObject* self,
  const C0StringView* host);

// HeapAllocator builtins
C0DynObject cursive_x3a_x3aruntime_x3a_x3aheap_x3a_x3awith_x5fquota(
  const C0DynObject* self,
  const uint64_t* size);

void* cursive_x3a_x3aruntime_x3a_x3aheap_x3a_x3aalloc_x5fraw(
  const C0DynObject* self,
  const uint64_t* count);

void cursive_x3a_x3aruntime_x3a_x3aheap_x3a_x3adealloc_x5fraw(
  const C0DynObject* self,
  void** ptr,
  const uint64_t* count);

// Time builtins
C0DynObject cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic(
  const C0DynObject* self);

C0DynObject cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3awall(
  const C0DynObject* self);

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5fnow(
  C0MonotonicInstant* out,
  const C0DynObject* self);

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5fresolution(
  C0Duration* out,
  const C0DynObject* self);

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5felapsed(
  C0Union_Duration_TimeError* out,
  const C0DynObject* self,
  const C0MonotonicInstant* start,
  const C0MonotonicInstant* end);

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5fcoarsen(
  C0Union_DynObject_TimeError* out,
  const C0DynObject* self,
  const C0Duration* resolution);

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3awall_x5fnow_x5futc(
  C0Union_UtcInstant_TimeError* out,
  const C0DynObject* self);

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3awall_x5fresolution(
  C0Union_Duration_TimeError* out,
  const C0DynObject* self);

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3awall_x5fcoarsen(
  C0Union_DynObject_TimeError* out,
  const C0DynObject* self,
  const C0Duration* resolution);

// System builtins
void cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3aexit(
  int32_t code);

C0StringView cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3aget_x5fenv(
  const C0StringView* key);

C0StringView cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3aexecutable_x5fpath(void);

uint64_t cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3aargument_x5fcount(void);

C0StringView cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3aargument(
  const uint64_t* index);

C0StringView cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3acurrent_x5fdirectory(void);

int32_t cursive_x3a_x3aruntime_x3a_x3asystem_x3a_x3arun(
  const C0StringView* command);

// -----------------------------------------------------------------------------
// C0X Extension: Structured Concurrency Runtime Support (Â§18)
// -----------------------------------------------------------------------------

// Â§18.2 Context execution domain constructors
C0DynObject cursive_x3a_x3aruntime_x3a_x3acontext_x3a_x3acpu(
  const C0Context* self);
C0DynObject cursive_x3a_x3aruntime_x3a_x3acontext_x3a_x3acpu_x5fconfigured(
  const C0Context* self,
  uint64_t affinity_mask,
  int32_t priority_hint);
C0DynObject cursive_x3a_x3aruntime_x3a_x3acontext_x3a_x3agpu(
  const C0Context* self);
C0DynObject cursive_x3a_x3aruntime_x3a_x3acontext_x3a_x3ainline(
  const C0Context* self);

// Section 19.4 Reactor methods
void* cursive_x3a_x3aruntime_x3a_x3areactor_x3a_x3aregister(
  const C0DynObject* self,
  const void* future);

// Â§18.2 ExecutionDomain methods
C0StringView cursive_x3a_x3aruntime_x3a_x3aexecution_x5fdomain_x3a_x3aname(
  const C0DynObject* self);
uint64_t cursive_x3a_x3aruntime_x3a_x3aexecution_x5fdomain_x3a_x3amax_x5fconcurrency(
  const C0DynObject* self);

// Â§18.2.2.4 GPU intrinsic functions
C0Usize3 cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3aglobal_x5fid(void);
C0Usize3 cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3alocal_x5fid(void);
C0Usize3 cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3aworkgroup_x5fid(void);
C0Usize3 cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3aworkgroup_x5fsize(void);
C0Usize3 cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3aglobal_x5fsize(void);
C0Usize3 cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3anum_x5fworkgroups(void);
uint64_t cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3alinear_x5fid(void);
void cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3abarrier(void);
void cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3amemory_x5fbarrier(void);
void cursive_x3a_x3aruntime_x3a_x3agpu_x3a_x3aworkgroup_x5fbarrier(void);

// Â§18.1.1 Begin parallel block - creates execution context
void* cursive_parallel_begin(C0DynObject domain, size_t cancel_token, const char* name);

// Â§18.1.2 Join parallel block - waits for completion, propagates first panic
int cursive_parallel_join(void* ctx_ptr);

// Â§18.4.2 Create spawn handle - returns Spawned<T>@Pending
void* cursive_spawn_create(void* env, size_t env_size,
                            void (*body)(void* hosted_env, void* env, void* result, void* panic_out),
                            void* hosted_env,
                            size_t result_size,
                            uint64_t affinity_mask,
                            int32_t priority_hint);

// Â§10.3 Wait for spawn result - blocks until ready, extracts value
void* cursive_spawn_wait(void* handle_ptr);

// Â§18.5.2 Dispatch iteration - parallel data iteration
void cursive_dispatch_run(C0Range range, size_t elem_size, size_t result_size,
                           void (*body)(void* hosted_env, void* elem, void* captured, void* result, void* panic_out),
                           void* hosted_env,
                           void* captured_env,
                           C0StringView reduce_op,
                           void* reduce_result,
                           void (*reduce_fn)(void* hosted_env, void* lhs, void* rhs, void* out, void* panic_out),
                           int ordered,
                           size_t chunk_size);

// Dynamic key runtime primitives.
void* cursive_key_scope_enter(void);
void cursive_key_scope_exit(void* scope_ptr);
void cursive_key_acquire(void* scope_ptr, C0StringView path, uint8_t mode);
void* cursive_key_release_all(void);
void cursive_key_reacquire(void* released_ptr);
void cursive_key_release_snapshot_discard(void* released_ptr);

// Â§18.6.1 Cancellation token operations
size_t cursive_cancel_token_new(void);
void cursive_cancel_token_cancel(size_t token_id);
int cursive_cancel_token_is_cancelled(size_t token_id);

// Â§18.7 Panic handling in parallel contexts
void cursive_parallel_work_panic(void* ctx_ptr, uint32_t code);

// Low-level panic helper (used by parallel runtime)
void cursive_panic(uint32_t code);

// CancelToken modal methods
size_t CancelToken_x3a_x3anew(void);
void CancelToken_x3a_x3aActive_x3a_x3acancel(void* self);
uint8_t CancelToken_x3a_x3aActive_x3a_x3ais_x5fcancelled(void* self);
size_t CancelToken_x3a_x3aActive_x3a_x3achild(void* self);
void CancelToken_x3a_x3aActive_x3a_x3await_x5fcancelled(void* out, void* self);

// File/DirIter methods (modal state methods / transitions)
C0Union_StringManaged_IoError File_x3a_x3aRead_x3a_x3aread_x5fall(
  const C0FileHandle* self);

C0Union_BytesManaged_IoError File_x3a_x3aRead_x3a_x3aread_x5fall_x5fbytes(
  const C0FileHandle* self);

void File_x3a_x3aRead_x3a_x3aclose(
  C0FileHandle self);

C0Union_Unit_IoError File_x3a_x3aWrite_x3a_x3awrite(
  C0FileHandle* self,
  const C0BytesView* data);

C0Union_Unit_IoError File_x3a_x3aWrite_x3a_x3aflush(
  C0FileHandle* self);

void File_x3a_x3aWrite_x3a_x3aclose(
  C0FileHandle self);

C0Union_Unit_IoError File_x3a_x3aAppend_x3a_x3awrite(
  C0FileHandle* self,
  const C0BytesView* data);

C0Union_Unit_IoError File_x3a_x3aAppend_x3a_x3aflush(
  C0FileHandle* self);

void File_x3a_x3aAppend_x3a_x3aclose(
  C0FileHandle self);

C0Union_DirEntry_Unit_IoError DirIter_x3a_x3aOpen_x3a_x3anext(
  C0DirIterHandle* self);

void DirIter_x3a_x3aOpen_x3a_x3aclose(
  C0DirIterHandle self);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CURSIVE0_RT_H
