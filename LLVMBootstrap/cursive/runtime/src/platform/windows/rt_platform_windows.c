#include "../../internal/rt_platform_types.h"
#include "rt_platform_windows_api.h"

static cursive_platform_once_t c0_panic_boundary_once =
    CURSIVE_PLATFORM_BACKEND_ONCE_INIT;
static cursive_platform_tls_key_t c0_panic_boundary_tls_index =
    CURSIVE_PLATFORM_TLS_KEY_INVALID;
static int c0_panic_boundary_depth_fallback = 0;

static cursive_platform_bool_t c0_panic_boundary_init(
    cursive_platform_once_t* init_once,
    void* parameter,
    void** context) {
  (void)init_once;
  (void)parameter;
  (void)context;
  c0_panic_boundary_tls_index = cursive_platform_backend_tls_key_create();
  return c0_panic_boundary_tls_index != CURSIVE_PLATFORM_TLS_KEY_INVALID
             ? CURSIVE_PLATFORM_BACKEND_TRUE
             : CURSIVE_PLATFORM_BACKEND_FALSE;
}

static int* c0_panic_boundary_depth_ptr(void) {
  int* depth_ptr = NULL;

  if (!cursive_platform_backend_once_execute(&c0_panic_boundary_once,
                                             c0_panic_boundary_init,
                                             NULL,
                                             NULL)) {
    return &c0_panic_boundary_depth_fallback;
  }

  depth_ptr = (int*)cursive_platform_backend_tls_get(c0_panic_boundary_tls_index);
  if (!depth_ptr) {
    depth_ptr = (int*)cursive_platform_backend_heap_alloc(
        cursive_platform_backend_heap_handle(), 0u, sizeof(int));
    if (!depth_ptr) {
      return &c0_panic_boundary_depth_fallback;
    }
    *depth_ptr = 0;
    if (!cursive_platform_backend_tls_set(c0_panic_boundary_tls_index,
                                          depth_ptr)) {
      cursive_platform_backend_heap_free(
          cursive_platform_backend_heap_handle(), 0u, depth_ptr);
      return &c0_panic_boundary_depth_fallback;
    }
  }

  return depth_ptr;
}

cursive_platform_bool_t cursive_platform_backend_panic_boundary_active(void) {
  return *c0_panic_boundary_depth_ptr() != 0
             ? CURSIVE_PLATFORM_BACKEND_TRUE
             : CURSIVE_PLATFORM_BACKEND_FALSE;
}

cursive_platform_bool_t cursive_platform_backend_panic_boundary_run(
    cursive_rt_panic_boundary_body_t body,
    void* context,
    cursive_platform_u32_t* panic_code) {
  cursive_platform_bool_t completed = CURSIVE_PLATFORM_BACKEND_TRUE;
  int* depth_ptr = NULL;

  if (panic_code) {
    *panic_code = 0u;
  }
  if (!body) {
    return CURSIVE_PLATFORM_BACKEND_FALSE;
  }

  depth_ptr = c0_panic_boundary_depth_ptr();
  *depth_ptr += 1;
  __try {
    body(context);
  } __except (GetExceptionCode() == 0xE000C0DEu
                  ? cursive_platform_windows_handle_panic_filter(
                        panic_code, GetExceptionInformation())
                  : EXCEPTION_CONTINUE_SEARCH) {
    completed = CURSIVE_PLATFORM_BACKEND_FALSE;
  }
  *depth_ptr -= 1;
  return completed;
}

void cursive_platform_backend_panic_boundary_raise(
    cursive_platform_u32_t panic_code) {
  uintptr_t arguments[1];
  arguments[0] = (uintptr_t)panic_code;
  RaiseException(0xE000C0DEu, 0u, 1u, arguments);
}
