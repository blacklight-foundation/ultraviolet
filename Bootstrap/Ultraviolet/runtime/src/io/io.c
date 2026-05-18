#include "../internal/rt_internal.h"
#include "../internal/rt_path.h"

#include <unicode/unorm2.h>
#include <unicode/ustring.h>

// C-compatible SPEC_RULE macro (no-op, tracing done via uv_trace_emit_rule)
#ifndef SPEC_RULE
#define SPEC_RULE(id) ((void)0)
#endif

static UVIoError uv_map_platform_error(uv_rt_u32_t err) {
  switch (err) {
    case UV_RT_ERROR_FILE_NOT_FOUND:
    case UV_RT_ERROR_PATH_NOT_FOUND:
    case UV_RT_ERROR_INVALID_DRIVE:
      return UV_IO_NOTFOUND;
    case UV_RT_ERROR_ACCESS_DENIED:
    case UV_RT_ERROR_PRIVILEGE_NOT_HELD:
      return UV_IO_PERMISSION_DENIED;
    case UV_RT_ERROR_FILE_EXISTS:
    case UV_RT_ERROR_ALREADY_EXISTS:
      return UV_IO_ALREADY_EXISTS;
    case UV_RT_ERROR_INVALID_NAME:
    case UV_RT_ERROR_BAD_PATHNAME:
    case UV_RT_ERROR_FILENAME_EXCED_RANGE:
    case UV_RT_ERROR_DIRECTORY:
    case UV_RT_ERROR_INVALID_PARAMETER:
      return UV_IO_INVALID_PATH;
    case UV_RT_ERROR_BUSY:
    case UV_RT_ERROR_SHARING_VIOLATION:
    case UV_RT_ERROR_LOCK_VIOLATION:
    case UV_RT_ERROR_PIPE_BUSY:
      return UV_IO_BUSY;
    default:
      return UV_IO_FAILURE;
  }
}

static UVIoError uv_last_io_error(void) {
  uv_rt_u32_t err = uv_rt_last_error_get();
  if (err == 0) {
    return UV_IO_FAILURE;
  }
  return uv_map_platform_error(err);
}

static UVFsState* uv_fs_state(const UVDynObject* io) {
  if (!io) {
    return NULL;
  }
  return (UVFsState*)io->data;
}

typedef enum UVTrackedFileStateTag {
  UV_TRACKED_FILE_OPEN_READ = 1,
  UV_TRACKED_FILE_OPEN_WRITE = 2,
  UV_TRACKED_FILE_OPEN_APPEND = 3,
  UV_TRACKED_FILE_CLOSED = 4,
} UVTrackedFileStateTag;

typedef struct UVTrackedFile {
  uint64_t id;
  uv_rt_handle_t handle;
  uint64_t position;
  uint64_t length;
  uint8_t state;
  uint8_t flushed;
  struct UVTrackedFile* next;
} UVTrackedFile;

typedef struct UVTrackedDirIter {
  uint64_t id;
  UVDirIterState* state;
  struct UVTrackedDirIter* next;
} UVTrackedDirIter;

static uv_rt_rwlock_t g_uv_io_registry_lock = UV_RT_RWLOCK_INIT;
static uint64_t g_uv_next_file_id = 1;
static uint64_t g_uv_next_dir_iter_id = 1;
static UVTrackedFile* g_uv_tracked_files = NULL;
static UVTrackedDirIter* g_uv_tracked_dir_iters = NULL;

static uint64_t uv_next_tracked_id(uint64_t* counter) {
  uint64_t id = *counter;
  if (id == 0) {
    id = 1;
  }
  *counter = id + 1;
  if (*counter == 0) {
    *counter = 1;
  }
  return id;
}

static uint64_t uv_file_length_handle(uv_rt_handle_t handle) {
  uv_rt_file_offset_t size;
  size.quad_part = 0;
  if (!handle || handle == UV_RT_INVALID_HANDLE) {
    return 0;
  }
  if (!uv_rt_file_size(handle, &size) || size.quad_part < 0) {
    return 0;
  }
  return (uint64_t)size.quad_part;
}

static UVTrackedFile* uv_find_tracked_file_locked(uint64_t id) {
  UVTrackedFile* it = g_uv_tracked_files;
  while (it) {
    if (it->id == id) {
      return it;
    }
    it = it->next;
  }
  return NULL;
}

static uint64_t uv_track_file_handle(uv_rt_handle_t handle,
                                     UVTrackedFileStateTag state,
                                     uint64_t position,
                                     uint64_t length) {
  UVTrackedFile* tracked =
      (UVTrackedFile*)uv_heap_alloc_raw(sizeof(UVTrackedFile));
  if (!tracked) {
    return 0;
  }

  uv_rt_rwlock_lock_exclusive(&g_uv_io_registry_lock);
  tracked->id = uv_next_tracked_id(&g_uv_next_file_id);
  tracked->handle = handle;
  tracked->position = position;
  tracked->length = length;
  tracked->state = (uint8_t)state;
  tracked->flushed = 0;
  tracked->next = g_uv_tracked_files;
  g_uv_tracked_files = tracked;
  uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
  return tracked->id;
}

static void uv_free_dir_state(UVDirIterState* state) {
  if (!state) {
    return;
  }
  if (state->names) {
    for (uint32_t i = 0; i < state->count; ++i) {
      if (state->names[i]) {
        uv_heap_free_raw(state->names[i]);
      }
    }
    uv_heap_free_raw(state->names);
  }
  if (state->name_lens) {
    uv_heap_free_raw(state->name_lens);
  }
  if (state->kinds) {
    uv_heap_free_raw(state->kinds);
  }
  if (state->base_path) {
    uv_heap_free_raw(state->base_path);
  }
  if (state->base_utf8) {
    uv_heap_free_raw(state->base_utf8);
  }
  uv_heap_free_raw(state);
}

static UVTrackedDirIter* uv_find_tracked_dir_iter_locked(uint64_t id) {
  UVTrackedDirIter* it = g_uv_tracked_dir_iters;
  while (it) {
    if (it->id == id) {
      return it;
    }
    it = it->next;
  }
  return NULL;
}

static uint64_t uv_track_dir_iter_state(UVDirIterState* state) {
  UVTrackedDirIter* tracked =
      (UVTrackedDirIter*)uv_heap_alloc_raw(sizeof(UVTrackedDirIter));
  if (!tracked) {
    return 0;
  }

  uv_rt_rwlock_lock_exclusive(&g_uv_io_registry_lock);
  tracked->id = uv_next_tracked_id(&g_uv_next_dir_iter_id);
  tracked->state = state;
  tracked->next = g_uv_tracked_dir_iters;
  g_uv_tracked_dir_iters = tracked;
  uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
  return tracked->id;
}

static UVTrackedFile* uv_require_open_file_locked(uint64_t id) {
  UVTrackedFile* tracked = uv_find_tracked_file_locked(id);
  if (!tracked) {
    return NULL;
  }
  if (tracked->state == (uint8_t)UV_TRACKED_FILE_CLOSED) {
    return NULL;
  }
  if (!tracked->handle || tracked->handle == UV_RT_INVALID_HANDLE) {
    return NULL;
  }
  return tracked;
}

static int uv_close_tracked_file_locked(uint64_t id, UVIoError* out_err) {
  if (out_err) {
    *out_err = UV_IO_FAILURE;
  }
  UVTrackedFile* tracked = uv_find_tracked_file_locked(id);
  if (!tracked) {
    return 0;
  }
  if (tracked->state == (uint8_t)UV_TRACKED_FILE_CLOSED) {
    return 0;
  }

  uv_rt_handle_t handle = tracked->handle;
  tracked->handle = UV_RT_INVALID_HANDLE;
  tracked->state = (uint8_t)UV_TRACKED_FILE_CLOSED;
  tracked->flushed = 0;
  if (!handle || handle == UV_RT_INVALID_HANDLE) {
    return 0;
  }
  if (!uv_rt_handle_release(handle)) {
    if (out_err) {
      *out_err = uv_last_io_error();
    }
    return 0;
  }
  return 1;
}

static UVTrackedDirIter* uv_require_open_dir_iter_locked(uint64_t id) {
  UVTrackedDirIter* tracked = uv_find_tracked_dir_iter_locked(id);
  if (!tracked) {
    return NULL;
  }
  if (!tracked->state) {
    return NULL;
  }
  return tracked;
}

