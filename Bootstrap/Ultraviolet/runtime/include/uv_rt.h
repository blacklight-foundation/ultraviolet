#ifndef UV_RT_H
#define UV_RT_H

#include <stdint.h>
#include <stddef.h>

#include "uv_rt_language_symbols.h"

#if defined(_MSC_VER)
#define UV_ALIGNED_STRUCT(name, alignment) __declspec(align(alignment)) struct name
#else
#define UV_ALIGNED_STRUCT(name, alignment) \
  struct __attribute__((aligned(alignment))) name
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Basic view/managed string and bytes types
typedef struct UVStringView {
  const uint8_t* data;
  uint64_t len;
} UVStringView;

void* uv_raw_dylib_resolve(const char* dll_name,
                                const char* symbol_name);

typedef struct UVStringManaged {
  uint8_t* data;
  uint64_t len;
  uint64_t cap;
} UVStringManaged;

typedef struct UVBytesView {
  const uint8_t* data;
  uint64_t len;
} UVBytesView;

typedef struct UVBytesManaged {
  uint8_t* data;
  uint64_t len;
  uint64_t cap;
} UVBytesManaged;

// Dynamic object layout (data pointer + vtable pointer)
typedef struct UVDynObject {
  void* data;
  void* vtable;
} UVDynObject;

// Context record. The System field is zero-sized in the runtime layout.
typedef struct UVContext {
  UVDynObject io;
  UVDynObject net;
  UVDynObject heap;
  UVDynObject reactor;
  UVDynObject time;
} UVContext;

typedef enum {
  UV_DOMAIN_CPU = 0,
  UV_DOMAIN_GPU = 1,
  UV_DOMAIN_INLINE = 2,
} UVDomainKind;

typedef struct UVExecutionDomain {
  uint8_t kind;
  uint8_t _pad[3];
  int32_t priority_hint;
  uint64_t max_concurrency;
  uint64_t affinity_mask;
} UVExecutionDomain;

typedef struct UVUsize3 {
  uint64_t x;
  uint64_t y;
  uint64_t z;
} UVUsize3;


// Modal string layout (string with unspecified state)
typedef struct UVStringModal {
  uint8_t disc;
  uint8_t _pad[7];
  UVStringManaged payload;
} UVStringModal;

// RegionOptions record: { stack_size: usize, name: string }
typedef struct UVRegionOptions {
  uint64_t stack_size;
  UVStringModal name;
} UVRegionOptions;

// Region modal layout (disc + handle payload)
typedef struct UVRegion {
  uint8_t disc;
  uint8_t _pad[7];
  uint64_t handle;
} UVRegion;

// Async<(), (), (), !> layout used by CancelToken::wait_cancelled
typedef struct UVAsyncUnitUnitUnitNever {
  uint8_t disc;
  uint8_t _pad0[7];
  uint8_t payload[8];
} UVAsyncUnitUnitUnitNever;

// Async<Out, In, Result, E> resume value used by current codegen paths
// (disc + payload with frame-carrying suspended state).
typedef struct UVAsyncResumeValue {
  uint8_t disc;
  uint8_t _pad0[7];
  uint8_t payload[16];
} UVAsyncResumeValue;

// Range layout (tag, lo, hi)
typedef struct UVRange {
  uint8_t tag;
  uint8_t _pad[7];
  uint64_t lo;
  uint64_t hi;
} UVRange;

// Slice layout for [u8]
typedef struct UVSliceU8 {
  const uint8_t* data;
  uint64_t len;
} UVSliceU8;

// File/DirIter state payloads (handle: usize)
typedef struct UVFileHandle {
  uint64_t handle;
} UVFileHandle;

typedef struct UVDirIterHandle {
  uint64_t handle;
} UVDirIterHandle;

// IoError enum (u8)
typedef uint8_t UVIoError;

enum {
  UV_IO_NOTFOUND = 0,
  UV_IO_PERMISSION_DENIED = 1,
  UV_IO_ALREADY_EXISTS = 2,
  UV_IO_INVALID_PATH = 3,
  UV_IO_BUSY = 4,
  UV_IO_FAILURE = 5,
  UV_IO_DIRECTORY_NOT_EMPTY = 6,
};

