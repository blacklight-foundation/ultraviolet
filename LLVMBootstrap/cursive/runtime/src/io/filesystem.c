#include "../internal/rt_internal.h"
#include "../internal/rt_path.h"

#include <unicode/unorm2.h>
#include <unicode/ustring.h>

// C-compatible SPEC_RULE macro (no-op, tracing done via c0_trace_emit_rule)
#ifndef SPEC_RULE
#define SPEC_RULE(id) ((void)0)
#endif

static C0IoError c0_map_platform_error(cursive_rt_u32_t err) {
  switch (err) {
    case CURSIVE_RT_ERROR_FILE_NOT_FOUND:
    case CURSIVE_RT_ERROR_PATH_NOT_FOUND:
    case CURSIVE_RT_ERROR_INVALID_DRIVE:
      return C0_IO_NOTFOUND;
    case CURSIVE_RT_ERROR_ACCESS_DENIED:
    case CURSIVE_RT_ERROR_PRIVILEGE_NOT_HELD:
      return C0_IO_PERMISSION_DENIED;
    case CURSIVE_RT_ERROR_FILE_EXISTS:
    case CURSIVE_RT_ERROR_ALREADY_EXISTS:
      return C0_IO_ALREADY_EXISTS;
    case CURSIVE_RT_ERROR_INVALID_NAME:
    case CURSIVE_RT_ERROR_BAD_PATHNAME:
    case CURSIVE_RT_ERROR_FILENAME_EXCED_RANGE:
    case CURSIVE_RT_ERROR_DIRECTORY:
    case CURSIVE_RT_ERROR_INVALID_PARAMETER:
      return C0_IO_INVALID_PATH;
    case CURSIVE_RT_ERROR_BUSY:
    case CURSIVE_RT_ERROR_SHARING_VIOLATION:
    case CURSIVE_RT_ERROR_LOCK_VIOLATION:
    case CURSIVE_RT_ERROR_PIPE_BUSY:
      return C0_IO_BUSY;
    default:
      return C0_IO_FAILURE;
  }
}

static C0IoError c0_last_io_error(void) {
  cursive_rt_u32_t err = cursive_rt_last_error_get();
  if (err == 0) {
    return C0_IO_FAILURE;
  }
  return c0_map_platform_error(err);
}

static C0FsState* c0_fs_state(const C0DynObject* fs) {
  if (!fs) {
    return NULL;
  }
  return (C0FsState*)fs->data;
}

typedef enum C0TrackedFileStateTag {
  C0_TRACKED_FILE_OPEN_READ = 1,
  C0_TRACKED_FILE_OPEN_WRITE = 2,
  C0_TRACKED_FILE_OPEN_APPEND = 3,
  C0_TRACKED_FILE_CLOSED = 4,
} C0TrackedFileStateTag;

typedef struct C0TrackedFile {
  uint64_t id;
  cursive_rt_handle_t handle;
  uint64_t position;
  uint64_t length;
  uint8_t state;
  uint8_t flushed;
  struct C0TrackedFile* next;
} C0TrackedFile;

typedef struct C0TrackedDirIter {
  uint64_t id;
  C0DirIterState* state;
  struct C0TrackedDirIter* next;
} C0TrackedDirIter;

static cursive_rt_rwlock_t g_c0_io_registry_lock = CURSIVE_RT_RWLOCK_INIT;
static uint64_t g_c0_next_file_id = 1;
static uint64_t g_c0_next_dir_iter_id = 1;
static C0TrackedFile* g_c0_tracked_files = NULL;
static C0TrackedDirIter* g_c0_tracked_dir_iters = NULL;

