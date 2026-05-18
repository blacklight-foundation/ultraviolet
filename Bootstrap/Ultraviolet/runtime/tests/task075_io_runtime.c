#include "uv_rt.h"
#include "../src/internal/rt_platform.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int report_failure(const char* label) {
  fprintf(stderr, "TASK-075 runtime regression failed: %s\n", label);
  return 1;
}

static UVStringView sv_from_cstr(const char* text) {
  UVStringView out;
  out.data = (const uint8_t*)text;
  out.len = text ? (uint64_t)strlen(text) : 0;
  return out;
}

static UVBytesView bytes_view_from_cstr(const char* text) {
  UVBytesView out;
  out.data = (const uint8_t*)text;
  out.len = text ? (uint64_t)strlen(text) : 0;
  return out;
}

static char* join_path_utf8(const char* base, const char* leaf) {
  size_t base_len = base ? strlen(base) : 0;
  size_t leaf_len = leaf ? strlen(leaf) : 0;
  const int needs_sep =
      (base_len != 0 && base[base_len - 1] != '/' && base[base_len - 1] != '\\');
  char* out = (char*)malloc(base_len + (size_t)needs_sep + leaf_len + 1);
  if (!out) {
    return NULL;
  }
  if (base_len != 0) {
    memcpy(out, base, base_len);
  }
  if (needs_sep) {
    out[base_len] = '/';
  }
  if (leaf_len != 0) {
    memcpy(out + base_len + (size_t)needs_sep, leaf, leaf_len);
  }
  out[base_len + (size_t)needs_sep + leaf_len] = 0;
  return out;
}

static wchar_t* wide_from_utf8(const char* utf8) {
  int needed;
  wchar_t* wide;
  if (!utf8) {
    return NULL;
  }
  needed = uv_rt_utf8_to_wide_chars(utf8, -1, NULL, 0);
  if (needed <= 0) {
    return NULL;
  }
  wide = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)needed);
  if (!wide) {
    return NULL;
  }
  if (uv_rt_utf8_to_wide_chars(utf8, -1, wide, needed) <= 0) {
    free(wide);
    return NULL;
  }
  return wide;
}

static char* utf8_from_wide(const wchar_t* wide) {
  int needed;
  char* utf8;
  if (!wide) {
    return NULL;
  }
  needed = uv_rt_wide_to_utf8_chars(wide, -1, NULL, 0);
  if (needed <= 0) {
    return NULL;
  }
  utf8 = (char*)malloc((size_t)needed);
  if (!utf8) {
    return NULL;
  }
  if (uv_rt_wide_to_utf8_chars(wide, -1, utf8, needed) <= 0) {
    free(utf8);
    return NULL;
  }
  return utf8;
}

static char* make_temp_dir_utf8(void) {
  wchar_t temp_root[4096];
  wchar_t temp_path[4096];
  if (uv_rt_temp_path_get_wide((uv_rt_dword_t)(sizeof(temp_root) / sizeof(temp_root[0])),
                                    temp_root) == 0) {
    return NULL;
  }
  if (uv_rt_temp_file_name_wide(temp_root, L"c75", 0u, temp_path) == 0u) {
    return NULL;
  }
  if (!uv_rt_file_delete_wide(temp_path)) {
    return NULL;
  }
  if (!uv_rt_directory_create_wide(temp_path, NULL)) {
    return NULL;
  }
  return utf8_from_wide(temp_path);
}

static void delete_path_utf8(const char* utf8_path) {
  wchar_t* wide = wide_from_utf8(utf8_path);
  uv_rt_dword_t attrs;
  if (!wide) {
    return;
  }
  attrs = uv_rt_file_attributes_get_wide(wide);
  if (attrs != UV_PLATFORM_INVALID_FILE_ATTRIBUTES) {
    if ((attrs & UV_PLATFORM_FILE_ATTRIBUTE_DIRECTORY) != 0u) {
      uv_rt_directory_remove_wide(wide);
    } else {
      uv_rt_file_delete_wide(wide);
    }
  }
  free(wide);
}

static int ensure_dir_ok(const UVDynObject* io,
                         const char* utf8_path,
                         const char* label) {
  UVStringView path = sv_from_cstr(utf8_path);
  UVUnion_Unit_IoError result =
      ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aensure_x5fdir(io, &path);
  if (result.disc != 0) {
    return report_failure(label);
  }
  return 0;
}