// FileKind enum (u8)
typedef uint8_t UVFileKind;

enum {
  UV_FILE_KIND_FILE = 0,
  UV_FILE_KIND_DIR = 1,
  UV_FILE_KIND_OTHER = 2,
};

// AllocationError enum with payload (u8 disc + usize payload)
typedef struct UVAllocationError {
  uint8_t disc;
  uint8_t _pad[7];
  uint64_t size;
} UVAllocationError;

enum {
  UV_ALLOC_OUT_OF_MEMORY = 0,
  UV_ALLOC_QUOTA_EXCEEDED = 1,
};

typedef UV_ALIGNED_STRUCT(UVU128, 16) {
  uint64_t lo;
  uint64_t hi;
} UVU128;

typedef UVU128 UVI128;

typedef struct UVDuration {
  UVU128 nanoseconds;
} UVDuration;

typedef UV_ALIGNED_STRUCT(UVMonotonicInstant, 16) {
  uint64_t domain;
  uint8_t _pad[8];
  UVU128 ticks;
} UVMonotonicInstant;

typedef struct UVUtcInstant {
  UVI128 unix_nanoseconds;
} UVUtcInstant;

typedef uint8_t UVTimeError;

enum {
  UV_TIME_UNSUPPORTED = 0,
  UV_TIME_CLOCK_UNAVAILABLE = 1,
  UV_TIME_OUT_OF_RANGE = 2,
  UV_TIME_INVALID_RESOLUTION = 3,
  UV_TIME_CLOCK_MISMATCH = 4,
};

// DirEntry record: { name: string@Managed, path: string@Managed, kind: FileKind }
typedef struct UVDirEntry {
  UVStringManaged name;
  UVStringManaged path;
  uint8_t kind;
  uint8_t _pad[7];
} UVDirEntry;

// Union layouts (tagged) used by runtime APIs

typedef struct UVUnion_StringManaged_AllocError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    UVAllocationError alloc_error;
    UVStringManaged value;
  } payload;
} UVUnion_StringManaged_AllocError;

typedef struct UVUnion_BytesManaged_AllocError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    UVAllocationError alloc_error;
    UVBytesManaged value;
  } payload;
} UVUnion_BytesManaged_AllocError;

typedef struct UVUnion_Unit_AllocError {
  uint8_t disc;
  uint8_t _pad[7];
  UVAllocationError error;
} UVUnion_Unit_AllocError;

typedef struct UVUnion_StringManaged_IoError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    uint8_t io_error;
    UVStringManaged value;
  } payload;
} UVUnion_StringManaged_IoError;

typedef struct UVUnion_BytesManaged_IoError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    uint8_t io_error;
    UVBytesManaged value;
  } payload;
} UVUnion_BytesManaged_IoError;

typedef struct UVUnion_File_IoError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    uint8_t io_error;
    uint64_t handle;
  } payload;
} UVUnion_File_IoError;

typedef struct UVUnion_DirIter_IoError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    uint8_t io_error;
    uint64_t handle;
  } payload;
} UVUnion_DirIter_IoError;

typedef struct UVUnion_Unit_IoError {
  uint8_t disc;
  uint8_t payload;
} UVUnion_Unit_IoError;

typedef struct UVUnion_FileKind_IoError {
  uint8_t disc;
  uint8_t payload;
} UVUnion_FileKind_IoError;

typedef UV_ALIGNED_STRUCT(UVUnion_Duration_TimeError, 16) {
  uint8_t disc;
  uint8_t _pad[15];
  union {
    UVTimeError time_error;
    UVDuration value;
  } payload;
} UVUnion_Duration_TimeError;

typedef struct UVUnion_DynObject_TimeError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    UVTimeError time_error;
    UVDynObject value;
  } payload;
} UVUnion_DynObject_TimeError;

typedef UV_ALIGNED_STRUCT(UVUnion_UtcInstant_TimeError, 16) {
  uint8_t disc;
  uint8_t _pad[15];
  union {
    UVTimeError time_error;
    UVUtcInstant value;
  } payload;
} UVUnion_UtcInstant_TimeError;

