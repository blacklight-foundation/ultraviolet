#include "../internal/rt_internal.h"

#include <stddef.h>
#include <stdint.h>

typedef struct UVKeyScope UVKeyScope;
typedef struct UVHeldKey UVHeldKey;
typedef struct UVReleasedSet UVReleasedSet;

enum {
    UV_KEY_MODE_READ = 0,
    UV_KEY_MODE_WRITE = 1,
    UV_KEY_MAX_SEGMENTS = 64,
};

struct UVKeyScope {
    UVHeldKey* head;
    UVHeldKey* tail;
};

struct UVHeldKey {
    uv_platform_thread_id_t owner_tid;
    uint8_t mode;
    char* path;
    uint64_t path_len;
    UVKeyScope* scope;
    struct UVKeyThreadState* owner_state;

    UVHeldKey* global_prev;
    UVHeldKey* global_next;

    UVHeldKey* thread_prev;
    UVHeldKey* thread_next;

    UVHeldKey* scope_prev;
    UVHeldKey* scope_next;

    UVHeldKey* released_prev;
    UVHeldKey* released_next;
};

struct UVReleasedSet {
    UVHeldKey* head;
    UVHeldKey* tail;
    uint64_t count;
};

typedef struct UVKeyThreadState {
    UVHeldKey* held_head;
    UVHeldKey* held_tail;
} UVKeyThreadState;

typedef struct UVKeySegView {
    const char* ptr;
    uint32_t len;
    uint8_t kind;
    uint8_t boundary;
} UVKeySegView;

typedef struct UVParsedKeyPath {
    const char* root;
    uint32_t root_len;
    uint32_t seg_count;
    UVKeySegView segs[UV_KEY_MAX_SEGMENTS];
} UVParsedKeyPath;

static uv_platform_once_t uv_key_once = UV_PLATFORM_ONCE_INIT;
static uv_platform_tls_key_t uv_key_tls_index = UV_PLATFORM_TLS_KEY_INVALID;
static UVKeyThreadState uv_key_tls_fallback = {0};

static uv_platform_mutex_t uv_key_lock;
static uv_platform_condition_t uv_key_cv;
static UVHeldKey* uv_key_global_head = NULL;
static UVHeldKey* uv_key_global_tail = NULL;
static uint64_t uv_key_next_ticket = 1;
static uint64_t uv_key_serving_ticket = 1;

void uv_key_release_snapshot_discard(void* released_ptr);

static uint32_t uv_key_min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static int uv_key_mem_equal(const char* lhs, uint32_t lhs_len,
                            const char* rhs, uint32_t rhs_len) {
    uint32_t i;
    if (lhs_len != rhs_len) {
        return 0;
    }
    for (i = 0; i < lhs_len; ++i) {
        if ((uint8_t)lhs[i] != (uint8_t)rhs[i]) {
            return 0;
        }
    }
    return 1;
}

static int uv_key_mem_lex(const char* lhs, uint32_t lhs_len,
                          const char* rhs, uint32_t rhs_len) {
    uint32_t i;
    uint32_t min_len = uv_key_min_u32(lhs_len, rhs_len);
    for (i = 0; i < min_len; ++i) {
        uint8_t a = (uint8_t)lhs[i];
        uint8_t b = (uint8_t)rhs[i];
        if (a < b) {
            return -1;
        }
        if (a > b) {
            return 1;
        }
    }
    if (lhs_len < rhs_len) {
        return -1;
    }
    if (lhs_len > rhs_len) {
        return 1;
    }
    return 0;
}

static uv_platform_bool_t uv_key_init(uv_platform_once_t* init_once,
                                     void* param,
                                     void** context) {
    (void)init_once;
    (void)param;
    (void)context;

    uv_platform_mutex_init(&uv_key_lock);
    uv_platform_condition_init(&uv_key_cv);

    uv_key_tls_index = uv_platform_tls_key_create();
    if (uv_key_tls_index == UV_PLATFORM_TLS_KEY_INVALID) {
        return UV_PLATFORM_FALSE;
    }
    return UV_PLATFORM_TRUE;
}

