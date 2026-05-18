#include "../../internal/rt_internal.h"
#include "../../internal/rt_path.h"

static int uv_rt_path_is_separator(uint8_t byte) {
  return byte == '/' || byte == '\\';
}

static int uv_rt_path_is_ascii_letter(uint8_t byte) {
  return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
}

static int uv_rt_path_is_drive_rooted(const uint8_t* data, uint64_t len) {
  if (!data || len < 3u) {
    return 0;
  }
  if (!uv_rt_path_is_ascii_letter(data[0])) {
    return 0;
  }
  if (data[1] != ':') {
    return 0;
  }
  return uv_rt_path_is_separator(data[2]);
}

static int uv_rt_path_is_unc(const uint8_t* data, uint64_t len) {
  if (!data || len < 2u) {
    return 0;
  }
  return (data[0] == '/' && data[1] == '/') ||
         (data[0] == '\\' && data[1] == '\\');
}

static int uv_rt_path_is_root_relative(const uint8_t* data, uint64_t len) {
  if (!data || len == 0u) {
    return 0;
  }
  if (uv_rt_path_is_unc(data, len) ||
      uv_rt_path_is_drive_rooted(data, len)) {
    return 0;
  }
  return data[0] == '/' || data[0] == '\\';
}

int uv_rt_path_is_absolute_utf8(const uint8_t* data, uint64_t len) {
  return uv_rt_path_is_drive_rooted(data, len) ||
         uv_rt_path_is_unc(data, len) ||
         uv_rt_path_is_root_relative(data, len);
}

typedef struct uv_rt_path_segment_t {
  const uint8_t* ptr;
  uint32_t len;
} uv_rt_path_segment_t;

uint8_t* uv_rt_path_canonicalize_utf8(const uint8_t* data,
                                           uint64_t len,
                                           uint32_t* out_len) {
  uint8_t root_tag[2];
  uint32_t root_len = 0u;
  uint64_t position = 0u;
  int drive_rooted = 0;
  uint32_t segment_count = 0u;
  uint64_t segment_start = 0u;
  uv_rt_path_segment_t* segments = NULL;
  uint32_t out_count = 0u;
  uint64_t out_len64 = 0u;
  uint32_t out_len_u32 = 0u;
  uint8_t* out = NULL;

  if (out_len) {
    *out_len = 0u;
  }
  if (!data && len != 0u) {
    return NULL;
  }
  if (len > UINT32_MAX) {
    return NULL;
  }

  if (uv_rt_path_is_drive_rooted(data, len)) {
    root_tag[0] = data[0];
    root_tag[1] = ':';
    root_len = 2u;
    position = 3u;
    drive_rooted = 1;
  } else if (uv_rt_path_is_unc(data, len)) {
    root_tag[0] = '/';
    root_tag[1] = '/';
    root_len = 2u;
    position = 2u;
  } else if (uv_rt_path_is_root_relative(data, len)) {
    root_tag[0] = '/';
    root_len = 1u;
    position = 1u;
  }

  segment_start = position;
  for (uint64_t index = position; index <= len; ++index) {
    if (index == len || uv_rt_path_is_separator(data[index])) {
      if (index > segment_start) {
        ++segment_count;
      }
      segment_start = index + 1u;
    }
  }

  if (segment_count > 0u) {
    segments = (uv_rt_path_segment_t*)uv_heap_alloc_raw(
        sizeof(uv_rt_path_segment_t) * segment_count);
    if (!segments) {
      return NULL;
    }
  }

  segment_start = position;
  for (uint64_t index = position; index <= len; ++index) {
    if (index == len || uv_rt_path_is_separator(data[index])) {
      if (index > segment_start) {
        uint32_t segment_len = (uint32_t)(index - segment_start);
        const uint8_t* segment_ptr = data + segment_start;
        if (segment_len == 1u && segment_ptr[0] == '.') {
        } else if (segment_len == 2u && segment_ptr[0] == '.' &&
                   segment_ptr[1] == '.') {
          if (segments) {
            uv_heap_free_raw(segments);
          }
          return NULL;
        } else {
          if (segments) {
            segments[out_count].ptr = segment_ptr;
            segments[out_count].len = segment_len;
          }
          ++out_count;
        }
      }
      segment_start = index + 1u;
    }
  }

  out_len64 = root_len;
  if (out_count > 0u) {
    if (root_len > 0u && drive_rooted) {
      out_len64 += 1u;
    }
    for (uint32_t index = 0u; index < out_count; ++index) {
      out_len64 += segments[index].len;
    }
    if (out_count > 1u) {
      out_len64 += (uint64_t)(out_count - 1u);
    }
  }

  if (out_len64 > UINT32_MAX) {
    if (segments) {
      uv_heap_free_raw(segments);
    }
    return NULL;
  }

  out_len_u32 = (uint32_t)out_len64;
  out = (uint8_t*)uv_heap_alloc_raw((size_t)out_len_u32 + 1u);
  if (!out) {
    if (segments) {
      uv_heap_free_raw(segments);
    }
    return NULL;
  }

  {
    uint32_t offset = 0u;
    if (root_len > 0u) {
      uv_memcpy(out + offset, root_tag, root_len);
      offset += root_len;
    }
    if (out_count > 0u) {
      if (root_len > 0u && drive_rooted) {
        out[offset++] = '/';
      }
      for (uint32_t index = 0u; index < out_count; ++index) {
        uv_memcpy(out + offset, segments[index].ptr, segments[index].len);
        offset += segments[index].len;
        if (index + 1u < out_count) {
          out[offset++] = '/';
        }
      }
    }
    out[offset] = 0;
  }

  if (segments) {
    uv_heap_free_raw(segments);
  }
  if (out_len) {
    *out_len = out_len_u32;
  }
  return out;
}