static uint64_t c0_next_tracked_id(uint64_t* counter) {
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

static uint64_t c0_file_length_handle(cursive_rt_handle_t handle) {
  cursive_rt_file_offset_t size;
  size.quad_part = 0;
  if (!handle || handle == CURSIVE_RT_INVALID_HANDLE) {
    return 0;
  }
  if (!cursive_rt_file_size(handle, &size) || size.quad_part < 0) {
    return 0;
  }
  return (uint64_t)size.quad_part;
}

static C0TrackedFile* c0_find_tracked_file_locked(uint64_t id) {
  C0TrackedFile* it = g_c0_tracked_files;
  while (it) {
    if (it->id == id) {
      return it;
    }
    it = it->next;
  }
  return NULL;
}

static uint64_t c0_track_file_handle(cursive_rt_handle_t handle,
                                     C0TrackedFileStateTag state,
                                     uint64_t position,
                                     uint64_t length) {
  C0TrackedFile* tracked =
      (C0TrackedFile*)c0_heap_alloc_raw(sizeof(C0TrackedFile));
  if (!tracked) {
    return 0;
  }

  cursive_rt_rwlock_lock_exclusive(&g_c0_io_registry_lock);
  tracked->id = c0_next_tracked_id(&g_c0_next_file_id);
  tracked->handle = handle;
  tracked->position = position;
  tracked->length = length;
  tracked->state = (uint8_t)state;
  tracked->flushed = 0;
  tracked->next = g_c0_tracked_files;
  g_c0_tracked_files = tracked;
  cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
  return tracked->id;
}

static void c0_free_dir_state(C0DirIterState* state) {
  if (!state) {
    return;
  }
  if (state->names) {
    for (uint32_t i = 0; i < state->count; ++i) {
      if (state->names[i]) {
        c0_heap_free_raw(state->names[i]);
      }
    }
    c0_heap_free_raw(state->names);
  }
  if (state->name_lens) {
    c0_heap_free_raw(state->name_lens);
  }
  if (state->kinds) {
    c0_heap_free_raw(state->kinds);
  }
  if (state->base_path) {
    c0_heap_free_raw(state->base_path);
  }
  if (state->base_utf8) {
    c0_heap_free_raw(state->base_utf8);
  }
  c0_heap_free_raw(state);
}

static C0TrackedDirIter* c0_find_tracked_dir_iter_locked(uint64_t id) {
  C0TrackedDirIter* it = g_c0_tracked_dir_iters;
  while (it) {
    if (it->id == id) {
      return it;
    }
    it = it->next;
  }
  return NULL;
}

static uint64_t c0_track_dir_iter_state(C0DirIterState* state) {
  C0TrackedDirIter* tracked =
      (C0TrackedDirIter*)c0_heap_alloc_raw(sizeof(C0TrackedDirIter));
  if (!tracked) {
    return 0;
  }

  cursive_rt_rwlock_lock_exclusive(&g_c0_io_registry_lock);
  tracked->id = c0_next_tracked_id(&g_c0_next_dir_iter_id);
  tracked->state = state;
  tracked->next = g_c0_tracked_dir_iters;
  g_c0_tracked_dir_iters = tracked;
  cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
  return tracked->id;
}

static C0TrackedFile* c0_require_open_file_locked(uint64_t id) {
  C0TrackedFile* tracked = c0_find_tracked_file_locked(id);
  if (!tracked) {
    return NULL;
  }
  if (tracked->state == (uint8_t)C0_TRACKED_FILE_CLOSED) {
    return NULL;
  }
  if (!tracked->handle || tracked->handle == CURSIVE_RT_INVALID_HANDLE) {
    return NULL;
  }
  return tracked;
}

static int c0_close_tracked_file_locked(uint64_t id, C0IoError* out_err) {
  if (out_err) {
    *out_err = C0_IO_FAILURE;
  }
  C0TrackedFile* tracked = c0_find_tracked_file_locked(id);
  if (!tracked) {
    return 0;
  }
  if (tracked->state == (uint8_t)C0_TRACKED_FILE_CLOSED) {
    return 0;
  }

  cursive_rt_handle_t handle = tracked->handle;
  tracked->handle = CURSIVE_RT_INVALID_HANDLE;
  tracked->state = (uint8_t)C0_TRACKED_FILE_CLOSED;
  tracked->flushed = 0;
  if (!handle || handle == CURSIVE_RT_INVALID_HANDLE) {
    return 0;
  }
  if (!cursive_rt_handle_release(handle)) {
    if (out_err) {
      *out_err = c0_last_io_error();
    }
    return 0;
  }
  return 1;
}

static C0TrackedDirIter* c0_require_open_dir_iter_locked(uint64_t id) {
  C0TrackedDirIter* tracked = c0_find_tracked_dir_iter_locked(id);
  if (!tracked) {
    return NULL;
  }
  if (!tracked->state) {
    return NULL;
  }
  return tracked;
}

static int c0_is_ascii_wide(const wchar_t* text, uint32_t len) {
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

static uint8_t* c0_ascii_casefold_utf8_from_wide(const wchar_t* text,
                                                 uint32_t len,
                                                 uint32_t* out_len) {
  uint8_t* folded = (uint8_t*)c0_heap_alloc_raw((size_t)len + 1u);
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

static UChar* c0_wide_to_uchar(const wchar_t* text,
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

  converted = (UChar*)c0_heap_alloc_raw(sizeof(UChar) * (size_t)(converted_len + 1));
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
    c0_heap_free_raw(converted);
    return NULL;
  }
  converted[converted_len] = 0;
  if (out_len) {
    *out_len = converted_len;
  }
  return converted;
}

static int c0_fs_resolve_path(const C0FsState* fs,
                              const C0StringView* path,
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
  if (!c0_utf8_valid(path->data, path->len)) {
    return 0;
  }
  if (c0_utf8_has_null(path->data, path->len)) {
    return 0;
  }

  if (fs && fs->restricted) {
    if (!fs->valid) {
      return 0;
    }
    if (cursive_rt_path_is_absolute_utf8(path->data, path->len)) {
      return 0;
    }
    uint32_t joined_len = 0;
    uint8_t* joined = cursive_rt_path_join_utf8(fs->base_utf8, fs->base_len,
                                                path->data, path->len,
                                                &joined_len);
    if (!joined && (fs->base_len != 0 || path->len != 0)) {
      return 0;
    }
    uint32_t canon_len = 0;
    uint8_t* canon = cursive_rt_path_canonicalize_utf8(
        joined ? joined : path->data, joined ? joined_len : path->len,
        &canon_len);
    if (joined) {
      c0_heap_free_raw(joined);
    }
    if (!canon) {
      return 0;
    }
    if (!cursive_rt_path_has_prefix_utf8(canon, canon_len, fs->base_utf8,
                                         fs->base_len)) {
      c0_heap_free_raw(canon);
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
      cursive_rt_path_canonicalize_utf8(path->data, path->len, &canon_len);
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

static C0Union_File_IoError c0_file_err(C0IoError err) {
  C0Union_File_IoError out;
  out.disc = 1;
  out.payload.io_error = err;
  return out;
}

static C0Union_DirIter_IoError c0_dir_err(C0IoError err) {
  C0Union_DirIter_IoError out;
  out.disc = 1;
  out.payload.io_error = err;
  return out;
}

static C0Union_Unit_IoError c0_unit_err(C0IoError err) {
  C0Union_Unit_IoError out;
  out.disc = 1;
  out.payload = err;
  return out;
}

static C0Union_Unit_IoError c0_unit_ok(void) {
  C0Union_Unit_IoError out;
  out.disc = 0;
  out.payload = 0;
  return out;
}

static C0Union_FileKind_IoError c0_kind_err(C0IoError err) {
  C0Union_FileKind_IoError out;
  out.disc = 1;
  out.payload = err;
  return out;
}

static C0Union_FileKind_IoError c0_kind_ok(C0FileKind kind) {
  C0Union_FileKind_IoError out;
  out.disc = 0;
  out.payload = kind;
  return out;
}

static C0Union_StringManaged_IoError c0_string_io_err(C0IoError err) {
  C0Union_StringManaged_IoError out;
  out.disc = 1;
  out.payload.io_error = err;
  return out;
}

static C0Union_BytesManaged_IoError c0_bytes_io_err(C0IoError err) {
  C0Union_BytesManaged_IoError out;
  out.disc = 1;
  out.payload.io_error = err;
  return out;
}
static C0Union_BytesManaged_IoError c0_read_all_bytes_handle(cursive_rt_handle_t handle) {
  if (!handle || handle == CURSIVE_RT_INVALID_HANDLE) {
    return c0_bytes_io_err(C0_IO_FAILURE);
  }

  cursive_rt_file_offset_t size;
  if (!cursive_rt_file_size(handle, &size)) {
    return c0_bytes_io_err(c0_last_io_error());
  }
  if (size.quad_part < 0) {
    return c0_bytes_io_err(C0_IO_FAILURE);
  }
  if ((uint64_t)size.quad_part > (uint64_t)SIZE_MAX) {
    return c0_bytes_io_err(C0_IO_FAILURE);
  }

  uint64_t len = (uint64_t)size.quad_part;
  if (len == 0) {
    C0Union_BytesManaged_IoError out;
    out.disc = 0;
    out.payload.value.data = NULL;
    out.payload.value.len = 0;
    out.payload.value.cap = 0;
    return out;
  }

  uint8_t* data = c0_alloc_managed_bytes(NULL, len, NULL);
  if (!data) {
    return c0_bytes_io_err(C0_IO_FAILURE);
  }

  uint64_t total = 0;
  while (total < len) {
    cursive_rt_u32_t chunk = 0;
    cursive_rt_u32_t to_read = (cursive_rt_u32_t)c0_min_u64(len - total, 0x7FFFFFFF);
    if (!cursive_rt_handle_read(handle, data + total, to_read, &chunk)) {
      c0_free_managed_bytes(data);
      return c0_bytes_io_err(c0_last_io_error());
    }
    if (chunk == 0) {
      break;
    }
    total += (uint64_t)chunk;
  }

  if (total != len) {
    c0_free_managed_bytes(data);
    return c0_bytes_io_err(C0_IO_FAILURE);
  }

  C0Union_BytesManaged_IoError out;
  out.disc = 0;
  out.payload.value.data = data;
  out.payload.value.len = len;
  out.payload.value.cap = len;
  return out;
}

static C0Union_StringManaged_IoError c0_read_all_string_handle(cursive_rt_handle_t handle) {
  C0Union_BytesManaged_IoError bytes = c0_read_all_bytes_handle(handle);
  if (bytes.disc == 1) {
    return c0_string_io_err(bytes.payload.io_error);
  }

  if (!c0_utf8_valid(bytes.payload.value.data, bytes.payload.value.len)) {
    c0_free_managed_bytes(bytes.payload.value.data);
    return c0_string_io_err(C0_IO_FAILURE);
  }

  C0Union_StringManaged_IoError out;
  out.disc = 0;
  out.payload.value.data = bytes.payload.value.data;
  out.payload.value.len = bytes.payload.value.len;
  out.payload.value.cap = bytes.payload.value.cap;
  return out;
}
C0Union_File_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fread(
    const C0DynObject* self,
    const C0StringView* path) {
  SPEC_RULE("Prim-FS-OpenRead");
  c0_trace_emit_rule("Prim-FS-OpenRead");
  C0FsState* fs = c0_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!c0_fs_resolve_path(fs, path, &canon, &canon_len)) {
    return c0_file_err(C0_IO_INVALID_PATH);
  }
  wchar_t* wide = cursive_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  c0_heap_free_raw(canon);
  if (!wide) {
    return c0_file_err(C0_IO_INVALID_PATH);
  }

  cursive_rt_handle_t h = cursive_rt_file_open_path_wide(
      wide,
      CURSIVE_RT_FILE_ACCESS_READ,
      CURSIVE_RT_FILE_SHARE_READ | CURSIVE_RT_FILE_SHARE_WRITE |
          CURSIVE_RT_FILE_SHARE_DELETE,
      NULL,
      CURSIVE_RT_FILE_OPEN_EXISTING,
      CURSIVE_RT_FILE_ATTRIBUTE_NORMAL,
      NULL);
  c0_heap_free_raw(wide);
  if (h == CURSIVE_RT_INVALID_HANDLE) {
    return c0_file_err(c0_last_io_error());
  }

  uint64_t tracked_id =
      c0_track_file_handle(h,
                           C0_TRACKED_FILE_OPEN_READ,
                           0,
                           c0_file_length_handle(h));
  if (tracked_id == 0) {
    cursive_rt_handle_release(h);
    return c0_file_err(C0_IO_FAILURE);
  }

  C0Union_File_IoError out;
  out.disc = 0;
  out.payload.handle = tracked_id;
  return out;
}

C0Union_File_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fwrite(
    const C0DynObject* self,
    const C0StringView* path) {
  SPEC_RULE("Prim-FS-OpenWrite");
  c0_trace_emit_rule("Prim-FS-OpenWrite");
  C0FsState* fs = c0_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!c0_fs_resolve_path(fs, path, &canon, &canon_len)) {
    return c0_file_err(C0_IO_INVALID_PATH);
  }
  wchar_t* wide = cursive_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  c0_heap_free_raw(canon);
  if (!wide) {
    return c0_file_err(C0_IO_INVALID_PATH);
  }

  cursive_rt_handle_t h = cursive_rt_file_open_path_wide(
      wide,
      CURSIVE_RT_FILE_ACCESS_WRITE,
      CURSIVE_RT_FILE_SHARE_READ,
      NULL,
      CURSIVE_RT_FILE_OPEN_EXISTING,
      CURSIVE_RT_FILE_ATTRIBUTE_NORMAL,
      NULL);
  c0_heap_free_raw(wide);
  if (h == CURSIVE_RT_INVALID_HANDLE) {
    return c0_file_err(c0_last_io_error());
  }

  uint64_t tracked_id =
      c0_track_file_handle(h,
                           C0_TRACKED_FILE_OPEN_WRITE,
                           0,
                           c0_file_length_handle(h));
  if (tracked_id == 0) {
    cursive_rt_handle_release(h);
    return c0_file_err(C0_IO_FAILURE);
  }

  C0Union_File_IoError out;
  out.disc = 0;
  out.payload.handle = tracked_id;
  return out;
}

C0Union_File_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fappend(
    const C0DynObject* self,
    const C0StringView* path) {
  SPEC_RULE("Prim-FS-OpenAppend");
  c0_trace_emit_rule("Prim-FS-OpenAppend");
  C0FsState* fs = c0_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!c0_fs_resolve_path(fs, path, &canon, &canon_len)) {
    return c0_file_err(C0_IO_INVALID_PATH);
  }
  wchar_t* wide = cursive_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  c0_heap_free_raw(canon);
  if (!wide) {
    return c0_file_err(C0_IO_INVALID_PATH);
  }

  cursive_rt_handle_t h = cursive_rt_file_open_path_wide(
      wide,
      CURSIVE_RT_FILE_ACCESS_APPEND | CURSIVE_RT_FILE_ACCESS_WRITE,
      CURSIVE_RT_FILE_SHARE_READ,
      NULL,
      CURSIVE_RT_FILE_OPEN_EXISTING,
      CURSIVE_RT_FILE_ATTRIBUTE_NORMAL,
      NULL);
  c0_heap_free_raw(wide);
  if (h == CURSIVE_RT_INVALID_HANDLE) {
    return c0_file_err(c0_last_io_error());
  }
  cursive_rt_file_offset_t eof;
  eof.quad_part = 0;
  if (!cursive_rt_file_seek(h, eof, NULL, CURSIVE_RT_FILE_SEEK_END)) {
    C0IoError err = c0_last_io_error();
    cursive_rt_handle_release(h);
    return c0_file_err(err);
  }

  uint64_t length = c0_file_length_handle(h);
  uint64_t tracked_id =
      c0_track_file_handle(h,
                           C0_TRACKED_FILE_OPEN_APPEND,
                           length,
                           length);
  if (tracked_id == 0) {
    cursive_rt_handle_release(h);
    return c0_file_err(C0_IO_FAILURE);
  }

  C0Union_File_IoError out;
  out.disc = 0;
  out.payload.handle = tracked_id;
  return out;
}

C0Union_File_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3acreate_x5fwrite(
    const C0DynObject* self,
    const C0StringView* path) {
  SPEC_RULE("Prim-FS-CreateWrite");
  c0_trace_emit_rule("Prim-FS-CreateWrite");
  C0FsState* fs = c0_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!c0_fs_resolve_path(fs, path, &canon, &canon_len)) {
    return c0_file_err(C0_IO_INVALID_PATH);
  }
  wchar_t* wide = cursive_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  c0_heap_free_raw(canon);
  if (!wide) {
    return c0_file_err(C0_IO_INVALID_PATH);
  }

  cursive_rt_handle_t h = cursive_rt_file_open_path_wide(
      wide,
      CURSIVE_RT_FILE_ACCESS_WRITE,
      CURSIVE_RT_FILE_SHARE_READ,
      NULL,
      CURSIVE_RT_FILE_OPEN_CREATE_NEW,
      CURSIVE_RT_FILE_ATTRIBUTE_NORMAL,
      NULL);
  c0_heap_free_raw(wide);
  if (h == CURSIVE_RT_INVALID_HANDLE) {
    return c0_file_err(c0_last_io_error());
  }

  uint64_t tracked_id =
      c0_track_file_handle(h,
                           C0_TRACKED_FILE_OPEN_WRITE,
                           0,
                           0);
  if (tracked_id == 0) {
    cursive_rt_handle_release(h);
    return c0_file_err(C0_IO_FAILURE);
  }

  C0Union_File_IoError out;
  out.disc = 0;
  out.payload.handle = tracked_id;
  return out;
}
void cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aread_x5ffile(
    C0Union_StringManaged_IoError* out,
    const C0DynObject* self,
    const C0StringView* path) {
  SPEC_RULE("Prim-FS-ReadFile");
  c0_trace_emit_rule("Prim-FS-ReadFile");
  if (!out) {
    return;
  }
  C0Union_File_IoError file = cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fread(self, path);
  if (file.disc == 1) {
    *out = c0_string_io_err(file.payload.io_error);
    return;
  }
  C0FileHandle handle;
  handle.handle = file.payload.handle;
  C0Union_StringManaged_IoError result =
      File_x3a_x3aRead_x3a_x3aread_x5fall(&handle);
  File_x3a_x3aRead_x3a_x3aclose(handle);
  *out = result;
}

void cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aread_x5fbytes(
    C0Union_BytesManaged_IoError* out,
    const C0DynObject* self,
    const C0StringView* path) {
  SPEC_RULE("Prim-FS-ReadBytes");
  c0_trace_emit_rule("Prim-FS-ReadBytes");
  if (!out) {
    return;
  }
  C0Union_File_IoError file = cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fread(self, path);
  if (file.disc == 1) {
    *out = c0_bytes_io_err(file.payload.io_error);
    return;
  }
  C0FileHandle handle;
  handle.handle = file.payload.handle;
  C0Union_BytesManaged_IoError result =
      File_x3a_x3aRead_x3a_x3aread_x5fall_x5fbytes(&handle);
  File_x3a_x3aRead_x3a_x3aclose(handle);
  *out = result;
}

C0Union_Unit_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3awrite_x5ffile(
    const C0DynObject* self,
    const C0StringView* path,
    const C0BytesView* data) {
  SPEC_RULE("Prim-FS-WriteFile");
  c0_trace_emit_rule("Prim-FS-WriteFile");
  C0FsState* fs = c0_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!c0_fs_resolve_path(fs, path, &canon, &canon_len)) {
    C0Union_Unit_IoError out = c0_unit_err(C0_IO_INVALID_PATH);
    return out;
  }
  wchar_t* wide = cursive_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  c0_heap_free_raw(canon);
  if (!wide) {
    C0Union_Unit_IoError out = c0_unit_err(C0_IO_INVALID_PATH);
    return out;
  }

  cursive_rt_handle_t h = cursive_rt_file_open_path_wide(
      wide,
      CURSIVE_RT_FILE_ACCESS_WRITE,
      CURSIVE_RT_FILE_SHARE_READ,
      NULL,
      CURSIVE_RT_FILE_OPEN_REPLACE_ALWAYS,
      CURSIVE_RT_FILE_ATTRIBUTE_NORMAL,
      NULL);
  c0_heap_free_raw(wide);
  if (h == CURSIVE_RT_INVALID_HANDLE) {
    C0Union_Unit_IoError out = c0_unit_err(c0_last_io_error());
    return out;
  }

  uint64_t len = data ? data->len : 0;
  uint64_t written = 0;
  while (written < len) {
    cursive_rt_u32_t chunk = 0;
    cursive_rt_u32_t to_write = (cursive_rt_u32_t)c0_min_u64(len - written, 0x7FFFFFFF);
    if (!cursive_rt_handle_write(h, data->data + written, to_write, &chunk)) {
      cursive_rt_handle_release(h);
      C0Union_Unit_IoError out = c0_unit_err(c0_last_io_error());
      return out;
    }
    if (chunk == 0) {
      break;
    }
    written += (uint64_t)chunk;
  }

  cursive_rt_handle_release(h);
  if (written != len) {
    C0Union_Unit_IoError out = c0_unit_err(C0_IO_FAILURE);
    return out;
  }
  {
    C0Union_Unit_IoError out = c0_unit_ok();
    return out;
  }
}