static int uv_is_ascii_wide(const wchar_t* text, uint32_t len) {
  if (!text) {
    return 0;
  }
  for (uint32_t i = 0; i < len; ++i) {
    if ((uint32_t)text[i] > 0x7Fu) {
      return 0;
    }
  }
  return 1;
}

static uint8_t* uv_ascii_casefold_utf8_from_wide(const wchar_t* text,
                                                 uint32_t len,
                                                 uint32_t* out_len) {
  uint8_t* folded = (uint8_t*)uv_heap_alloc_raw((size_t)len + 1u);
  if (!folded) {
    return NULL;
  }
  for (uint32_t i = 0; i < len; ++i) {
    wchar_t ch = text[i];
    if (ch >= L'A' && ch <= L'Z') {
      ch = (wchar_t)(ch - L'A' + L'a');
    }
    folded[i] = (uint8_t)ch;
  }
  folded[len] = 0;
  if (out_len) {
    *out_len = len;
  }
  return folded;
}

static UChar* uv_wide_to_uchar(const wchar_t* text,
                               uint32_t len,
                               int32_t* out_len) {
  UErrorCode status = U_ZERO_ERROR;
  int32_t converted_len = 0;
  UChar* converted = NULL;

  if (out_len) {
    *out_len = 0;
  }
  if (!text) {
    return NULL;
  }

  u_strFromWCS(NULL, 0, &converted_len, text, (int32_t)len, &status);
  if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
    return NULL;
  }

  converted = (UChar*)uv_heap_alloc_raw(sizeof(UChar) * (size_t)(converted_len + 1));
  if (!converted) {
    return NULL;
  }

  status = U_ZERO_ERROR;
  u_strFromWCS(converted,
               converted_len + 1,
               &converted_len,
               text,
               (int32_t)len,
               &status);
  if (U_FAILURE(status)) {
    uv_heap_free_raw(converted);
    return NULL;
  }
  converted[converted_len] = 0;
  if (out_len) {
    *out_len = converted_len;
  }
  return converted;
}

static int uv_fs_resolve_path(const UVFsState* io,
                              const UVStringView* path,
                              uint8_t** out_utf8,
                              uint32_t* out_len) {
  if (out_utf8) {
    *out_utf8 = NULL;
  }
  if (out_len) {
    *out_len = 0;
  }
  if (!path) {
    return 0;
  }
  if (!path->data && path->len != 0) {
    return 0;
  }
  if (!uv_utf8_valid(path->data, path->len)) {
    return 0;
  }
  if (uv_utf8_has_null(path->data, path->len)) {
    return 0;
  }

  if (io && io->restricted) {
    if (!io->valid) {
      return 0;
    }
    if (uv_rt_path_is_absolute_utf8(path->data, path->len)) {
      return 0;
    }
    uint32_t joined_len = 0;
    uint8_t* joined = uv_rt_path_join_utf8(io->base_utf8, io->base_len,
                                                path->data, path->len,
                                                &joined_len);
    if (!joined && (io->base_len != 0 || path->len != 0)) {
      return 0;
    }
    uint32_t canon_len = 0;
    uint8_t* canon = uv_rt_path_canonicalize_utf8(
        joined ? joined : path->data, joined ? joined_len : path->len,
        &canon_len);
    if (joined) {
      uv_heap_free_raw(joined);
    }
    if (!canon) {
      return 0;
    }
    if (!uv_rt_path_has_prefix_utf8(canon, canon_len, io->base_utf8,
                                         io->base_len)) {
      uv_heap_free_raw(canon);
      return 0;
    }
    if (out_utf8) {
      *out_utf8 = canon;
    }
    if (out_len) {
      *out_len = canon_len;
    }
    return 1;
  }

  uint32_t canon_len = 0;
  uint8_t* canon =
      uv_rt_path_canonicalize_utf8(path->data, path->len, &canon_len);
  if (!canon) {
    return 0;
  }
  if (out_utf8) {
    *out_utf8 = canon;
  }
  if (out_len) {
    *out_len = canon_len;
  }
  return 1;
}

static UVUnion_File_IoError uv_file_err(UVIoError err) {
  UVUnion_File_IoError out;
  out.disc = 1;
  out.payload.io_error = err;
  return out;
}

static UVUnion_DirIter_IoError uv_dir_err(UVIoError err) {
  UVUnion_DirIter_IoError out;
  out.disc = 1;
  out.payload.io_error = err;
  return out;
}

static UVUnion_Unit_IoError uv_unit_err(UVIoError err) {
  UVUnion_Unit_IoError out;
  out.disc = 1;
  out.payload = err;
  return out;
}

static UVUnion_Unit_IoError uv_unit_ok(void) {
  UVUnion_Unit_IoError out;
  out.disc = 0;
  out.payload = 0;
  return out;
}

static UVUnion_FileKind_IoError uv_kind_err(UVIoError err) {
  UVUnion_FileKind_IoError out;
  out.disc = 1;
  out.payload = err;
  return out;
}

static UVUnion_FileKind_IoError uv_kind_ok(UVFileKind kind) {
  UVUnion_FileKind_IoError out;
  out.disc = 0;
  out.payload = kind;
  return out;
}

static UVUnion_StringManaged_IoError uv_string_io_err(UVIoError err) {
  UVUnion_StringManaged_IoError out;
  out.disc = 1;
  out.payload.io_error = err;
  return out;
}

static UVUnion_BytesManaged_IoError uv_bytes_io_err(UVIoError err) {
  UVUnion_BytesManaged_IoError out;
  out.disc = 1;
  out.payload.io_error = err;
  return out;
}
static UVUnion_BytesManaged_IoError uv_read_all_bytes_handle(uv_rt_handle_t handle) {
  if (!handle || handle == UV_RT_INVALID_HANDLE) {
    return uv_bytes_io_err(UV_IO_FAILURE);
  }

  uv_rt_file_offset_t size;
  if (!uv_rt_file_size(handle, &size)) {
    return uv_bytes_io_err(uv_last_io_error());
  }
  if (size.quad_part < 0) {
    return uv_bytes_io_err(UV_IO_FAILURE);
  }
  if ((uint64_t)size.quad_part > (uint64_t)SIZE_MAX) {
    return uv_bytes_io_err(UV_IO_FAILURE);
  }

  uint64_t len = (uint64_t)size.quad_part;
  if (len == 0) {
    UVUnion_BytesManaged_IoError out;
    out.disc = 0;
    out.payload.value.data = NULL;
    out.payload.value.len = 0;
    out.payload.value.cap = 0;
    return out;
  }

  uint8_t* data = uv_alloc_managed_bytes(NULL, len, NULL);
  if (!data) {
    return uv_bytes_io_err(UV_IO_FAILURE);
  }

  uint64_t total = 0;
  while (total < len) {
    uv_rt_u32_t chunk = 0;
    uv_rt_u32_t to_read = (uv_rt_u32_t)uv_min_u64(len - total, 0x7FFFFFFF);
    if (!uv_rt_handle_read(handle, data + total, to_read, &chunk)) {
      uv_free_managed_bytes(data);
      return uv_bytes_io_err(uv_last_io_error());
    }
    if (chunk == 0) {
      break;
    }
    total += (uint64_t)chunk;
  }

  if (total != len) {
    uv_free_managed_bytes(data);
    return uv_bytes_io_err(UV_IO_FAILURE);
  }

  UVUnion_BytesManaged_IoError out;
  out.disc = 0;
  out.payload.value.data = data;
  out.payload.value.len = len;
  out.payload.value.cap = len;
  return out;
}