static int write_file_ok(const UVDynObject* io,
                         const char* utf8_path,
                         const char* payload,
                         const char* label) {
  UVStringView path = sv_from_cstr(utf8_path);
  UVBytesView data = bytes_view_from_cstr(payload);
  UVUnion_Unit_IoError result =
      ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3awrite_x5ffile(io, &path, &data);
  if (result.disc != 0) {
    return report_failure(label);
  }
  return 0;
}

static int expect_file_error(UVUnion_File_IoError result,
                             UVIoError expected,
                             const char* label) {
  if (result.disc != 1 || result.payload.io_error != expected) {
    fprintf(stderr,
            "TASK-075 detail: %s disc=%u actual=%u expected=%u\n",
            label,
            (unsigned)result.disc,
            (unsigned)result.payload.io_error,
            (unsigned)expected);
    return report_failure(label);
  }
  return 0;
}

static int expect_dir_error(UVUnion_DirIter_IoError result,
                            UVIoError expected,
                            const char* label) {
  if (result.disc != 1 || result.payload.io_error != expected) {
    fprintf(stderr,
            "TASK-075 detail: %s disc=%u actual=%u expected=%u\n",
            label,
            (unsigned)result.disc,
            (unsigned)result.payload.io_error,
            (unsigned)expected);
    return report_failure(label);
  }
  return 0;
}

static int expect_unit_error(UVUnion_Unit_IoError result,
                             UVIoError expected,
                             const char* label) {
  if (result.disc != 1 || result.payload != expected) {
    fprintf(stderr,
            "TASK-075 detail: %s disc=%u actual=%u expected=%u\n",
            label,
            (unsigned)result.disc,
            (unsigned)result.payload,
            (unsigned)expected);
    return report_failure(label);
  }
  return 0;
}

static int expect_kind_error(UVUnion_FileKind_IoError result,
                             UVIoError expected,
                             const char* label) {
  if (result.disc != 1 || result.payload != expected) {
    fprintf(stderr,
            "TASK-075 detail: %s disc=%u actual=%u expected=%u\n",
            label,
            (unsigned)result.disc,
            (unsigned)result.payload,
            (unsigned)expected);
    return report_failure(label);
  }
  return 0;
}

static int expect_string_error(UVUnion_StringManaged_IoError result,
                               UVIoError expected,
                               const char* label) {
  if (result.disc != 1 || result.payload.io_error != expected) {
    fprintf(stderr,
            "TASK-075 detail: %s disc=%u actual=%u expected=%u\n",
            label,
            (unsigned)result.disc,
            (unsigned)result.payload.io_error,
            (unsigned)expected);
    return report_failure(label);
  }
  return 0;
}

static int expect_bytes_error(UVUnion_BytesManaged_IoError result,
                              UVIoError expected,
                              const char* label) {
  if (result.disc != 1 || result.payload.io_error != expected) {
    fprintf(stderr,
            "TASK-075 detail: %s disc=%u actual=%u expected=%u\n",
            label,
            (unsigned)result.disc,
            (unsigned)result.payload.io_error,
            (unsigned)expected);
    return report_failure(label);
  }
  return 0;
}

static int expect_next_name(UVDirIterHandle* iter,
                            const char* expected_name,
                            const char* label) {
  UVUnion_DirEntry_Unit_IoError next =
      DirIter_x3a_x3aOpen_x3a_x3anext(iter);
  if (next.disc != 0 || next.payload.value.disc != 1) {
    return report_failure(label);
  }
  if (next.payload.value.entry.kind != UV_FILE_KIND_FILE) {
    return report_failure(label);
  }
  if (next.payload.value.entry.name.len != (uint64_t)strlen(expected_name)) {
    return report_failure(label);
  }
  if (memcmp(next.payload.value.entry.name.data,
             expected_name,
             (size_t)next.payload.value.entry.name.len) != 0) {
    return report_failure(label);
  }
  return 0;
}