static int uv_key_ensure_init(void) {
    if (!uv_platform_once_execute(&uv_key_once, uv_key_init, NULL, NULL)) {
        return 0;
    }
    return 1;
}

static UVKeyThreadState* uv_key_thread_state(void) {
    UVKeyThreadState* state;
    if (!uv_key_ensure_init()) {
        return &uv_key_tls_fallback;
    }

    state = (UVKeyThreadState*)uv_platform_tls_get(uv_key_tls_index);
    if (!state) {
        state = (UVKeyThreadState*)uv_heap_alloc_raw(sizeof(UVKeyThreadState));
        if (!state) {
            return &uv_key_tls_fallback;
        }
        uv_memset(state, 0, sizeof(UVKeyThreadState));
        uv_platform_tls_set(uv_key_tls_index, state);
    }
    return state;
}

static void uv_key_global_link(UVHeldKey* key) {
    key->global_prev = uv_key_global_tail;
    key->global_next = NULL;
    if (uv_key_global_tail) {
        uv_key_global_tail->global_next = key;
    } else {
        uv_key_global_head = key;
    }
    uv_key_global_tail = key;
}

static void uv_key_global_unlink(UVHeldKey* key) {
    if (key->global_prev) {
        key->global_prev->global_next = key->global_next;
    } else {
        uv_key_global_head = key->global_next;
    }
    if (key->global_next) {
        key->global_next->global_prev = key->global_prev;
    } else {
        uv_key_global_tail = key->global_prev;
    }
    key->global_prev = NULL;
    key->global_next = NULL;
}

static void uv_key_thread_link(UVKeyThreadState* state, UVHeldKey* key) {
    if (!state) {
        return;
    }
    key->owner_state = state;
    key->thread_prev = state->held_tail;
    key->thread_next = NULL;
    if (state->held_tail) {
        state->held_tail->thread_next = key;
    } else {
        state->held_head = key;
    }
    state->held_tail = key;
}

static void uv_key_thread_unlink(UVHeldKey* key) {
    UVKeyThreadState* state = key ? key->owner_state : NULL;
    if (!key || !state) {
        return;
    }
    if (key->thread_prev) {
        key->thread_prev->thread_next = key->thread_next;
    } else {
        state->held_head = key->thread_next;
    }
    if (key->thread_next) {
        key->thread_next->thread_prev = key->thread_prev;
    } else {
        state->held_tail = key->thread_prev;
    }
    key->thread_prev = NULL;
    key->thread_next = NULL;
    key->owner_state = NULL;
}

static void uv_key_scope_link(UVKeyScope* scope, UVHeldKey* key) {
    if (!scope || !key) {
        return;
    }
    key->scope = scope;
    key->scope_prev = scope->tail;
    key->scope_next = NULL;
    if (scope->tail) {
        scope->tail->scope_next = key;
    } else {
        scope->head = key;
    }
    scope->tail = key;
}

static void uv_key_scope_unlink(UVHeldKey* key) {
    UVKeyScope* scope = key ? key->scope : NULL;
    if (!key || !scope) {
        return;
    }
    if (key->scope_prev) {
        key->scope_prev->scope_next = key->scope_next;
    } else {
        scope->head = key->scope_next;
    }
    if (key->scope_next) {
        key->scope_next->scope_prev = key->scope_prev;
    } else {
        scope->tail = key->scope_prev;
    }
    key->scope_prev = NULL;
    key->scope_next = NULL;
}

static void uv_key_release_link(UVReleasedSet* set, UVHeldKey* key) {
    if (!set || !key) {
        return;
    }
    key->released_prev = set->tail;
    key->released_next = NULL;
    if (set->tail) {
        set->tail->released_next = key;
    } else {
        set->head = key;
    }
    set->tail = key;
    set->count += 1;
}