static cursive_rt_handle_t c0_open_console_out(void) {
  return cursive_rt_file_open_path_wide(L"CONOUT$",
                                        CURSIVE_RT_FILE_ACCESS_WRITE,
                                        CURSIVE_RT_FILE_SHARE_READ |
                                            CURSIVE_RT_FILE_SHARE_WRITE,
                                        NULL,
                                        CURSIVE_RT_FILE_OPEN_EXISTING,
                                        CURSIVE_RT_FILE_ATTRIBUTE_NORMAL,
                                        NULL);
}

static C0Union_Unit_IoError c0_write_stream_utf8(cursive_rt_u32_t std_handle_id,
                                                  const C0StringView* data) {
  uint64_t len = data ? data->len : 0;
  cursive_rt_handle_t h = cursive_rt_std_stream(std_handle_id);
  int close_handle = 0;

  if (!h || h == CURSIVE_RT_INVALID_HANDLE) {
    h = c0_open_console_out();
    close_handle = 1;
  }
  if (!h || h == CURSIVE_RT_INVALID_HANDLE) {
    return c0_unit_err(C0_IO_FAILURE);
  }

  uint64_t written = 0;
  while (written < len) {
    cursive_rt_u32_t chunk = 0;
    cursive_rt_u32_t to_write = (cursive_rt_u32_t)c0_min_u64(len - written, 0x7FFFFFFF);

    if (!cursive_rt_handle_write(h, data->data + written, to_write, &chunk)) {
      cursive_rt_u32_t mode = 0;
      if (cursive_rt_console_mode_get(h, &mode)) {
        if (!cursive_rt_console_write_utf8(h,
                           (const char*)(data->data + written),
                           to_write,
                           &chunk)) {
          cursive_rt_u32_t err = cursive_rt_last_error_get();
          if (close_handle) {
            cursive_rt_handle_release(h);
          }
          return c0_unit_err(c0_map_platform_error(err));
        }
      } else {
        cursive_rt_u32_t err = cursive_rt_last_error_get();
        if (close_handle) {
          cursive_rt_handle_release(h);
        }
        return c0_unit_err(c0_map_platform_error(err));
      }
    }

    if (chunk == 0) {
      break;
    }
    written += (uint64_t)chunk;
  }

  if (close_handle) {
    cursive_rt_handle_release(h);
  }

  if (written != len) {
    return c0_unit_err(C0_IO_FAILURE);
  }
  return c0_unit_ok();
}

