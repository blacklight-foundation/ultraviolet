#include "../../internal/rt_platform_types.h"
#include "rt_platform_windows_api.h"

static uv_platform_once_t uv_panic_boundary_once =
    UV_PLATFORM_BACKEND_ONCE_INIT;
static uv_platform_tls_key_t uv_panic_boundary_tls_index =
    UV_PLATFORM_TLS_KEY_INVALID;
static int uv_panic_boundary_depth_fallback = 0;

static uv_platform_bool_t uv_panic_boundary_init(
    uv_platform_once_t* init_once,
    void* parameter,
    void** context) {
  (void)init_once;
  (void)parameter;
  (void)context;
  uv_panic_boundary_tls_index = uv_platform_backend_tls_key_create();
  return uv_panic_boundary_tls_index != UV_PLATFORM_TLS_KEY_INVALID
             ? UV_PLATFORM_BACKEND_TRUE
             : UV_PLATFORM_BACKEND_FALSE;
}

static int* uv_panic_boundary_depth_ptr(void) {
  int* depth_ptr = NULL;

  if (!uv_platform_backend_once_execute(&uv_panic_boundary_once,
                                             uv_panic_boundary_init,
                                             NULL,
                                             NULL)) {
    return &uv_panic_boundary_depth_fallback;
  }

  depth_ptr = (int*)uv_platform_backend_tls_get(uv_panic_boundary_tls_index);
  if (!depth_ptr) {
    depth_ptr = (int*)uv_platform_backend_heap_alloc(
        uv_platform_backend_heap_handle(), 0u, sizeof(int));
    if (!depth_ptr) {
      return &uv_panic_boundary_depth_fallback;
    }
    *depth_ptr = 0;
    if (!uv_platform_backend_tls_set(uv_panic_boundary_tls_index,
                                          depth_ptr)) {
      uv_platform_backend_heap_free(
          uv_platform_backend_heap_handle(), 0u, depth_ptr);
      return &uv_panic_boundary_depth_fallback;
    }
  }

  return depth_ptr;
}

uv_platform_bool_t uv_platform_backend_panic_boundary_active(void) {
  return *uv_panic_boundary_depth_ptr() != 0
             ? UV_PLATFORM_BACKEND_TRUE
             : UV_PLATFORM_BACKEND_FALSE;
}

uv_platform_bool_t uv_platform_backend_panic_boundary_run(
    uv_rt_panic_boundary_body_t body,
    void* context,
    uv_platform_u32_t* panic_code) {
  uv_platform_bool_t completed = UV_PLATFORM_BACKEND_TRUE;
  int* depth_ptr = NULL;

  if (panic_code) {
    *panic_code = 0u;
  }
  if (!body) {
    return UV_PLATFORM_BACKEND_FALSE;
  }

  depth_ptr = uv_panic_boundary_depth_ptr();
  *depth_ptr += 1;
  __try {
    body(context);
  } __except (GetExceptionCode() == 0xE000C0DEu
                  ? uv_platform_windows_handle_panic_filter(
                        panic_code, GetExceptionInformation())
                  : EXCEPTION_CONTINUE_SEARCH) {
    completed = UV_PLATFORM_BACKEND_FALSE;
  }
  *depth_ptr -= 1;
  return completed;
}

void uv_platform_backend_panic_boundary_raise(
    uv_platform_u32_t panic_code) {
  uintptr_t arguments[1];
  arguments[0] = (uintptr_t)panic_code;
  RaiseException(0xE000C0DEu, 0u, 1u, arguments);
}