static UVUnion_StringManaged_IoError uv_read_all_string_handle(uv_rt_handle_t handle) {
  UVUnion_BytesManaged_IoError bytes = uv_read_all_bytes_handle(handle);
  if (bytes.disc == 1) {
    return uv_string_io_err(bytes.payload.io_error);
  }

  if (!uv_utf8_valid(bytes.payload.value.data, bytes.payload.value.len)) {
    uv_free_managed_bytes(bytes.payload.value.data);
    return uv_string_io_err(UV_IO_FAILURE);
  }

  UVUnion_StringManaged_IoError out;
  out.disc = 0;
  out.payload.value.data = bytes.payload.value.data;
  out.payload.value.len = bytes.payload.value.len;
  out.payload.value.cap = bytes.payload.value.cap;
  return out;
}
UVUnion_File_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fread(
    const UVDynObject* self,
    const UVStringView* path) {
  SPEC_RULE("Prim-IO-OpenRead");
  uv_trace_emit_rule("Prim-IO-OpenRead");
  UVFsState* io = uv_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!uv_fs_resolve_path(io, path, &canon, &canon_len)) {
    return uv_file_err(UV_IO_INVALID_PATH);
  }
  wchar_t* wide = uv_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  uv_heap_free_raw(canon);
  if (!wide) {
    return uv_file_err(UV_IO_INVALID_PATH);
  }

  uv_rt_handle_t h = uv_rt_file_open_path_wide(
      wide,
      UV_RT_FILE_ACCESS_READ,
      UV_RT_FILE_SHARE_READ | UV_RT_FILE_SHARE_WRITE |
          UV_RT_FILE_SHARE_DELETE,
      NULL,
      UV_RT_FILE_OPEN_EXISTING,
      UV_RT_FILE_ATTRIBUTE_NORMAL,
      NULL);
  uv_heap_free_raw(wide);
  if (h == UV_RT_INVALID_HANDLE) {
    return uv_file_err(uv_last_io_error());
  }

  uint64_t tracked_id =
      uv_track_file_handle(h,
                           UV_TRACKED_FILE_OPEN_READ,
                           0,
                           uv_file_length_handle(h));
  if (tracked_id == 0) {
    uv_rt_handle_release(h);
    return uv_file_err(UV_IO_FAILURE);
  }

  UVUnion_File_IoError out;
  out.disc = 0;
  out.payload.handle = tracked_id;
  return out;
}

UVUnion_File_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fwrite(
    const UVDynObject* self,
    const UVStringView* path) {
  SPEC_RULE("Prim-IO-OpenWrite");
  uv_trace_emit_rule("Prim-IO-OpenWrite");
  UVFsState* io = uv_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!uv_fs_resolve_path(io, path, &canon, &canon_len)) {
    return uv_file_err(UV_IO_INVALID_PATH);
  }
  wchar_t* wide = uv_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  uv_heap_free_raw(canon);
  if (!wide) {
    return uv_file_err(UV_IO_INVALID_PATH);
  }

  uv_rt_handle_t h = uv_rt_file_open_path_wide(
      wide,
      UV_RT_FILE_ACCESS_WRITE,
      UV_RT_FILE_SHARE_READ,
      NULL,
      UV_RT_FILE_OPEN_EXISTING,
      UV_RT_FILE_ATTRIBUTE_NORMAL,
      NULL);
  uv_heap_free_raw(wide);
  if (h == UV_RT_INVALID_HANDLE) {
    return uv_file_err(uv_last_io_error());
  }

  uint64_t tracked_id =
      uv_track_file_handle(h,
                           UV_TRACKED_FILE_OPEN_WRITE,
                           0,
                           uv_file_length_handle(h));
  if (tracked_id == 0) {
    uv_rt_handle_release(h);
    return uv_file_err(UV_IO_FAILURE);
  }

  UVUnion_File_IoError out;
  out.disc = 0;
  out.payload.handle = tracked_id;
  return out;
}

UVUnion_File_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fappend(
    const UVDynObject* self,
    const UVStringView* path) {
  SPEC_RULE("Prim-IO-OpenAppend");
  uv_trace_emit_rule("Prim-IO-OpenAppend");
  UVFsState* io = uv_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!uv_fs_resolve_path(io, path, &canon, &canon_len)) {
    return uv_file_err(UV_IO_INVALID_PATH);
  }
  wchar_t* wide = uv_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  uv_heap_free_raw(canon);
  if (!wide) {
    return uv_file_err(UV_IO_INVALID_PATH);
  }

  uv_rt_handle_t h = uv_rt_file_open_path_wide(
      wide,
      UV_RT_FILE_ACCESS_APPEND | UV_RT_FILE_ACCESS_WRITE,
      UV_RT_FILE_SHARE_READ,
      NULL,
      UV_RT_FILE_OPEN_EXISTING,
      UV_RT_FILE_ATTRIBUTE_NORMAL,
      NULL);
  uv_heap_free_raw(wide);
  if (h == UV_RT_INVALID_HANDLE) {
    return uv_file_err(uv_last_io_error());
  }
  uv_rt_file_offset_t eof;
  eof.quad_part = 0;
  if (!uv_rt_file_seek(h, eof, NULL, UV_RT_FILE_SEEK_END)) {
    UVIoError err = uv_last_io_error();
    uv_rt_handle_release(h);
    return uv_file_err(err);
  }

  uint64_t length = uv_file_length_handle(h);
  uint64_t tracked_id =
      uv_track_file_handle(h,
                           UV_TRACKED_FILE_OPEN_APPEND,
                           length,
                           length);
  if (tracked_id == 0) {
    uv_rt_handle_release(h);
    return uv_file_err(UV_IO_FAILURE);
  }

  UVUnion_File_IoError out;
  out.disc = 0;
  out.payload.handle = tracked_id;
  return out;
}

UVUnion_File_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3acreate_x5fwrite(
    const UVDynObject* self,
    const UVStringView* path) {
  SPEC_RULE("Prim-IO-CreateWrite");
  uv_trace_emit_rule("Prim-IO-CreateWrite");
  UVFsState* io = uv_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!uv_fs_resolve_path(io, path, &canon, &canon_len)) {
    return uv_file_err(UV_IO_INVALID_PATH);
  }
  wchar_t* wide = uv_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  uv_heap_free_raw(canon);
  if (!wide) {
    return uv_file_err(UV_IO_INVALID_PATH);
  }

  uv_rt_handle_t h = uv_rt_file_open_path_wide(
      wide,
      UV_RT_FILE_ACCESS_WRITE,
      UV_RT_FILE_SHARE_READ,
      NULL,
      UV_RT_FILE_OPEN_CREATE_NEW,
      UV_RT_FILE_ATTRIBUTE_NORMAL,
      NULL);
  uv_heap_free_raw(wide);
  if (h == UV_RT_INVALID_HANDLE) {
    return uv_file_err(uv_last_io_error());
  }

  uint64_t tracked_id =
      uv_track_file_handle(h,
                           UV_TRACKED_FILE_OPEN_WRITE,
                           0,
                           0);
  if (tracked_id == 0) {
    uv_rt_handle_release(h);
    return uv_file_err(UV_IO_FAILURE);
  }

  UVUnion_File_IoError out;
  out.disc = 0;
  out.payload.handle = tracked_id;
  return out;
}
void ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aread_x5ffile(
    UVUnion_StringManaged_IoError* out,
    const UVDynObject* self,
    const UVStringView* path) {
  SPEC_RULE("Prim-IO-ReadFile");
  uv_trace_emit_rule("Prim-IO-ReadFile");
  if (!out) {
    return;
  }
  UVUnion_File_IoError file = ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fread(self, path);
  if (file.disc == 1) {
    *out = uv_string_io_err(file.payload.io_error);
    return;
  }
  UVFileHandle handle;
  handle.handle = file.payload.handle;
  UVUnion_StringManaged_IoError result =
      File_x3a_x3aRead_x3a_x3aread_x5fall(&handle);
  File_x3a_x3aRead_x3a_x3aclose(handle);
  *out = result;
}

void ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aread_x5fbytes(
    UVUnion_BytesManaged_IoError* out,
    const UVDynObject* self,
    const UVStringView* path) {
  SPEC_RULE("Prim-IO-ReadBytes");
  uv_trace_emit_rule("Prim-IO-ReadBytes");
  if (!out) {
    return;
  }
  UVUnion_File_IoError file = ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fread(self, path);
  if (file.disc == 1) {
    *out = uv_bytes_io_err(file.payload.io_error);
    return;
  }
  UVFileHandle handle;
  handle.handle = file.payload.handle;
  UVUnion_BytesManaged_IoError result =
      File_x3a_x3aRead_x3a_x3aread_x5fall_x5fbytes(&handle);
  File_x3a_x3aRead_x3a_x3aclose(handle);
  *out = result;
}