C0Union_Unit_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3awrite_x5fstdout(
    const C0DynObject* self,
    const C0StringView* data) {
  SPEC_RULE("Prim-FS-WriteStdout");
  c0_trace_emit_rule("Prim-FS-WriteStdout");
  (void)self;
  return c0_write_stream_utf8(CURSIVE_RT_STD_STREAM_OUTPUT, data);
}

C0Union_Unit_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3awrite_x5fstderr(
    const C0DynObject* self,
    const C0StringView* data) {
  SPEC_RULE("Prim-FS-WriteStderr");
  c0_trace_emit_rule("Prim-FS-WriteStderr");
  (void)self;
  return c0_write_stream_utf8(CURSIVE_RT_STD_STREAM_ERROR, data);
}


uint8_t cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aexists(
    const C0DynObject* self,
    const C0StringView* path) {
  SPEC_RULE("Prim-FS-Exists");
  c0_trace_emit_rule("Prim-FS-Exists");
  C0FsState* fs = c0_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!c0_fs_resolve_path(fs, path, &canon, &canon_len)) {
    return 0;
  }
  wchar_t* wide = cursive_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  c0_heap_free_raw(canon);
  if (!wide) {
    return 0;
  }
  cursive_rt_u32_t attrs = cursive_rt_path_attributes_wide(wide);
  c0_heap_free_raw(wide);
  if (attrs == CURSIVE_RT_FILE_ATTRIBUTES_INVALID) {
    return 0;
  }
  return 1;
}

