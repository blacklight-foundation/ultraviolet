#include "../internal/rt_internal.h"

typedef struct C0HostSessionNode {
  uint64_t handle;
  const void* owner;
  void* env;
  int state;
  struct C0HostSessionNode* next;
} C0HostSessionNode;

typedef struct C0HostSessionTlsFrame {
  uint64_t handle;
  int kind;
  void* env;
  struct C0HostSessionTlsFrame* prev;
} C0HostSessionTlsFrame;

typedef struct C0HostThreadState {
  C0HostSessionTlsFrame* env_stack;
} C0HostThreadState;

typedef struct C0HostSharedState {
  volatile cursive_platform_i64_t next_handle;
} C0HostSharedState;

static cursive_platform_once_t c0_host_once = CURSIVE_PLATFORM_ONCE_INIT;
static cursive_platform_mutex_t c0_host_lock;
static int c0_host_lock_ready = 0;
static cursive_platform_tls_key_t c0_host_tls_index = CURSIVE_PLATFORM_TLS_KEY_INVALID;
static C0HostThreadState c0_host_tls_fallback = {0};
static C0HostSessionNode* c0_host_sessions = NULL;
static cursive_platform_handle_t c0_host_shared_mapping = NULL;
static C0HostSharedState* c0_host_shared = NULL;

enum {
  C0_HOST_FRAME_LIVE = 1,
  C0_HOST_FRAME_RETIRED = 2,
};

enum {
  C0_HOST_SESSION_LIVE_IDLE = 1,
  C0_HOST_SESSION_LIVE_ENTERED = 2,
  C0_HOST_SESSION_RETIRED_BUSY = 3,
};

static cursive_platform_bool_t c0_host_init(cursive_platform_once_t* init_once,
                                      void* param,
                                      void** context) {
  static const wchar_t kHostSharedCounterName[] =
      L"Local\\CursiveHostSessionCounterV1";
  (void)init_once;
  (void)param;
  (void)context;
  cursive_platform_mutex_init(&c0_host_lock);
  c0_host_tls_index = cursive_platform_tls_key_create();
  if (c0_host_tls_index == CURSIVE_PLATFORM_TLS_KEY_INVALID) {
    return CURSIVE_PLATFORM_FALSE;
  }
  c0_host_shared_mapping = cursive_platform_mapping_create(CURSIVE_PLATFORM_INVALID_HANDLE,
                                                     NULL,
                                                     CURSIVE_PLATFORM_PAGE_READWRITE,
                                                     0,
                                                     (cursive_platform_u32_t)sizeof(C0HostSharedState),
                                                     kHostSharedCounterName);
  if (!c0_host_shared_mapping) {
    return CURSIVE_PLATFORM_FALSE;
  }
  c0_host_shared =
      (C0HostSharedState*)cursive_platform_mapping_view(c0_host_shared_mapping,
                                                  CURSIVE_PLATFORM_FILE_MAP_ALL_ACCESS,
                                                  0,
                                                  0,
                                                  sizeof(C0HostSharedState));
  if (!c0_host_shared) {
    return CURSIVE_PLATFORM_FALSE;
  }
  c0_host_lock_ready = 1;
  return CURSIVE_PLATFORM_TRUE;
}

static int c0_host_ensure_init(void) {
  return cursive_platform_once_execute(&c0_host_once, c0_host_init, NULL, NULL) != 0;
}

static C0HostThreadState* c0_host_thread_state(void) {
  C0HostThreadState* state = NULL;
  if (!c0_host_ensure_init()) {
    return &c0_host_tls_fallback;
  }

  state = (C0HostThreadState*)cursive_platform_tls_get(c0_host_tls_index);
  if (!state) {
    state = (C0HostThreadState*)c0_heap_alloc_raw(sizeof(C0HostThreadState));
    if (!state) {
      return &c0_host_tls_fallback;
    }
    c0_memset(state, 0, sizeof(C0HostThreadState));
    cursive_platform_tls_set(c0_host_tls_index, state);
  }
  return state;
}

static int c0_host_push_frame(uint64_t handle, int kind, void* env) {
  C0HostThreadState* state;
  C0HostSessionTlsFrame* frame;
  if (handle == 0u || !env) {
    return 0;
  }
  state = c0_host_thread_state();
  if (!state) {
    return 0;
  }
  frame = (C0HostSessionTlsFrame*)c0_heap_alloc_raw(sizeof(C0HostSessionTlsFrame));
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

static int c0_host_pop_frame(uint64_t handle, int kind) {
  C0HostThreadState* state = c0_host_thread_state();
  C0HostSessionTlsFrame* frame;
  if (!state || !state->env_stack) {
    return 0;
  }
  frame = state->env_stack;
  if (frame->handle != handle || frame->kind != kind) {
    return 0;
  }
  state->env_stack = frame->prev;
  c0_heap_free_raw(frame);
  return 1;
}

static int c0_host_remove_matching_frame(uint64_t handle, int kind, void** out_env) {
  C0HostThreadState* state = c0_host_thread_state();
  C0HostSessionTlsFrame** link = NULL;
  C0HostSessionTlsFrame* frame = NULL;
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
      c0_heap_free_raw(frame);
      return 1;
    }
    link = &frame->prev;
  }
  return 0;
}