static UVHeldKey* uv_key_release_pop_head(UVReleasedSet* set) {
    UVHeldKey* key;
    if (!set || !set->head) {
        return NULL;
    }
    key = set->head;
    set->head = key->released_next;
    if (set->head) {
        set->head->released_prev = NULL;
    } else {
        set->tail = NULL;
    }
    key->released_prev = NULL;
    key->released_next = NULL;
    if (set->count > 0) {
        set->count -= 1;
    }
    return key;
}

static void uv_key_free(UVHeldKey* key) {
    if (!key) {
        return;
    }
    if (key->path) {
        uv_heap_free_raw(key->path);
        key->path = NULL;
    }
    uv_heap_free_raw(key);
}

static int uv_key_parse_path(const char* text,
                             uint64_t len,
                             UVParsedKeyPath* out) {
    uint64_t pos = 0;
    uint64_t root_end = 0;
    if (!text || !out || len == 0 || len > (uint64_t)UINT32_MAX) {
        return 0;
    }

    uv_memset(out, 0, sizeof(*out));
    out->root = text;

    while (root_end < len && text[root_end] != '.') {
        root_end += 1;
    }
    out->root_len = (uint32_t)root_end;
    pos = root_end;

    while (pos < len) {
        UVKeySegView seg;
        const char* value_start;
        uint64_t value_end;

        if (text[pos] != '.') {
            return 0;
        }
        pos += 1;

        uv_memset(&seg, 0, sizeof(seg));
        if (pos + 3 <= len && text[pos] == 'b' && text[pos + 1] == 'f' && text[pos + 2] == ':') {
            seg.boundary = 1;
            seg.kind = 0;
            pos += 3;
        } else if (pos + 2 <= len && text[pos] == 'f' && text[pos + 1] == ':') {
            seg.boundary = 0;
            seg.kind = 0;
            pos += 2;
        } else if (pos + 3 <= len && text[pos] == 'b' && text[pos + 1] == 'i' && text[pos + 2] == ':') {
            seg.boundary = 1;
            seg.kind = 1;
            pos += 3;
        } else if (pos + 2 <= len && text[pos] == 'i' && text[pos + 1] == ':') {
            seg.boundary = 0;
            seg.kind = 1;
            pos += 2;
        } else {
            return 0;
        }

        value_start = text + pos;
        value_end = pos;
        while (value_end < len && text[value_end] != '.') {
            value_end += 1;
        }
        if (value_end <= pos) {
            return 0;
        }

        seg.ptr = value_start;
        seg.len = (uint32_t)(value_end - pos);

        if (out->seg_count >= UV_KEY_MAX_SEGMENTS) {
            return 0;
        }
        out->segs[out->seg_count++] = seg;
        pos = value_end;
    }

    return 1;
}

static int uv_key_is_prefix(const UVParsedKeyPath* prefix,
                            const UVParsedKeyPath* path) {
    uint32_t i;
    if (!prefix || !path) {
        return 0;
    }
    if (!uv_key_mem_equal(prefix->root, prefix->root_len,
                          path->root, path->root_len)) {
        return 0;
    }

    for (i = 0; i < prefix->seg_count; ++i) {
        const UVKeySegView* left;
        const UVKeySegView* right;
        if (i >= path->seg_count) {
            return 0;
        }
        left = &prefix->segs[i];
        right = &path->segs[i];
        if (left->kind != right->kind) {
            return 0;
        }
        if (!uv_key_mem_equal(left->ptr, left->len, right->ptr, right->len)) {
            return 0;
        }
    }

    return 1;
}

static int uv_key_paths_overlap(const char* p1,
                                uint64_t p1_len,
                                const char* p2,
                                uint64_t p2_len) {
    UVParsedKeyPath left;
    UVParsedKeyPath right;

    if (!uv_key_parse_path(p1, p1_len, &left) ||
        !uv_key_parse_path(p2, p2_len, &right)) {
        return 1;
    }
    if (uv_key_is_prefix(&left, &right)) {
        return 1;
    }
    if (uv_key_is_prefix(&right, &left)) {
        return 1;
    }
    return 0;
}