C0Union_Unit_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aremove(
    const C0DynObject* self,
    const C0StringView* path) {
  SPEC_RULE("Prim-FS-Remove");
  c0_trace_emit_rule("Prim-FS-Remove");
  C0FsState* fs = c0_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!c0_fs_resolve_path(fs, path, &canon, &canon_len)) {
    return c0_unit_err(C0_IO_INVALID_PATH);
  }
  wchar_t* wide = cursive_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  c0_heap_free_raw(canon);
  if (!wide) {
    return c0_unit_err(C0_IO_INVALID_PATH);
  }
  cursive_rt_u32_t attrs = cursive_rt_path_attributes_wide(wide);
  if (attrs == CURSIVE_RT_FILE_ATTRIBUTES_INVALID) {
    c0_heap_free_raw(wide);
    return c0_unit_err(c0_last_io_error());
  }
  cursive_rt_bool_t ok = CURSIVE_RT_FALSE;
  if (attrs & CURSIVE_RT_FILE_ATTRIBUTE_DIRECTORY) {
    ok = cursive_rt_directory_remove_path_wide(wide);
  } else {
    ok = cursive_rt_file_remove_wide(wide);
  }
  if (!ok) {
    C0IoError err = c0_last_io_error();
    c0_heap_free_raw(wide);
    return c0_unit_err(err);
  }
  c0_heap_free_raw(wide);
  return c0_unit_ok();
}
static uint8_t* c0_entry_key_utf8(const wchar_t* name,
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
    uint8_t* empty = (uint8_t*)c0_heap_alloc_raw(1);
    if (!empty) {
      return NULL;
    }
    empty[0] = 0;
    return empty;
  }

  if (c0_is_ascii_wide(name, name_len)) {
    return c0_ascii_casefold_utf8_from_wide(name, name_len, out_len);
  }

  cursive_rt_icu_data_configure();
  UErrorCode status = U_ZERO_ERROR;
  const UNormalizer2* nfc = unorm2_getNFCInstance(&status);
  if (U_FAILURE(status) || !nfc) {
    return NULL;
  }

  name_utf16 = c0_wide_to_uchar(name, name_len, &name_utf16_len);
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
    c0_heap_free_raw(name_utf16);
    return NULL;
  }
  status = U_ZERO_ERROR;
  UChar* normalized =
      (UChar*)c0_heap_alloc_raw(sizeof(UChar) * (size_t)(nfc_len + 1));
  if (!normalized) {
    c0_heap_free_raw(name_utf16);
    return NULL;
  }
  nfc_len = unorm2_normalize(nfc,
                             name_utf16,
                             name_utf16_len,
                             normalized,
                             nfc_len + 1,
                             &status);
  c0_heap_free_raw(name_utf16);
  if (U_FAILURE(status)) {
    c0_heap_free_raw(normalized);
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
    c0_heap_free_raw(normalized);
    return NULL;
  }
  status = U_ZERO_ERROR;
  UChar* folded = (UChar*)c0_heap_alloc_raw(sizeof(UChar) * (size_t)(fold_len + 1));
  if (!folded) {
    c0_heap_free_raw(normalized);
    return NULL;
  }
  fold_len = u_strFoldCase(folded,
                           fold_len + 1,
                           normalized,
                           nfc_len,
                           U_FOLD_CASE_DEFAULT,
                           &status);
  c0_heap_free_raw(normalized);
  if (U_FAILURE(status)) {
    c0_heap_free_raw(folded);
    return NULL;
  }
  folded[fold_len] = 0;

  status = U_ZERO_ERROR;
  int32_t utf8_len = 0;
  u_strToUTF8(NULL, 0, &utf8_len, folded, fold_len, &status);
  if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
    c0_heap_free_raw(folded);
    return NULL;
  }
  status = U_ZERO_ERROR;
  uint8_t* key_utf8 = (uint8_t*)c0_heap_alloc_raw((size_t)utf8_len + 1u);
  if (!key_utf8) {
    c0_heap_free_raw(folded);
    return NULL;
  }
  u_strToUTF8((char*)key_utf8,
              utf8_len + 1,
              &utf8_len,
              folded,
              fold_len,
              &status);
  c0_heap_free_raw(folded);
  if (U_FAILURE(status)) {
    c0_heap_free_raw(key_utf8);
    return NULL;
  }
  key_utf8[utf8_len] = 0;
  if (out_len) {
    *out_len = (uint32_t)utf8_len;
  }
  return key_utf8;
}

