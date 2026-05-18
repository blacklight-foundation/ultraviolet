#include "../internal/rt_internal.h"

typedef struct UVHostSessionNode {
  uint64_t handle;
  const void* owner;
  void* env;
  int state;
  struct UVHostSessionNode* next;
} UVHostSessionNode;

typedef struct UVHostSessionTlsFrame {
  uint64_t handle;
  int kind;
  void* env;
  struct UVHostSessionTlsFrame* prev;
} UVHostSessionTlsFrame;

typedef struct UVHostThreadState {
  UVHostSessionTlsFrame* env_stack;
} UVHostThreadState;

typedef struct UVHostSharedState {
  volatile uv_platform_i64_t next_handle;
} UVHostSharedState;

static uv_platform_once_t uv_host_once = UV_PLATFORM_ONCE_INIT;
static uv_platform_mutex_t uv_host_lock;
static int uv_host_lock_ready = 0;
static uv_platform_tls_key_t uv_host_tls_index = UV_PLATFORM_TLS_KEY_INVALID;
static UVHostThreadState uv_host_tls_fallback = {0};
static UVHostSessionNode* uv_host_sessions = NULL;
static uv_platform_handle_t uv_host_shared_mapping = NULL;
static UVHostSharedState* uv_host_shared = NULL;

enum {
  UV_HOST_FRAME_LIVE = 1,
  UV_HOST_FRAME_RETIRED = 2,
};

enum {
  UV_HOST_SESSION_LIVE_IDLE = 1,
  UV_HOST_SESSION_LIVE_ENTERED = 2,
  UV_HOST_SESSION_RETIRED_BUSY = 3,
};

static uv_platform_bool_t uv_host_init(uv_platform_once_t* init_once,
                                      void* param,
                                      void** context) {
  static const wchar_t kHostSharedCounterName[] =
      L"Local\\CursiveHostSessionCounterV1";
  (void)init_once;
  (void)param;
  (void)context;
  uv_platform_mutex_init(&uv_host_lock);
  uv_host_tls_index = uv_platform_tls_key_create();
  if (uv_host_tls_index == UV_PLATFORM_TLS_KEY_INVALID) {
    return UV_PLATFORM_FALSE;
  }
  uv_host_shared_mapping = uv_platform_mapping_create(UV_PLATFORM_INVALID_HANDLE,
                                                     NULL,
                                                     UV_PLATFORM_PAGE_READWRITE,
                                                     0,
                                                     (uv_platform_u32_t)sizeof(UVHostSharedState),
                                                     kHostSharedCounterName);
  if (!uv_host_shared_mapping) {
    return UV_PLATFORM_FALSE;
  }
  uv_host_shared =
      (UVHostSharedState*)uv_platform_mapping_view(uv_host_shared_mapping,
                                                  UV_PLATFORM_FILE_MAP_ALL_ACCESS,
                                                  0,
                                                  0,
                                                  sizeof(UVHostSharedState));
  if (!uv_host_shared) {
    return UV_PLATFORM_FALSE;
  }
  uv_host_lock_ready = 1;
  return UV_PLATFORM_TRUE;
}

static int uv_host_ensure_init(void) {
  return uv_platform_once_execute(&uv_host_once, uv_host_init, NULL, NULL) != 0;
}

static UVHostThreadState* uv_host_thread_state(void) {
  UVHostThreadState* state = NULL;
  if (!uv_host_ensure_init()) {
    return &uv_host_tls_fallback;
  }

  state = (UVHostThreadState*)uv_platform_tls_get(uv_host_tls_index);
  if (!state) {
    state = (UVHostThreadState*)uv_heap_alloc_raw(sizeof(UVHostThreadState));
    if (!state) {
      return &uv_host_tls_fallback;
    }
    uv_memset(state, 0, sizeof(UVHostThreadState));
    uv_platform_tls_set(uv_host_tls_index, state);
  }
  return state;
}

static int uv_host_push_frame(uint64_t handle, int kind, void* env) {
  UVHostThreadState* state;
  UVHostSessionTlsFrame* frame;
  if (handle == 0u || !env) {
    return 0;
  }
  state = uv_host_thread_state();
  if (!state) {
    return 0;
  }
  frame = (UVHostSessionTlsFrame*)uv_heap_alloc_raw(sizeof(UVHostSessionTlsFrame));
  if (!frame) {
    return 0;
  }
  frame->handle = handle;
  frame->kind = kind;
  frame->env = env;
  frame->prev = state->env_stack;
  state->env_stack = frame;
  return 1;
}

static int uv_host_pop_frame(uint64_t handle, int kind) {
  UVHostThreadState* state = uv_host_thread_state();
  UVHostSessionTlsFrame* frame;
  if (!state || !state->env_stack) {
    return 0;
  }
  frame = state->env_stack;
  if (frame->handle != handle || frame->kind != kind) {
    return 0;
  }
  state->env_stack = frame->prev;
  uv_heap_free_raw(frame);
  return 1;
}