static int uv_key_modes_conflict(uint8_t lhs, uint8_t rhs) {
    return !(lhs == UV_KEY_MODE_READ && rhs == UV_KEY_MODE_READ);
}

static int uv_key_has_conflict(uv_platform_thread_id_t owner_tid,
                               const char* path,
                               uint64_t path_len,
                               uint8_t mode) {
    UVHeldKey* it = uv_key_global_head;
    while (it) {
        if (it->owner_tid != owner_tid &&
            uv_key_modes_conflict(mode, it->mode) &&
            uv_key_paths_overlap(path, path_len, it->path, it->path_len)) {
            return 1;
        }
        it = it->global_next;
    }
    return 0;
}

static char* uv_key_copy_path(const UVStringView* path_view,
                              uint64_t* out_len) {
    char* path_copy;
    uint64_t len;
    if (!path_view || !path_view->data) {
        return NULL;
    }
    len = path_view->len;
    if (len > (uint64_t)SIZE_MAX - 1) {
        return NULL;
    }

    path_copy = (char*)uv_heap_alloc_raw((size_t)(len + 1));
    if (!path_copy) {
        return NULL;
    }
    if (len > 0) {
        uv_memcpy(path_copy, path_view->data, (size_t)len);
    }
    path_copy[len] = 0;
    if (out_len) {
        *out_len = len;
    }
    return path_copy;
}

static int uv_key_order_compare(const UVHeldKey* lhs, const UVHeldKey* rhs) {
    if (!lhs || !rhs) {
        return 0;
    }
    return uv_key_mem_lex(lhs->path, (uint32_t)lhs->path_len,
                          rhs->path, (uint32_t)rhs->path_len);
}

static void uv_key_insertion_sort(UVHeldKey** keys, uint64_t count) {
    uint64_t i;
    if (!keys || count < 2) {
        return;
    }
    for (i = 1; i < count; ++i) {
        UVHeldKey* item = keys[i];
        uint64_t j = i;
        while (j > 0 && uv_key_order_compare(item, keys[j - 1]) < 0) {
            keys[j] = keys[j - 1];
            j -= 1;
        }
        keys[j] = item;
    }
}

void* uv_key_scope_enter(void) {
    UVKeyScope* scope;
    uv_trace_emit_rule("K-Block-Enter");

    if (!uv_key_ensure_init()) {
        return NULL;
    }

    scope = (UVKeyScope*)uv_heap_alloc_raw(sizeof(UVKeyScope));
    if (!scope) {
        return NULL;
    }
    uv_memset(scope, 0, sizeof(UVKeyScope));
    return scope;
}

void uv_key_check_conflict(UVStringView path, uint8_t mode) {
    uv_platform_thread_id_t owner_tid;
    uint8_t checked_mode = (mode == UV_KEY_MODE_READ) ? UV_KEY_MODE_READ
                                                      : UV_KEY_MODE_WRITE;
    if (!path.data) {
        return;
    }
    if (!uv_key_ensure_init()) {
        return;
    }

    owner_tid = uv_platform_current_thread_id();
    uv_platform_mutex_lock(&uv_key_lock);
    while (uv_key_has_conflict(owner_tid, path.data, path.len, checked_mode)) {
        uv_platform_condition_wait(&uv_key_cv, &uv_key_lock, UV_PLATFORM_INFINITE);
    }
    uv_platform_mutex_unlock(&uv_key_lock);
}