UVUnion_Unit_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3awrite_x5ffile(
    const UVDynObject* self,
    const UVStringView* path,
    const UVBytesView* data) {
  SPEC_RULE("Prim-IO-WriteFile");
  uv_trace_emit_rule("Prim-IO-WriteFile");
  UVFsState* io = uv_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!uv_fs_resolve_path(io, path, &canon, &canon_len)) {
    UVUnion_Unit_IoError out = uv_unit_err(UV_IO_INVALID_PATH);
    return out;
  }
  wchar_t* wide = uv_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  uv_heap_free_raw(canon);
  if (!wide) {
    UVUnion_Unit_IoError out = uv_unit_err(UV_IO_INVALID_PATH);
    return out;
  }

  uv_rt_handle_t h = uv_rt_file_open_path_wide(
      wide,
      UV_RT_FILE_ACCESS_WRITE,
      UV_RT_FILE_SHARE_READ,
      NULL,
      UV_RT_FILE_OPEN_REPLACE_ALWAYS,
      UV_RT_FILE_ATTRIBUTE_NORMAL,
      NULL);
  uv_heap_free_raw(wide);
  if (h == UV_RT_INVALID_HANDLE) {
    UVUnion_Unit_IoError out = uv_unit_err(uv_last_io_error());
    return out;
  }

  uint64_t len = data ? data->len : 0;
  uint64_t written = 0;
  while (written < len) {
    uv_rt_u32_t chunk = 0;
    uv_rt_u32_t to_write = (uv_rt_u32_t)uv_min_u64(len - written, 0x7FFFFFFF);
    if (!uv_rt_handle_write(h, data->data + written, to_write, &chunk)) {
      if (chunk > 0) {
        written += (uint64_t)chunk;
        continue;
      }
      uv_rt_handle_release(h);
      UVUnion_Unit_IoError out = uv_unit_err(uv_last_io_error());
      return out;
    }
    if (chunk == 0) {
      break;
    }
    written += (uint64_t)chunk;
  }

  uv_rt_handle_release(h);
  if (written != len) {
    UVUnion_Unit_IoError out = uv_unit_err(UV_IO_FAILURE);
    return out;
  }
  {
    UVUnion_Unit_IoError out = uv_unit_ok();
    return out;
  }
}

static uv_rt_handle_t uv_open_console_out(void) {
  return uv_rt_file_open_path_wide(L"CONOUT$",
                                        UV_RT_FILE_ACCESS_WRITE,
                                        UV_RT_FILE_SHARE_READ |
                                            UV_RT_FILE_SHARE_WRITE,
                                        NULL,
                                        UV_RT_FILE_OPEN_EXISTING,
                                        UV_RT_FILE_ATTRIBUTE_NORMAL,
                                        NULL);
}

static UVUnion_Unit_IoError uv_write_stream_utf8(uv_rt_u32_t std_handle_id,
                                                  const UVStringView* data) {
  uint64_t len = data ? data->len : 0;
  uv_rt_handle_t h = uv_rt_std_stream(std_handle_id);
  int close_handle = 0;

  if (!h || h == UV_RT_INVALID_HANDLE) {
    h = uv_open_console_out();
    close_handle = 1;
  }
  if (!h || h == UV_RT_INVALID_HANDLE) {
    return uv_unit_err(UV_IO_FAILURE);
  }

  uint64_t written = 0;
  while (written < len) {
    uv_rt_u32_t chunk = 0;
    uv_rt_u32_t to_write = (uv_rt_u32_t)uv_min_u64(len - written, 0x7FFFFFFF);

    if (!uv_rt_handle_write(h, data->data + written, to_write, &chunk)) {
      if (chunk > 0) {
        written += (uint64_t)chunk;
        continue;
      }
      uv_rt_u32_t mode = 0;
      if (uv_rt_console_mode_get(h, &mode)) {
        if (!uv_rt_console_write_utf8(h,
                           (const char*)(data->data + written),
                           to_write,
                           &chunk)) {
          if (chunk > 0) {
            written += (uint64_t)chunk;
            continue;
          }
          uv_rt_u32_t err = uv_rt_last_error_get();
          if (close_handle) {
            uv_rt_handle_release(h);
          }
          return uv_unit_err(uv_map_platform_error(err));
        }
      } else {
        uv_rt_u32_t err = uv_rt_last_error_get();
        if (close_handle) {
          uv_rt_handle_release(h);
        }
        return uv_unit_err(uv_map_platform_error(err));
      }
    }

    if (chunk == 0) {
      break;
    }
    written += (uint64_t)chunk;
  }

  if (close_handle) {
    uv_rt_handle_release(h);
  }

  if (written != len) {
    return uv_unit_err(UV_IO_FAILURE);
  }
  return uv_unit_ok();
}

UVUnion_Unit_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3awrite_x5fstdout(
    const UVDynObject* self,
    const UVStringView* data) {
  SPEC_RULE("Prim-IO-WriteStdout");
  uv_trace_emit_rule("Prim-IO-WriteStdout");
  (void)self;
  return uv_write_stream_utf8(UV_RT_STD_STREAM_OUTPUT, data);
}

UVUnion_Unit_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3awrite_x5fstderr(
    const UVDynObject* self,
    const UVStringView* data) {
  SPEC_RULE("Prim-IO-WriteStderr");
  uv_trace_emit_rule("Prim-IO-WriteStderr");
  (void)self;
  return uv_write_stream_utf8(UV_RT_STD_STREAM_ERROR, data);
}


uint8_t ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aexists(
    const UVDynObject* self,
    const UVStringView* path) {
  SPEC_RULE("Prim-IO-Exists");
  uv_trace_emit_rule("Prim-IO-Exists");
  UVFsState* io = uv_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!uv_fs_resolve_path(io, path, &canon, &canon_len)) {
    return 0;
  }
  wchar_t* wide = uv_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  uv_heap_free_raw(canon);
  if (!wide) {
    return 0;
  }
  uv_rt_u32_t attrs = uv_rt_path_attributes_wide(wide);
  uv_heap_free_raw(wide);
  if (attrs == UV_RT_FILE_ATTRIBUTES_INVALID) {
    return 0;
  }
  return 1;
}

