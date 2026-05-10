#include "../internal/rt_internal.h"

#include <stddef.h>
#include <stdint.h>

typedef struct C0KeyScope C0KeyScope;
typedef struct C0HeldKey C0HeldKey;
typedef struct C0ReleasedSet C0ReleasedSet;

enum {
    C0_KEY_MODE_READ = 0,
    C0_KEY_MODE_WRITE = 1,
    C0_KEY_MAX_SEGMENTS = 64,
};

struct C0KeyScope {
    C0HeldKey* head;
    C0HeldKey* tail;
};

struct C0HeldKey {
    cursive_platform_thread_id_t owner_tid;
    uint8_t mode;
    char* path;
    uint64_t path_len;
    C0KeyScope* scope;
    struct C0KeyThreadState* owner_state;

    C0HeldKey* global_prev;
    C0HeldKey* global_next;

    C0HeldKey* thread_prev;
    C0HeldKey* thread_next;

    C0HeldKey* scope_prev;
    C0HeldKey* scope_next;

    C0HeldKey* released_prev;
    C0HeldKey* released_next;
};

struct C0ReleasedSet {
    C0HeldKey* head;
    C0HeldKey* tail;
    uint64_t count;
};

typedef struct C0KeyThreadState {
    C0HeldKey* held_head;
    C0HeldKey* held_tail;
} C0KeyThreadState;

typedef struct C0KeySegView {
    const char* ptr;
    uint32_t len;
    uint8_t kind;
    uint8_t boundary;
} C0KeySegView;

typedef struct C0ParsedKeyPath {
    const char* root;
    uint32_t root_len;
    uint32_t seg_count;
    C0KeySegView segs[C0_KEY_MAX_SEGMENTS];
} C0ParsedKeyPath;

static cursive_platform_once_t c0_key_once = CURSIVE_PLATFORM_ONCE_INIT;
static cursive_platform_tls_key_t c0_key_tls_index = CURSIVE_PLATFORM_TLS_KEY_INVALID;
static C0KeyThreadState c0_key_tls_fallback = {0};

static cursive_platform_mutex_t c0_key_lock;
static cursive_platform_condition_t c0_key_cv;
static C0HeldKey* c0_key_global_head = NULL;
static C0HeldKey* c0_key_global_tail = NULL;
static uint64_t c0_key_next_ticket = 1;
static uint64_t c0_key_serving_ticket = 1;

void cursive_key_release_snapshot_discard(void* released_ptr);

