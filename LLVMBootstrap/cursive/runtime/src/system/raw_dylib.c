#include "../internal/rt_internal.h"

void* cursive_raw_dylib_resolve(const char* dll_name,
                                const char* symbol_name) {
  if (!dll_name || !symbol_name || dll_name[0] == '\0' ||
      symbol_name[0] == '\0') {
    return NULL;
  }

  cursive_rt_module_t module = cursive_rt_module_open_loaded(dll_name);
  if (module == NULL) {
    module = cursive_rt_module_open(dll_name);
  }
  if (module == NULL) {
    return NULL;
  }

  return cursive_rt_module_lookup(module, symbol_name);
}
