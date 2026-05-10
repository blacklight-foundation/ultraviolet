#include "cursive_rt.h"
#include "../src/internal/rt_platform.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int report_failure(const char* label) {
  fprintf(stderr, "TASK-075 runtime regression failed: %s\n", label);
  return 1;
}

static C0StringView sv_from_cstr(const char* text) {
  C0StringView out;
  out.data = (const uint8_t*)text;
  out.len = text ? (uint64_t)strlen(text) : 0;
  return out;
}

static C0BytesView bytes_view_from_cstr(const char* text) {
  C0BytesView out;
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
  needed = cursive_rt_utf8_to_wide_chars(utf8, -1, NULL, 0);
  if (needed <= 0) {
    return NULL;
  }
  wide = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)needed);
  if (!wide) {
    return NULL;
  }
  if (cursive_rt_utf8_to_wide_chars(utf8, -1, wide, needed) <= 0) {
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
  needed = cursive_rt_wide_to_utf8_chars(wide, -1, NULL, 0);
  if (needed <= 0) {
    return NULL;
  }
  utf8 = (char*)malloc((size_t)needed);
  if (!utf8) {
    return NULL;
  }
  if (cursive_rt_wide_to_utf8_chars(wide, -1, utf8, needed) <= 0) {
    free(utf8);
    return NULL;
  }
  return utf8;
}

static char* make_temp_dir_utf8(void) {
  wchar_t temp_root[4096];
  wchar_t temp_path[4096];
  if (cursive_rt_temp_path_get_wide((cursive_rt_dword_t)(sizeof(temp_root) / sizeof(temp_root[0])),
                                    temp_root) == 0) {
    return NULL;
  }
  if (cursive_rt_temp_file_name_wide(temp_root, L"c75", 0u, temp_path) == 0u) {
    return NULL;
  }
  if (!cursive_rt_file_delete_wide(temp_path)) {
    return NULL;
  }
  if (!cursive_rt_directory_create_wide(temp_path, NULL)) {
    return NULL;
  }
  return utf8_from_wide(temp_path);
}

static void delete_path_utf8(const char* utf8_path) {
  wchar_t* wide = wide_from_utf8(utf8_path);
  cursive_rt_dword_t attrs;
  if (!wide) {
    return;
  }
  attrs = cursive_rt_file_attributes_get_wide(wide);
  if (attrs != CURSIVE_PLATFORM_INVALID_FILE_ATTRIBUTES) {
    if ((attrs & CURSIVE_PLATFORM_FILE_ATTRIBUTE_DIRECTORY) != 0u) {
      cursive_rt_directory_remove_wide(wide);
    } else {
      cursive_rt_file_delete_wide(wide);
    }
  }
  free(wide);
}

static int ensure_dir_ok(const C0DynObject* fs,
                         const char* utf8_path,
                         const char* label) {
  C0StringView path = sv_from_cstr(utf8_path);
  C0Union_Unit_IoError result =
      cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aensure_x5fdir(fs, &path);
  if (result.disc != 0) {
    return report_failure(label);
  }
  return 0;
}

static int write_file_ok(const C0DynObject* fs,
                         const char* utf8_path,
                         const char* payload,
                         const char* label) {
  C0StringView path = sv_from_cstr(utf8_path);
  C0BytesView data = bytes_view_from_cstr(payload);
  C0Union_Unit_IoError result =
      cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3awrite_x5ffile(fs, &path, &data);
  if (result.disc != 0) {
    return report_failure(label);
  }
  return 0;
}