static uint32_t c0_key_min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static int c0_key_mem_equal(const char* lhs, uint32_t lhs_len,
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

static int c0_key_mem_lex(const char* lhs, uint32_t lhs_len,
                          const char* rhs, uint32_t rhs_len) {
    uint32_t i;
    uint32_t min_len = c0_key_min_u32(lhs_len, rhs_len);
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

static cursive_platform_bool_t c0_key_init(cursive_platform_once_t* init_once,
                                     void* param,
                                     void** context) {
    (void)init_once;
    (void)param;
    (void)context;

    cursive_platform_mutex_init(&c0_key_lock);
    cursive_platform_condition_init(&c0_key_cv);

    c0_key_tls_index = cursive_platform_tls_key_create();
    if (c0_key_tls_index == CURSIVE_PLATFORM_TLS_KEY_INVALID) {
        return CURSIVE_PLATFORM_FALSE;
    }
    return CURSIVE_PLATFORM_TRUE;
}

static int c0_key_ensure_init(void) {
    if (!cursive_platform_once_execute(&c0_key_once, c0_key_init, NULL, NULL)) {
        return 0;
    }
    return 1;
}

static C0KeyThreadState* c0_key_thread_state(void) {
    C0KeyThreadState* state;
    if (!c0_key_ensure_init()) {
        return &c0_key_tls_fallback;
    }

    state = (C0KeyThreadState*)cursive_platform_tls_get(c0_key_tls_index);
    if (!state) {
        state = (C0KeyThreadState*)c0_heap_alloc_raw(sizeof(C0KeyThreadState));
        if (!state) {
            return &c0_key_tls_fallback;
        }
        c0_memset(state, 0, sizeof(C0KeyThreadState));
        cursive_platform_tls_set(c0_key_tls_index, state);
    }
    return state;
}

static void c0_key_global_link(C0HeldKey* key) {
    key->global_prev = c0_key_global_tail;
    key->global_next = NULL;
    if (c0_key_global_tail) {
        c0_key_global_tail->global_next = key;
    } else {
        c0_key_global_head = key;
    }
    c0_key_global_tail = key;
}

static void c0_key_global_unlink(C0HeldKey* key) {
    if (key->global_prev) {
        key->global_prev->global_next = key->global_next;
    } else {
        c0_key_global_head = key->global_next;
    }
    if (key->global_next) {
        key->global_next->global_prev = key->global_prev;
    } else {
        c0_key_global_tail = key->global_prev;
    }
    key->global_prev = NULL;
    key->global_next = NULL;
}

static void c0_key_thread_link(C0KeyThreadState* state, C0HeldKey* key) {
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

static void c0_key_thread_unlink(C0HeldKey* key) {
    C0KeyThreadState* state = key ? key->owner_state : NULL;
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

static void c0_key_scope_link(C0KeyScope* scope, C0HeldKey* key) {
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

static void c0_key_scope_unlink(C0HeldKey* key) {
    C0KeyScope* scope = key ? key->scope : NULL;
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

static void c0_key_release_link(C0ReleasedSet* set, C0HeldKey* key) {
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

static C0HeldKey* c0_key_release_pop_head(C0ReleasedSet* set) {
    C0HeldKey* key;
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

static void c0_key_free(C0HeldKey* key) {
    if (!key) {
        return;
    }
    if (key->path) {
        c0_heap_free_raw(key->path);
        key->path = NULL;
    }
    c0_heap_free_raw(key);
}

static int c0_key_parse_path(const char* text,
                             uint64_t len,
                             C0ParsedKeyPath* out) {
    uint64_t pos = 0;
    uint64_t root_end = 0;
    if (!text || !out || len == 0 || len > (uint64_t)UINT32_MAX) {
        return 0;
    }

    c0_memset(out, 0, sizeof(*out));
    out->root = text;

    while (root_end < len && text[root_end] != '.') {
        root_end += 1;
    }
    out->root_len = (uint32_t)root_end;
    pos = root_end;

    while (pos < len) {
        C0KeySegView seg;
        const char* value_start;
        uint64_t value_end;

        if (text[pos] != '.') {
            return 0;
        }
        pos += 1;

        c0_memset(&seg, 0, sizeof(seg));
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

        if (out->seg_count >= C0_KEY_MAX_SEGMENTS) {
            return 0;
        }
        out->segs[out->seg_count++] = seg;
        pos = value_end;
    }

    return 1;
}

static int c0_key_is_prefix(const C0ParsedKeyPath* prefix,
                            const C0ParsedKeyPath* path) {
    uint32_t i;
    if (!prefix || !path) {
        return 0;
    }
    if (!c0_key_mem_equal(prefix->root, prefix->root_len,
                          path->root, path->root_len)) {
        return 0;
    }

    for (i = 0; i < prefix->seg_count; ++i) {
        const C0KeySegView* left;
        const C0KeySegView* right;
        if (i >= path->seg_count) {
            return 0;
        }
        left = &prefix->segs[i];
        right = &path->segs[i];
        if (left->kind != right->kind) {
            return 0;
        }
        if (!c0_key_mem_equal(left->ptr, left->len, right->ptr, right->len)) {
            return 0;
        }
    }

    return 1;
}

static int c0_key_paths_overlap(const char* p1,
                                uint64_t p1_len,
                                const char* p2,
                                uint64_t p2_len) {
    C0ParsedKeyPath left;
    C0ParsedKeyPath right;

    if (!c0_key_parse_path(p1, p1_len, &left) ||
        !c0_key_parse_path(p2, p2_len, &right)) {
        return 1;
    }
    if (c0_key_is_prefix(&left, &right)) {
        return 1;
    }
    if (c0_key_is_prefix(&right, &left)) {
        return 1;
    }
    return 0;
}

static int c0_key_modes_conflict(uint8_t lhs, uint8_t rhs) {
    return !(lhs == C0_KEY_MODE_READ && rhs == C0_KEY_MODE_READ);
}

static int c0_key_has_conflict(cursive_platform_thread_id_t owner_tid,
                               const char* path,
                               uint64_t path_len,
                               uint8_t mode) {
    C0HeldKey* it = c0_key_global_head;
    while (it) {
        if (it->owner_tid != owner_tid &&
            c0_key_modes_conflict(mode, it->mode) &&
            c0_key_paths_overlap(path, path_len, it->path, it->path_len)) {
            return 1;
        }
        it = it->global_next;
    }
    return 0;
}

static char* c0_key_copy_path(const C0StringView* path_view,
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

    path_copy = (char*)c0_heap_alloc_raw((size_t)(len + 1));
    if (!path_copy) {
        return NULL;
    }
    if (len > 0) {
        c0_memcpy(path_copy, path_view->data, (size_t)len);
    }
    path_copy[len] = 0;
    if (out_len) {
        *out_len = len;
    }
    return path_copy;
}

static int c0_key_order_compare(const C0HeldKey* lhs, const C0HeldKey* rhs) {
    if (!lhs || !rhs) {
        return 0;
    }
    return c0_key_mem_lex(lhs->path, (uint32_t)lhs->path_len,
                          rhs->path, (uint32_t)rhs->path_len);
}

static void c0_key_insertion_sort(C0HeldKey** keys, uint64_t count) {
    uint64_t i;
    if (!keys || count < 2) {
        return;
    }
    for (i = 1; i < count; ++i) {
        C0HeldKey* item = keys[i];
        uint64_t j = i;
        while (j > 0 && c0_key_order_compare(item, keys[j - 1]) < 0) {
            keys[j] = keys[j - 1];
            j -= 1;
        }
        keys[j] = item;
    }
}

void* cursive_key_scope_enter(void) {
    C0KeyScope* scope;
    c0_trace_emit_rule("K-Block-Enter");

    if (!c0_key_ensure_init()) {
        return NULL;
    }

    scope = (C0KeyScope*)c0_heap_alloc_raw(sizeof(C0KeyScope));
    if (!scope) {
        return NULL;
    }
    c0_memset(scope, 0, sizeof(C0KeyScope));
    return scope;
}

void cursive_key_check_conflict(C0StringView path, uint8_t mode) {
    cursive_platform_thread_id_t owner_tid;
    uint8_t checked_mode = (mode == C0_KEY_MODE_READ) ? C0_KEY_MODE_READ
                                                      : C0_KEY_MODE_WRITE;
    if (!path.data) {
        return;
    }
    if (!c0_key_ensure_init()) {
        return;
    }

    owner_tid = cursive_platform_current_thread_id();
    cursive_platform_mutex_lock(&c0_key_lock);
    while (c0_key_has_conflict(owner_tid, path.data, path.len, checked_mode)) {
        cursive_platform_condition_wait(&c0_key_cv, &c0_key_lock, CURSIVE_PLATFORM_INFINITE);
    }
    cursive_platform_mutex_unlock(&c0_key_lock);
}

void* cursive_key_release_one(void* scope_ptr, C0StringView path) {
    C0KeyScope* scope = (C0KeyScope*)scope_ptr;
    C0HeldKey* key;

    if (!scope || !path.data) {
        return NULL;
    }
    if (!c0_key_ensure_init()) {
        return NULL;
    }

    cursive_platform_mutex_lock(&c0_key_lock);
    key = scope->tail;
    while (key) {
        C0HeldKey* prev = key->scope_prev;
        if (c0_key_mem_equal(key->path, (uint32_t)key->path_len,
                             path.data, (uint32_t)path.len)) {
            c0_key_scope_unlink(key);
            c0_key_thread_unlink(key);
            c0_key_global_unlink(key);
            cursive_platform_mutex_unlock(&c0_key_lock);
            cursive_platform_condition_wake_all(&c0_key_cv);
            return key;
        }
        key = prev;
    }
    cursive_platform_mutex_unlock(&c0_key_lock);
    return NULL;
}

void cursive_key_scope_exit(void* scope_ptr) {
    C0KeyScope* scope = (C0KeyScope*)scope_ptr;
    C0HeldKey* key;

    c0_trace_emit_rule("K-Block-Exit");
    if (!scope) {
        return;
    }
    if (!c0_key_ensure_init()) {
        c0_heap_free_raw(scope);
        return;
    }

    cursive_platform_mutex_lock(&c0_key_lock);
    key = scope->tail;
    while (key) {
        C0HeldKey* prev = key->scope_prev;
        c0_key_scope_unlink(key);
        c0_key_thread_unlink(key);
        c0_key_global_unlink(key);
        c0_key_free(key);
        key = prev;
    }
    cursive_platform_mutex_unlock(&c0_key_lock);
    cursive_platform_condition_wake_all(&c0_key_cv);

    c0_heap_free_raw(scope);
}

void cursive_key_acquire(void* scope_ptr, C0StringView path, uint8_t mode) {
    C0HeldKey* key;
    C0KeyScope* scope = (C0KeyScope*)scope_ptr;
    C0KeyThreadState* state = c0_key_thread_state();
    uint64_t ticket;

    c0_trace_emit_rule("K-Acquire");
    if (!scope || !path.data) {
        return;
    }
    if (!c0_key_ensure_init()) {
        return;
    }

    key = (C0HeldKey*)c0_heap_alloc_raw(sizeof(C0HeldKey));
    if (!key) {
        return;
    }
    c0_memset(key, 0, sizeof(C0HeldKey));

    key->path = c0_key_copy_path(&path, &key->path_len);
    if (!key->path) {
        c0_heap_free_raw(key);
        return;
    }

    key->mode = (mode == C0_KEY_MODE_READ) ? C0_KEY_MODE_READ : C0_KEY_MODE_WRITE;
    key->owner_tid = cursive_platform_current_thread_id();

    cursive_platform_mutex_lock(&c0_key_lock);
    ticket = c0_key_next_ticket++;
    while (ticket != c0_key_serving_ticket ||
           c0_key_has_conflict(key->owner_tid, key->path, key->path_len, key->mode)) {
        cursive_platform_condition_wait(&c0_key_cv, &c0_key_lock, CURSIVE_PLATFORM_INFINITE);
    }

    c0_key_global_link(key);
    c0_key_thread_link(state, key);
    c0_key_scope_link(scope, key);
    c0_key_serving_ticket += 1;

    cursive_platform_mutex_unlock(&c0_key_lock);
    cursive_platform_condition_wake_all(&c0_key_cv);
}

void* cursive_key_release_all(void) {
    C0ReleasedSet* released;
    C0KeyThreadState* state = c0_key_thread_state();
    C0HeldKey* key;

    c0_trace_emit_rule("K-Yield-Release");
    if (!c0_key_ensure_init()) {
        return NULL;
    }

    released = (C0ReleasedSet*)c0_heap_alloc_raw(sizeof(C0ReleasedSet));
    if (!released) {
        return NULL;
    }
    c0_memset(released, 0, sizeof(C0ReleasedSet));

    cursive_platform_mutex_lock(&c0_key_lock);
    key = state->held_tail;
    while (key) {
        C0HeldKey* prev = key->thread_prev;
        c0_key_scope_unlink(key);
        c0_key_thread_unlink(key);
        c0_key_global_unlink(key);
        c0_key_release_link(released, key);
        key = prev;
    }
    cursive_platform_mutex_unlock(&c0_key_lock);

    cursive_platform_condition_wake_all(&c0_key_cv);

    if (released->count == 0) {
        c0_heap_free_raw(released);
        return NULL;
    }
    return released;
}

void cursive_key_reacquire_one(void* released_ptr) {
    C0HeldKey* key = (C0HeldKey*)released_ptr;
    C0KeyThreadState* state = c0_key_thread_state();
    cursive_platform_thread_id_t owner_tid;
    uint64_t ticket;

    if (!key) {
        return;
    }
    if (!c0_key_ensure_init()) {
        c0_key_free(key);
        return;
    }

    owner_tid = cursive_platform_current_thread_id();
    cursive_platform_mutex_lock(&c0_key_lock);
    key->owner_tid = owner_tid;
    ticket = c0_key_next_ticket++;
    while (ticket != c0_key_serving_ticket ||
           c0_key_has_conflict(owner_tid, key->path, key->path_len, key->mode)) {
        cursive_platform_condition_wait(&c0_key_cv, &c0_key_lock, CURSIVE_PLATFORM_INFINITE);
    }
    c0_key_global_link(key);
    c0_key_thread_link(state, key);
    c0_key_scope_link(key->scope, key);
    c0_key_serving_ticket += 1;
    cursive_platform_mutex_unlock(&c0_key_lock);
    cursive_platform_condition_wake_all(&c0_key_cv);
}

void cursive_key_reacquire(void* released_ptr) {
    C0ReleasedSet* released = (C0ReleasedSet*)released_ptr;
    C0KeyThreadState* state = c0_key_thread_state();
    C0HeldKey** ordered;
    C0HeldKey* key;
    uint64_t idx = 0;
    uint64_t count;
    cursive_platform_thread_id_t owner_tid;

    c0_trace_emit_rule("K-Reacquire-After-Release");
    if (!released) {
        return;
    }
    if (!c0_key_ensure_init()) {
        cursive_key_release_snapshot_discard(released_ptr);
        return;
    }

    count = released->count;
    if (count == 0) {
        c0_heap_free_raw(released);
        return;
    }

    ordered = (C0HeldKey**)c0_heap_alloc_raw((size_t)(count * sizeof(C0HeldKey*)));
    if (!ordered) {
        cursive_key_release_snapshot_discard(released_ptr);
        return;
    }

    key = released->head;
    while (key && idx < count) {
        ordered[idx++] = key;
        key = key->released_next;
    }
    count = idx;

    c0_key_insertion_sort(ordered, count);

    owner_tid = cursive_platform_current_thread_id();
    cursive_platform_mutex_lock(&c0_key_lock);
    for (idx = 0; idx < count; ++idx) {
        C0HeldKey* held = ordered[idx];
        uint64_t ticket = c0_key_next_ticket++;
        held->released_prev = NULL;
        held->released_next = NULL;
        held->owner_tid = owner_tid;

        while (ticket != c0_key_serving_ticket ||
               c0_key_has_conflict(owner_tid, held->path, held->path_len, held->mode)) {
            cursive_platform_condition_wait(&c0_key_cv, &c0_key_lock, CURSIVE_PLATFORM_INFINITE);
        }

        c0_key_global_link(held);
        c0_key_thread_link(state, held);
        c0_key_scope_link(held->scope, held);
        c0_key_serving_ticket += 1;
    }
    cursive_platform_mutex_unlock(&c0_key_lock);

    cursive_platform_condition_wake_all(&c0_key_cv);

    c0_heap_free_raw(ordered);
    c0_heap_free_raw(released);
}

void cursive_key_release_snapshot_discard(void* released_ptr) {
    C0ReleasedSet* released = (C0ReleasedSet*)released_ptr;
    C0HeldKey* key;
    if (!released) {
        return;
    }

    key = c0_key_release_pop_head(released);
    while (key) {
        c0_key_free(key);
        key = c0_key_release_pop_head(released);
    }

    c0_heap_free_raw(released);
}