typedef struct UVUnion_DirEntry_Unit {
  uint8_t disc;
  uint8_t _pad[7];
  UVDirEntry entry;
} UVUnion_DirEntry_Unit;

typedef struct UVUnion_DirEntry_Unit_IoError {
  uint8_t disc;
  uint8_t _pad[7];
  union {
    UVUnion_DirEntry_Unit value;
    uint8_t io_error;
  } payload;
} UVUnion_DirEntry_Unit_IoError;

// -----------------------------------------------------------------------------
// Runtime functions (mangled symbol names)
// -----------------------------------------------------------------------------

// Panic
void ultraviolet_x3a_x3aruntime_x3a_x3apanic(uint32_t code);

// Runtime conformance trace
void ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit(
  const UVStringView* rule_id,
  const UVStringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const UVStringView* payload);
void ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fint(
  const UVStringView* rule_id,
  const UVStringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const UVStringView* payload_prefix,
  uint64_t raw,
  uint8_t bits,
  uint8_t is_signed);
void ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fbool(
  const UVStringView* rule_id,
  const UVStringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const UVStringView* payload_prefix,
  uint8_t actual);
void ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5ffloat(
  const UVStringView* rule_id,
  const UVStringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const UVStringView* payload_prefix,
  double actual,
  uint8_t bits);
void ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fptr(
  const UVStringView* rule_id,
  const UVStringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const UVStringView* payload_prefix,
  const void* actual);
void ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fstring(
  const UVStringView* rule_id,
  const UVStringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const UVStringView* payload_prefix,
  const UVStringView* actual);
void ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fstring_x5fmanaged(
  const UVStringView* rule_id,
  const UVStringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const UVStringView* payload_prefix,
  const UVStringManaged* actual);
void ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fbytes(
  const UVStringView* rule_id,
  const UVStringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const UVStringView* payload_prefix,
  const UVBytesView* actual);
void ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit_x5fbytes_x5fmanaged(
  const UVStringView* rule_id,
  const UVStringView* file,
  uint64_t start_line,
  uint64_t start_col,
  uint64_t end_line,
  uint64_t end_col,
  const UVStringView* payload_prefix,
  const UVBytesManaged* actual);
void ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aset_x5fsink(
  uint8_t sink_kind,
  const uint8_t* path_utf8,
  uint64_t path_len);
// Sink semantics:
//   0 -> console
//   1 -> file
//   2 -> console + file
void ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aset_x5froot(
  const uint8_t* path_utf8,
  uint64_t path_len);
// Filter semantics:
//   bit0=log, bit1=diagnostic, bit2=runtime
//   0 is treated as "all"
void ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aset_x5flog_x5ffilter(
  uint8_t mask_bits);
// Level semantics:
//   0 -> trace, 1 -> info, 2 -> warning, 3 -> error
void ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aset_x5fmin_x5flevel(
  uint8_t level);

// Context initialization
void ultraviolet_x3a_x3aruntime_x3a_x3acontext_x5finit(UVContext* out);

// String/bytes drop
void ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3adrop_x5fmanaged(UVStringManaged* value);
void ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3adrop_x5fmanaged(UVBytesManaged* value);

// Region procs
UVRegion ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3anew_x5fscoped(const UVRegionOptions* options);
void* ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3aalloc(const UVRegion* self, uint64_t size, uint64_t align);
uint64_t ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3amark(const UVRegion* self);
void ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3areset_x5fto(const UVRegion* self, uint64_t mark);
UVRegion ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3areset_x5funchecked(const UVRegion* self);
UVRegion ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3afreeze(const UVRegion* self);
UVRegion ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3athaw(const UVRegion* self);
UVRegion ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3afree_x5funchecked(const UVRegion* self);
uint8_t ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3aaddr_x5fis_x5factive(const void* addr);
void ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3aaddr_x5ftag_x5ffrom(const void* addr, const void* base);
void ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fenter(uint64_t scope_id);
void ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fexit(uint64_t scope_id);
void ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3aaddr_x5ftag_x5fscope(const void* addr, uint64_t scope_id);