static int c0_lex_bytes(const uint8_t* a, uint32_t alen,
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

static int c0_entry_cmp(const DirEntryTmp* a, const DirEntryTmp* b) {
  int key_cmp = c0_lex_bytes(a->key_utf8, a->key_utf8_len,
                             b->key_utf8, b->key_utf8_len);
  if (key_cmp != 0) {
    return key_cmp;
  }
  return c0_lex_bytes(a->name_utf8, a->name_utf8_len,
                      b->name_utf8, b->name_utf8_len);
}

static void c0_sort_entries(DirEntryTmp* entries, uint32_t count) {
  for (uint32_t i = 1; i < count; ++i) {
    DirEntryTmp tmp = entries[i];
    uint32_t j = i;
    while (j > 0 && c0_entry_cmp(&tmp, &entries[j - 1]) < 0) {
      entries[j] = entries[j - 1];
      --j;
    }
    entries[j] = tmp;
  }
}
C0Union_DirIter_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fdir(
    const C0DynObject* self,
    const C0StringView* path) {
  SPEC_RULE("Prim-FS-OpenDir");
  c0_trace_emit_rule("Prim-FS-OpenDir");
  C0FsState* fs = c0_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!c0_fs_resolve_path(fs, path, &canon, &canon_len)) {
    return c0_dir_err(C0_IO_INVALID_PATH);
  }
  uint32_t wide_len = 0;
  wchar_t* wide =
      cursive_rt_path_utf8_to_native_wide(canon, canon_len, &wide_len);
  if (!wide) {
    c0_heap_free_raw(canon);
    return c0_dir_err(C0_IO_INVALID_PATH);
  }

  cursive_rt_u32_t attrs = cursive_rt_path_attributes_wide(wide);
  if (attrs == CURSIVE_RT_FILE_ATTRIBUTES_INVALID) {
    C0IoError err = c0_last_io_error();
    c0_heap_free_raw(wide);
    c0_heap_free_raw(canon);
    return c0_dir_err(err);
  }
  if ((attrs & CURSIVE_RT_FILE_ATTRIBUTE_DIRECTORY) == 0) {
    c0_heap_free_raw(wide);
    c0_heap_free_raw(canon);
    return c0_dir_err(C0_IO_INVALID_PATH);
  }

  uint32_t pattern_len = wide_len + 2;
  wchar_t* pattern = (wchar_t*)c0_heap_alloc_raw(sizeof(wchar_t) * (pattern_len + 1));
  if (!pattern) {
    c0_heap_free_raw(wide);
    c0_heap_free_raw(canon);
    return c0_dir_err(C0_IO_FAILURE);
  }
  c0_memcpy(pattern, wide, sizeof(wchar_t) * wide_len);
  if (wide_len > 0 && wide[wide_len - 1] != L'\\') {
    pattern[wide_len] = L'\\';
    pattern[wide_len + 1] = L'*';
    pattern[wide_len + 2] = 0;
  } else {
    pattern[wide_len] = L'*';
    pattern[wide_len + 1] = 0;
  }

  cursive_rt_find_data_t data;
  cursive_rt_handle_t find = cursive_rt_directory_scan_first_wide(pattern, &data);
  c0_heap_free_raw(pattern);
  if (find == CURSIVE_RT_INVALID_HANDLE) {
    C0IoError err = c0_last_io_error();
    c0_heap_free_raw(wide);
    c0_heap_free_raw(canon);
    return c0_dir_err(err);
  }

  uint32_t cap = 16;
  uint32_t count = 0;
  DirEntryTmp* entries = (DirEntryTmp*)c0_heap_alloc_raw(sizeof(DirEntryTmp) * cap);
  if (!entries) {
    cursive_rt_directory_scan_close(find);
    c0_heap_free_raw(wide);
    c0_heap_free_raw(canon);
    return c0_dir_err(C0_IO_FAILURE);
  }

  do {
    const wchar_t* name = data.file_name;
    if (name[0] == L'.' && name[1] == 0) {
      continue;
    }
    if (name[0] == L'.' && name[1] == L'.' && name[2] == 0) {
      continue;
    }

    uint32_t name_len = (uint32_t)c0_wcslen(name);
    wchar_t* name_copy = (wchar_t*)c0_heap_alloc_raw(sizeof(wchar_t) * (name_len + 1));
    if (!name_copy) {
      cursive_rt_directory_scan_close(find);
      for (uint32_t i = 0; i < count; ++i) {
        c0_heap_free_raw(entries[i].name_w);
        c0_heap_free_raw(entries[i].name_utf8);
        c0_heap_free_raw(entries[i].key_utf8);
      }
      c0_heap_free_raw(entries);
      c0_heap_free_raw(wide);
      c0_heap_free_raw(canon);
      return c0_dir_err(C0_IO_FAILURE);
    }
    c0_memcpy(name_copy, name, sizeof(wchar_t) * name_len);
    name_copy[name_len] = 0;

    uint32_t name_utf8_len = 0;
    uint8_t* name_utf8 = c0_wide_to_utf8(name_copy, name_len, &name_utf8_len);
    if (!name_utf8) {
      c0_heap_free_raw(name_copy);
      cursive_rt_directory_scan_close(find);
      for (uint32_t i = 0; i < count; ++i) {
        c0_heap_free_raw(entries[i].name_w);
        c0_heap_free_raw(entries[i].name_utf8);
        c0_heap_free_raw(entries[i].key_utf8);
      }
      c0_heap_free_raw(entries);
      c0_heap_free_raw(wide);
      c0_heap_free_raw(canon);
      return c0_dir_err(C0_IO_FAILURE);
    }

    uint32_t key_utf8_len = 0;
    uint8_t* key_utf8 = c0_entry_key_utf8(name_copy, name_len, &key_utf8_len);
    if (!key_utf8) {
      key_utf8 = name_utf8;
      key_utf8_len = name_utf8_len;
    }

    if (count == cap) {
      uint32_t new_cap = cap * 2;
      DirEntryTmp* resized = (DirEntryTmp*)c0_heap_alloc_raw(sizeof(DirEntryTmp) * new_cap);
      if (!resized) {
        if (key_utf8 != name_utf8) {
          c0_heap_free_raw(key_utf8);
        }
        c0_heap_free_raw(name_utf8);
        c0_heap_free_raw(name_copy);
        cursive_rt_directory_scan_close(find);
        for (uint32_t i = 0; i < count; ++i) {
          c0_heap_free_raw(entries[i].name_w);
          c0_heap_free_raw(entries[i].name_utf8);
          c0_heap_free_raw(entries[i].key_utf8);
        }
        c0_heap_free_raw(entries);
        c0_heap_free_raw(wide);
        c0_heap_free_raw(canon);
        return c0_dir_err(C0_IO_FAILURE);
      }
      for (uint32_t i = 0; i < count; ++i) {
        resized[i] = entries[i];
      }
      c0_heap_free_raw(entries);
      entries = resized;
      cap = new_cap;
    }

    entries[count].name_w = name_copy;
    entries[count].name_w_len = name_len;
    entries[count].name_utf8 = name_utf8;
    entries[count].name_utf8_len = name_utf8_len;
    entries[count].key_utf8 = key_utf8;
    entries[count].key_utf8_len = key_utf8_len;
    entries[count].kind = (data.file_attributes & CURSIVE_RT_FILE_ATTRIBUTE_DIRECTORY)
        ? C0_FILE_KIND_DIR
        : C0_FILE_KIND_FILE;
    ++count;
  } while (cursive_rt_directory_scan_next(find, &data));

  cursive_rt_directory_scan_close(find);

  if (count > 1) {
    c0_sort_entries(entries, count);
  }

  C0DirIterState* state = (C0DirIterState*)c0_heap_alloc_raw(sizeof(C0DirIterState));
  if (!state) {
    for (uint32_t i = 0; i < count; ++i) {
      if (entries[i].key_utf8 != entries[i].name_utf8) {
        c0_heap_free_raw(entries[i].key_utf8);
      }
      c0_heap_free_raw(entries[i].name_utf8);
      c0_heap_free_raw(entries[i].name_w);
    }
    c0_heap_free_raw(entries);
    c0_heap_free_raw(wide);
    c0_heap_free_raw(canon);
    return c0_dir_err(C0_IO_FAILURE);
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
    state->names = (wchar_t**)c0_heap_alloc_raw(sizeof(wchar_t*) * count);
    state->name_lens = (uint32_t*)c0_heap_alloc_raw(sizeof(uint32_t) * count);
    state->kinds = (uint8_t*)c0_heap_alloc_raw(sizeof(uint8_t) * count);
    if (!state->names || !state->name_lens || !state->kinds) {
      for (uint32_t i = 0; i < count; ++i) {
        if (entries[i].key_utf8 != entries[i].name_utf8) {
          c0_heap_free_raw(entries[i].key_utf8);
        }
        c0_heap_free_raw(entries[i].name_utf8);
        c0_heap_free_raw(entries[i].name_w);
      }
      if (state->names) {
        c0_heap_free_raw(state->names);
      }
      if (state->name_lens) {
        c0_heap_free_raw(state->name_lens);
      }
      if (state->kinds) {
        c0_heap_free_raw(state->kinds);
      }
      c0_heap_free_raw(state);
      c0_heap_free_raw(wide);
      c0_heap_free_raw(canon);
      c0_heap_free_raw(entries);
      return c0_dir_err(C0_IO_FAILURE);
    }

    for (uint32_t i = 0; i < count; ++i) {
      state->names[i] = entries[i].name_w;
      state->name_lens[i] = entries[i].name_w_len;
      state->kinds[i] = entries[i].kind;
      if (entries[i].key_utf8 != entries[i].name_utf8) {
        c0_heap_free_raw(entries[i].key_utf8);
      }
      c0_heap_free_raw(entries[i].name_utf8);
    }
  }

  c0_heap_free_raw(entries);

  uint64_t tracked_id = c0_track_dir_iter_state(state);
  if (tracked_id == 0) {
    c0_free_dir_state(state);
    return c0_dir_err(C0_IO_FAILURE);
  }

  C0Union_DirIter_IoError out;
  out.disc = 0;
  out.payload.handle = tracked_id;
  return out;
}
C0Union_Unit_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3acreate_x5fdir(
    const C0DynObject* self,
    const C0StringView* path) {
  SPEC_RULE("Prim-FS-CreateDir");
  c0_trace_emit_rule("Prim-FS-CreateDir");
  C0FsState* fs = c0_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!c0_fs_resolve_path(fs, path, &canon, &canon_len)) {
    return c0_unit_err(C0_IO_INVALID_PATH);
  }
  wchar_t* wide = cursive_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  c0_heap_free_raw(canon);
  if (!wide) {
    return c0_unit_err(C0_IO_INVALID_PATH);
  }
  cursive_rt_bool_t ok = cursive_rt_directory_create_path_wide(wide, NULL);
  if (!ok) {
    C0IoError err = c0_last_io_error();
    c0_heap_free_raw(wide);
    return c0_unit_err(err);
  }
  c0_heap_free_raw(wide);
  return c0_unit_ok();
}