static int expect_file_error(C0Union_File_IoError result,
                             C0IoError expected,
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

static int expect_dir_error(C0Union_DirIter_IoError result,
                            C0IoError expected,
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

static int expect_unit_error(C0Union_Unit_IoError result,
                             C0IoError expected,
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

static int expect_kind_error(C0Union_FileKind_IoError result,
                             C0IoError expected,
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

static int expect_string_error(C0Union_StringManaged_IoError result,
                               C0IoError expected,
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

static int expect_bytes_error(C0Union_BytesManaged_IoError result,
                              C0IoError expected,
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

static int expect_next_name(C0DirIterHandle* iter,
                            const char* expected_name,
                            const char* label) {
  C0Union_DirEntry_Unit_IoError next =
      DirIter_x3a_x3aOpen_x3a_x3anext(iter);
  if (next.disc != 0 || next.payload.value.disc != 1) {
    return report_failure(label);
  }
  if (next.payload.value.entry.kind != C0_FILE_KIND_FILE) {
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
  C0Context ctx;
  memset(&ctx, 0, sizeof(ctx));
  cursive_x3a_x3aruntime_x3a_x3acontext_x5finit(&ctx);

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

  if (ensure_dir_ok(&ctx.fs, restricted_root, "restricted root create")) {
    goto fail;
  }
  if (write_file_ok(&ctx.fs, outside_file, "outside", "restricted outside seed")) {
    goto fail;
  }

  C0StringView restricted_root_view = sv_from_cstr(restricted_root);
  C0DynObject restricted =
      cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3arestrict(&ctx.fs,
                                                        &restricted_root_view);

  {
    C0StringView escape = sv_from_cstr("../outside.txt");
    if (cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aexists(&restricted, &escape) != 0) {
      goto fail_label_escape_exists;
    }
    if (expect_file_error(
            cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fread(&restricted,
                                                                  &escape),
            C0_IO_INVALID_PATH,
            "restricted open_read invalid path")) {
      goto fail;
    }
    if (expect_file_error(
            cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3acreate_x5fwrite(&restricted,
                                                                     &escape),
            C0_IO_INVALID_PATH,
            "restricted create_write invalid path")) {
      goto fail;
    }
  }

  {
    C0StringView missing = sv_from_cstr("missing.txt");
    if (expect_file_error(
            cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fread(&restricted,
                                                                  &missing),
            C0_IO_NOTFOUND,
            "restricted open_read missing")) {
      goto fail;
    }
    if (expect_file_error(
            cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fwrite(&restricted,
                                                                   &missing),
            C0_IO_NOTFOUND,
            "restricted open_write missing")) {
      goto fail;
    }
    if (expect_file_error(
            cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fappend(&restricted,
                                                                    &missing),
            C0_IO_NOTFOUND,
            "restricted open_append missing")) {
      goto fail;
    }
    if (expect_unit_error(
            cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aremove(&restricted, &missing),
            C0_IO_NOTFOUND,
            "restricted remove missing")) {
      goto fail;
    }
    if (expect_dir_error(
            cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fdir(&restricted,
                                                                 &missing),
            C0_IO_NOTFOUND,
            "restricted open_dir missing")) {
      goto fail;
    }
    if (expect_kind_error(
            cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3akind(&restricted, &missing),
            C0_IO_NOTFOUND,
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
  C0Context ctx;
  memset(&ctx, 0, sizeof(ctx));
  cursive_x3a_x3aruntime_x3a_x3acontext_x5finit(&ctx);

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
    C0StringView path = sv_from_cstr(file_path);
    C0Union_File_IoError opened =
        cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3acreate_x5fwrite(&ctx.fs, &path);
    if (opened.disc != 0) {
      goto fail_label_open_write;
    }
    C0FileHandle writer;
    C0BytesView payload = bytes_view_from_cstr("tracked-write");
    writer.handle = opened.payload.handle;
    if (File_x3a_x3aWrite_x3a_x3awrite(&writer, &payload).disc != 0) {
      goto fail_label_write;
    }
    File_x3a_x3aWrite_x3a_x3aclose(writer);
    if (expect_unit_error(File_x3a_x3aWrite_x3a_x3aflush(&writer),
                          C0_IO_FAILURE,
                          "flush on closed write handle")) {
      goto fail;
    }
    if (expect_unit_error(File_x3a_x3aWrite_x3a_x3awrite(&writer, &payload),
                          C0_IO_FAILURE,
                          "write on closed write handle")) {
      goto fail;
    }
  }

  {
    C0StringView path = sv_from_cstr(file_path);
    C0Union_File_IoError opened =
        cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fread(&ctx.fs, &path);
    if (opened.disc != 0) {
      goto fail_label_open_read;
    }
    C0FileHandle reader;
    reader.handle = opened.payload.handle;
    File_x3a_x3aRead_x3a_x3aclose(reader);
    if (expect_string_error(File_x3a_x3aRead_x3a_x3aread_x5fall(&reader),
                            C0_IO_FAILURE,
                            "read_all on closed read handle")) {
      goto fail;
    }
    if (expect_bytes_error(File_x3a_x3aRead_x3a_x3aread_x5fall_x5fbytes(&reader),
                           C0_IO_FAILURE,
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
  C0Context ctx;
  memset(&ctx, 0, sizeof(ctx));
  cursive_x3a_x3aruntime_x3a_x3acontext_x5finit(&ctx);

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

  if (ensure_dir_ok(&ctx.fs, dir_path, "ordered dir create")) {
    goto fail;
  }
  if (write_file_ok(&ctx.fs, ss_path, "1", "ordered seed ss")) {
    goto fail;
  }
  if (write_file_ok(&ctx.fs, sharp_s_path, "2", "ordered seed sharp-s")) {
    goto fail;
  }
  if (write_file_ok(&ctx.fs, st_path, "3", "ordered seed st")) {
    goto fail;
  }

  {
    C0StringView dir_view = sv_from_cstr(dir_path);
    C0Union_DirIter_IoError opened =
        cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fdir(&ctx.fs, &dir_view);
    if (opened.disc != 0) {
      goto fail_label_open_dir;
    }
    C0DirIterHandle iter;
    C0Union_DirEntry_Unit_IoError eof;
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
    if (eof.disc != 1 || eof.payload.io_error != C0_IO_FAILURE) {
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
  C0Context ctx;
  memset(&ctx, 0, sizeof(ctx));
  cursive_x3a_x3aruntime_x3a_x3acontext_x5finit(&ctx);

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

  if (ensure_dir_ok(&ctx.fs, dir_path, "snapshot dir create")) {
    goto fail;
  }
  if (write_file_ok(&ctx.fs, file_path, "snap", "snapshot seed file")) {
    goto fail;
  }

  {
    C0StringView dir_view = sv_from_cstr(dir_path);
    C0StringView file_view = sv_from_cstr(file_path);
    C0Union_DirIter_IoError opened =
        cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aopen_x5fdir(&ctx.fs, &dir_view);
    if (opened.disc != 0) {
      goto fail_label_snapshot_open_dir;
    }
    C0DirIterHandle iter;
    C0Union_DirEntry_Unit_IoError next;
    iter.handle = opened.payload.handle;
    if (cursive_x3a_x3aruntime_x3a_x3afs_x3a_x3aremove(&ctx.fs, &file_view).disc != 0) {
      goto fail_label_snapshot_remove;
    }
    next = DirIter_x3a_x3aOpen_x3a_x3anext(&iter);
    if (next.disc != 0 || next.payload.value.disc != 1) {
      goto fail_label_snapshot_next;
    }
    if (next.payload.value.entry.kind != C0_FILE_KIND_FILE) {
      goto fail_label_snapshot_kind;
    }
    if (next.payload.value.entry.name.len != strlen("entry.txt") ||
        memcmp(next.payload.value.entry.name.data, "entry.txt", strlen("entry.txt")) != 0) {
      goto fail_label_snapshot_name;
    }
    C0Union_DirEntry_Unit_IoError eof =
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