// Async frame allocation
void* ultraviolet_x3a_x3aruntime_x3a_x3aasync_x3a_x3aalloc_x5fframe(uint64_t size, uint64_t align);
void ultraviolet_x3a_x3aruntime_x3a_x3aasync_x3a_x3afree_x5fframe(void* frame);
UVAsyncResumeValue ultraviolet_x3a_x3aruntime_x3a_x3aasync_x3a_x3aresume(
    const UVAsyncResumeValue* suspended,
    const void* input,
    void* panic_out);
UVAsyncResumeValue ultraviolet_x3a_x3aruntime_x3a_x3aasync_x3a_x3atake(
    const UVAsyncResumeValue* source,
    uint64_t count,
    void* panic_out);

// String builtins
void ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3afrom(
  UVUnion_StringManaged_AllocError* out,
  const UVStringView* source,
  const UVDynObject* heap);

UVStringView ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3aas_x5fview(
  const UVStringManaged* self);

UVStringView ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3aslice(
  const UVStringView* self,
  const uint64_t* start,
  const uint64_t* end);

void ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3ato_x5fmanaged(
  UVUnion_StringManaged_AllocError* out,
  const UVStringView* self,
  const UVDynObject* heap);

void ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3aclone_x5fwith(
  UVUnion_StringManaged_AllocError* out,
  const UVStringManaged* self,
  const UVDynObject* heap);

void ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3aappend(
  UVUnion_Unit_AllocError* out,
  UVStringManaged* self,
  const UVStringView* data,
  const UVDynObject* heap);

uint64_t ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3alength(
  const UVStringView* self);

uint8_t ultraviolet_x3a_x3aruntime_x3a_x3astring_x3a_x3ais_x5fempty(
  const UVStringView* self);

// Bytes builtins
void ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3awith_x5fcapacity(
  UVUnion_BytesManaged_AllocError* out,
  const uint64_t* cap,
  const UVDynObject* heap);

void ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3afrom_x5fslice(
  UVUnion_BytesManaged_AllocError* out,
  const UVSliceU8* data,
  const UVDynObject* heap);

UVBytesView ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3aas_x5fview(
  const UVBytesManaged* self);

void ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3ato_x5fmanaged(
  UVUnion_BytesManaged_AllocError* out,
  const UVBytesView* self,
  const UVDynObject* heap);

UVBytesView ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3aview(
  const UVSliceU8* data);

UVSliceU8 ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3aas_x5fslice(
  const UVBytesView* self);

UVBytesView ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3aview_x5fstring(
  const UVStringView* data);

void ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3aappend(
  UVUnion_Unit_AllocError* out,
  UVBytesManaged* self,
  const UVBytesView* data,
  const UVDynObject* heap);

uint64_t ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3alength(
  const UVBytesView* self);

uint8_t ultraviolet_x3a_x3aruntime_x3a_x3abytes_x3a_x3ais_x5fempty(
  const UVBytesView* self);

// IO builtins
UVUnion_File_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fread(
  const UVDynObject* self,
  const UVStringView* path);

UVUnion_File_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fwrite(
  const UVDynObject* self,
  const UVStringView* path);

UVUnion_File_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fappend(
  const UVDynObject* self,
  const UVStringView* path);

UVUnion_File_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3acreate_x5fwrite(
  const UVDynObject* self,
  const UVStringView* path);

void ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aread_x5ffile(
  UVUnion_StringManaged_IoError* out,
  const UVDynObject* self,
  const UVStringView* path);

void ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aread_x5fbytes(
  UVUnion_BytesManaged_IoError* out,
  const UVDynObject* self,
  const UVStringView* path);

UVUnion_Unit_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3awrite_x5ffile(
  const UVDynObject* self,
  const UVStringView* path,
  const UVBytesView* data);

UVUnion_Unit_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3awrite_x5fstdout(
  const UVDynObject* self,
  const UVStringView* data);

UVUnion_Unit_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3awrite_x5fstderr(
  const UVDynObject* self,
  const UVStringView* data);

uint8_t ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aexists(
  const UVDynObject* self,
  const UVStringView* path);

UVUnion_Unit_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aremove(
  const UVDynObject* self,
  const UVStringView* path);

UVUnion_DirIter_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fdir(
  const UVDynObject* self,
  const UVStringView* path);