uint8_t* uv_rt_path_join_utf8(const uint8_t* base,
                                   uint32_t base_len,
                                   const uint8_t* rel,
                                   uint64_t rel_len,
                                   uint32_t* out_len) {
  uint8_t* out = NULL;
  uint32_t extra = 0u;
  uint64_t total = 0u;
  uint32_t offset = 0u;

  if (out_len) {
    *out_len = 0u;
  }
  if (!base && base_len != 0u) {
    return NULL;
  }
  if (!rel && rel_len != 0u) {
    return NULL;
  }
  if (rel_len > UINT32_MAX) {
    return NULL;
  }

  if (base_len == 0u) {
    out = (uint8_t*)uv_heap_alloc_raw((size_t)rel_len + 1u);
    if (!out) {
      return NULL;
    }
    if (rel_len > 0u) {
      uv_memcpy(out, rel, (size_t)rel_len);
    }
    out[rel_len] = 0;
    if (out_len) {
      *out_len = (uint32_t)rel_len;
    }
    return out;
  }

  if (rel_len == 0u) {
    out = (uint8_t*)uv_heap_alloc_raw((size_t)base_len + 1u);
    if (!out) {
      return NULL;
    }
    uv_memcpy(out, base, base_len);
    out[base_len] = 0;
    if (out_len) {
      *out_len = base_len;
    }
    return out;
  }

  extra = (base[base_len - 1u] == '/') ? 0u : 1u;
  total = (uint64_t)base_len + extra + rel_len;
  if (total > UINT32_MAX) {
    return NULL;
  }

  out = (uint8_t*)uv_heap_alloc_raw((size_t)total + 1u);
  if (!out) {
    return NULL;
  }
  uv_memcpy(out, base, base_len);
  offset = base_len;
  if (extra) {
    out[offset++] = '/';
  }
  uv_memcpy(out + offset, rel, (size_t)rel_len);
  offset += (uint32_t)rel_len;
  out[offset] = 0;
  if (out_len) {
    *out_len = (uint32_t)total;
  }
  return out;
}

int uv_rt_path_has_prefix_utf8(const uint8_t* path,
                                    uint32_t path_len,
                                    const uint8_t* base,
                                    uint32_t base_len) {
  if (base_len == 0u) {
    return 1;
  }
  if (!path || !base || path_len < base_len) {
    return 0;
  }
  for (uint32_t index = 0u; index < base_len; ++index) {
    if (path[index] != base[index]) {
      return 0;
    }
  }
  if (path_len == base_len) {
    return 1;
  }
  if (base[base_len - 1u] == '/') {
    return 1;
  }
  return path[base_len] == '/';
}

wchar_t* uv_rt_path_utf8_to_native_wide(const uint8_t* utf8,
                                             uint32_t len,
                                             uint32_t* out_len) {
  wchar_t* wide = NULL;
  uint32_t wide_len = 0u;

  if (out_len) {
    *out_len = 0u;
  }
  if (len == 0u) {
    wide = (wchar_t*)uv_heap_alloc_raw(sizeof(wchar_t));
    if (!wide) {
      return NULL;
    }
    wide[0] = 0;
    return wide;
  }

  wide = uv_utf8_to_wide(utf8, len, &wide_len);
  if (!wide) {
    return NULL;
  }
  for (uint32_t index = 0u; index < wide_len; ++index) {
    if (wide[index] == L'/') {
      wide[index] = L'\\';
    }
  }
  if (wide_len == 2u && wide[1] == L':') {
    wchar_t* rooted =
        (wchar_t*)uv_heap_alloc_raw(sizeof(wchar_t) * 4u);
    if (!rooted) {
      uv_heap_free_raw(wide);
      return NULL;
    }
    rooted[0] = wide[0];
    rooted[1] = wide[1];
    rooted[2] = L'\\';
    rooted[3] = 0;
    uv_heap_free_raw(wide);
    if (out_len) {
      *out_len = 3u;
    }
    return rooted;
  }
  if (out_len) {
    *out_len = wide_len;
  }
  return wide;
}
