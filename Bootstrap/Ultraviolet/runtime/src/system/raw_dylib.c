#include "../internal/rt_internal.h"

void* uv_raw_dylib_resolve(const char* dll_name,
                                const char* symbol_name) {
  if (!dll_name || !symbol_name || dll_name[0] == '\0' ||
      symbol_name[0] == '\0') {
    return NULL;
  }

  uv_rt_module_t module = uv_rt_module_open_loaded(dll_name);
  if (module == NULL) {
    module = uv_rt_module_open(dll_name);
  }
  if (module == NULL) {
    return NULL;
  }

  return uv_rt_module_lookup(module, symbol_name);
}