UVUnion_Unit_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aremove(
    const UVDynObject* self,
    const UVStringView* path) {
  SPEC_RULE("Prim-IO-Remove");
  uv_trace_emit_rule("Prim-IO-Remove");
  UVFsState* io = uv_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!uv_fs_resolve_path(io, path, &canon, &canon_len)) {
    return uv_unit_err(UV_IO_INVALID_PATH);
  }
  wchar_t* wide = uv_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  uv_heap_free_raw(canon);
  if (!wide) {
    return uv_unit_err(UV_IO_INVALID_PATH);
  }
  uv_rt_u32_t attrs = uv_rt_path_attributes_wide(wide);
  if (attrs == UV_RT_FILE_ATTRIBUTES_INVALID) {
    uv_heap_free_raw(wide);
    return uv_unit_err(uv_last_io_error());
  }
  uv_rt_bool_t ok = UV_RT_FALSE;
  if (attrs & UV_RT_FILE_ATTRIBUTE_DIRECTORY) {
    ok = uv_rt_directory_remove_path_wide(wide);
  } else {
    ok = uv_rt_file_remove_wide(wide);
  }
  if (!ok) {
    UVIoError err = uv_last_io_error();
    uv_heap_free_raw(wide);
    return uv_unit_err(err);
  }
  uv_heap_free_raw(wide);
  return uv_unit_ok();
}
static uint8_t* uv_entry_key_utf8(const wchar_t* name,
                                  uint32_t name_len,
                                  uint32_t* out_len) {
  int32_t name_utf16_len = 0;
  UChar* name_utf16 = NULL;
  if (out_len) {
    *out_len = 0;
  }
  if (!name) {
    return NULL;
  }
  if (name_len == 0) {
    uint8_t* empty = (uint8_t*)uv_heap_alloc_raw(1);
    if (!empty) {
      return NULL;
    }
    empty[0] = 0;
    return empty;
  }

  if (uv_is_ascii_wide(name, name_len)) {
    return uv_ascii_casefold_utf8_from_wide(name, name_len, out_len);
  }

  uv_rt_icu_data_configure();
  UErrorCode status = U_ZERO_ERROR;
  const UNormalizer2* nfc = unorm2_getNFCInstance(&status);
  if (U_FAILURE(status) || !nfc) {
    return NULL;
  }

  name_utf16 = uv_wide_to_uchar(name, name_len, &name_utf16_len);
  if (!name_utf16) {
    return NULL;
  }

  status = U_ZERO_ERROR;
  int32_t nfc_len = unorm2_normalize(nfc,
                                     name_utf16,
                                     name_utf16_len,
                                     NULL,
                                     0,
                                     &status);
  if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
    uv_heap_free_raw(name_utf16);
    return NULL;
  }
  status = U_ZERO_ERROR;
  UChar* normalized =
      (UChar*)uv_heap_alloc_raw(sizeof(UChar) * (size_t)(nfc_len + 1));
  if (!normalized) {
    uv_heap_free_raw(name_utf16);
    return NULL;
  }
  nfc_len = unorm2_normalize(nfc,
                             name_utf16,
                             name_utf16_len,
                             normalized,
                             nfc_len + 1,
                             &status);
  uv_heap_free_raw(name_utf16);
  if (U_FAILURE(status)) {
    uv_heap_free_raw(normalized);
    return NULL;
  }
  normalized[nfc_len] = 0;

  status = U_ZERO_ERROR;
  int32_t fold_len = u_strFoldCase(NULL,
                                   0,
                                   normalized,
                                   nfc_len,
                                   U_FOLD_CASE_DEFAULT,
                                   &status);
  if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
    uv_heap_free_raw(normalized);
    return NULL;
  }
  status = U_ZERO_ERROR;
  UChar* folded = (UChar*)uv_heap_alloc_raw(sizeof(UChar) * (size_t)(fold_len + 1));
  if (!folded) {
    uv_heap_free_raw(normalized);
    return NULL;
  }
  fold_len = u_strFoldCase(folded,
                           fold_len + 1,
                           normalized,
                           nfc_len,
                           U_FOLD_CASE_DEFAULT,
                           &status);
  uv_heap_free_raw(normalized);
  if (U_FAILURE(status)) {
    uv_heap_free_raw(folded);
    return NULL;
  }
  folded[fold_len] = 0;

  status = U_ZERO_ERROR;
  int32_t utf8_len = 0;
  u_strToUTF8(NULL, 0, &utf8_len, folded, fold_len, &status);
  if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
    uv_heap_free_raw(folded);
    return NULL;
  }
  status = U_ZERO_ERROR;
  uint8_t* key_utf8 = (uint8_t*)uv_heap_alloc_raw((size_t)utf8_len + 1u);
  if (!key_utf8) {
    uv_heap_free_raw(folded);
    return NULL;
  }
  u_strToUTF8((char*)key_utf8,
              utf8_len + 1,
              &utf8_len,
              folded,
              fold_len,
              &status);
  uv_heap_free_raw(folded);
  if (U_FAILURE(status)) {
    uv_heap_free_raw(key_utf8);
    return NULL;
  }
  key_utf8[utf8_len] = 0;
  if (out_len) {
    *out_len = (uint32_t)utf8_len;
  }
  return key_utf8;
}