UVUnion_Unit_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3acreate_x5fdir(
  const UVDynObject* self,
  const UVStringView* path);

UVUnion_Unit_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aensure_x5fdir(
  const UVDynObject* self,
  const UVStringView* path);

UVUnion_FileKind_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3akind(
  const UVDynObject* self,
  const UVStringView* path);

UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3arestrict(
  const UVDynObject* self,
  const UVStringView* path);

// Network builtins
UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3anet_x3a_x3arestrict_x5fto_x5fhost(
  const UVDynObject* self,
  const UVStringView* host);

// HeapAllocator builtins
UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3aheap_x3a_x3awith_x5fquota(
  const UVDynObject* self,
  const uint64_t* size);

void* ultraviolet_x3a_x3aruntime_x3a_x3aheap_x3a_x3aalloc_x5fraw(
  const UVDynObject* self,
  const uint64_t* count);

void ultraviolet_x3a_x3aruntime_x3a_x3aheap_x3a_x3adealloc_x5fraw(
  const UVDynObject* self,
  void** ptr,
  const uint64_t* count);

// Time builtins
UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic(
  const UVDynObject* self);

UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3awall(
  const UVDynObject* self);

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5fnow(
  UVMonotonicInstant* out,
  const UVDynObject* self);

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5fresolution(
  UVDuration* out,
  const UVDynObject* self);

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5felapsed(
  UVUnion_Duration_TimeError* out,
  const UVDynObject* self,
  const UVMonotonicInstant* start,
  const UVMonotonicInstant* end);

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5fcoarsen(
  UVUnion_DynObject_TimeError* out,
  const UVDynObject* self,
  const UVDuration* resolution);

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3awall_x5fnow_x5futc(
  UVUnion_UtcInstant_TimeError* out,
  const UVDynObject* self);

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3awall_x5fresolution(
  UVUnion_Duration_TimeError* out,
  const UVDynObject* self);

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3awall_x5fcoarsen(
  UVUnion_DynObject_TimeError* out,
  const UVDynObject* self,
  const UVDuration* resolution);

// System builtins
void ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3aexit(
  int32_t code);

UVStringView ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3aget_x5fenv(
  const UVStringView* key);

UVStringView ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3aexecutable_x5fpath(void);

uint64_t ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3aargument_x5fcount(void);

UVStringView ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3aargument(
  const uint64_t* index);

UVStringView ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3acurrent_x5fdirectory(void);

int32_t ultraviolet_x3a_x3aruntime_x3a_x3asystem_x3a_x3arun(
  const UVStringView* command);

// -----------------------------------------------------------------------------
// UVX Extension: Structured Concurrency Runtime Support (Â§18)
// -----------------------------------------------------------------------------

// Â§18.2 Context execution domain constructors
UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3acontext_x3a_x3acpu(
  const UVContext* self);
UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3acontext_x3a_x3acpu_x5fconfigured(
  const UVContext* self,
  uint64_t affinity_mask,
  int32_t priority_hint);
UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3acontext_x3a_x3agpu(
  const UVContext* self);
UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3acontext_x3a_x3ainline(
  const UVContext* self);

// Section 19.4 Reactor methods
void* ultraviolet_x3a_x3aruntime_x3a_x3areactor_x3a_x3aregister(
  const UVDynObject* self,
  const void* future);

// Â§18.2 ExecutionDomain methods
UVStringView ultraviolet_x3a_x3aruntime_x3a_x3aexecution_x5fdomain_x3a_x3aname(
  const UVDynObject* self);
uint64_t ultraviolet_x3a_x3aruntime_x3a_x3aexecution_x5fdomain_x3a_x3amax_x5fconcurrency(
  const UVDynObject* self);

// Â§18.2.2.4 GPU intrinsic functions
UVUsize3 ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3aglobal_x5fid(void);
UVUsize3 ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3alocal_x5fid(void);
UVUsize3 ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3aworkgroup_x5fid(void);
UVUsize3 ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3aworkgroup_x5fsize(void);
UVUsize3 ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3aglobal_x5fsize(void);
UVUsize3 ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3anum_x5fworkgroups(void);
uint64_t ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3alinear_x5fid(void);
void ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3abarrier(void);
void ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3amemory_x5fbarrier(void);
void ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3aworkgroup_x5fbarrier(void);

