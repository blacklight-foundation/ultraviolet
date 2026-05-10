#ifndef CURSIVE_RT_PATH_H
#define CURSIVE_RT_PATH_H

#include <stdint.h>
#include <wchar.h>

uint8_t* cursive_rt_path_canonicalize_utf8(const uint8_t* data,
                                           uint64_t len,
                                           uint32_t* out_len);
uint8_t* cursive_rt_path_join_utf8(const uint8_t* base,
                                   uint32_t base_len,
                                   const uint8_t* rel,
                                   uint64_t rel_len,
                                   uint32_t* out_len);
int cursive_rt_path_has_prefix_utf8(const uint8_t* path,
                                    uint32_t path_len,
                                    const uint8_t* base,
                                    uint32_t base_len);
int cursive_rt_path_is_absolute_utf8(const uint8_t* data, uint64_t len);
wchar_t* cursive_rt_path_utf8_to_native_wide(const uint8_t* utf8,
                                             uint32_t len,
                                             uint32_t* out_len);

#endif  // CURSIVE_RT_PATH_H