static int uv_lex_bytes(const uint8_t* a, uint32_t alen,
                        const uint8_t* b, uint32_t blen) {
  uint32_t n = alen < blen ? alen : blen;
  for (uint32_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  if (alen < blen) {
    return -1;
  }
  if (alen > blen) {
    return 1;
  }
  return 0;
}

typedef struct DirEntryTmp {
  wchar_t* name_w;
  uint32_t name_w_len;
  uint8_t* name_utf8;
  uint32_t name_utf8_len;
  uint8_t* key_utf8;
  uint32_t key_utf8_len;
  uint8_t kind;
} DirEntryTmp;

static int uv_entry_cmp(const DirEntryTmp* a, const DirEntryTmp* b) {
  int key_cmp = uv_lex_bytes(a->key_utf8, a->key_utf8_len,
                             b->key_utf8, b->key_utf8_len);
  if (key_cmp != 0) {
    return key_cmp;
  }
  return uv_lex_bytes(a->name_utf8, a->name_utf8_len,
                      b->name_utf8, b->name_utf8_len);
}

static void uv_sort_entries(DirEntryTmp* entries, uint32_t count) {
  for (uint32_t i = 1; i < count; ++i) {
    DirEntryTmp tmp = entries[i];
    uint32_t j = i;
    while (j > 0 && uv_entry_cmp(&tmp, &entries[j - 1]) < 0) {
      entries[j] = entries[j - 1];
      --j;
    }
    entries[j] = tmp;
  }
}
UVUnion_DirIter_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fdir(
    const UVDynObject* self,
    const UVStringView* path) {
  SPEC_RULE("Prim-IO-OpenDir");
  uv_trace_emit_rule("Prim-IO-OpenDir");
  UVFsState* io = uv_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!uv_fs_resolve_path(io, path, &canon, &canon_len)) {
    return uv_dir_err(UV_IO_INVALID_PATH);
  }
  uint32_t wide_len = 0;
  wchar_t* wide =
      uv_rt_path_utf8_to_native_wide(canon, canon_len, &wide_len);
  if (!wide) {
    uv_heap_free_raw(canon);
    return uv_dir_err(UV_IO_INVALID_PATH);
  }

  uv_rt_u32_t attrs = uv_rt_path_attributes_wide(wide);
  if (attrs == UV_RT_FILE_ATTRIBUTES_INVALID) {
    UVIoError err = uv_last_io_error();
    uv_heap_free_raw(wide);
    uv_heap_free_raw(canon);
    return uv_dir_err(err);
  }
  if ((attrs & UV_RT_FILE_ATTRIBUTE_DIRECTORY) == 0) {
    uv_heap_free_raw(wide);
    uv_heap_free_raw(canon);
    return uv_dir_err(UV_IO_INVALID_PATH);
  }

  uint32_t pattern_len = wide_len + 2;
  wchar_t* pattern = (wchar_t*)uv_heap_alloc_raw(sizeof(wchar_t) * (pattern_len + 1));
  if (!pattern) {
    uv_heap_free_raw(wide);
    uv_heap_free_raw(canon);
    return uv_dir_err(UV_IO_FAILURE);
  }
  uv_memcpy(pattern, wide, sizeof(wchar_t) * wide_len);
  if (wide_len > 0 && wide[wide_len - 1] != L'\\') {
    pattern[wide_len] = L'\\';
    pattern[wide_len + 1] = L'*';
    pattern[wide_len + 2] = 0;
  } else {
    pattern[wide_len] = L'*';
    pattern[wide_len + 1] = 0;
  }

  uv_rt_find_data_t data;
  uv_rt_handle_t find = uv_rt_directory_scan_first_wide(pattern, &data);
  uv_heap_free_raw(pattern);
  if (find == UV_RT_INVALID_HANDLE) {
    UVIoError err = uv_last_io_error();
    uv_heap_free_raw(wide);
    uv_heap_free_raw(canon);
    return uv_dir_err(err);
  }

  uint32_t cap = 16;
  uint32_t count = 0;
  DirEntryTmp* entries = (DirEntryTmp*)uv_heap_alloc_raw(sizeof(DirEntryTmp) * cap);
  if (!entries) {
    uv_rt_directory_scan_close(find);
    uv_heap_free_raw(wide);
    uv_heap_free_raw(canon);
    return uv_dir_err(UV_IO_FAILURE);
  }

  do {
    const wchar_t* name = data.file_name;
    if (name[0] == L'.' && name[1] == 0) {
      continue;
    }
    if (name[0] == L'.' && name[1] == L'.' && name[2] == 0) {
      continue;
    }

    uint32_t name_len = (uint32_t)uv_wcslen(name);
    wchar_t* name_copy = (wchar_t*)uv_heap_alloc_raw(sizeof(wchar_t) * (name_len + 1));
    if (!name_copy) {
      uv_rt_directory_scan_close(find);
      for (uint32_t i = 0; i < count; ++i) {
        uv_heap_free_raw(entries[i].name_w);
        uv_heap_free_raw(entries[i].name_utf8);
        uv_heap_free_raw(entries[i].key_utf8);
      }
      uv_heap_free_raw(entries);
      uv_heap_free_raw(wide);
      uv_heap_free_raw(canon);
      return uv_dir_err(UV_IO_FAILURE);
    }
    uv_memcpy(name_copy, name, sizeof(wchar_t) * name_len);
    name_copy[name_len] = 0;

    uint32_t name_utf8_len = 0;
    uint8_t* name_utf8 = uv_wide_to_utf8(name_copy, name_len, &name_utf8_len);
    if (!name_utf8) {
      uv_heap_free_raw(name_copy);
      uv_rt_directory_scan_close(find);
      for (uint32_t i = 0; i < count; ++i) {
        uv_heap_free_raw(entries[i].name_w);
        uv_heap_free_raw(entries[i].name_utf8);
        uv_heap_free_raw(entries[i].key_utf8);
      }
      uv_heap_free_raw(entries);
      uv_heap_free_raw(wide);
      uv_heap_free_raw(canon);
      return uv_dir_err(UV_IO_FAILURE);
    }

    uint32_t key_utf8_len = 0;
    uint8_t* key_utf8 = uv_entry_key_utf8(name_copy, name_len, &key_utf8_len);
    if (!key_utf8) {
      key_utf8 = name_utf8;
      key_utf8_len = name_utf8_len;
    }

    if (count == cap) {
      uint32_t new_cap = cap * 2;
      DirEntryTmp* resized = (DirEntryTmp*)uv_heap_alloc_raw(sizeof(DirEntryTmp) * new_cap);
      if (!resized) {
        if (key_utf8 != name_utf8) {
          uv_heap_free_raw(key_utf8);
        }
        uv_heap_free_raw(name_utf8);
        uv_heap_free_raw(name_copy);
        uv_rt_directory_scan_close(find);
        for (uint32_t i = 0; i < count; ++i) {
          uv_heap_free_raw(entries[i].name_w);
          uv_heap_free_raw(entries[i].name_utf8);
          uv_heap_free_raw(entries[i].key_utf8);
        }
        uv_heap_free_raw(entries);
        uv_heap_free_raw(wide);
        uv_heap_free_raw(canon);
        return uv_dir_err(UV_IO_FAILURE);
      }
      for (uint32_t i = 0; i < count; ++i) {
        resized[i] = entries[i];
      }
      uv_heap_free_raw(entries);
      entries = resized;
      cap = new_cap;
    }

    entries[count].name_w = name_copy;
    entries[count].name_w_len = name_len;
    entries[count].name_utf8 = name_utf8;
    entries[count].name_utf8_len = name_utf8_len;
    entries[count].key_utf8 = key_utf8;
    entries[count].key_utf8_len = key_utf8_len;
    entries[count].kind = (data.file_attributes & UV_RT_FILE_ATTRIBUTE_DIRECTORY)
        ? UV_FILE_KIND_DIR
        : UV_FILE_KIND_FILE;
    ++count;
  } while (uv_rt_directory_scan_next(find, &data));

  uv_rt_directory_scan_close(find);

  if (count > 1) {
    uv_sort_entries(entries, count);
  }

  UVDirIterState* state = (UVDirIterState*)uv_heap_alloc_raw(sizeof(UVDirIterState));
  if (!state) {
    for (uint32_t i = 0; i < count; ++i) {
      if (entries[i].key_utf8 != entries[i].name_utf8) {
        uv_heap_free_raw(entries[i].key_utf8);
      }
      uv_heap_free_raw(entries[i].name_utf8);
      uv_heap_free_raw(entries[i].name_w);
    }
    uv_heap_free_raw(entries);
    uv_heap_free_raw(wide);
    uv_heap_free_raw(canon);
    return uv_dir_err(UV_IO_FAILURE);
  }

  state->base_path = wide;
  state->base_len = wide_len;
  state->base_utf8 = canon;
  state->base_utf8_len = canon_len;
  state->count = count;
  state->index = 0;
  state->names = NULL;
  state->name_lens = NULL;
  state->kinds = NULL;

  if (count > 0) {
    state->names = (wchar_t**)uv_heap_alloc_raw(sizeof(wchar_t*) * count);
    state->name_lens = (uint32_t*)uv_heap_alloc_raw(sizeof(uint32_t) * count);
    state->kinds = (uint8_t*)uv_heap_alloc_raw(sizeof(uint8_t) * count);
    if (!state->names || !state->name_lens || !state->kinds) {
      for (uint32_t i = 0; i < count; ++i) {
        if (entries[i].key_utf8 != entries[i].name_utf8) {
          uv_heap_free_raw(entries[i].key_utf8);
        }
        uv_heap_free_raw(entries[i].name_utf8);
        uv_heap_free_raw(entries[i].name_w);
      }
      if (state->names) {
        uv_heap_free_raw(state->names);
      }
      if (state->name_lens) {
        uv_heap_free_raw(state->name_lens);
      }
      if (state->kinds) {
        uv_heap_free_raw(state->kinds);
      }
      uv_heap_free_raw(state);
      uv_heap_free_raw(wide);
      uv_heap_free_raw(canon);
      uv_heap_free_raw(entries);
      return uv_dir_err(UV_IO_FAILURE);
    }

    for (uint32_t i = 0; i < count; ++i) {
      state->names[i] = entries[i].name_w;
      state->name_lens[i] = entries[i].name_w_len;
      state->kinds[i] = entries[i].kind;
      if (entries[i].key_utf8 != entries[i].name_utf8) {
        uv_heap_free_raw(entries[i].key_utf8);
      }
      uv_heap_free_raw(entries[i].name_utf8);
    }
  }

  uv_heap_free_raw(entries);

  uint64_t tracked_id = uv_track_dir_iter_state(state);
  if (tracked_id == 0) {
    uv_free_dir_state(state);
    return uv_dir_err(UV_IO_FAILURE);
  }

  UVUnion_DirIter_IoError out;
  out.disc = 0;
  out.payload.handle = tracked_id;
  return out;
}
UVUnion_Unit_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3acreate_x5fdir(
    const UVDynObject* self,
    const UVStringView* path) {
  SPEC_RULE("Prim-IO-CreateDir");
  uv_trace_emit_rule("Prim-IO-CreateDir");
  UVFsState* io = uv_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!uv_fs_resolve_path(io, path, &canon, &canon_len)) {
    return uv_unit_err(UV_IO_INVALID_PATH);
  }
  wchar_t* wide = uv_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  uv_heap_free_raw(canon);
  if (!wide) {
    return uv_unit_err(UV_IO_INVALID_PATH);
  }
  uv_rt_bool_t ok = uv_rt_directory_create_path_wide(wide, NULL);
  if (!ok) {
    UVIoError err = uv_last_io_error();
    uv_heap_free_raw(wide);
    return uv_unit_err(err);
  }
  uv_heap_free_raw(wide);
  return uv_unit_ok();
}