static int test_restricted_invalid_and_missing(void) {
  UVContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ultraviolet_x3a_x3aruntime_x3a_x3acontext_x5finit(&ctx);

  char* base = make_temp_dir_utf8();
  char* restricted_root = NULL;
  char* outside_file = NULL;
  if (!base) {
    return report_failure("restricted temp dir");
  }

  restricted_root = join_path_utf8(base, "restricted");
  outside_file = join_path_utf8(base, "outside.txt");
  if (!restricted_root || !outside_file) {
    free(base);
    free(restricted_root);
    free(outside_file);
    return report_failure("restricted path allocation");
  }

  if (ensure_dir_ok(&ctx.io, restricted_root, "restricted root create")) {
    goto fail;
  }
  if (write_file_ok(&ctx.io, outside_file, "outside", "restricted outside seed")) {
    goto fail;
  }

  UVStringView restricted_root_view = sv_from_cstr(restricted_root);
  UVDynObject restricted =
      ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3arestrict(&ctx.io,
                                                        &restricted_root_view);

  {
    UVStringView escape = sv_from_cstr("../outside.txt");
    if (ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aexists(&restricted, &escape) != 0) {
      goto fail_label_escape_exists;
    }
    if (expect_file_error(
            ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fread(&restricted,
                                                                  &escape),
            UV_IO_INVALID_PATH,
            "restricted open_read invalid path")) {
      goto fail;
    }
    if (expect_file_error(
            ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3acreate_x5fwrite(&restricted,
                                                                     &escape),
            UV_IO_INVALID_PATH,
            "restricted create_write invalid path")) {
      goto fail;
    }
  }

  {
    UVStringView missing = sv_from_cstr("missing.txt");
    if (expect_file_error(
            ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fread(&restricted,
                                                                  &missing),
            UV_IO_NOTFOUND,
            "restricted open_read missing")) {
      goto fail;
    }
    if (expect_file_error(
            ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fwrite(&restricted,
                                                                   &missing),
            UV_IO_NOTFOUND,
            "restricted open_write missing")) {
      goto fail;
    }
    if (expect_file_error(
            ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fappend(&restricted,
                                                                    &missing),
            UV_IO_NOTFOUND,
            "restricted open_append missing")) {
      goto fail;
    }
    if (expect_unit_error(
            ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aremove(&restricted, &missing),
            UV_IO_NOTFOUND,
            "restricted remove missing")) {
      goto fail;
    }
    if (expect_dir_error(
            ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fdir(&restricted,
                                                                 &missing),
            UV_IO_NOTFOUND,
            "restricted open_dir missing")) {
      goto fail;
    }
    if (expect_kind_error(
            ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3akind(&restricted, &missing),
            UV_IO_NOTFOUND,
            "restricted kind missing")) {
      goto fail;
    }
  }

  delete_path_utf8(outside_file);
  delete_path_utf8(restricted_root);
  delete_path_utf8(base);
  free(outside_file);
  free(restricted_root);
  free(base);
  return 0;

fail_label_escape_exists:
  report_failure("restricted exists invalid path");
fail:
  delete_path_utf8(outside_file);
  delete_path_utf8(restricted_root);
  delete_path_utf8(base);
  free(outside_file);
  free(restricted_root);
  free(base);
  return 1;
}

static int test_closed_file_handles_reject_further_ops(void) {
  UVContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ultraviolet_x3a_x3aruntime_x3a_x3acontext_x5finit(&ctx);

  char* base = make_temp_dir_utf8();
  char* file_path = NULL;
  if (!base) {
    return report_failure("file temp dir");
  }

  file_path = join_path_utf8(base, "tracked.txt");
  if (!file_path) {
    free(base);
    return report_failure("file path allocation");
  }

  {
    UVStringView path = sv_from_cstr(file_path);
    UVUnion_File_IoError opened =
        ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3acreate_x5fwrite(&ctx.io, &path);
    if (opened.disc != 0) {
      goto fail_label_open_write;
    }
    UVFileHandle writer;
    UVBytesView payload = bytes_view_from_cstr("tracked-write");
    writer.handle = opened.payload.handle;
    if (File_x3a_x3aWrite_x3a_x3awrite(&writer, &payload).disc != 0) {
      goto fail_label_write;
    }
    File_x3a_x3aWrite_x3a_x3aclose(writer);
    if (expect_unit_error(File_x3a_x3aWrite_x3a_x3aflush(&writer),
                          UV_IO_FAILURE,
                          "flush on closed write handle")) {
      goto fail;
    }
    if (expect_unit_error(File_x3a_x3aWrite_x3a_x3awrite(&writer, &payload),
                          UV_IO_FAILURE,
                          "write on closed write handle")) {
      goto fail;
    }
  }

  {
    UVStringView path = sv_from_cstr(file_path);
    UVUnion_File_IoError opened =
        ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fread(&ctx.io, &path);
    if (opened.disc != 0) {
      goto fail_label_open_read;
    }
    UVFileHandle reader;
    reader.handle = opened.payload.handle;
    File_x3a_x3aRead_x3a_x3aclose(reader);
    if (expect_string_error(File_x3a_x3aRead_x3a_x3aread_x5fall(&reader),
                            UV_IO_FAILURE,
                            "read_all on closed read handle")) {
      goto fail;
    }
    if (expect_bytes_error(File_x3a_x3aRead_x3a_x3aread_x5fall_x5fbytes(&reader),
                           UV_IO_FAILURE,
                           "read_all_bytes on closed read handle")) {
      goto fail;
    }
  }

  delete_path_utf8(file_path);
  delete_path_utf8(base);
  free(file_path);
  free(base);
  return 0;

fail_label_open_write:
  report_failure("create_write tracked file");
  goto fail;
fail_label_write:
  report_failure("write tracked file");
  goto fail;
fail_label_open_read:
  report_failure("open_read tracked file");
  goto fail;
fail:
  delete_path_utf8(file_path);
  delete_path_utf8(base);
  free(file_path);
  free(base);
  return 1;
}