void* uv_key_release_one(void* scope_ptr, UVStringView path) {
    UVKeyScope* scope = (UVKeyScope*)scope_ptr;
    UVHeldKey* key;

    if (!scope || !path.data) {
        return NULL;
    }
    if (!uv_key_ensure_init()) {
        return NULL;
    }

    uv_platform_mutex_lock(&uv_key_lock);
    key = scope->tail;
    while (key) {
        UVHeldKey* prev = key->scope_prev;
        if (uv_key_mem_equal(key->path, (uint32_t)key->path_len,
                             path.data, (uint32_t)path.len)) {
            uv_key_scope_unlink(key);
            uv_key_thread_unlink(key);
            uv_key_global_unlink(key);
            uv_platform_mutex_unlock(&uv_key_lock);
            uv_platform_condition_wake_all(&uv_key_cv);
            return key;
        }
        key = prev;
    }
    uv_platform_mutex_unlock(&uv_key_lock);
    return NULL;
}

void uv_key_scope_exit(void* scope_ptr) {
    UVKeyScope* scope = (UVKeyScope*)scope_ptr;
    UVHeldKey* key;

    uv_trace_emit_rule("K-Block-Exit");
    if (!scope) {
        return;
    }
    if (!uv_key_ensure_init()) {
        uv_heap_free_raw(scope);
        return;
    }

    uv_platform_mutex_lock(&uv_key_lock);
    key = scope->tail;
    while (key) {
        UVHeldKey* prev = key->scope_prev;
        uv_key_scope_unlink(key);
        uv_key_thread_unlink(key);
        uv_key_global_unlink(key);
        uv_key_free(key);
        key = prev;
    }
    uv_platform_mutex_unlock(&uv_key_lock);
    uv_platform_condition_wake_all(&uv_key_cv);

    uv_heap_free_raw(scope);
}

void uv_key_acquire(void* scope_ptr, UVStringView path, uint8_t mode) {
    UVHeldKey* key;
    UVKeyScope* scope = (UVKeyScope*)scope_ptr;
    UVKeyThreadState* state = uv_key_thread_state();
    uint64_t ticket;

    uv_trace_emit_rule("K-Acquire");
    if (!scope || !path.data) {
        return;
    }
    if (!uv_key_ensure_init()) {
        return;
    }

    key = (UVHeldKey*)uv_heap_alloc_raw(sizeof(UVHeldKey));
    if (!key) {
        return;
    }
    uv_memset(key, 0, sizeof(UVHeldKey));

    key->path = uv_key_copy_path(&path, &key->path_len);
    if (!key->path) {
        uv_heap_free_raw(key);
        return;
    }

    key->mode = (mode == UV_KEY_MODE_READ) ? UV_KEY_MODE_READ : UV_KEY_MODE_WRITE;
    key->owner_tid = uv_platform_current_thread_id();

    uv_platform_mutex_lock(&uv_key_lock);
    ticket = uv_key_next_ticket++;
    while (ticket != uv_key_serving_ticket ||
           uv_key_has_conflict(key->owner_tid, key->path, key->path_len, key->mode)) {
        uv_platform_condition_wait(&uv_key_cv, &uv_key_lock, UV_PLATFORM_INFINITE);
    }

    uv_key_global_link(key);
    uv_key_thread_link(state, key);
    uv_key_scope_link(scope, key);
    uv_key_serving_ticket += 1;

    uv_platform_mutex_unlock(&uv_key_lock);
    uv_platform_condition_wake_all(&uv_key_cv);
}

void* uv_key_release_all(void) {
    UVReleasedSet* released;
    UVKeyThreadState* state = uv_key_thread_state();
    UVHeldKey* key;

    uv_trace_emit_rule("K-Yield-Release");
    if (!uv_key_ensure_init()) {
        return NULL;
    }

    released = (UVReleasedSet*)uv_heap_alloc_raw(sizeof(UVReleasedSet));
    if (!released) {
        return NULL;
    }
    uv_memset(released, 0, sizeof(UVReleasedSet));

    uv_platform_mutex_lock(&uv_key_lock);
    key = state->held_tail;
    while (key) {
        UVHeldKey* prev = key->thread_prev;
        uv_key_scope_unlink(key);
        uv_key_thread_unlink(key);
        uv_key_global_unlink(key);
        uv_key_release_link(released, key);
        key = prev;
    }
    uv_platform_mutex_unlock(&uv_key_lock);

    uv_platform_condition_wake_all(&uv_key_cv);

    if (released->count == 0) {
        uv_heap_free_raw(released);
        return NULL;
    }
    return released;
}