// Â§18.1.1 Begin parallel block - creates execution context
void* uv_parallel_begin(UVDynObject domain, size_t cancel_token, const char* name);

// Â§18.1.2 Join parallel block - waits for completion, propagates first panic
int uv_parallel_join(void* ctx_ptr);

// Â§18.4.2 Create spawn handle - returns Spawned<T>@Pending
void* uv_spawn_create(void* env, size_t env_size,
                            void (*body)(void* hosted_env, void* env, void* result, void* panic_out),
                            void* hosted_env,
                            size_t result_size,
                            uint64_t affinity_mask,
                            int32_t priority_hint);

// Â§10.3 Wait for spawn result - blocks until ready, extracts value
void* uv_spawn_wait(void* handle_ptr);

// Â§18.5.2 Dispatch iteration - parallel data iteration
void uv_dispatch_run(UVRange range, size_t elem_size, size_t result_size,
                           void (*body)(void* hosted_env, void* elem, void* captured, void* result, void* panic_out),
                           void* hosted_env,
                           void* captured_env,
                           UVStringView reduce_op,
                           void* reduce_result,
                           void (*reduce_fn)(void* hosted_env, void* lhs, void* rhs, void* out, void* panic_out),
                           int ordered,
                           size_t chunk_size);

// Dynamic key runtime primitives.
void* uv_key_scope_enter(void);
void uv_key_scope_exit(void* scope_ptr);
void uv_key_acquire(void* scope_ptr, UVStringView path, uint8_t mode);
void* uv_key_release_all(void);
void uv_key_reacquire(void* released_ptr);
void uv_key_release_snapshot_discard(void* released_ptr);

// Â§18.6.1 Cancellation token operations
size_t uv_cancel_token_new(void);
void uv_cancel_token_cancel(size_t token_id);
int uv_cancel_token_is_cancelled(size_t token_id);

// Â§18.7 Panic handling in parallel contexts
void uv_parallel_work_panic(void* ctx_ptr, uint32_t code);

// Low-level panic helper (used by parallel runtime)
void uv_panic(uint32_t code);

// CancelToken modal methods
size_t CancelToken_x3a_x3anew(void);
void CancelToken_x3a_x3aActive_x3a_x3acancel(void* self);
uint8_t CancelToken_x3a_x3aActive_x3a_x3ais_x5fcancelled(void* self);
size_t CancelToken_x3a_x3aActive_x3a_x3achild(void* self);
void CancelToken_x3a_x3aActive_x3a_x3await_x5fcancelled(void* out, void* self);

// File/DirIter methods (modal state methods / transitions)
UVUnion_StringManaged_IoError File_x3a_x3aRead_x3a_x3aread_x5fall(
  UVFileHandle self);

UVUnion_BytesManaged_IoError File_x3a_x3aRead_x3a_x3aread_x5fall_x5fbytes(
  UVFileHandle self);

void File_x3a_x3aRead_x3a_x3aclose(
  UVFileHandle self);

UVUnion_Unit_IoError File_x3a_x3aWrite_x3a_x3awrite(
  UVFileHandle self,
  const UVBytesView* data);

UVUnion_Unit_IoError File_x3a_x3aWrite_x3a_x3aflush(
  UVFileHandle self);

void File_x3a_x3aWrite_x3a_x3aclose(
  UVFileHandle self);

UVUnion_Unit_IoError File_x3a_x3aAppend_x3a_x3awrite(
  UVFileHandle self,
  const UVBytesView* data);

UVUnion_Unit_IoError File_x3a_x3aAppend_x3a_x3aflush(
  UVFileHandle self);

void File_x3a_x3aAppend_x3a_x3aclose(
  UVFileHandle self);

UVUnion_DirEntry_Unit_IoError DirIter_x3a_x3aOpen_x3a_x3anext(
  UVDirIterHandle self);

void DirIter_x3a_x3aOpen_x3a_x3aclose(
  UVDirIterHandle self);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // UV_RT_H