static int test_dir_iter_order_and_close_state(void) {
  UVContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ultraviolet_x3a_x3aruntime_x3a_x3acontext_x5finit(&ctx);

  char* base = make_temp_dir_utf8();
  char* dir_path = NULL;
  char* ss_path = NULL;
  char* sharp_s_path = NULL;
  char* st_path = NULL;
  if (!base) {
    return report_failure("dir temp dir");
  }

  dir_path = join_path_utf8(base, "ordered");
  ss_path = join_path_utf8(dir_path, "ss.txt");
  sharp_s_path = join_path_utf8(dir_path, "\xC3\x9F.txt");
  st_path = join_path_utf8(dir_path, "st.txt");
  if (!dir_path || !ss_path || !sharp_s_path || !st_path) {
    free(base);
    free(dir_path);
    free(ss_path);
    free(sharp_s_path);
    free(st_path);
    return report_failure("dir path allocation");
  }

  if (ensure_dir_ok(&ctx.io, dir_path, "ordered dir create")) {
    goto fail;
  }
  if (write_file_ok(&ctx.io, ss_path, "1", "ordered seed ss")) {
    goto fail;
  }
  if (write_file_ok(&ctx.io, sharp_s_path, "2", "ordered seed sharp-s")) {
    goto fail;
  }
  if (write_file_ok(&ctx.io, st_path, "3", "ordered seed st")) {
    goto fail;
  }

  {
    UVStringView dir_view = sv_from_cstr(dir_path);
    UVUnion_DirIter_IoError opened =
        ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fdir(&ctx.io, &dir_view);
    if (opened.disc != 0) {
      goto fail_label_open_dir;
    }
    UVDirIterHandle iter;
    UVUnion_DirEntry_Unit_IoError eof;
    iter.handle = opened.payload.handle;
    if (expect_next_name(&iter, "ss.txt", "ordered first entry")) {
      goto fail;
    }
    if (expect_next_name(&iter, "\xC3\x9F.txt", "ordered second entry")) {
      goto fail;
    }
    if (expect_next_name(&iter, "st.txt", "ordered third entry")) {
      goto fail;
    }
    eof = DirIter_x3a_x3aOpen_x3a_x3anext(&iter);
    if (eof.disc != 0 || eof.payload.value.disc != 0) {
      goto fail_label_dir_eof;
    }
    DirIter_x3a_x3aOpen_x3a_x3aclose(iter);
    eof = DirIter_x3a_x3aOpen_x3a_x3anext(&iter);
    if (eof.disc != 1 || eof.payload.io_error != UV_IO_FAILURE) {
      goto fail_label_dir_closed_next;
    }
  }

  delete_path_utf8(ss_path);
  delete_path_utf8(sharp_s_path);
  delete_path_utf8(st_path);
  delete_path_utf8(dir_path);
  delete_path_utf8(base);
  free(st_path);
  free(sharp_s_path);
  free(ss_path);
  free(dir_path);
  free(base);
  return 0;

fail_label_open_dir:
  report_failure("open_dir ordered");
  goto fail;
fail_label_dir_eof:
  report_failure("dir eof");
  goto fail;
fail_label_dir_closed_next:
  report_failure("dir next after close");
fail:
  delete_path_utf8(ss_path);
  delete_path_utf8(sharp_s_path);
  delete_path_utf8(st_path);
  delete_path_utf8(dir_path);
  delete_path_utf8(base);
  free(st_path);
  free(sharp_s_path);
  free(ss_path);
  free(dir_path);
  free(base);
  return 1;
}