void uv_key_reacquire_one(void* released_ptr) {
    UVHeldKey* key = (UVHeldKey*)released_ptr;
    UVKeyThreadState* state = uv_key_thread_state();
    uv_platform_thread_id_t owner_tid;
    uint64_t ticket;

    if (!key) {
        return;
    }
    if (!uv_key_ensure_init()) {
        uv_key_free(key);
        return;
    }

    owner_tid = uv_platform_current_thread_id();
    uv_platform_mutex_lock(&uv_key_lock);
    key->owner_tid = owner_tid;
    ticket = uv_key_next_ticket++;
    while (ticket != uv_key_serving_ticket ||
           uv_key_has_conflict(owner_tid, key->path, key->path_len, key->mode)) {
        uv_platform_condition_wait(&uv_key_cv, &uv_key_lock, UV_PLATFORM_INFINITE);
    }
    uv_key_global_link(key);
    uv_key_thread_link(state, key);
    uv_key_scope_link(key->scope, key);
    uv_key_serving_ticket += 1;
    uv_platform_mutex_unlock(&uv_key_lock);
    uv_platform_condition_wake_all(&uv_key_cv);
}

void uv_key_reacquire(void* released_ptr) {
    UVReleasedSet* released = (UVReleasedSet*)released_ptr;
    UVKeyThreadState* state = uv_key_thread_state();
    UVHeldKey** ordered;
    UVHeldKey* key;
    uint64_t idx = 0;
    uint64_t count;
    uv_platform_thread_id_t owner_tid;

    uv_trace_emit_rule("K-Reacquire-After-Release");
    if (!released) {
        return;
    }
    if (!uv_key_ensure_init()) {
        uv_key_release_snapshot_discard(released_ptr);
        return;
    }

    count = released->count;
    if (count == 0) {
        uv_heap_free_raw(released);
        return;
    }

    ordered = (UVHeldKey**)uv_heap_alloc_raw((size_t)(count * sizeof(UVHeldKey*)));
    if (!ordered) {
        uv_key_release_snapshot_discard(released_ptr);
        return;
    }

    key = released->head;
    while (key && idx < count) {
        ordered[idx++] = key;
        key = key->released_next;
    }
    count = idx;

    uv_key_insertion_sort(ordered, count);

    owner_tid = uv_platform_current_thread_id();
    uv_platform_mutex_lock(&uv_key_lock);
    for (idx = 0; idx < count; ++idx) {
        UVHeldKey* held = ordered[idx];
        uint64_t ticket = uv_key_next_ticket++;
        held->released_prev = NULL;
        held->released_next = NULL;
        held->owner_tid = owner_tid;

        while (ticket != uv_key_serving_ticket ||
               uv_key_has_conflict(owner_tid, held->path, held->path_len, held->mode)) {
            uv_platform_condition_wait(&uv_key_cv, &uv_key_lock, UV_PLATFORM_INFINITE);
        }

        uv_key_global_link(held);
        uv_key_thread_link(state, held);
        uv_key_scope_link(held->scope, held);
        uv_key_serving_ticket += 1;
    }
    uv_platform_mutex_unlock(&uv_key_lock);

    uv_platform_condition_wake_all(&uv_key_cv);

    uv_heap_free_raw(ordered);
    uv_heap_free_raw(released);
}

void uv_key_release_snapshot_discard(void* released_ptr) {
    UVReleasedSet* released = (UVReleasedSet*)released_ptr;
    UVHeldKey* key;
    if (!released) {
        return;
    }

    key = uv_key_release_pop_head(released);
    while (key) {
        uv_key_free(key);
        key = uv_key_release_pop_head(released);
    }

    uv_heap_free_raw(released);
}