UVUnion_Unit_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aensure_x5fdir(
    const UVDynObject* self,
    const UVStringView* path) {
  SPEC_RULE("Prim-IO-EnsureDir");
  uv_trace_emit_rule("Prim-IO-EnsureDir");
  UVFsState* io = uv_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!uv_fs_resolve_path(io, path, &canon, &canon_len)) {
    return uv_unit_err(UV_IO_INVALID_PATH);
  }
  uint32_t wide_len = 0;
  wchar_t* wide =
      uv_rt_path_utf8_to_native_wide(canon, canon_len, &wide_len);
  uv_heap_free_raw(canon);
  if (!wide) {
    return uv_unit_err(UV_IO_INVALID_PATH);
  }

  if (wide_len == 0) {
    uv_heap_free_raw(wide);
    return uv_unit_err(UV_IO_INVALID_PATH);
  }

  uv_rt_u32_t attrs = uv_rt_path_attributes_wide(wide);
  if (attrs != UV_RT_FILE_ATTRIBUTES_INVALID) {
    if (attrs & UV_RT_FILE_ATTRIBUTE_DIRECTORY) {
      uv_heap_free_raw(wide);
      return uv_unit_ok();
    }
    uv_heap_free_raw(wide);
    return uv_unit_err(UV_IO_ALREADY_EXISTS);
  }

  wchar_t* buf = (wchar_t*)uv_heap_alloc_raw(sizeof(wchar_t) * (wide_len + 1));
  if (!buf) {
    uv_heap_free_raw(wide);
    return uv_unit_err(UV_IO_FAILURE);
  }
  uv_memcpy(buf, wide, sizeof(wchar_t) * wide_len);
  buf[wide_len] = 0;

  uint32_t start = 0;
  if (wide_len >= 3 && wide[1] == L':' && wide[2] == L'\\') {
    start = 3;
  } else if (wide_len >= 2 && wide[0] == L'\\' && wide[1] == L'\\') {
    uint32_t idx = 2;
    while (idx < wide_len && wide[idx] != L'\\') {
      ++idx;
    }
    if (idx < wide_len) {
      ++idx;
    }
    while (idx < wide_len && wide[idx] != L'\\') {
      ++idx;
    }
    if (idx < wide_len) {
      ++idx;
    }
    start = idx;
  } else if (wide[0] == L'\\') {
    start = 1;
  }

  for (uint32_t i = start; i <= wide_len; ++i) {
    if (i == wide_len || buf[i] == L'\\') {
      wchar_t saved = buf[i];
      buf[i] = 0;
      if (buf[0] != 0) {
        if (!uv_rt_directory_create_path_wide(buf, NULL)) {
          uv_rt_u32_t err = uv_rt_last_error_get();
          if (err == UV_RT_ERROR_ALREADY_EXISTS) {
            uv_rt_u32_t a = uv_rt_path_attributes_wide(buf);
            if (a == UV_RT_FILE_ATTRIBUTES_INVALID || !(a & UV_RT_FILE_ATTRIBUTE_DIRECTORY)) {
              buf[i] = saved;
              uv_heap_free_raw(buf);
              uv_heap_free_raw(wide);
              return uv_unit_err(UV_IO_ALREADY_EXISTS);
            }
          } else {
            UVIoError mapped = uv_map_platform_error(err);
            buf[i] = saved;
            uv_heap_free_raw(buf);
            uv_heap_free_raw(wide);
            return uv_unit_err(mapped);
          }
        }
      }
      buf[i] = saved;
    }
  }

  uv_heap_free_raw(buf);
  uv_heap_free_raw(wide);
  return uv_unit_ok();
}

UVUnion_FileKind_IoError ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3akind(
    const UVDynObject* self,
    const UVStringView* path) {
  SPEC_RULE("Prim-IO-Kind");
  uv_trace_emit_rule("Prim-IO-Kind");
  UVFsState* io = uv_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!uv_fs_resolve_path(io, path, &canon, &canon_len)) {
    UVUnion_FileKind_IoError out = uv_kind_err(UV_IO_INVALID_PATH);
    return out;
  }
  wchar_t* wide = uv_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  uv_heap_free_raw(canon);
  if (!wide) {
    UVUnion_FileKind_IoError out = uv_kind_err(UV_IO_INVALID_PATH);
    return out;
  }

  uv_rt_u32_t attrs = uv_rt_path_attributes_wide(wide);
  uv_heap_free_raw(wide);
  if (attrs == UV_RT_FILE_ATTRIBUTES_INVALID) {
    UVUnion_FileKind_IoError out = uv_kind_err(uv_last_io_error());
    return out;
  }
  if (attrs & UV_RT_FILE_ATTRIBUTE_DIRECTORY) {
    UVUnion_FileKind_IoError out = uv_kind_ok(UV_FILE_KIND_DIR);
    return out;
  }
  {
    UVUnion_FileKind_IoError out = uv_kind_ok(UV_FILE_KIND_FILE);
    return out;
  }
}

UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3arestrict(
    const UVDynObject* self,
    const UVStringView* path) {
  SPEC_RULE("Prim-IO-Restrict");
  uv_trace_emit_rule("Prim-IO-Restrict");
  UVDynObject out;
  out.data = NULL;
  out.vtable = self ? self->vtable : NULL;
  const UVFsState* parent = uv_fs_state(self);

  UVFsState* state = (UVFsState*)uv_heap_alloc_raw(sizeof(UVFsState));
  if (!state) {
    return out;
  }
  state->restricted = 1;
  state->valid = 0;
  state->base_utf8 = NULL;
  state->base_len = 0;

  if (path && path->data) {
    uint32_t base_len = 0;
    uint8_t* base = NULL;
    if (uv_fs_resolve_path(parent, path, &base, &base_len)) {
      state->base_utf8 = base;
      state->base_len = base_len;
      state->valid = 1;
    }
  }

  out.data = state;
  return out;
}
UVUnion_StringManaged_IoError File_x3a_x3aRead_x3a_x3aread_x5fall(
    const UVFileHandle* self) {
  SPEC_RULE("Prim-File-ReadAll");
  uv_trace_emit_rule("Prim-File-ReadAll");
  if (!self) {
    return uv_string_io_err(UV_IO_FAILURE);
  }
  uv_rt_rwlock_lock_exclusive(&g_uv_io_registry_lock);
  UVTrackedFile* tracked = uv_require_open_file_locked(self->handle);
  if (!tracked) {
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return uv_string_io_err(UV_IO_FAILURE);
  }
  UVUnion_StringManaged_IoError out = uv_read_all_string_handle(tracked->handle);
  if (out.disc == 0) {
    tracked->position = tracked->length;
  }
  uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
  return out;
}

UVUnion_BytesManaged_IoError File_x3a_x3aRead_x3a_x3aread_x5fall_x5fbytes(
    const UVFileHandle* self) {
  SPEC_RULE("Prim-File-ReadAllBytes");
  uv_trace_emit_rule("Prim-File-ReadAllBytes");
  if (!self) {
    return uv_bytes_io_err(UV_IO_FAILURE);
  }
  uv_rt_rwlock_lock_exclusive(&g_uv_io_registry_lock);
  UVTrackedFile* tracked = uv_require_open_file_locked(self->handle);
  if (!tracked) {
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return uv_bytes_io_err(UV_IO_FAILURE);
  }
  UVUnion_BytesManaged_IoError out = uv_read_all_bytes_handle(tracked->handle);
  if (out.disc == 0) {
    tracked->position = tracked->length;
  }
  uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
  return out;
}

void File_x3a_x3aRead_x3a_x3aclose(UVFileHandle self) {
  SPEC_RULE("Prim-File-Close-Read");
  uv_trace_emit_rule("Prim-File-Close-Read");
  uv_rt_rwlock_lock_exclusive(&g_uv_io_registry_lock);
  (void)uv_close_tracked_file_locked(self.handle, NULL);
  uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
}

static UVUnion_Unit_IoError uv_file_write_handle(uv_rt_handle_t h,
                                                 const UVBytesView* data) {
  if (!h || h == UV_RT_INVALID_HANDLE) {
    return uv_unit_err(UV_IO_FAILURE);
  }
  uint64_t len = data ? data->len : 0;
  uint64_t written = 0;
  while (written < len) {
    uv_rt_u32_t chunk = 0;
    uv_rt_u32_t to_write = (uv_rt_u32_t)uv_min_u64(len - written, 0x7FFFFFFF);
    if (!uv_rt_handle_write(h, data->data + written, to_write, &chunk)) {
      if (chunk > 0) {
        written += (uint64_t)chunk;
        continue;
      }
      return uv_unit_err(uv_last_io_error());
    }
    if (chunk == 0) {
      break;
    }
    written += (uint64_t)chunk;
  }
  if (written != len) {
    return uv_unit_err(UV_IO_FAILURE);
  }
  return uv_unit_ok();
}

UVUnion_Unit_IoError File_x3a_x3aWrite_x3a_x3awrite(
    UVFileHandle* self,
    const UVBytesView* data) {
  SPEC_RULE("Prim-File-Write");
  uv_trace_emit_rule("Prim-File-Write");
  if (!self) {
    return uv_unit_err(UV_IO_FAILURE);
  }
  uv_rt_rwlock_lock_exclusive(&g_uv_io_registry_lock);
  UVTrackedFile* tracked = uv_require_open_file_locked(self->handle);
  if (!tracked) {
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return uv_unit_err(UV_IO_FAILURE);
  }
  UVUnion_Unit_IoError out = uv_file_write_handle(tracked->handle, data);
  if (out.disc == 0) {
    uint64_t bytes = data ? data->len : 0;
    uint64_t new_pos = tracked->position + bytes;
    tracked->position = new_pos;
    if (new_pos > tracked->length) {
      tracked->length = new_pos;
    }
    tracked->flushed = 0;
  }
  uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
  return out;
}