static int test_dir_iter_snapshot_survives_removal(void) {
  UVContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ultraviolet_x3a_x3aruntime_x3a_x3acontext_x5finit(&ctx);

  char* base = make_temp_dir_utf8();
  char* dir_path = NULL;
  char* file_path = NULL;
  if (!base) {
    return report_failure("snapshot temp dir");
  }

  dir_path = join_path_utf8(base, "snapshot");
  file_path = join_path_utf8(dir_path, "entry.txt");
  if (!dir_path || !file_path) {
    free(base);
    free(dir_path);
    free(file_path);
    return report_failure("snapshot path allocation");
  }

  if (ensure_dir_ok(&ctx.io, dir_path, "snapshot dir create")) {
    goto fail;
  }
  if (write_file_ok(&ctx.io, file_path, "snap", "snapshot seed file")) {
    goto fail;
  }

  {
    UVStringView dir_view = sv_from_cstr(dir_path);
    UVStringView file_view = sv_from_cstr(file_path);
    UVUnion_DirIter_IoError opened =
        ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aopen_x5fdir(&ctx.io, &dir_view);
    if (opened.disc != 0) {
      goto fail_label_snapshot_open_dir;
    }
    UVDirIterHandle iter;
    UVUnion_DirEntry_Unit_IoError next;
    iter.handle = opened.payload.handle;
    if (ultraviolet_x3a_x3aruntime_x3a_x3aio_x3a_x3aremove(&ctx.io, &file_view).disc != 0) {
      goto fail_label_snapshot_remove;
    }
    next = DirIter_x3a_x3aOpen_x3a_x3anext(&iter);
    if (next.disc != 0 || next.payload.value.disc != 1) {
      goto fail_label_snapshot_next;
    }
    if (next.payload.value.entry.kind != UV_FILE_KIND_FILE) {
      goto fail_label_snapshot_kind;
    }
    if (next.payload.value.entry.name.len != strlen("entry.txt") ||
        memcmp(next.payload.value.entry.name.data, "entry.txt", strlen("entry.txt")) != 0) {
      goto fail_label_snapshot_name;
    }
    UVUnion_DirEntry_Unit_IoError eof =
        DirIter_x3a_x3aOpen_x3a_x3anext(&iter);
    if (eof.disc != 0 || eof.payload.value.disc != 0) {
      goto fail_label_snapshot_eof;
    }
    DirIter_x3a_x3aOpen_x3a_x3aclose(iter);
  }

  delete_path_utf8(dir_path);
  delete_path_utf8(base);
  free(file_path);
  free(dir_path);
  free(base);
  return 0;

fail_label_snapshot_open_dir:
  report_failure("snapshot open_dir");
  goto fail;
fail_label_snapshot_remove:
  report_failure("snapshot remove after open");
  goto fail;
fail_label_snapshot_next:
  report_failure("snapshot next after remove");
  goto fail;
fail_label_snapshot_kind:
  report_failure("snapshot kind preserved");
  goto fail;
fail_label_snapshot_name:
  report_failure("snapshot name preserved");
  goto fail;
fail_label_snapshot_eof:
  report_failure("snapshot eof");
fail:
  delete_path_utf8(file_path);
  delete_path_utf8(dir_path);
  delete_path_utf8(base);
  free(file_path);
  free(dir_path);
  free(base);
  return 1;
}

int main(void) {
  if (test_restricted_invalid_and_missing()) {
    return 1;
  }
  if (test_closed_file_handles_reject_further_ops()) {
    return 1;
  }
  if (test_dir_iter_order_and_close_state()) {
    return 1;
  }
  if (test_dir_iter_snapshot_survives_removal()) {
    return 1;
  }

  puts("TASK-075 runtime regression passed.");
  return 0;
}