C0Union_Unit_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aensure_x5fdir(
    const C0DynObject* self,
    const C0StringView* path) {
  SPEC_RULE("Prim-FS-EnsureDir");
  c0_trace_emit_rule("Prim-FS-EnsureDir");
  C0FsState* fs = c0_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!c0_fs_resolve_path(fs, path, &canon, &canon_len)) {
    return c0_unit_err(C0_IO_INVALID_PATH);
  }
  uint32_t wide_len = 0;
  wchar_t* wide =
      cursive_rt_path_utf8_to_native_wide(canon, canon_len, &wide_len);
  c0_heap_free_raw(canon);
  if (!wide) {
    return c0_unit_err(C0_IO_INVALID_PATH);
  }

  if (wide_len == 0) {
    c0_heap_free_raw(wide);
    return c0_unit_err(C0_IO_INVALID_PATH);
  }

  cursive_rt_u32_t attrs = cursive_rt_path_attributes_wide(wide);
  if (attrs != CURSIVE_RT_FILE_ATTRIBUTES_INVALID) {
    if (attrs & CURSIVE_RT_FILE_ATTRIBUTE_DIRECTORY) {
      c0_heap_free_raw(wide);
      return c0_unit_ok();
    }
    c0_heap_free_raw(wide);
    return c0_unit_err(C0_IO_ALREADY_EXISTS);
  }

  wchar_t* buf = (wchar_t*)c0_heap_alloc_raw(sizeof(wchar_t) * (wide_len + 1));
  if (!buf) {
    c0_heap_free_raw(wide);
    return c0_unit_err(C0_IO_FAILURE);
  }
  c0_memcpy(buf, wide, sizeof(wchar_t) * wide_len);
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
        if (!cursive_rt_directory_create_path_wide(buf, NULL)) {
          cursive_rt_u32_t err = cursive_rt_last_error_get();
          if (err == CURSIVE_RT_ERROR_ALREADY_EXISTS) {
            cursive_rt_u32_t a = cursive_rt_path_attributes_wide(buf);
            if (a == CURSIVE_RT_FILE_ATTRIBUTES_INVALID || !(a & CURSIVE_RT_FILE_ATTRIBUTE_DIRECTORY)) {
              buf[i] = saved;
              c0_heap_free_raw(buf);
              c0_heap_free_raw(wide);
              return c0_unit_err(C0_IO_ALREADY_EXISTS);
            }
          } else {
            C0IoError mapped = c0_map_platform_error(err);
            buf[i] = saved;
            c0_heap_free_raw(buf);
            c0_heap_free_raw(wide);
            return c0_unit_err(mapped);
          }
        }
      }
      buf[i] = saved;
    }
  }

  c0_heap_free_raw(buf);
  c0_heap_free_raw(wide);
  return c0_unit_ok();
}

C0Union_FileKind_IoError cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3akind(
    const C0DynObject* self,
    const C0StringView* path) {
  SPEC_RULE("Prim-FS-Kind");
  c0_trace_emit_rule("Prim-FS-Kind");
  C0FsState* fs = c0_fs_state(self);
  uint8_t* canon = NULL;
  uint32_t canon_len = 0;
  if (!c0_fs_resolve_path(fs, path, &canon, &canon_len)) {
    C0Union_FileKind_IoError out = c0_kind_err(C0_IO_INVALID_PATH);
    return out;
  }
  wchar_t* wide = cursive_rt_path_utf8_to_native_wide(canon, canon_len, NULL);
  c0_heap_free_raw(canon);
  if (!wide) {
    C0Union_FileKind_IoError out = c0_kind_err(C0_IO_INVALID_PATH);
    return out;
  }

  cursive_rt_u32_t attrs = cursive_rt_path_attributes_wide(wide);
  c0_heap_free_raw(wide);
  if (attrs == CURSIVE_RT_FILE_ATTRIBUTES_INVALID) {
    C0Union_FileKind_IoError out = c0_kind_err(c0_last_io_error());
    return out;
  }
  if (attrs & CURSIVE_RT_FILE_ATTRIBUTE_DIRECTORY) {
    C0Union_FileKind_IoError out = c0_kind_ok(C0_FILE_KIND_DIR);
    return out;
  }
  {
    C0Union_FileKind_IoError out = c0_kind_ok(C0_FILE_KIND_FILE);
    return out;
  }
}

C0DynObject cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3arestrict(
    const C0DynObject* self,
    const C0StringView* path) {
  SPEC_RULE("Prim-FS-Restrict");
  c0_trace_emit_rule("Prim-FS-Restrict");
  C0DynObject out;
  out.data = NULL;
  out.vtable = self ? self->vtable : NULL;
  const C0FsState* parent = c0_fs_state(self);

  C0FsState* state = (C0FsState*)c0_heap_alloc_raw(sizeof(C0FsState));
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
    if (c0_fs_resolve_path(parent, path, &base, &base_len)) {
      state->base_utf8 = base;
      state->base_len = base_len;
      state->valid = 1;
    }
  }

  out.data = state;
  return out;
}
C0Union_StringManaged_IoError File_x3a_x3aRead_x3a_x3aread_x5fall(
    const C0FileHandle* self) {
  SPEC_RULE("Prim-File-ReadAll");
  c0_trace_emit_rule("Prim-File-ReadAll");
  if (!self) {
    return c0_string_io_err(C0_IO_FAILURE);
  }
  cursive_rt_rwlock_lock_exclusive(&g_c0_io_registry_lock);
  C0TrackedFile* tracked = c0_require_open_file_locked(self->handle);
  if (!tracked) {
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return c0_string_io_err(C0_IO_FAILURE);
  }
  C0Union_StringManaged_IoError out = c0_read_all_string_handle(tracked->handle);
  if (out.disc == 0) {
    tracked->position = tracked->length;
  }
  cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
  return out;
}

C0Union_BytesManaged_IoError File_x3a_x3aRead_x3a_x3aread_x5fall_x5fbytes(
    const C0FileHandle* self) {
  SPEC_RULE("Prim-File-ReadAllBytes");
  c0_trace_emit_rule("Prim-File-ReadAllBytes");
  if (!self) {
    return c0_bytes_io_err(C0_IO_FAILURE);
  }
  cursive_rt_rwlock_lock_exclusive(&g_c0_io_registry_lock);
  C0TrackedFile* tracked = c0_require_open_file_locked(self->handle);
  if (!tracked) {
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return c0_bytes_io_err(C0_IO_FAILURE);
  }
  C0Union_BytesManaged_IoError out = c0_read_all_bytes_handle(tracked->handle);
  if (out.disc == 0) {
    tracked->position = tracked->length;
  }
  cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
  return out;
}

void File_x3a_x3aRead_x3a_x3aclose(C0FileHandle self) {
  SPEC_RULE("Prim-File-Close-Read");
  c0_trace_emit_rule("Prim-File-Close-Read");
  cursive_rt_rwlock_lock_exclusive(&g_c0_io_registry_lock);
  (void)c0_close_tracked_file_locked(self.handle, NULL);
  cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
}

static C0Union_Unit_IoError c0_file_write_handle(cursive_rt_handle_t h,
                                                 const C0BytesView* data) {
  if (!h || h == CURSIVE_RT_INVALID_HANDLE) {
    return c0_unit_err(C0_IO_FAILURE);
  }
  uint64_t len = data ? data->len : 0;
  uint64_t written = 0;
  while (written < len) {
    cursive_rt_u32_t chunk = 0;
    cursive_rt_u32_t to_write = (cursive_rt_u32_t)c0_min_u64(len - written, 0x7FFFFFFF);
    if (!cursive_rt_handle_write(h, data->data + written, to_write, &chunk)) {
      return c0_unit_err(c0_last_io_error());
    }
    if (chunk == 0) {
      break;
    }
    written += (uint64_t)chunk;
  }
  if (written != len) {
    return c0_unit_err(C0_IO_FAILURE);
  }
  return c0_unit_ok();
}

C0Union_Unit_IoError File_x3a_x3aWrite_x3a_x3awrite(
    C0FileHandle* self,
    const C0BytesView* data) {
  SPEC_RULE("Prim-File-Write");
  c0_trace_emit_rule("Prim-File-Write");
  if (!self) {
    return c0_unit_err(C0_IO_FAILURE);
  }
  cursive_rt_rwlock_lock_exclusive(&g_c0_io_registry_lock);
  C0TrackedFile* tracked = c0_require_open_file_locked(self->handle);
  if (!tracked) {
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return c0_unit_err(C0_IO_FAILURE);
  }
  C0Union_Unit_IoError out = c0_file_write_handle(tracked->handle, data);
  if (out.disc == 0) {
    uint64_t bytes = data ? data->len : 0;
    uint64_t new_pos = tracked->position + bytes;
    tracked->position = new_pos;
    if (new_pos > tracked->length) {
      tracked->length = new_pos;
    }
    tracked->flushed = 0;
  }
  cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
  return out;
}