static int uv_host_remove_matching_frame(uint64_t handle, int kind, void** out_env) {
  UVHostThreadState* state = uv_host_thread_state();
  UVHostSessionTlsFrame** link = NULL;
  UVHostSessionTlsFrame* frame = NULL;
  if (out_env) {
    *out_env = NULL;
  }
  if (!state) {
    return 0;
  }
  link = &state->env_stack;
  while (*link) {
    frame = *link;
    if (frame->handle == handle && frame->kind == kind) {
      *link = frame->prev;
      if (out_env) {
        *out_env = frame->env;
      }
      uv_heap_free_raw(frame);
      return 1;
    }
    link = &frame->prev;
  }
  return 0;
}

static int uv_host_owner_eq(const void* lhs, const void* rhs) {
  return lhs != NULL && rhs != NULL && lhs == rhs;
}

static int uv_host_session_is_live(const UVHostSessionNode* node) {
  return node && node->state != UV_HOST_SESSION_RETIRED_BUSY;
}

static int uv_host_session_is_busy(const UVHostSessionNode* node) {
  return node &&
         (node->state == UV_HOST_SESSION_LIVE_ENTERED ||
          node->state == UV_HOST_SESSION_RETIRED_BUSY);
}

static uint64_t uv_host_allocate_handle(void) {
  uv_platform_signed_size64_t next = 0;
  if (!uv_host_shared) {
    return 0u;
  }
  next = uv_platform_atomic_increment64(&uv_host_shared->next_handle);
  if (next <= 0) {
    return 0u;
  }
  return (uint64_t)next;
}

static UVHostSessionNode* uv_host_find_session(uint64_t handle,
                                               UVHostSessionNode*** prev_next) {
  UVHostSessionNode** link = &uv_host_sessions;
  while (*link) {
    if ((*link)->handle == handle) {
      if (prev_next) {
        *prev_next = link;
      }
      return *link;
    }
    link = &(*link)->next;
  }
  if (prev_next) {
    *prev_next = link;
  }
  return NULL;
}

static int uv_host_remove_session_for_owner(uint64_t handle,
                                            const void* owner,
                                            void** out_env) {
  UVHostSessionNode** prev_next = NULL;
  UVHostSessionNode* node = NULL;
  if (out_env) {
    *out_env = NULL;
  }
  node = uv_host_find_session(handle, &prev_next);
  if (!node || !uv_host_owner_eq(node->owner, owner)) {
    return 0;
  }
  *prev_next = node->next;
  if (out_env) {
    *out_env = node->env;
  }
  uv_heap_free_raw(node);
  return 1;
}

void* uv_host_alloc(size_t size) {
  return uv_heap_alloc_raw(size);
}

void uv_host_free(void* ptr) {
  uv_heap_free_raw(ptr);
}

uint64_t uv_host_session_register(const void* owner, void* env) {
  if (!owner || !env || !uv_host_ensure_init()) {
    return 0u;
  }

  UVHostSessionNode* node =
      (UVHostSessionNode*)uv_heap_alloc_raw(sizeof(UVHostSessionNode));
  if (!node) {
    return 0u;
  }

  uv_platform_mutex_lock(&uv_host_lock);
  node->handle = uv_host_allocate_handle();
  if (node->handle == 0u) {
    uv_platform_mutex_unlock(&uv_host_lock);
    uv_heap_free_raw(node);
    return 0u;
  }
  node->owner = owner;
  node->env = env;
  node->state = UV_HOST_SESSION_LIVE_IDLE;
  node->next = uv_host_sessions;
  uv_host_sessions = node;
  uv_platform_mutex_unlock(&uv_host_lock);
  return node->handle;
}

int uv_host_session_try_enter(uint64_t handle,
                                   const void* owner,
                                   void** out_env) {
  if (out_env) {
    *out_env = NULL;
  }
  if (handle == 0u || !owner || !uv_host_ensure_init()) {
    return 0;
  }

  void* env = NULL;
  uv_platform_mutex_lock(&uv_host_lock);
  UVHostSessionNode* node = uv_host_find_session(handle, NULL);
  if (!node || !uv_host_owner_eq(node->owner, owner) ||
      !uv_host_session_is_live(node) || uv_host_session_is_busy(node)) {
    uv_platform_mutex_unlock(&uv_host_lock);
    return 0;
  }
  node->state = UV_HOST_SESSION_LIVE_ENTERED;
  env = node->env;
  uv_platform_mutex_unlock(&uv_host_lock);

  if (!uv_host_push_frame(handle, UV_HOST_FRAME_LIVE, env)) {
    uv_platform_mutex_lock(&uv_host_lock);
    node = uv_host_find_session(handle, NULL);
    if (node && uv_host_owner_eq(node->owner, owner) &&
        node->state == UV_HOST_SESSION_LIVE_ENTERED) {
      node->state = UV_HOST_SESSION_LIVE_IDLE;
    }
    uv_platform_mutex_unlock(&uv_host_lock);
    return 0;
  }

  if (out_env) {
    *out_env = env;
  }
  return 1;
}

