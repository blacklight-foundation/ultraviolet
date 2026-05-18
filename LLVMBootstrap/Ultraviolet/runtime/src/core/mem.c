#include "../internal/rt_internal.h"

#ifdef _MSC_VER
#pragma function(memcpy, memmove, memset, memcmp)
#endif

void* memcpy(void* dst, const void* src, size_t n) {
  return uv_memcpy(dst, src, n);
}

void* memmove(void* dst, const void* src, size_t n) {
  return uv_memmove(dst, src, n);
}

void* memset(void* dst, int c, size_t n) {
  return uv_memset(dst, c, n);
}

int memcmp(const void* a, const void* b, size_t n) {
  const unsigned char* x = (const unsigned char*)a;
  const unsigned char* y = (const unsigned char*)b;
  for (size_t i = 0; i < n; ++i) {
    if (x[i] != y[i]) {
      return x[i] < y[i] ? -1 : 1;
    }
  }
  return 0;
}