C0Union_Unit_IoError File_x3a_x3aWrite_x3a_x3aflush(
    C0FileHandle* self) {
  SPEC_RULE("Prim-File-Flush");
  c0_trace_emit_rule("Prim-File-Flush");
  if (!self) {
    return c0_unit_err(C0_IO_FAILURE);
  }
  cursive_rt_rwlock_lock_exclusive(&g_c0_io_registry_lock);
  C0TrackedFile* tracked = c0_require_open_file_locked(self->handle);
  if (!tracked) {
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return c0_unit_err(C0_IO_FAILURE);
  }
  if (!cursive_rt_file_sync(tracked->handle)) {
    C0Union_Unit_IoError out = c0_unit_err(c0_last_io_error());
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return out;
  }
  tracked->flushed = 1;
  cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
  return c0_unit_ok();
}

void File_x3a_x3aWrite_x3a_x3aclose(C0FileHandle self) {
  SPEC_RULE("Prim-File-Close-Write");
  c0_trace_emit_rule("Prim-File-Close-Write");
  cursive_rt_rwlock_lock_exclusive(&g_c0_io_registry_lock);
  (void)c0_close_tracked_file_locked(self.handle, NULL);
  cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
}

C0Union_Unit_IoError File_x3a_x3aAppend_x3a_x3awrite(
    C0FileHandle* self,
    const C0BytesView* data) {
  SPEC_RULE("Prim-File-Write-Append");
  c0_trace_emit_rule("Prim-File-Write-Append");
  if (!self) {
    return c0_unit_err(C0_IO_FAILURE);
  }
  cursive_rt_rwlock_lock_exclusive(&g_c0_io_registry_lock);
  C0TrackedFile* tracked = c0_require_open_file_locked(self->handle);
  if (!tracked) {
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return c0_unit_err(C0_IO_FAILURE);
  }
  C0Union_Unit_IoError out = c0_file_write_handle(tracked->handle, data);
  if (out.disc == 0) {
    uint64_t bytes = data ? data->len : 0;
    uint64_t new_pos = tracked->length + bytes;
    tracked->position = new_pos;
    tracked->length = new_pos;
    tracked->flushed = 0;
  }
  cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
  return out;
}

C0Union_Unit_IoError File_x3a_x3aAppend_x3a_x3aflush(
    C0FileHandle* self) {
  SPEC_RULE("Prim-File-Flush-Append");
  c0_trace_emit_rule("Prim-File-Flush-Append");
  if (!self) {
    return c0_unit_err(C0_IO_FAILURE);
  }
  cursive_rt_rwlock_lock_exclusive(&g_c0_io_registry_lock);
  C0TrackedFile* tracked = c0_require_open_file_locked(self->handle);
  if (!tracked) {
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return c0_unit_err(C0_IO_FAILURE);
  }
  if (!cursive_rt_file_sync(tracked->handle)) {
    C0Union_Unit_IoError out = c0_unit_err(c0_last_io_error());
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return out;
  }
  tracked->flushed = 1;
  cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
  return c0_unit_ok();
}

void File_x3a_x3aAppend_x3a_x3aclose(C0FileHandle self) {
  SPEC_RULE("Prim-File-Close-Append");
  c0_trace_emit_rule("Prim-File-Close-Append");
  cursive_rt_rwlock_lock_exclusive(&g_c0_io_registry_lock);
  (void)c0_close_tracked_file_locked(self.handle, NULL);
  cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
}
C0Union_DirEntry_Unit_IoError DirIter_x3a_x3aOpen_x3a_x3anext(
    C0DirIterHandle* self) {
  SPEC_RULE("Prim-Dir-Next");
  c0_trace_emit_rule("Prim-Dir-Next");
  C0Union_DirEntry_Unit_IoError out;
  if (!self) {
    out.disc = 1;
    out.payload.io_error = C0_IO_FAILURE;
    return out;
  }
  cursive_rt_rwlock_lock_exclusive(&g_c0_io_registry_lock);
  C0TrackedDirIter* tracked = c0_require_open_dir_iter_locked(self->handle);
  if (!tracked) {
    out.disc = 1;
    out.payload.io_error = C0_IO_FAILURE;
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return out;
  }
  C0DirIterState* state = tracked->state;
  if (state->index >= state->count) {
    out.disc = 0;
    out.payload.value.disc = 0;
    out.payload.value.entry = (C0DirEntry){0};
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return out;
  }

  uint32_t idx = state->index;
  wchar_t* name_w = state->names[idx];
  uint32_t name_w_len = state->name_lens[idx];
  uint32_t name_utf8_len = 0;
  uint8_t* name_utf8 = c0_wide_to_utf8(name_w, name_w_len, &name_utf8_len);
  if (!name_utf8 && name_w_len != 0) {
    out.disc = 1;
    out.payload.io_error = C0_IO_FAILURE;
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return out;
  }

  uint32_t path_len = 0;
  uint8_t* path_utf8 = cursive_rt_path_join_utf8(
      state->base_utf8, state->base_utf8_len, name_utf8, name_utf8_len,
      &path_len);
  if (!path_utf8 && (state->base_utf8_len != 0 || name_utf8_len != 0)) {
    if (name_utf8) {
      c0_heap_free_raw(name_utf8);
    }
    out.disc = 1;
    out.payload.io_error = C0_IO_FAILURE;
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return out;
  }

  uint8_t* name_managed = NULL;
  uint64_t name_len64 = name_utf8_len;
  if (name_len64 == 0) {
    name_managed = NULL;
  } else {
    name_managed = c0_alloc_managed_bytes(NULL, name_len64, NULL);
  }
  if (name_len64 != 0 && !name_managed) {
    if (name_utf8) {
      c0_heap_free_raw(name_utf8);
    }
    if (path_utf8) {
      c0_heap_free_raw(path_utf8);
    }
    out.disc = 1;
    out.payload.io_error = C0_IO_FAILURE;
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return out;
  }

  uint8_t* path_managed = NULL;
  uint64_t path_len64 = path_len;
  if (path_len64 == 0) {
    path_managed = NULL;
  } else {
    path_managed = c0_alloc_managed_bytes(NULL, path_len64, NULL);
  }
  if (path_len64 != 0 && !path_managed) {
    if (name_managed) {
      c0_free_managed_bytes(name_managed);
    }
    if (name_utf8) {
      c0_heap_free_raw(name_utf8);
    }
    if (path_utf8) {
      c0_heap_free_raw(path_utf8);
    }
    out.disc = 1;
    out.payload.io_error = C0_IO_FAILURE;
    cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
    return out;
  }

  if (name_len64 != 0) {
    c0_memcpy(name_managed, name_utf8, (size_t)name_len64);
  }
  if (path_len64 != 0) {
    c0_memcpy(path_managed, path_utf8, (size_t)path_len64);
  }

  if (name_utf8) {
    c0_heap_free_raw(name_utf8);
  }
  if (path_utf8) {
    c0_heap_free_raw(path_utf8);
  }

  C0DirEntry entry;
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
  cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
  return out;
}

void DirIter_x3a_x3aOpen_x3a_x3aclose(C0DirIterHandle self) {
  SPEC_RULE("Prim-Dir-Close");
  c0_trace_emit_rule("Prim-Dir-Close");
  cursive_rt_rwlock_lock_exclusive(&g_c0_io_registry_lock);
  C0TrackedDirIter* tracked = c0_find_tracked_dir_iter_locked(self.handle);
  if (tracked && tracked->state) {
    C0DirIterState* state = tracked->state;
    tracked->state = NULL;
    c0_free_dir_state(state);
  }
  cursive_rt_rwlock_unlock_exclusive(&g_c0_io_registry_lock);
}