int uv_host_session_leave(uint64_t handle, const void* owner) {
  if (handle == 0u || !owner || !uv_host_ensure_init()) {
    return 0;
  }

  uv_platform_mutex_lock(&uv_host_lock);
  UVHostSessionNode* node = uv_host_find_session(handle, NULL);
  if (!node || !uv_host_owner_eq(node->owner, owner) ||
      node->state != UV_HOST_SESSION_LIVE_ENTERED) {
    uv_platform_mutex_unlock(&uv_host_lock);
    return 0;
  }
  uv_platform_mutex_unlock(&uv_host_lock);

  if (!uv_host_pop_frame(handle, UV_HOST_FRAME_LIVE)) {
    return 0;
  }

  uv_platform_mutex_lock(&uv_host_lock);
  node = uv_host_find_session(handle, NULL);
  if (node && uv_host_owner_eq(node->owner, owner) &&
      node->state == UV_HOST_SESSION_LIVE_ENTERED) {
    node->state = UV_HOST_SESSION_LIVE_IDLE;
    uv_platform_mutex_unlock(&uv_host_lock);
    return 1;
  }
  uv_platform_mutex_unlock(&uv_host_lock);
  return 0;
}

int uv_host_session_try_retire(uint64_t handle,
                                    const void* owner,
                                    void** out_env) {
  if (out_env) {
    *out_env = NULL;
  }
  if (handle == 0u || !owner || !uv_host_ensure_init()) {
    return 0;
  }

  UVHostSessionNode* node = NULL;
  void* env = NULL;

  uv_platform_mutex_lock(&uv_host_lock);
  node = uv_host_find_session(handle, NULL);
  if (!node || !uv_host_owner_eq(node->owner, owner) ||
      node->state != UV_HOST_SESSION_LIVE_IDLE) {
    uv_platform_mutex_unlock(&uv_host_lock);
    return 0;
  }
  env = node->env;
  node->state = UV_HOST_SESSION_RETIRED_BUSY;
  uv_platform_mutex_unlock(&uv_host_lock);

  if (out_env) {
    *out_env = env;
  }
  return 1;
}

int uv_host_session_abort_live(uint64_t handle,
                                    const void* owner,
                                    void** out_env) {
  void* env = NULL;
  void* frame_env = NULL;
  int removed = 0;
  if (out_env) {
    *out_env = NULL;
  }
  if (handle == 0u || !owner || !uv_host_ensure_init()) {
    return 0;
  }

  uv_platform_mutex_lock(&uv_host_lock);
  removed = uv_host_remove_session_for_owner(handle, owner, &env);
  uv_platform_mutex_unlock(&uv_host_lock);
  if (!removed) {
    return 0;
  }

  if (!uv_host_remove_matching_frame(handle, UV_HOST_FRAME_LIVE, &frame_env)) {
    return 0;
  }
  if (!env) {
    env = frame_env;
  }
  if (out_env) {
    *out_env = env;
  }
  return 1;
}

void* uv_host_session_current_env(void) {
  UVHostThreadState* state = uv_host_thread_state();
  if (!state || !state->env_stack) {
    return NULL;
  }
  return state->env_stack->env;
}

int uv_host_session_enter_retired(uint64_t handle,
                                       const void* owner,
                                       void* env) {
  int result = 0;
  UVHostSessionNode* node = NULL;
  if (handle == 0u || !owner || !env || !uv_host_ensure_init()) {
    return 0;
  }
  uv_platform_mutex_lock(&uv_host_lock);
  node = uv_host_find_session(handle, NULL);
  if (!node || !uv_host_owner_eq(node->owner, owner) ||
      node->state != UV_HOST_SESSION_RETIRED_BUSY || node->env != env) {
    uv_platform_mutex_unlock(&uv_host_lock);
    return 0;
  }
  uv_platform_mutex_unlock(&uv_host_lock);
  result = uv_host_push_frame(handle, UV_HOST_FRAME_RETIRED, env);
  if (!result) {
    return 0;
  }
  return 1;
}

int uv_host_session_leave_retired(uint64_t handle, const void* owner) {
  int popped = 0;
  int removed = 0;
  if (handle == 0u || !owner || !uv_host_ensure_init()) {
    return 0;
  }
  popped = uv_host_pop_frame(handle, UV_HOST_FRAME_RETIRED);
  if (!popped) {
    return 0;
  }
  uv_platform_mutex_lock(&uv_host_lock);
  removed = uv_host_remove_session_for_owner(handle, owner, NULL);
  uv_platform_mutex_unlock(&uv_host_lock);
  return removed;
}

int uv_host_session_abort_retired(uint64_t handle,
                                       const void* owner,
                                       void* env) {
  void* frame_env = NULL;
  int removed_frame = 0;
  int removed = 0;
  if (handle == 0u || !owner || !env || !uv_host_ensure_init()) {
    return 0;
  }
  removed_frame =
      uv_host_remove_matching_frame(handle, UV_HOST_FRAME_RETIRED, &frame_env);
  uv_platform_mutex_lock(&uv_host_lock);
  removed = uv_host_remove_session_for_owner(handle, owner, NULL);
  uv_platform_mutex_unlock(&uv_host_lock);
  if (!removed) {
    return 0;
  }
  if (!removed_frame) {
    return 1;
  }
  return frame_env == NULL || frame_env == env ? 1 : 0;
}