UVUnion_Unit_IoError File_x3a_x3aWrite_x3a_x3aflush(
    UVFileHandle* self) {
  SPEC_RULE("Prim-File-Flush");
  uv_trace_emit_rule("Prim-File-Flush");
  if (!self) {
    return uv_unit_err(UV_IO_FAILURE);
  }
  uv_rt_rwlock_lock_exclusive(&g_uv_io_registry_lock);
  UVTrackedFile* tracked = uv_require_open_file_locked(self->handle);
  if (!tracked) {
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return uv_unit_err(UV_IO_FAILURE);
  }
  if (!uv_rt_file_sync(tracked->handle)) {
    UVUnion_Unit_IoError out = uv_unit_err(uv_last_io_error());
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return out;
  }
  tracked->flushed = 1;
  uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
  return uv_unit_ok();
}

void File_x3a_x3aWrite_x3a_x3aclose(UVFileHandle self) {
  SPEC_RULE("Prim-File-Close-Write");
  uv_trace_emit_rule("Prim-File-Close-Write");
  uv_rt_rwlock_lock_exclusive(&g_uv_io_registry_lock);
  (void)uv_close_tracked_file_locked(self.handle, NULL);
  uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
}

UVUnion_Unit_IoError File_x3a_x3aAppend_x3a_x3awrite(
    UVFileHandle* self,
    const UVBytesView* data) {
  SPEC_RULE("Prim-File-Write-Append");
  uv_trace_emit_rule("Prim-File-Write-Append");
  if (!self) {
    return uv_unit_err(UV_IO_FAILURE);
  }
  uv_rt_rwlock_lock_exclusive(&g_uv_io_registry_lock);
  UVTrackedFile* tracked = uv_require_open_file_locked(self->handle);
  if (!tracked) {
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return uv_unit_err(UV_IO_FAILURE);
  }
  UVUnion_Unit_IoError out = uv_file_write_handle(tracked->handle, data);
  if (out.disc == 0) {
    uint64_t bytes = data ? data->len : 0;
    uint64_t new_pos = tracked->length + bytes;
    tracked->position = new_pos;
    tracked->length = new_pos;
    tracked->flushed = 0;
  }
  uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
  return out;
}

UVUnion_Unit_IoError File_x3a_x3aAppend_x3a_x3aflush(
    UVFileHandle* self) {
  SPEC_RULE("Prim-File-Flush-Append");
  uv_trace_emit_rule("Prim-File-Flush-Append");
  if (!self) {
    return uv_unit_err(UV_IO_FAILURE);
  }
  uv_rt_rwlock_lock_exclusive(&g_uv_io_registry_lock);
  UVTrackedFile* tracked = uv_require_open_file_locked(self->handle);
  if (!tracked) {
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return uv_unit_err(UV_IO_FAILURE);
  }
  if (!uv_rt_file_sync(tracked->handle)) {
    UVUnion_Unit_IoError out = uv_unit_err(uv_last_io_error());
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return out;
  }
  tracked->flushed = 1;
  uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
  return uv_unit_ok();
}

void File_x3a_x3aAppend_x3a_x3aclose(UVFileHandle self) {
  SPEC_RULE("Prim-File-Close-Append");
  uv_trace_emit_rule("Prim-File-Close-Append");
  uv_rt_rwlock_lock_exclusive(&g_uv_io_registry_lock);
  (void)uv_close_tracked_file_locked(self.handle, NULL);
  uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
}
UVUnion_DirEntry_Unit_IoError DirIter_x3a_x3aOpen_x3a_x3anext(
    UVDirIterHandle* self) {
  SPEC_RULE("Prim-Dir-Next");
  uv_trace_emit_rule("Prim-Dir-Next");
  UVUnion_DirEntry_Unit_IoError out;
  if (!self) {
    out.disc = 1;
    out.payload.io_error = UV_IO_FAILURE;
    return out;
  }
  uv_rt_rwlock_lock_exclusive(&g_uv_io_registry_lock);
  UVTrackedDirIter* tracked = uv_require_open_dir_iter_locked(self->handle);
  if (!tracked) {
    out.disc = 1;
    out.payload.io_error = UV_IO_FAILURE;
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return out;
  }
  UVDirIterState* state = tracked->state;
  if (state->index >= state->count) {
    out.disc = 0;
    out.payload.value.disc = 0;
    out.payload.value.entry = (UVDirEntry){0};
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return out;
  }

  uint32_t idx = state->index;
  wchar_t* name_w = state->names[idx];
  uint32_t name_w_len = state->name_lens[idx];
  uint32_t name_utf8_len = 0;
  uint8_t* name_utf8 = uv_wide_to_utf8(name_w, name_w_len, &name_utf8_len);
  if (!name_utf8 && name_w_len != 0) {
    out.disc = 1;
    out.payload.io_error = UV_IO_FAILURE;
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return out;
  }

  uint32_t path_len = 0;
  uint8_t* path_utf8 = uv_rt_path_join_utf8(
      state->base_utf8, state->base_utf8_len, name_utf8, name_utf8_len,
      &path_len);
  if (!path_utf8 && (state->base_utf8_len != 0 || name_utf8_len != 0)) {
    if (name_utf8) {
      uv_heap_free_raw(name_utf8);
    }
    out.disc = 1;
    out.payload.io_error = UV_IO_FAILURE;
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return out;
  }

  uint8_t* name_managed = NULL;
  uint64_t name_len64 = name_utf8_len;
  if (name_len64 == 0) {
    name_managed = NULL;
  } else {
    name_managed = uv_alloc_managed_bytes(NULL, name_len64, NULL);
  }
  if (name_len64 != 0 && !name_managed) {
    if (name_utf8) {
      uv_heap_free_raw(name_utf8);
    }
    if (path_utf8) {
      uv_heap_free_raw(path_utf8);
    }
    out.disc = 1;
    out.payload.io_error = UV_IO_FAILURE;
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return out;
  }

  uint8_t* path_managed = NULL;
  uint64_t path_len64 = path_len;
  if (path_len64 == 0) {
    path_managed = NULL;
  } else {
    path_managed = uv_alloc_managed_bytes(NULL, path_len64, NULL);
  }
  if (path_len64 != 0 && !path_managed) {
    if (name_managed) {
      uv_free_managed_bytes(name_managed);
    }
    if (name_utf8) {
      uv_heap_free_raw(name_utf8);
    }
    if (path_utf8) {
      uv_heap_free_raw(path_utf8);
    }
    out.disc = 1;
    out.payload.io_error = UV_IO_FAILURE;
    uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
    return out;
  }

  if (name_len64 != 0) {
    uv_memcpy(name_managed, name_utf8, (size_t)name_len64);
  }
  if (path_len64 != 0) {
    uv_memcpy(path_managed, path_utf8, (size_t)path_len64);
  }

  if (name_utf8) {
    uv_heap_free_raw(name_utf8);
  }
  if (path_utf8) {
    uv_heap_free_raw(path_utf8);
  }

  UVDirEntry entry;
  entry.name.data = name_managed;
  entry.name.len = name_len64;
  entry.name.cap = name_len64;
  entry.path.data = path_managed;
  entry.path.len = path_len64;
  entry.path.cap = path_len64;
  entry.kind = state->kinds[idx];

  state->index += 1;

  out.disc = 0;
  out.payload.value.disc = 1;
  out.payload.value.entry = entry;
  uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
  return out;
}

void DirIter_x3a_x3aOpen_x3a_x3aclose(UVDirIterHandle self) {
  SPEC_RULE("Prim-Dir-Close");
  uv_trace_emit_rule("Prim-Dir-Close");
  uv_rt_rwlock_lock_exclusive(&g_uv_io_registry_lock);
  UVTrackedDirIter* tracked = uv_find_tracked_dir_iter_locked(self.handle);
  if (tracked && tracked->state) {
    UVDirIterState* state = tracked->state;
    tracked->state = NULL;
    uv_free_dir_state(state);
  }
  uv_rt_rwlock_unlock_exclusive(&g_uv_io_registry_lock);
}