static int c0_host_owner_eq(const void* lhs, const void* rhs) {
  return lhs != NULL && rhs != NULL && lhs == rhs;
}

static int c0_host_session_is_live(const C0HostSessionNode* node) {
  return node && node->state != C0_HOST_SESSION_RETIRED_BUSY;
}

static int c0_host_session_is_busy(const C0HostSessionNode* node) {
  return node &&
         (node->state == C0_HOST_SESSION_LIVE_ENTERED ||
          node->state == C0_HOST_SESSION_RETIRED_BUSY);
}

static uint64_t c0_host_allocate_handle(void) {
  cursive_platform_signed_size64_t next = 0;
  if (!c0_host_shared) {
    return 0u;
  }
  next = cursive_platform_atomic_increment64(&c0_host_shared->next_handle);
  if (next <= 0) {
    return 0u;
  }
  return (uint64_t)next;
}

static C0HostSessionNode* c0_host_find_session(uint64_t handle,
                                               C0HostSessionNode*** prev_next) {
  C0HostSessionNode** link = &c0_host_sessions;
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

static int c0_host_remove_session_for_owner(uint64_t handle,
                                            const void* owner,
                                            void** out_env) {
  C0HostSessionNode** prev_next = NULL;
  C0HostSessionNode* node = NULL;
  if (out_env) {
    *out_env = NULL;
  }
  node = c0_host_find_session(handle, &prev_next);
  if (!node || !c0_host_owner_eq(node->owner, owner)) {
    return 0;
  }
  *prev_next = node->next;
  if (out_env) {
    *out_env = node->env;
  }
  c0_heap_free_raw(node);
  return 1;
}

void* cursive_host_alloc(size_t size) {
  return c0_heap_alloc_raw(size);
}

void cursive_host_free(void* ptr) {
  c0_heap_free_raw(ptr);
}

uint64_t cursive_host_session_register(const void* owner, void* env) {
  if (!owner || !env || !c0_host_ensure_init()) {
    return 0u;
  }

  C0HostSessionNode* node =
      (C0HostSessionNode*)c0_heap_alloc_raw(sizeof(C0HostSessionNode));
  if (!node) {
    return 0u;
  }

  cursive_platform_mutex_lock(&c0_host_lock);
  node->handle = c0_host_allocate_handle();
  if (node->handle == 0u) {
    cursive_platform_mutex_unlock(&c0_host_lock);
    c0_heap_free_raw(node);
    return 0u;
  }
  node->owner = owner;
  node->env = env;
  node->state = C0_HOST_SESSION_LIVE_IDLE;
  node->next = c0_host_sessions;
  c0_host_sessions = node;
  cursive_platform_mutex_unlock(&c0_host_lock);
  return node->handle;
}

int cursive_host_session_try_enter(uint64_t handle,
                                   const void* owner,
                                   void** out_env) {
  if (out_env) {
    *out_env = NULL;
  }
  if (handle == 0u || !owner || !c0_host_ensure_init()) {
    return 0;
  }

  void* env = NULL;
  cursive_platform_mutex_lock(&c0_host_lock);
  C0HostSessionNode* node = c0_host_find_session(handle, NULL);
  if (!node || !c0_host_owner_eq(node->owner, owner) ||
      !c0_host_session_is_live(node) || c0_host_session_is_busy(node)) {
    cursive_platform_mutex_unlock(&c0_host_lock);
    return 0;
  }
  node->state = C0_HOST_SESSION_LIVE_ENTERED;
  env = node->env;
  cursive_platform_mutex_unlock(&c0_host_lock);

  if (!c0_host_push_frame(handle, C0_HOST_FRAME_LIVE, env)) {
    cursive_platform_mutex_lock(&c0_host_lock);
    node = c0_host_find_session(handle, NULL);
    if (node && c0_host_owner_eq(node->owner, owner) &&
        node->state == C0_HOST_SESSION_LIVE_ENTERED) {
      node->state = C0_HOST_SESSION_LIVE_IDLE;
    }
    cursive_platform_mutex_unlock(&c0_host_lock);
    return 0;
  }

  if (out_env) {
    *out_env = env;
  }
  return 1;
}

int cursive_host_session_leave(uint64_t handle, const void* owner) {
  if (handle == 0u || !owner || !c0_host_ensure_init()) {
    return 0;
  }

  cursive_platform_mutex_lock(&c0_host_lock);
  C0HostSessionNode* node = c0_host_find_session(handle, NULL);
  if (!node || !c0_host_owner_eq(node->owner, owner) ||
      node->state != C0_HOST_SESSION_LIVE_ENTERED) {
    cursive_platform_mutex_unlock(&c0_host_lock);
    return 0;
  }
  cursive_platform_mutex_unlock(&c0_host_lock);

  if (!c0_host_pop_frame(handle, C0_HOST_FRAME_LIVE)) {
    return 0;
  }

  cursive_platform_mutex_lock(&c0_host_lock);
  node = c0_host_find_session(handle, NULL);
  if (node && c0_host_owner_eq(node->owner, owner) &&
      node->state == C0_HOST_SESSION_LIVE_ENTERED) {
    node->state = C0_HOST_SESSION_LIVE_IDLE;
    cursive_platform_mutex_unlock(&c0_host_lock);
    return 1;
  }
  cursive_platform_mutex_unlock(&c0_host_lock);
  return 0;
}

int cursive_host_session_try_retire(uint64_t handle,
                                    const void* owner,
                                    void** out_env) {
  if (out_env) {
    *out_env = NULL;
  }
  if (handle == 0u || !owner || !c0_host_ensure_init()) {
    return 0;
  }

  C0HostSessionNode* node = NULL;
  void* env = NULL;

  cursive_platform_mutex_lock(&c0_host_lock);
  node = c0_host_find_session(handle, NULL);
  if (!node || !c0_host_owner_eq(node->owner, owner) ||
      node->state != C0_HOST_SESSION_LIVE_IDLE) {
    cursive_platform_mutex_unlock(&c0_host_lock);
    return 0;
  }
  env = node->env;
  node->state = C0_HOST_SESSION_RETIRED_BUSY;
  cursive_platform_mutex_unlock(&c0_host_lock);

  if (out_env) {
    *out_env = env;
  }
  return 1;
}

int cursive_host_session_abort_live(uint64_t handle,
                                    const void* owner,
                                    void** out_env) {
  void* env = NULL;
  void* frame_env = NULL;
  int removed = 0;
  if (out_env) {
    *out_env = NULL;
  }
  if (handle == 0u || !owner || !c0_host_ensure_init()) {
    return 0;
  }

  cursive_platform_mutex_lock(&c0_host_lock);
  removed = c0_host_remove_session_for_owner(handle, owner, &env);
  cursive_platform_mutex_unlock(&c0_host_lock);
  if (!removed) {
    return 0;
  }

  if (!c0_host_remove_matching_frame(handle, C0_HOST_FRAME_LIVE, &frame_env)) {
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

void* cursive_host_session_current_env(void) {
  C0HostThreadState* state = c0_host_thread_state();
  if (!state || !state->env_stack) {
    return NULL;
  }
  return state->env_stack->env;
}

int cursive_host_session_enter_retired(uint64_t handle,
                                       const void* owner,
                                       void* env) {
  int result = 0;
  C0HostSessionNode* node = NULL;
  if (handle == 0u || !owner || !env || !c0_host_ensure_init()) {
    return 0;
  }
  cursive_platform_mutex_lock(&c0_host_lock);
  node = c0_host_find_session(handle, NULL);
  if (!node || !c0_host_owner_eq(node->owner, owner) ||
      node->state != C0_HOST_SESSION_RETIRED_BUSY || node->env != env) {
    cursive_platform_mutex_unlock(&c0_host_lock);
    return 0;
  }
  cursive_platform_mutex_unlock(&c0_host_lock);
  result = c0_host_push_frame(handle, C0_HOST_FRAME_RETIRED, env);
  if (!result) {
    return 0;
  }
  return 1;
}

int cursive_host_session_leave_retired(uint64_t handle, const void* owner) {
  int popped = 0;
  int removed = 0;
  if (handle == 0u || !owner || !c0_host_ensure_init()) {
    return 0;
  }
  popped = c0_host_pop_frame(handle, C0_HOST_FRAME_RETIRED);
  if (!popped) {
    return 0;
  }
  cursive_platform_mutex_lock(&c0_host_lock);
  removed = c0_host_remove_session_for_owner(handle, owner, NULL);
  cursive_platform_mutex_unlock(&c0_host_lock);
  return removed;
}

int cursive_host_session_abort_retired(uint64_t handle,
                                       const void* owner,
                                       void* env) {
  void* frame_env = NULL;
  int removed_frame = 0;
  int removed = 0;
  if (handle == 0u || !owner || !env || !c0_host_ensure_init()) {
    return 0;
  }
  removed_frame =
      c0_host_remove_matching_frame(handle, C0_HOST_FRAME_RETIRED, &frame_env);
  cursive_platform_mutex_lock(&c0_host_lock);
  removed = c0_host_remove_session_for_owner(handle, owner, NULL);
  cursive_platform_mutex_unlock(&c0_host_lock);
  if (!removed) {
    return 0;
  }
  if (!removed_frame) {
    return 1;
  }
  return frame_env == NULL || frame_env == env ? 1 : 0;
}
