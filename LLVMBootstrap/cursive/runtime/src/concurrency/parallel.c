// C0X Extension: Structured Concurrency Runtime Support (§18)
// 
// This file provides runtime support for:
// - §18.1 Parallel blocks (fork-join semantics)
// - §18.4 Spawn/wait (task management)
// - §18.5 Dispatch (data parallelism)
// - §18.6 Cancellation
// - §18.7 Panic handling in parallel contexts

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#include "../internal/rt_internal.h"
// Note: <string.h> is NOT included - we use c0_memset/c0_memcpy from rt_internal.h

// Ensure INT64_MAX/MIN are defined
#ifndef INT64_MAX
#define INT64_MAX 9223372036854775807LL
#endif
#ifndef INT64_MIN
#define INT64_MIN (-9223372036854775807LL - 1)
#endif

// Forward declarations for internal types
typedef struct WorkItem WorkItem;
typedef struct WorkerPool WorkerPool;
typedef struct ParallelContext ParallelContext;
typedef struct SpawnHandle SpawnHandle;
typedef size_t C0CancelId;
static void c0_parallel_context_panic(ParallelContext* ctx, uint32_t code);
static cursive_rt_u32_t c0_worker_thread_proc(void* param);
static void c0_start_worker_threads(WorkerPool* pool);

// -----------------------------------------------------------------------------
// Panic boundary + TLS state
// -----------------------------------------------------------------------------

#define C0_PANIC_REDUCED_EMPTY_DISPATCH 2862u

typedef struct {
    ParallelContext* ctx;
    WorkItem* item;
} C0ThreadState;
static cursive_rt_once_t c0_tls_once = CURSIVE_RT_ONCE_INIT;
static cursive_rt_tls_key_t c0_tls_index = CURSIVE_RT_TLS_KEY_INVALID;
static C0ThreadState c0_tls_fallback = {0};
static volatile cursive_rt_i64_t c0_next_task_id = 0;
static volatile cursive_rt_i64_t c0_next_completion_seq = 0;

static cursive_rt_bool_t c0_tls_init(cursive_rt_once_t* init_once,
                                     void* param,
                                     void** context) {
    (void)init_once;
    (void)param;
    (void)context;
    cursive_rt_tls_key_t idx = cursive_rt_tls_key_create();
    if (idx == CURSIVE_RT_TLS_KEY_INVALID) {
        return CURSIVE_RT_FALSE;
    }
    c0_tls_index = idx;
    return CURSIVE_RT_TRUE;
}

static C0ThreadState* c0_tls_state(void) {
    if (!cursive_rt_once_execute(&c0_tls_once, c0_tls_init, NULL, NULL)) {
        return &c0_tls_fallback;
    }
    C0ThreadState* state = (C0ThreadState*)cursive_rt_tls_get(c0_tls_index);
    if (!state) {
        state = (C0ThreadState*)c0_heap_alloc_raw(sizeof(C0ThreadState));
        if (!state) {
            return &c0_tls_fallback;
        }
        state->ctx = NULL;
        state->item = NULL;
        cursive_rt_tls_set(c0_tls_index, state);
    }
    return state;
}

static uint64_t c0_fresh_task_id(void) {
    return (uint64_t)cursive_rt_atomic_increment64(&c0_next_task_id);
}

static uint64_t c0_fresh_completion_seq(void) {
    return (uint64_t)cursive_rt_atomic_increment64(&c0_next_completion_seq);
}

static ParallelContext* c0_current_ctx(void) {
    return c0_tls_state()->ctx;
}

static WorkItem* c0_current_item(void) {
    return c0_tls_state()->item;
}

static void c0_set_current_ctx(ParallelContext* ctx) {
    c0_tls_state()->ctx = ctx;
}

static void c0_set_current_item(WorkItem* item) {
    c0_tls_state()->item = item;
}
// §18.1.2 Work item state
typedef enum {
    WORK_PENDING,
    WORK_RUNNING,
    WORK_COMPLETED,
    WORK_CANCELLED,
    WORK_PANICKED
} WorkState;

typedef enum {
    SPAWN_HANDLE_PENDING,
    SPAWN_HANDLE_READY,
    SPAWN_HANDLE_FAILED
} SpawnHandleStateTag;

// Work item (created by spawn/dispatch)
struct WorkItem {
    WorkState state;
    uint64_t task_id;        // Stable creation identifier
    uint64_t completion_seq; // Global completion identifier once settled
    ParallelContext* owner_ctx;
    void* captured_env;      // Captured environment
    void* hosted_env;        // Hosted session environment
    void (*body)(void* hosted_env, void* env, void* result, void* panic_out);     // Work function
    void* result;            // Result value
    size_t result_size;      // Size of result
    uint64_t affinity_mask;  // CpuSet affinity hint (0 => domain default)
    int32_t priority_hint;   // Priority::Low(0) / Normal(1) / High(2)
    uint32_t panic_code;     // Panic code if panicked
    WorkItem* next;          // Linked list for work queue
    WorkItem* all_next;      // Linked list for cleanup
    cursive_rt_handle_t done_event;       // Signaled on completion
    SpawnHandle* handle;     // Owning handle
};

// §18.4.2 Spawned runtime representation (internal: SpawnHandle)
struct SpawnHandle {
    uint64_t id;
    SpawnHandleStateTag state;
    WorkItem* item;
    SpawnHandle* next;
};

enum {
    C0_CANCEL_STATUS_ACTIVE = 0,
    C0_CANCEL_STATUS_CANCELLED = 1
};

#define C0_CANCEL_INVALID_ID ((C0CancelId)SIZE_MAX)

typedef struct {
    C0CancelId parent;
    uint8_t status;
    uint8_t _pad[7];
} C0CancelStateEntry;

typedef struct {
    cursive_rt_mutex_t lock;
    C0CancelStateEntry* entries;
    size_t count;
    size_t capacity;
} C0CancelRegistry;

static cursive_rt_once_t c0_cancel_registry_once = CURSIVE_RT_ONCE_INIT;
static C0CancelRegistry c0_cancel_registry = {0};
static int c0_token_is_cancelled(C0CancelId token_id);

typedef struct C0CancelWaitFrame {
    uint64_t resume_state;
    void* resume_fn;
    void* hosted_env;
    C0CancelId token_id;
} C0CancelWaitFrame;

enum {
    C0_ASYNC_DISC_SUSPENDED_LOCAL = 0,
    C0_ASYNC_DISC_COMPLETED_LOCAL = 1,
    C0_ASYNC_PAYLOAD_FRAME_PTR_OFFSET_LOCAL = 8,
};

static void c0_cancel_wait_write_completed(C0AsyncResumeValue* out) {
    if (!out) {
        return;
    }
    c0_memset(out, 0, sizeof(*out));
    out->disc = C0_ASYNC_DISC_COMPLETED_LOCAL;
}

static void c0_cancel_wait_write_suspended(C0AsyncResumeValue* out,
                                           C0CancelWaitFrame* frame) {
    if (!out || !frame) {
        return;
    }
    c0_memset(out, 0, sizeof(*out));
    out->disc = C0_ASYNC_DISC_SUSPENDED_LOCAL;
    void* frame_ptr = frame;
    c0_memcpy(out->payload + C0_ASYNC_PAYLOAD_FRAME_PTR_OFFSET_LOCAL,
              &frame_ptr,
              sizeof(frame_ptr));
}

static void c0_cancel_wait_resume(void* hosted_env,
                                  C0AsyncResumeValue* out,
                                  void* frame_ptr,
                                  void* input,
                                  void* panic_out) {
    (void)hosted_env;
    (void)input;
    (void)panic_out;
    C0CancelWaitFrame* frame = (C0CancelWaitFrame*)frame_ptr;
    if (!frame || frame->token_id == C0_CANCEL_INVALID_ID) {
        c0_cancel_wait_write_completed(out);
        if (frame) {
            c0_heap_free_raw(frame);
        }
        return;
    }
    if (c0_token_is_cancelled(frame->token_id)) {
        c0_cancel_wait_write_completed(out);
        c0_heap_free_raw(frame);
        return;
    }
    c0_cancel_wait_write_suspended(out, frame);
}

typedef struct {
    uint8_t panic;
    uint8_t _pad[3];
    uint32_t code;
} C0PanicRecord;

// Worker pool for parallel execution
struct WorkerPool {
    int num_workers;
    int active_workers;
    WorkItem* queue_head;
    WorkItem* queue_tail;
    cursive_rt_handle_t* threads;
    cursive_rt_mutex_t lock;
    cursive_rt_condition_t work_cv;
    cursive_rt_condition_t done_cv;
    volatile int shutdown;
    uint8_t threads_started;
    size_t pending_count;
    C0CancelId cancel_token;
};

// §18.1 Parallel context
struct ParallelContext {
    WorkerPool* pool;
    uint32_t domain_kind;
    uint8_t owns_pool;
    size_t pending_count;
    cursive_rt_condition_t done_cv;
    C0CancelId cancel_token;
    uint64_t domain_affinity_mask;
    int32_t domain_priority_hint;
    WorkItem* first_panic;    // First panicked work item
    uint32_t context_panic_code;
    int panic_count;          // Number of panics
    const char* name;         // Debug name
    SpawnHandle* handles_head;
    SpawnHandle* handles_tail;
    WorkItem* all_items;      // All work items for cleanup
    ParallelContext* prev_ctx;
    int inline_domain;
};

static void c0_lock_parallel_ctx(ParallelContext* ctx) {
    if (ctx && ctx->pool) {
        cursive_rt_mutex_lock(&ctx->pool->lock);
    }
}

static void c0_unlock_parallel_ctx(ParallelContext* ctx) {
    if (ctx && ctx->pool) {
        cursive_rt_mutex_unlock(&ctx->pool->lock);
    }
}

static void c0_record_item_settlement(ParallelContext* ctx, WorkItem* item) {
    if (!item) {
        return;
    }
    if (item->state != WORK_COMPLETED && item->state != WORK_CANCELLED &&
        item->state != WORK_PANICKED) {
        return;
    }

    c0_lock_parallel_ctx(ctx);
    if (item->completion_seq == 0) {
        item->completion_seq = c0_fresh_completion_seq();
    }
    if (item->handle) {
        item->handle->state =
            (item->state == WORK_PANICKED) ? SPAWN_HANDLE_FAILED
                                           : SPAWN_HANDLE_READY;
    }
    if (ctx && item->state == WORK_PANICKED &&
        (!ctx->first_panic ||
         item->completion_seq < ctx->first_panic->completion_seq)) {
        ctx->first_panic = item;
    }
    c0_unlock_parallel_ctx(ctx);
}

static void c0_register_spawn_handle(ParallelContext* ctx, SpawnHandle* handle) {
    if (!ctx || !handle) {
        return;
    }
    c0_lock_parallel_ctx(ctx);
    handle->next = NULL;
    if (ctx->handles_tail) {
        ctx->handles_tail->next = handle;
        ctx->handles_tail = handle;
    } else {
        ctx->handles_head = handle;
        ctx->handles_tail = handle;
    }
    c0_unlock_parallel_ctx(ctx);
}

static cursive_rt_bool_t c0_cancel_registry_init(cursive_rt_once_t* init_once,
                                                 void* param,
                                                 void** context) {
    (void)init_once;
    (void)param;
    (void)context;
    cursive_rt_mutex_init(&c0_cancel_registry.lock);
    c0_cancel_registry.entries = NULL;
    c0_cancel_registry.count = 0;
    c0_cancel_registry.capacity = 0;
    return CURSIVE_RT_TRUE;
}

static int c0_cancel_registry_ready(void) {
    return cursive_rt_once_execute(&c0_cancel_registry_once,
                                   c0_cancel_registry_init,
                                   NULL,
                                   NULL)
               ? 1
               : 0;
}

static int c0_cancel_registry_valid_id_locked(C0CancelId id) {
    return id != C0_CANCEL_INVALID_ID && id < c0_cancel_registry.count;
}

static int c0_cancel_registry_reserve_locked(size_t needed) {
    if (needed <= c0_cancel_registry.capacity) {
        return 1;
    }

    size_t new_capacity = c0_cancel_registry.capacity ? c0_cancel_registry.capacity : 16u;
    while (new_capacity < needed) {
        if (new_capacity > (SIZE_MAX / 2u)) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2u;
    }

    const size_t bytes = new_capacity * sizeof(C0CancelStateEntry);
    C0CancelStateEntry* new_entries =
        (C0CancelStateEntry*)c0_heap_alloc_raw(bytes);
    if (!new_entries) {
        return 0;
    }

    c0_memset(new_entries, 0, bytes);
    if (c0_cancel_registry.entries && c0_cancel_registry.count > 0) {
        c0_memcpy(new_entries,
                  c0_cancel_registry.entries,
                  c0_cancel_registry.count * sizeof(C0CancelStateEntry));
        c0_heap_free_raw(c0_cancel_registry.entries);
    }

    c0_cancel_registry.entries = new_entries;
    c0_cancel_registry.capacity = new_capacity;
    return 1;
}

static C0CancelId c0_cancel_registry_new_locked(C0CancelId parent) {
    const size_t next = c0_cancel_registry.count;
    if (!c0_cancel_registry_reserve_locked(next + 1u)) {
        return C0_CANCEL_INVALID_ID;
    }

    c0_cancel_registry.entries[next].parent = parent;
    c0_cancel_registry.entries[next].status = C0_CANCEL_STATUS_ACTIVE;
    c0_cancel_registry.count = next + 1u;
    return (C0CancelId)next;
}

static int c0_cancel_registry_descendant_locked(C0CancelId root,
                                                C0CancelId candidate) {
    if (!c0_cancel_registry_valid_id_locked(root) ||
        !c0_cancel_registry_valid_id_locked(candidate)) {
        return 0;
    }

    C0CancelId current = candidate;
    for (;;) {
        if (current == root) {
            return 1;
        }
        if (!c0_cancel_registry_valid_id_locked(current)) {
            return 0;
        }
        const C0CancelId parent = c0_cancel_registry.entries[current].parent;
        if (parent == C0_CANCEL_INVALID_ID) {
            return 0;
        }
        current = parent;
    }
}

static void c0_cancel_registry_cancel_locked(C0CancelId id) {
    if (!c0_cancel_registry_valid_id_locked(id)) {
        return;
    }

    for (size_t i = 0; i < c0_cancel_registry.count; ++i) {
        if (c0_cancel_registry_descendant_locked(id, (C0CancelId)i)) {
            c0_cancel_registry.entries[i].status = C0_CANCEL_STATUS_CANCELLED;
        }
    }
}

// Thread-local parallel context tracking (for nested parallel support)

static int c0_token_is_cancelled(C0CancelId token_id) {
    int cancelled = 0;
    if (!c0_cancel_registry_ready()) {
        return 0;
    }

    cursive_rt_mutex_lock(&c0_cancel_registry.lock);
    if (c0_cancel_registry_valid_id_locked(token_id)) {
        cancelled =
            c0_cancel_registry.entries[token_id].status == C0_CANCEL_STATUS_CANCELLED;
    }
    cursive_rt_mutex_unlock(&c0_cancel_registry.lock);
    return cancelled;
}

static uint32_t c0_u64_to_dec(uint64_t value, char* out) {
    char rev[32];
    uint32_t count = 0;
    do {
        rev[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < (uint32_t)sizeof(rev));
    for (uint32_t i = 0; i < count; ++i) {
        out[i] = rev[count - 1 - i];
    }
    return count;
}

static int32_t c0_priority_rank(int32_t priority_hint) {
    if (priority_hint <= 0) {
        return 0;
    }
    if (priority_hint == 1) {
        return 1;
    }
    return 2;
}

static int32_t c0_effective_priority_rank(ParallelContext* ctx, int32_t priority_hint) {
    if (priority_hint < 0) {
        return ctx ? ctx->domain_priority_hint : 1;
    }
    return c0_priority_rank(priority_hint);
}

static int c0_thread_priority_from_rank(int32_t rank) {
    switch (rank) {
        case 0:
            return CURSIVE_RT_THREAD_PRIORITY_LOW;
        case 2:
            return CURSIVE_RT_THREAD_PRIORITY_HIGH;
        case 1:
        default:
            return CURSIVE_RT_THREAD_PRIORITY_NORMAL;
    }
}

typedef struct {
    cursive_rt_uptr_t prev_affinity;
    int prev_priority;
    int affinity_changed;
    int priority_changed;
} C0WorkHintScope;

static void c0_apply_work_hints(const WorkItem* item, C0WorkHintScope* scope) {
    if (!scope) {
        return;
    }

    scope->prev_affinity = 0;
    scope->prev_priority = CURSIVE_RT_THREAD_PRIORITY_NORMAL;
    scope->affinity_changed = 0;
    scope->priority_changed = 0;

    if (!item) {
        return;
    }

    if (item->affinity_mask != 0) {
        cursive_rt_uptr_t mask = (cursive_rt_uptr_t)item->affinity_mask;
        if (mask != 0) {
            scope->prev_affinity =
                cursive_rt_thread_affinity_set(cursive_rt_current_thread(), mask);
            if (scope->prev_affinity != 0) {
                scope->affinity_changed = 1;
            }
        }
    }

    const int desired_priority =
        c0_thread_priority_from_rank(c0_priority_rank(item->priority_hint));
    if (desired_priority != CURSIVE_RT_THREAD_PRIORITY_NORMAL) {
        int prev = cursive_rt_thread_priority_get(cursive_rt_current_thread());
        if (prev != CURSIVE_RT_THREAD_PRIORITY_INVALID &&
            cursive_rt_thread_priority_set(cursive_rt_current_thread(), desired_priority)) {
            scope->prev_priority = prev;
            scope->priority_changed = 1;
        }
    }
}

static void c0_restore_work_hints(const C0WorkHintScope* scope) {
    if (!scope) {
        return;
    }
    if (scope->priority_changed) {
        cursive_rt_thread_priority_set(cursive_rt_current_thread(),
                                       scope->prev_priority);
    }
    if (scope->affinity_changed && scope->prev_affinity != 0) {
        cursive_rt_thread_affinity_set(cursive_rt_current_thread(),
                                       scope->prev_affinity);
    }
}

static uint32_t c0_copy_cstr(char* out, const char* text) {
    uint32_t count = 0;
    if (!out || !text) {
        return 0;
    }
    while (text[count] != 0) {
        out[count] = text[count];
        ++count;
    }
    return count;
}

static int c0_debug_flag_enabled(const char* name) {
    if (!name || name[0] == '\0') {
        return 0;
    }
    char probe[2];
    cursive_rt_u32_t len =
        cursive_rt_env_query_utf8(name, probe, (cursive_rt_u32_t)sizeof(probe));
    return len > 0;
}

static cursive_rt_handle_t c0_debug_stderr_handle(void) {
    return cursive_rt_std_stream(CURSIVE_RT_STD_STREAM_ERROR);
}

static void c0_debug_write(cursive_rt_handle_t handle,
                           const char* text,
                           uint32_t len) {
    cursive_rt_u32_t written = 0;
    cursive_rt_handle_write(handle, text, (cursive_rt_u32_t)len, &written);
}

static void c0_debug_write_spawn_result(const WorkItem* item) {
    if (!item || !item->result || item->result_size == 0) {
        return;
    }
    cursive_rt_handle_t h = c0_debug_stderr_handle();
    if (h == NULL || h == CURSIVE_RT_INVALID_HANDLE) {
        return;
    }

    uint32_t first_u32 = 0;
    if (item->result_size >= 4) {
        first_u32 = *(const uint32_t*)item->result;
    }

    char dbg_result[128];
    uint32_t pos = 0;
    pos += c0_copy_cstr(dbg_result + pos, "[SPAWN-RESULT size=");
    pos += c0_u64_to_dec((uint64_t)item->result_size, dbg_result + pos);
    pos += c0_copy_cstr(dbg_result + pos, " first_u32=");
    pos += c0_u64_to_dec((uint64_t)first_u32, dbg_result + pos);
    dbg_result[pos++] = ']';
    dbg_result[pos++] = '\n';

    c0_debug_write(h, dbg_result, pos);
}

static void c0_debug_write_wait_result(const WorkItem* item) {
    if (!item || !item->result || item->result_size == 0) {
        return;
    }
    cursive_rt_handle_t h = c0_debug_stderr_handle();
    if (h == NULL || h == CURSIVE_RT_INVALID_HANDLE) {
        return;
    }

    uint32_t first_u32 = 0;
    if (item->result_size >= 4) {
        first_u32 = *(const uint32_t*)item->result;
    }

    char dbg_result[128];
    uint32_t pos = 0;
    pos += c0_copy_cstr(dbg_result + pos, "[WAIT-RESULT size=");
    pos += c0_u64_to_dec((uint64_t)item->result_size, dbg_result + pos);
    pos += c0_copy_cstr(dbg_result + pos, " first_u32=");
    pos += c0_u64_to_dec((uint64_t)first_u32, dbg_result + pos);
    dbg_result[pos++] = ']';
    dbg_result[pos++] = '\n';

    c0_debug_write(h, dbg_result, pos);
}

static void c0_debug_write_dispatch_range(C0Range range,
                                          uint64_t start,
                                          uint64_t end,
                                          size_t elem_size,
                                          size_t result_size,
                                          int ordered,
                                          size_t chunk_size) {
    if (!c0_debug_flag_enabled("CURSIVE_DEBUG_DISPATCH_RANGE_RUNTIME")) {
        return;
    }
    cursive_rt_handle_t h = c0_debug_stderr_handle();
    if (h == NULL || h == CURSIVE_RT_INVALID_HANDLE) {
        return;
    }

    char dbg[256];
    uint32_t pos = 0;
    pos += c0_copy_cstr(dbg + pos, "[DISPATCH tag=");
    pos += c0_u64_to_dec((uint64_t)range.tag, dbg + pos);
    pos += c0_copy_cstr(dbg + pos, " lo=");
    pos += c0_u64_to_dec(range.lo, dbg + pos);
    pos += c0_copy_cstr(dbg + pos, " hi=");
    pos += c0_u64_to_dec(range.hi, dbg + pos);
    pos += c0_copy_cstr(dbg + pos, " start=");
    pos += c0_u64_to_dec(start, dbg + pos);
    pos += c0_copy_cstr(dbg + pos, " end=");
    pos += c0_u64_to_dec(end, dbg + pos);
    pos += c0_copy_cstr(dbg + pos, " elem_size=");
    pos += c0_u64_to_dec((uint64_t)elem_size, dbg + pos);
    pos += c0_copy_cstr(dbg + pos, " result_size=");
    pos += c0_u64_to_dec((uint64_t)result_size, dbg + pos);
    pos += c0_copy_cstr(dbg + pos, " ordered=");
    pos += c0_u64_to_dec((uint64_t)(ordered ? 1 : 0), dbg + pos);
    pos += c0_copy_cstr(dbg + pos, " chunk=");
    pos += c0_u64_to_dec((uint64_t)chunk_size, dbg + pos);
    dbg[pos++] = ']';
    dbg[pos++] = '\n';

    c0_debug_write(h, dbg, pos);
}

static void c0_debug_write_dispatch_chunk_value(const char* label,
                                                uint64_t start,
                                                uint64_t end,
                                                const void* result_ptr,
                                                size_t result_size) {
    if (!label || !result_ptr || result_size == 0) {
        return;
    }
    cursive_rt_handle_t h = c0_debug_stderr_handle();
    if (h == NULL || h == CURSIVE_RT_INVALID_HANDLE) {
        return;
    }
    uint32_t first_u32 = 0;
    if (result_size >= 4) {
        first_u32 = *(const uint32_t*)result_ptr;
    }
    char dbg[192];
    uint32_t pos = 0;
    pos += c0_copy_cstr(dbg + pos, "[DISPATCH-CHUNK ");
    pos += c0_copy_cstr(dbg + pos, label);
    pos += c0_copy_cstr(dbg + pos, " start=");
    pos += c0_u64_to_dec(start, dbg + pos);
    pos += c0_copy_cstr(dbg + pos, " end=");
    pos += c0_u64_to_dec(end, dbg + pos);
    pos += c0_copy_cstr(dbg + pos, " size=");
    pos += c0_u64_to_dec((uint64_t)result_size, dbg + pos);
    pos += c0_copy_cstr(dbg + pos, " first_u32=");
    pos += c0_u64_to_dec((uint64_t)first_u32, dbg + pos);
    dbg[pos++] = ']';
    dbg[pos++] = '\n';
    c0_debug_write(h, dbg, pos);
}

static void c0_debug_write_cancel_state(const char* stage,
                                        const ParallelContext* ctx,
                                        const WorkItem* item,
                                        C0CancelId token_id,
                                        int cancelled) {
    if (!c0_debug_flag_enabled("CURSIVE_DEBUG_PARALLEL_RUNTIME")) {
        return;
    }
    cursive_rt_handle_t h = c0_debug_stderr_handle();
    if (h == NULL || h == CURSIVE_RT_INVALID_HANDLE) {
        return;
    }
    if (!stage) {
        stage = "unknown";
    }

    char dbg[256];
    uint32_t pos = 0;
    pos += c0_copy_cstr(dbg + pos, "[CANCEL stage=");
    pos += c0_copy_cstr(dbg + pos, stage);
    pos += c0_copy_cstr(dbg + pos, " ctx=");
    pos += c0_u64_to_dec((uint64_t)(uintptr_t)ctx, dbg + pos);
    pos += c0_copy_cstr(dbg + pos, " item=");
    pos += c0_u64_to_dec((uint64_t)(uintptr_t)item, dbg + pos);
    pos += c0_copy_cstr(dbg + pos, " token=");
    if (token_id == C0_CANCEL_INVALID_ID) {
        pos += c0_copy_cstr(dbg + pos, "none");
    } else {
        pos += c0_u64_to_dec((uint64_t)token_id, dbg + pos);
    }
    pos += c0_copy_cstr(dbg + pos, " cancelled=");
    pos += c0_u64_to_dec((uint64_t)(cancelled ? 1 : 0), dbg + pos);
    dbg[pos++] = ']';
    dbg[pos++] = '\n';
    c0_debug_write(h, dbg, pos);
}

typedef struct C0RunItemBoundaryContext {
    ParallelContext* ctx;
    WorkItem* item;
    C0PanicRecord* panic_record;
} C0RunItemBoundaryContext;

typedef struct C0ReduceBoundaryContext {
    void* hosted_env;
    void* left_result;
    void* right_result;
    void* out_result;
    void (*reduce_fn)(void* hosted_env,
                      void* lhs,
                      void* rhs,
                      void* out,
                      void* panic_out);
    C0PanicRecord* panic_record;
} C0ReduceBoundaryContext;

static void cursive_parallel_run_item_body(void* context) {
    C0RunItemBoundaryContext* boundary = (C0RunItemBoundaryContext*)context;
    ParallelContext* ctx;
    WorkItem* item;
    C0PanicRecord* panic_record;
    int has_cancel_token;
    int is_cancelled;

    if (!boundary) {
        return;
    }

    ctx = boundary->ctx;
    item = boundary->item;
    panic_record = boundary->panic_record;
    has_cancel_token = ctx && ctx->cancel_token != C0_CANCEL_INVALID_ID;
    is_cancelled = has_cancel_token ? c0_token_is_cancelled(ctx->cancel_token)
                                    : 0;

    if (has_cancel_token && is_cancelled) {
        c0_debug_write_cancel_state("run_item-cancel", ctx, item,
                                    ctx->cancel_token, 1);
        item->state = WORK_CANCELLED;
        if (item->result && item->result_size > 0) {
            c0_memset(item->result, 0, item->result_size);
        }
        return;
    }

    if (ctx) {
        c0_debug_write_cancel_state("run_item-start", ctx, item,
                                    ctx->cancel_token, is_cancelled);
    } else {
        c0_debug_write_cancel_state("run_item-start", ctx, item,
                                    C0_CANCEL_INVALID_ID, 0);
    }

    item->state = WORK_RUNNING;
    if (item->body) {
        item->body(item->hosted_env, item->captured_env, item->result,
                   panic_record);
        if (item->result && item->result_size > 0 &&
            c0_debug_flag_enabled("CURSIVE_DEBUG_SPAWN_RESULT_RUNTIME")) {
            c0_debug_write_spawn_result(item);
        }
    }
    if (panic_record->panic) {
        cursive_parallel_work_panic(ctx, panic_record->code);
    }
    if (item->state == WORK_RUNNING) {
        item->state = WORK_COMPLETED;
    }
}

static void cursive_parallel_run_reduce_body(void* context) {
    C0ReduceBoundaryContext* boundary = (C0ReduceBoundaryContext*)context;
    if (!boundary || !boundary->reduce_fn) {
        return;
    }
    boundary->reduce_fn(boundary->hosted_env,
                        boundary->left_result,
                        boundary->right_result,
                        boundary->out_result,
                        boundary->panic_record);
}

static void c0_run_item(ParallelContext* ctx, WorkItem* item) {
    C0RunItemBoundaryContext boundary;
    cursive_rt_u32_t panic_code = 0u;

    if (!item) {
        return;
    }
    C0ThreadState* state = c0_tls_state();
    ParallelContext* prev_ctx = state->ctx;
    WorkItem* prev_item = state->item;
    C0PanicRecord panic_record;

    panic_record.panic = 0;
    panic_record.code = 0;
    boundary.ctx = ctx;
    boundary.item = item;
    boundary.panic_record = &panic_record;

    state->ctx = ctx;
    state->item = item;
    if (!cursive_rt_panic_boundary_run(cursive_parallel_run_item_body,
                                       &boundary,
                                       &panic_code)) {
        cursive_parallel_work_panic(ctx, panic_code);
    }
    c0_record_item_settlement(ctx, item);
    state->item = prev_item;
    state->ctx = prev_ctx;
}

static SpawnHandle* c0_await_spawned(ParallelContext* ctx) {
    if (!ctx) {
        return NULL;
    }

    for (SpawnHandle* handle = ctx->handles_head; handle; handle = handle->next) {
        WorkItem* item = handle->item;
        if (!item) {
            continue;
        }

        if (item->state == WORK_PENDING &&
            (!item->owner_ctx || !item->owner_ctx->pool)) {
            C0WorkHintScope hints;
            c0_apply_work_hints(item, &hints);
            c0_run_item(item->owner_ctx, item);
            c0_restore_work_hints(&hints);
            if (item->done_event) {
                cursive_rt_event_set(item->done_event);
            }
        }

        if (item->done_event) {
            cursive_rt_wait(item->done_event, CURSIVE_RT_WAIT_FOREVER);
        }
    }

    SpawnHandle* failed = NULL;
    for (SpawnHandle* handle = ctx->handles_head; handle; handle = handle->next) {
        WorkItem* item = handle->item;
        if (!item || handle->state != SPAWN_HANDLE_FAILED) {
            continue;
        }
        if (!failed ||
            item->completion_seq < failed->item->completion_seq) {
            failed = handle;
        }
    }
    return failed;
}

static void c0_enqueue_item(WorkerPool* pool, WorkItem* item) {
    if (!pool || !item) {
        return;
    }
    c0_start_worker_threads(pool);
    cursive_rt_mutex_lock(&pool->lock);
    if (pool->queue_tail) {
        pool->queue_tail->next = item;
        pool->queue_tail = item;
    } else {
        pool->queue_head = item;
        pool->queue_tail = item;
    }
    pool->pending_count += 1;
    if (item->owner_ctx) {
        item->owner_ctx->pending_count += 1;
    }
        cursive_rt_condition_wake_one(&pool->work_cv);
        cursive_rt_mutex_unlock(&pool->lock);
}

static WorkItem* c0_dequeue_item_locked(WorkerPool* pool) {
    if (!pool || !pool->queue_head) {
        return NULL;
    }

    WorkItem* best = pool->queue_head;
    WorkItem* best_prev = NULL;
    WorkItem* prev = pool->queue_head;
    WorkItem* cur = pool->queue_head->next;

    while (cur) {
        if (cur->priority_hint > best->priority_hint ||
            (cur->priority_hint == best->priority_hint &&
             cur->task_id < best->task_id)) {
            best = cur;
            best_prev = prev;
        }
        prev = cur;
        cur = cur->next;
    }

    if (best_prev) {
        best_prev->next = best->next;
    } else {
        pool->queue_head = best->next;
    }
    if (pool->queue_tail == best) {
        pool->queue_tail = best_prev;
    }
    best->next = NULL;
    return best;
}

int c0_parallel_in_panic_scope(void) {
    return cursive_rt_panic_boundary_active() != CURSIVE_RT_FALSE;
}

void c0_parallel_raise_panic(uint32_t code) {
    if (!c0_parallel_in_panic_scope()) {
        cursive_panic(code);
        return;
    }
    cursive_rt_panic_boundary_raise(code);
}
static cursive_rt_u32_t c0_worker_thread_proc(void* param) {
    WorkerPool* pool = (WorkerPool*)param;
    for (;;) {
        cursive_rt_mutex_lock(&pool->lock);
        while (!pool->shutdown && pool->queue_head == NULL) {
            cursive_rt_condition_wait(&pool->work_cv, &pool->lock,
                                      CURSIVE_RT_WAIT_FOREVER);
        }
        if (pool->shutdown) {
            cursive_rt_mutex_unlock(&pool->lock);
            return 0;
        }

        WorkItem* item = c0_dequeue_item_locked(pool);
        pool->active_workers += 1;
        cursive_rt_mutex_unlock(&pool->lock);

        if (item) {
            C0WorkHintScope hints;
            c0_apply_work_hints(item, &hints);
            c0_run_item(item->owner_ctx, item);
            c0_restore_work_hints(&hints);
            cursive_rt_event_set(item->done_event);
        }

        cursive_rt_mutex_lock(&pool->lock);
        pool->active_workers -= 1;
        if (pool->pending_count > 0) {
            pool->pending_count -= 1;
        }
        if (item && item->owner_ctx && item->owner_ctx->pending_count > 0) {
            item->owner_ctx->pending_count -= 1;
            if (item->owner_ctx->pending_count == 0) {
                cursive_rt_condition_wake_all(&item->owner_ctx->done_cv);
            }
        }
        if (pool->pending_count == 0) {
            cursive_rt_condition_wake_all(&pool->done_cv);
        }
        cursive_rt_mutex_unlock(&pool->lock);
    }
}

static void c0_start_worker_threads(WorkerPool* pool) {
    if (!pool) {
        return;
    }

    cursive_rt_mutex_lock(&pool->lock);
    if (pool->threads_started || pool->shutdown) {
        cursive_rt_mutex_unlock(&pool->lock);
        return;
    }

    pool->threads_started = 1;
    pool->threads =
        (cursive_rt_handle_t*)c0_heap_alloc_raw(
            sizeof(cursive_rt_handle_t) * pool->num_workers);
    if (!pool->threads) {
        pool->threads_started = 0;
        cursive_rt_mutex_unlock(&pool->lock);
        return;
    }

    for (int i = 0; i < pool->num_workers; ++i) {
        pool->threads[i] =
            cursive_rt_thread_spawn(NULL, 0, c0_worker_thread_proc,
                                    pool, 0, NULL);
    }
    cursive_rt_mutex_unlock(&pool->lock);
}

// §18.1.1 Begin parallel block
// runtime_parallel_begin(domain) -> ParallelContext*
void* cursive_parallel_begin(C0DynObject domain,
                             C0CancelId cancel_token,
                             const char* name) {
    const C0ExecutionDomain* dom = (const C0ExecutionDomain*)domain.data;
    const uint32_t domain_kind = dom ? dom->kind : (uint32_t)C0_DOMAIN_CPU;
    const int inline_domain = dom && dom->kind == C0_DOMAIN_INLINE;
    const uint64_t domain_affinity_mask = dom ? dom->affinity_mask : 0;
    const int32_t domain_priority_hint = dom ? c0_priority_rank(dom->priority_hint) : 1;
    ParallelContext* prev_ctx = c0_current_ctx();
    if (c0_debug_flag_enabled("CURSIVE_DEBUG_PARALLEL_RUNTIME")) {
        char dbg[256];
        uint32_t pos = 0;
        pos += c0_copy_cstr(dbg + pos, "[PAR-BEGIN domain_data=");
        pos += c0_u64_to_dec((uint64_t)(uintptr_t)domain.data, dbg + pos);
        pos += c0_copy_cstr(dbg + pos, " domain_vtable=");
        pos += c0_u64_to_dec((uint64_t)(uintptr_t)domain.vtable, dbg + pos);
        pos += c0_copy_cstr(dbg + pos, " cancel=");
        if (cancel_token == C0_CANCEL_INVALID_ID) {
            pos += c0_copy_cstr(dbg + pos, "none");
        } else {
            pos += c0_u64_to_dec((uint64_t)cancel_token, dbg + pos);
        }
        pos += c0_copy_cstr(dbg + pos, " inline=");
        pos += c0_u64_to_dec((uint64_t)(inline_domain ? 1 : 0), dbg + pos);
        if (dom) {
            pos += c0_copy_cstr(dbg + pos, " kind=");
            pos += c0_u64_to_dec((uint64_t)dom->kind, dbg + pos);
            pos += c0_copy_cstr(dbg + pos, " max=");
            pos += c0_u64_to_dec((uint64_t)dom->max_concurrency, dbg + pos);
            pos += c0_copy_cstr(dbg + pos, " affinity=");
            pos += c0_u64_to_dec(dom->affinity_mask, dbg + pos);
            pos += c0_copy_cstr(dbg + pos, " priority=");
            pos += c0_u64_to_dec((uint64_t)c0_priority_rank(dom->priority_hint), dbg + pos);
        } else {
            pos += c0_copy_cstr(dbg + pos, " kind=null");
        }
        dbg[pos++] = ']';
        dbg[pos++] = '\n';
        cursive_rt_handle_write(cursive_rt_std_stream(CURSIVE_RT_STD_STREAM_ERROR),
                                dbg,
                                (cursive_rt_u32_t)pos,
                                NULL);
    }

    ParallelContext* ctx =
        (ParallelContext*)c0_heap_alloc_raw(sizeof(ParallelContext));
    if (!ctx) return NULL;

    ctx->pool = NULL;
    ctx->domain_kind = domain_kind;
    ctx->owns_pool = 0;
    ctx->pending_count = 0;
    cursive_rt_condition_init(&ctx->done_cv);
    ctx->cancel_token = cancel_token;
    ctx->domain_affinity_mask = domain_affinity_mask;
    ctx->domain_priority_hint = domain_priority_hint;
    ctx->first_panic = NULL;
    ctx->context_panic_code = 0;
    ctx->panic_count = 0;
    ctx->name = name;
    ctx->handles_head = NULL;
    ctx->handles_tail = NULL;
    ctx->all_items = NULL;
    ctx->prev_ctx = prev_ctx;
    ctx->inline_domain = inline_domain;

    if (!inline_domain) {
        if (domain_kind == (uint32_t)C0_DOMAIN_CPU && prev_ctx &&
            !prev_ctx->inline_domain &&
            prev_ctx->domain_kind == (uint32_t)C0_DOMAIN_CPU &&
            prev_ctx->pool) {
            ctx->pool = prev_ctx->pool;
        } else {
            ctx->pool = (WorkerPool*)c0_heap_alloc_raw(sizeof(WorkerPool));
            if (!ctx->pool) {
                c0_heap_free_raw(ctx);
                return NULL;
            }
            ctx->owns_pool = 1;
            int workers = dom && dom->max_concurrency > 0
                              ? (int)dom->max_concurrency
                              : 4;
            if (workers < 1) {
                workers = 1;
            }
            ctx->pool->num_workers = workers;
            ctx->pool->active_workers = 0;
            ctx->pool->queue_head = NULL;
            ctx->pool->queue_tail = NULL;
            ctx->pool->threads = NULL;
            ctx->pool->shutdown = 0;
            ctx->pool->threads_started = 0;
            ctx->pool->pending_count = 0;
            ctx->pool->cancel_token = cancel_token;
            cursive_rt_mutex_init(&ctx->pool->lock);
            cursive_rt_condition_init(&ctx->pool->work_cv);
            cursive_rt_condition_init(&ctx->pool->done_cv);
        }
    }

    c0_set_current_ctx(ctx);

    return ctx;
}

// §18.1.2 Join parallel block
// Waits for all work to complete and propagates first panic
int cursive_parallel_join(void* ctx_ptr) {
    if (c0_debug_flag_enabled("CURSIVE_DEBUG_PARALLEL_RUNTIME")) {
        const char* dbg1 = "[JOIN] enter\n";
        cursive_rt_handle_write(cursive_rt_std_stream(CURSIVE_RT_STD_STREAM_ERROR),
                                dbg1,
                                13,
                                NULL);
    }
    
    ParallelContext* ctx = (ParallelContext*)ctx_ptr;
    if (!ctx) {
        if (c0_debug_flag_enabled("CURSIVE_DEBUG_PARALLEL_RUNTIME")) {
            const char* dbg2 = "[JOIN] null ctx\n";
            cursive_rt_handle_write(cursive_rt_std_stream(CURSIVE_RT_STD_STREAM_ERROR),
                                    dbg2,
                                    15,
                                    NULL);
        }
        return 0;
    }
    
    if (ctx->pool) {
        // §18.7.1 Wait for all work to complete
        cursive_rt_mutex_lock(&ctx->pool->lock);
        while (ctx->pending_count > 0) {
            cursive_rt_condition_wait(&ctx->done_cv, &ctx->pool->lock,
                                      CURSIVE_RT_WAIT_FOREVER);
        }
        if (ctx->owns_pool) {
            while (ctx->pool->pending_count > 0) {
                cursive_rt_condition_wait(&ctx->pool->done_cv,
                                          &ctx->pool->lock,
                                          CURSIVE_RT_WAIT_FOREVER);
            }
            ctx->pool->shutdown = 1;
            cursive_rt_condition_wake_all(&ctx->pool->work_cv);
        }
        cursive_rt_mutex_unlock(&ctx->pool->lock);

        if (ctx->owns_pool && ctx->pool->threads) {
            for (int i = 0; i < ctx->pool->num_workers; ++i) {
                if (ctx->pool->threads[i]) {
                    cursive_rt_wait(ctx->pool->threads[i],
                                    CURSIVE_RT_WAIT_FOREVER);
                    cursive_rt_handle_release(ctx->pool->threads[i]);
                }
            }
            c0_heap_free_raw(ctx->pool->threads);
            ctx->pool->threads = NULL;
        }
    }
    
    SpawnHandle* failed_spawn = c0_await_spawned(ctx);
    if (failed_spawn && failed_spawn->item && !ctx->first_panic) {
        ctx->first_panic = failed_spawn->item;
    }

    // §18.7.2 Check for panics
    uint32_t panic_code = 0;
    if (ctx->first_panic) {
        panic_code = ctx->first_panic->panic_code;
        if (panic_code == 0) {
            panic_code = 1;
        }
    } else if (ctx->context_panic_code != 0) {
        panic_code = ctx->context_panic_code;
    }
    int had_panic = (panic_code != 0);
    
    // Cleanup
    WorkItem* item = ctx->all_items;
    while (item) {
        WorkItem* next = item->all_next;
        if (item->captured_env) {
            c0_heap_free_raw(item->captured_env);
        }
        if (item->result) {
            c0_heap_free_raw(item->result);
        }
        if (item->done_event) {
                    cursive_rt_handle_release(item->done_event);
        }
        if (item->handle) {
            c0_heap_free_raw(item->handle);
        }
        c0_heap_free_raw(item);
        item = next;
    }

    if (ctx->pool && ctx->owns_pool) {
            cursive_rt_mutex_destroy(&ctx->pool->lock);
        c0_heap_free_raw(ctx->pool);
    }
    c0_set_current_ctx(ctx->prev_ctx);
    c0_heap_free_raw(ctx);
    
    // §18.7.1 Report panic-at-boundary to caller.
    // The caller is responsible for re-emitting panic in the active boundary
    // mechanism (panic record / catch-zero at FFI boundary).
    if (had_panic && c0_debug_flag_enabled("CURSIVE_DEBUG_PARALLEL_RUNTIME")) {
        const char* dbg = "[JOIN] propagating panic\n";
        cursive_rt_handle_write(cursive_rt_std_stream(CURSIVE_RT_STD_STREAM_ERROR),
                                dbg,
                                24,
                                NULL);
    }
    
    if (c0_debug_flag_enabled("CURSIVE_DEBUG_PARALLEL_RUNTIME")) {
        const char* dbg_exit = "[JOIN] done\n";
        cursive_rt_handle_write(cursive_rt_std_stream(CURSIVE_RT_STD_STREAM_ERROR),
                                dbg_exit,
                                12,
                                NULL);
    }
    return (int)panic_code;
}

// §18.4.2 Create spawn handle
// Returns Spawned<T>@Pending (or @Ready if body is NULL for inline execution)
void* cursive_spawn_create(void* env, size_t env_size,
                            void (*body)(void* hosted_env, void* env, void* result, void* panic_out),
                            void* hosted_env,
                            size_t result_size,
                            uint64_t affinity_mask,
                            int32_t priority_hint) {
    ParallelContext* current_ctx = c0_current_ctx();
    if (c0_debug_flag_enabled("CURSIVE_DEBUG_PARALLEL_RUNTIME")) {
        static int spawn_count = 0;
        spawn_count++;
        char dbg[32];
        dbg[0] = '['; dbg[1] = 'S'; dbg[2] = 'P'; dbg[3] = 'A'; dbg[4] = 'W'; dbg[5] = 'N';
        dbg[6] = ' '; dbg[7] = '#'; dbg[8] = '0' + (spawn_count % 10); dbg[9] = ']'; dbg[10] = '\n';
        cursive_rt_handle_write(cursive_rt_std_stream(CURSIVE_RT_STD_STREAM_ERROR),
                                dbg,
                                11,
                                NULL);
    }
    SpawnHandle* handle =
        (SpawnHandle*)c0_heap_alloc_raw(sizeof(SpawnHandle));
    if (!handle) return NULL;
    
    WorkItem* item = (WorkItem*)c0_heap_alloc_raw(sizeof(WorkItem));
    if (!item) {
        c0_heap_free_raw(handle);
        return NULL;
    }
    
    // Copy captured environment
    item->captured_env = NULL;
    if (env && env_size > 0) {
        item->captured_env = c0_heap_alloc_raw(env_size);
        if (item->captured_env) {
            c0_memcpy(item->captured_env, env, env_size);
        }
    }

    item->done_event =
        cursive_rt_event_open(NULL, CURSIVE_RT_TRUE, CURSIVE_RT_FALSE, NULL);

    item->state = WORK_PENDING;
    item->task_id = c0_fresh_task_id();
    item->completion_seq = 0;
    item->owner_ctx = current_ctx;
    item->hosted_env = hosted_env;
    item->body = body;
    item->result = result_size > 0 ? c0_heap_alloc_raw(result_size) : NULL;
    item->result_size = result_size;
    item->affinity_mask = affinity_mask != 0
                              ? affinity_mask
                              : (current_ctx ? current_ctx->domain_affinity_mask : 0);
    item->priority_hint = c0_effective_priority_rank(current_ctx, priority_hint);
    item->panic_code = 0;
    item->next = NULL;
    item->all_next = NULL;
    handle->id = item->task_id;
    handle->state = body == NULL ? SPAWN_HANDLE_READY : SPAWN_HANDLE_PENDING;
    handle->item = item;
    handle->next = NULL;
    item->handle = handle;

    if (body == NULL) {
        item->state = WORK_COMPLETED;
        c0_record_item_settlement(current_ctx, item);
        if (item->done_event) {
            cursive_rt_event_set(item->done_event);
        }
    } else if (current_ctx && current_ctx->pool) {
        c0_enqueue_item(current_ctx->pool, item);
    } else if (!current_ctx) {
        // Defensive fallback for invalid outside-parallel runtime entry.
        C0WorkHintScope hints;
        c0_apply_work_hints(item, &hints);
        c0_run_item(item->owner_ctx, item);
        c0_restore_work_hints(&hints);
        if (item->done_event) {
            cursive_rt_event_set(item->done_event);
        }
    } else {
        // Inline/no-pool parallel domains keep spawned work pending until wait/join.
    }

    if (current_ctx) {
        c0_register_spawn_handle(current_ctx, handle);
        item->all_next = current_ctx->all_items;
        current_ctx->all_items = item;
    }
    
    return handle;
}

// §10.3 Wait for spawn result
// Blocks until handle is ready, returns extracted value
void* cursive_spawn_wait(void* handle_ptr) {
    SpawnHandle* handle = (SpawnHandle*)handle_ptr;
    if (!handle || !handle->item) {
        if (c0_debug_flag_enabled("CURSIVE_DEBUG_PARALLEL_RUNTIME")) {
            const char* dbg = "[WAIT] null handle\n";
            cursive_rt_handle_write(cursive_rt_std_stream(CURSIVE_RT_STD_STREAM_ERROR),
                                    dbg,
                                    18,
                                    NULL);
        }
        return NULL;
    }
    
    WorkItem* item = handle->item;
    
    if (item->state == WORK_PENDING &&
        (!item->done_event || (item->owner_ctx && !item->owner_ctx->pool))) {
        c0_run_item(item->owner_ctx, item);
        if (item->done_event) {
            cursive_rt_event_set(item->done_event);
        }
    } else if (item->done_event) {
        cursive_rt_wait(item->done_event, CURSIVE_RT_WAIT_FOREVER);
    }

    if (c0_debug_flag_enabled("CURSIVE_DEBUG_PARALLEL_RUNTIME")) {
        char dbg[32];
        dbg[0] = '['; dbg[1] = 'W'; dbg[2] = 'A'; dbg[3] = 'I'; dbg[4] = 'T';
        dbg[5] = ' '; dbg[6] = 's'; dbg[7] = 't'; dbg[8] = '=';
        dbg[9] = '0' + item->state; dbg[10] = ']'; dbg[11] = '\n';
        cursive_rt_handle_write(cursive_rt_std_stream(CURSIVE_RT_STD_STREAM_ERROR),
                                dbg,
                                12,
                                NULL);
    }
    
    // §18.7.1 Panic is propagated at the enclosing parallel boundary after
    // all started work settles. wait returns the current value slot and does
    // not directly abort/rethrow.
    if (item->state == WORK_PANICKED) {
        if (c0_debug_flag_enabled("CURSIVE_DEBUG_PARALLEL_RUNTIME")) {
            const char* dbg2 = "[WAIT] propagating panic\n";
            cursive_rt_handle_write(cursive_rt_std_stream(CURSIVE_RT_STD_STREAM_ERROR),
                                    dbg2,
                                    24,
                                    NULL);
        }
        if (item->result && item->result_size > 0) {
            c0_memset(item->result, 0, item->result_size);
        }
    }

    if (c0_debug_flag_enabled("CURSIVE_DEBUG_SPAWN_RESULT_RUNTIME")) {
        c0_debug_write_wait_result(item);
    }
    
    return item->result;
}

// Reactor::register runtime hook.
// ABI is type-erased at this boundary: both reactor and future are passed by
// pointer, and the returned tracked handle is the same opaque spawn handle used
// by wait. For Cursive0 exercises, Future<T,E> values here are immediate and we
// materialize a ready handle carrying T|E in a compact tagged payload.
void* cursive_x3a_x3aruntime_x3a_x3areactor_x3a_x3aregister(
    const C0DynObject* reactor,
    const void* future) {
    (void)reactor;

    SpawnHandle* handle =
        (SpawnHandle*)cursive_spawn_create(NULL, 0, NULL, NULL, 8, 0, 1);
    if (!handle || !handle->item || !handle->item->result) {
        return handle;
    }

    uint8_t* out = (uint8_t*)handle->item->result;
    c0_memset(out, 0, 8);

    if (!future) {
        return handle;
    }

    const uint8_t* future_bytes = (const uint8_t*)future;
    const uint8_t future_disc = future_bytes[0];
    const uint8_t* future_payload = future_bytes + 8;

    // Async@Completed -> union success arm
    if (future_disc == 1) {
        out[0] = 1;
        c0_memcpy(out + 4, future_payload, 4);
        return handle;
    }

    // Async@Failed -> union error arm
    if (future_disc == 2) {
        out[0] = 0;
        out[4] = future_payload[0];
        return handle;
    }

    // Async@Suspended maps to error arm in this runtime fallback path.
    out[0] = 0;
    out[4] = 0;
    return handle;
}

static int64_t c0_read_int(const void* ptr, size_t size) {
    switch (size) {
        case 1: return *(const int8_t*)ptr;
        case 2: return *(const int16_t*)ptr;
        case 4: return *(const int32_t*)ptr;
        case 8: return *(const int64_t*)ptr;
        default: return 0;
    }
}

static void c0_write_int(void* ptr, size_t size, int64_t value) {
    switch (size) {
        case 1: *(int8_t*)ptr = (int8_t)value; break;
        case 2: *(int16_t*)ptr = (int16_t)value; break;
        case 4: *(int32_t*)ptr = (int32_t)value; break;
        case 8: *(int64_t*)ptr = (int64_t)value; break;
        default: break;
    }
}

static int64_t c0_int_max_for_size(size_t size) {
    switch (size) {
        case 1: return INT8_MAX;
        case 2: return INT16_MAX;
        case 4: return INT32_MAX;
        case 8: return INT64_MAX;
        default: return INT64_MAX;
    }
}

static int64_t c0_int_min_for_size(size_t size) {
    switch (size) {
        case 1: return INT8_MIN;
        case 2: return INT16_MIN;
        case 4: return INT32_MIN;
        case 8: return INT64_MIN;
        default: return INT64_MIN;
    }
}

static int c0_reduce_has_op(C0StringView op) {
    return op.data && op.len > 0;
}

static char c0_reduce_char(C0StringView op, size_t idx) {
    if (!op.data || idx >= op.len) {
        return 0;
    }
    return (char)op.data[idx];
}

static void c0_reduce_init(C0StringView op, void* accum, size_t size) {
    if (!c0_reduce_has_op(op) || !accum) {
        return;
    }
    const char c0 = c0_reduce_char(op, 0);
    const char c1 = c0_reduce_char(op, 1);
    if (c0 == '*') {
        c0_write_int(accum, size, 1);
        return;
    }
    if (c0 == 'a') {
        c0_write_int(accum, size, 1);
        return;
    }
    if (c0 == 'm' && c1 == 'i') {
        c0_write_int(accum, size, c0_int_max_for_size(size));
        return;
    }
    if (c0 == 'm' && c1 == 'a') {
        c0_write_int(accum, size, c0_int_min_for_size(size));
        return;
    }
    c0_write_int(accum, size, 0);
}

static void c0_reduce_apply(C0StringView op, void* accum, const void* value, size_t size) {
    if (!c0_reduce_has_op(op) || !accum || !value) {
        return;
    }
    int64_t a = c0_read_int(accum, size);
    int64_t b = c0_read_int(value, size);
    const char c0 = c0_reduce_char(op, 0);
    const char c1 = c0_reduce_char(op, 1);
    if (c0 == '+') {
        c0_write_int(accum, size, a + b);
        return;
    }
    if (c0 == '*') {
        c0_write_int(accum, size, a * b);
        return;
    }
    if (c0 == 'm' && c1 == 'i') {
        c0_write_int(accum, size, a < b ? a : b);
        return;
    }
    if (c0 == 'm' && c1 == 'a') {
        c0_write_int(accum, size, a > b ? a : b);
        return;
    }
    if (c0 == 'a') {
        c0_write_int(accum, size, (a != 0 && b != 0) ? 1 : 0);
        return;
    }
    if (c0 == 'o') {
        c0_write_int(accum, size, (a != 0 || b != 0) ? 1 : 0);
        return;
    }
    c0_write_int(accum, size, b);
}

typedef struct {
    uint64_t start;
    uint64_t end;
    size_t elem_size;
    size_t result_size;
    void (*body)(void* hosted_env, void* elem, void* captured, void* result, void* panic_out);
    void* hosted_env;
    void* captured_env;
    C0StringView reduce_op;
    void (*reduce_fn)(void* hosted_env, void* lhs, void* rhs, void* out, void* panic_out);
} DispatchChunkEnv;

static void c0_dispatch_chunk(void* hosted_env, void* env_ptr, void* result_ptr, void* panic_out) {
    (void)hosted_env;
    DispatchChunkEnv* env = (DispatchChunkEnv*)env_ptr;
    if (!env || !env->body) {
        return;
    }
    C0PanicRecord local_panic;
    local_panic.panic = 0;
    local_panic.code = 0;
    C0PanicRecord* panic_record =
        panic_out ? (C0PanicRecord*)panic_out : &local_panic;
    uint8_t idx_buf[8];
    uint8_t* iter_result = NULL;
    if (env->result_size > 0) {
        iter_result = (uint8_t*)c0_heap_alloc_raw(env->result_size);
    }
    const int has_reduce = result_ptr && env->result_size > 0;
    const int use_custom = env->reduce_fn != NULL;
    const int use_builtin = c0_reduce_has_op(env->reduce_op);
    int has_accum = 0;
    if (use_builtin && has_reduce) {
        c0_reduce_init(env->reduce_op, result_ptr, env->result_size);
    }
    for (uint64_t i = env->start; i < env->end; ++i) {
        c0_memset(idx_buf, 0, sizeof(idx_buf));
        const size_t copy = env->elem_size < sizeof(uint64_t) ? env->elem_size : sizeof(uint64_t);
        c0_memcpy(idx_buf, &i, copy);
        void* out_ptr = iter_result ? (void*)iter_result : result_ptr;
        env->body(env->hosted_env, idx_buf, env->captured_env, out_ptr, panic_record);
        if (panic_record->panic) {
            cursive_parallel_work_panic(c0_current_ctx(), panic_record->code);
            break;
        }
        if (has_reduce && iter_result) {
            if (use_custom) {
                if (!has_accum) {
                    c0_memcpy(result_ptr, iter_result, env->result_size);
                    has_accum = 1;
                } else {
                    env->reduce_fn(env->hosted_env, result_ptr, iter_result, result_ptr, panic_record);
                    if (panic_record->panic) {
                        cursive_parallel_work_panic(c0_current_ctx(), panic_record->code);
                        break;
                    }
                }
            } else if (use_builtin) {
                c0_reduce_apply(env->reduce_op, result_ptr, iter_result, env->result_size);
            }
        }
    }
    if (use_custom && has_reduce && !has_accum) {
        c0_memset(result_ptr, 0, env->result_size);
    }
    if (c0_debug_flag_enabled("CURSIVE_DEBUG_DISPATCH_RESULT_RUNTIME")) {
        c0_debug_write_dispatch_chunk_value("local",
                                            env->start,
                                            env->end,
                                            result_ptr,
                                            env->result_size);
    }
    if (iter_result) {
        c0_heap_free_raw(iter_result);
    }
}

// §18.5.2 Dispatch iteration
// Executes body for each element in range with optional reduction
void cursive_dispatch_run(C0Range range, size_t elem_size, size_t result_size,
                           void (*body)(void* hosted_env, void* elem, void* captured, void* result, void* panic_out),
                           void* hosted_env,
                           void* captured_env,
                           C0StringView reduce_op,
                           void* reduce_result,
                           void (*reduce_fn)(void* hosted_env, void* lhs, void* rhs, void* out, void* panic_out),
                           int ordered,
                           size_t chunk_size) {
    if (!body) return;

    uint64_t start = 0;
    uint64_t end = 0;
    switch (range.tag) {
        case 0:  // To
            start = 0;
            end = range.hi;
            break;
        case 1:  // ToInclusive
            start = 0;
            end = range.hi + 1;
            break;
        case 2:  // Full
            start = range.lo;
            end = range.hi;
            break;
        case 3:  // From
            start = range.lo;
            end = range.hi;
            break;
        case 4:  // Exclusive
            start = range.lo;
            end = range.hi;
            break;
        case 5:  // Inclusive
            start = range.lo;
            end = range.hi + 1;
            break;
        default:
            c0_debug_write_dispatch_range(range,
                                          0,
                                          0,
                                          elem_size,
                                          result_size,
                                          ordered,
                                          chunk_size);
            return;
    }
    c0_debug_write_dispatch_range(range,
                                  start,
                                  end,
                                  elem_size,
                                  result_size,
                                  ordered,
                                  chunk_size);
    if (end <= start) {
        if ((reduce_fn || c0_reduce_has_op(reduce_op)) && result_size > 0) {
            ParallelContext* ctx = c0_current_ctx();
            if (ctx) {
                c0_parallel_context_panic(ctx, C0_PANIC_REDUCED_EMPTY_DISPATCH);
            } else {
                cursive_panic(C0_PANIC_REDUCED_EMPTY_DISPATCH);
            }
        }
        return;
    }

    // Spec permits concurrency but does not require it; execute dispatch
    // deterministically in-process to preserve result correctness.
    const int use_threaded_dispatch = 0;
    if (!use_threaded_dispatch || !c0_current_ctx() || !c0_current_ctx()->pool || ordered) {
        DispatchChunkEnv env;
        env.start = start;
        env.end = end;
        env.elem_size = elem_size;
        env.result_size = result_size;
        env.body = body;
        env.hosted_env = hosted_env;
        env.captured_env = captured_env;
        env.reduce_op = reduce_op;
        env.reduce_fn = reduce_fn;
    WorkItem* item = (WorkItem*)c0_heap_alloc_raw(sizeof(WorkItem));
        if (!item) {
            c0_dispatch_chunk(NULL, &env, reduce_result, NULL);
            return;
        }
        item->state = WORK_PENDING;
        item->task_id = c0_fresh_task_id();
        item->completion_seq = 0;
        item->owner_ctx = c0_current_ctx();
        item->captured_env = &env;
        item->hosted_env = NULL;
        item->body = c0_dispatch_chunk;
        item->result = reduce_result;
        item->result_size = result_size;
        item->affinity_mask = c0_current_ctx() ? c0_current_ctx()->domain_affinity_mask : 0;
        item->priority_hint = c0_current_ctx() ? c0_current_ctx()->domain_priority_hint : 1;
        item->panic_code = 0;
        item->next = NULL;
        item->all_next = NULL;
        item->handle = NULL;
        item->done_event = NULL;
        if (c0_current_ctx()) {
            item->all_next = c0_current_ctx()->all_items;
            c0_current_ctx()->all_items = item;
        }
        c0_run_item(item->owner_ctx, item);
        item->captured_env = NULL;
        item->result = NULL;
        item->result_size = 0;
        if (!c0_current_ctx()) {
        c0_heap_free_raw(item);
        }
        return;
    }

    ParallelContext* ctx = c0_current_ctx();
    WorkerPool* pool = ctx->pool;
    uint64_t count = end - start;
    if (chunk_size == 0) {
        size_t denom = pool->num_workers > 0 ? (size_t)pool->num_workers : 1;
        chunk_size = (size_t)((count + denom - 1) / denom);
    }
    if (chunk_size == 0) {
        chunk_size = 1;
    }

    size_t num_chunks = (size_t)((count + chunk_size - 1) / chunk_size);
    WorkItem** items =
        (WorkItem**)c0_heap_alloc_raw(sizeof(WorkItem*) * num_chunks);
    if (!items) {
        DispatchChunkEnv env;
        env.start = start;
        env.end = end;
        env.elem_size = elem_size;
        env.result_size = result_size;
        env.body = body;
        env.hosted_env = hosted_env;
        env.captured_env = captured_env;
        env.reduce_op = reduce_op;
        env.reduce_fn = reduce_fn;
        c0_dispatch_chunk(NULL, &env, reduce_result, NULL);
        return;
    }

    for (size_t c = 0; c < num_chunks; ++c) {
        uint64_t chunk_start = start + (uint64_t)c * (uint64_t)chunk_size;
        uint64_t chunk_end = chunk_start + (uint64_t)chunk_size;
        if (chunk_end > end) {
            chunk_end = end;
        }
        DispatchChunkEnv* env =
            (DispatchChunkEnv*)c0_heap_alloc_raw(sizeof(DispatchChunkEnv));
        if (!env) {
            items[c] = NULL;
            continue;
        }
        env->start = chunk_start;
        env->end = chunk_end;
        env->elem_size = elem_size;
        env->result_size = result_size;
        env->body = body;
        env->hosted_env = hosted_env;
        env->captured_env = captured_env;
        env->reduce_op = reduce_op;
        env->reduce_fn = reduce_fn;

        WorkItem* item = (WorkItem*)c0_heap_alloc_raw(sizeof(WorkItem));
        if (!item) {
            c0_heap_free_raw(env);
            items[c] = NULL;
            continue;
        }
        item->state = WORK_PENDING;
        item->task_id = c0_fresh_task_id();
        item->completion_seq = 0;
        item->owner_ctx = ctx;
        item->captured_env = env;
        item->hosted_env = NULL;
        item->body = c0_dispatch_chunk;
        item->result = (reduce_fn || c0_reduce_has_op(reduce_op)) && result_size > 0
                           ? c0_heap_alloc_raw(result_size)
                           : NULL;
        item->result_size = result_size;
        item->affinity_mask = ctx ? ctx->domain_affinity_mask : 0;
        item->priority_hint = ctx ? ctx->domain_priority_hint : 1;
        item->panic_code = 0;
        item->next = NULL;
        item->all_next = NULL;
        item->handle = NULL;
        item->done_event = cursive_rt_event_open(
            NULL, CURSIVE_RT_TRUE, CURSIVE_RT_FALSE, NULL);

        items[c] = item;
        if (ctx) {
            item->all_next = ctx->all_items;
            ctx->all_items = item;
        }
        c0_enqueue_item(pool, item);
    }

    const int use_custom = reduce_fn != NULL;
    const int use_builtin = c0_reduce_has_op(reduce_op);
    int has_accum = 0;
    WorkItem* reduce_item = NULL;
    if (use_builtin && reduce_result && result_size > 0) {
        c0_reduce_init(reduce_op, reduce_result, result_size);
    }

    for (size_t c = 0; c < num_chunks; ++c) {
        WorkItem* item = items[c];
        if (!item) {
            continue;
        }
        if (item->done_event) {
            cursive_rt_wait(item->done_event, CURSIVE_RT_WAIT_FOREVER);
        }
        if (reduce_result && item->result && result_size > 0) {
            if (c0_debug_flag_enabled("CURSIVE_DEBUG_DISPATCH_RESULT_RUNTIME")) {
                uint64_t chunk_start = start + (uint64_t)c * (uint64_t)chunk_size;
                uint64_t chunk_end = chunk_start + (uint64_t)chunk_size;
                if (chunk_end > end) {
                    chunk_end = end;
                }
                c0_debug_write_dispatch_chunk_value("merge-in",
                                                    chunk_start,
                                                    chunk_end,
                                                    item->result,
                                                    result_size);
            }
            if (use_custom) {
                if (!has_accum) {
                    c0_memcpy(reduce_result, item->result, result_size);
                    has_accum = 1;
                } else {
                    if (!reduce_item && ctx) {
                        reduce_item =
                            (WorkItem*)c0_heap_alloc_raw(sizeof(WorkItem));
                        if (reduce_item) {
                            c0_memset(reduce_item, 0, sizeof(WorkItem));
                            reduce_item->state = WORK_RUNNING;
                            reduce_item->all_next = ctx->all_items;
                            ctx->all_items = reduce_item;
                        }
                    }
                    if (!reduce_item) {
                        C0PanicRecord panic_record;
                        panic_record.panic = 0;
                        panic_record.code = 0;
                        reduce_fn(hosted_env, reduce_result, item->result, reduce_result, &panic_record);
                        if (panic_record.panic) {
                            cursive_parallel_work_panic(ctx, panic_record.code);
                            break;
                        }
                    } else {
                        C0ReduceBoundaryContext boundary;
                        cursive_rt_u32_t panic_code = 0u;
                        C0PanicRecord panic_record;
                        panic_record.panic = 0;
                        panic_record.code = 0;
                        C0ThreadState* state = c0_tls_state();
                        ParallelContext* prev_ctx = state->ctx;
                        WorkItem* prev_item = state->item;
                        state->ctx = ctx;
                        state->item = reduce_item;
                        boundary.hosted_env = hosted_env;
                        boundary.left_result = reduce_result;
                        boundary.right_result = item->result;
                        boundary.out_result = reduce_result;
                        boundary.reduce_fn = reduce_fn;
                        boundary.panic_record = &panic_record;
                        if (!cursive_rt_panic_boundary_run(
                                cursive_parallel_run_reduce_body,
                                &boundary,
                                &panic_code)) {
                            cursive_parallel_work_panic(ctx, panic_code);
                            state->ctx = prev_ctx;
                            state->item = prev_item;
                            break;
                        }
                        if (panic_record.panic) {
                            cursive_parallel_work_panic(ctx, panic_record.code);
                        }
                        state->ctx = prev_ctx;
                        state->item = prev_item;
                    }
                }
            } else if (use_builtin) {
                c0_reduce_apply(reduce_op, reduce_result, item->result, result_size);
            }
        }
    }

    if (c0_debug_flag_enabled("CURSIVE_DEBUG_DISPATCH_RESULT_RUNTIME")) {
        c0_debug_write_dispatch_chunk_value("final",
                                            start,
                                            end,
                                            reduce_result,
                                            result_size);
    }

    c0_heap_free_raw(items);
}

// §18.6.1 Create cancellation token
C0CancelId cursive_cancel_token_new(void) {
    C0CancelId token_id = C0_CANCEL_INVALID_ID;
    if (!c0_cancel_registry_ready()) {
        return C0_CANCEL_INVALID_ID;
    }

    cursive_rt_mutex_lock(&c0_cancel_registry.lock);
    token_id = c0_cancel_registry_new_locked(C0_CANCEL_INVALID_ID);
    cursive_rt_mutex_unlock(&c0_cancel_registry.lock);
    return token_id;
}

// §18.6.1 Request cancellation
void cursive_cancel_token_cancel(C0CancelId token_id) {
    if (!c0_cancel_registry_ready()) {
        return;
    }

    cursive_rt_mutex_lock(&c0_cancel_registry.lock);
    c0_cancel_registry_cancel_locked(token_id);
    cursive_rt_mutex_unlock(&c0_cancel_registry.lock);
}

// §18.6.1 Check if cancelled
int cursive_cancel_token_is_cancelled(C0CancelId token_id) {
    return c0_token_is_cancelled(token_id);
}

// §18.7 Record panic in work item
static void c0_parallel_context_panic(ParallelContext* ctx, uint32_t code) {
    if (!ctx) {
        cursive_panic(code);
        return;
    }

    int request_cancel = 0;
    c0_lock_parallel_ctx(ctx);
    if (ctx->context_panic_code == 0 && !ctx->first_panic) {
        ctx->context_panic_code = code;
    }
    ctx->panic_count += 1;
    request_cancel =
        (ctx->cancel_token != C0_CANCEL_INVALID_ID && ctx->panic_count == 1);
    c0_unlock_parallel_ctx(ctx);

    if (request_cancel) {
        cursive_cancel_token_cancel(ctx->cancel_token);
    }
}

void cursive_parallel_work_panic(void* ctx_ptr, uint32_t code) {
    ParallelContext* ctx = (ParallelContext*)ctx_ptr;
    if (!ctx) {
        ctx = c0_current_ctx();
    }
    WorkItem* item = c0_current_item();
    if (!ctx) {
        cursive_panic(code);
        return;
    }
    if (!item) {
        c0_parallel_context_panic(ctx, code);
        return;
    }

    int request_cancel = 0;
    c0_lock_parallel_ctx(ctx);
    item->state = WORK_PANICKED;
    item->panic_code = code;
    if (item->result && item->result_size > 0) {
        c0_memset(item->result, 0, item->result_size);
    }
    ctx->panic_count += 1;
    request_cancel =
        (ctx->cancel_token != C0_CANCEL_INVALID_ID && ctx->panic_count == 1);
    c0_unlock_parallel_ctx(ctx);

    if (request_cancel) {
        cursive_cancel_token_cancel(ctx->cancel_token);
    }
}

C0CancelId CancelToken_x3a_x3anew(void) {
    return cursive_cancel_token_new();
}

static C0CancelId c0_cancel_token_from_self_ref(void* self_ref) {
    if (!self_ref) {
        return C0_CANCEL_INVALID_ID;
    }
    return *((const C0CancelId*)self_ref);
}

void CancelToken_x3a_x3aActive_x3a_x3acancel(void* self) {
    cursive_cancel_token_cancel(c0_cancel_token_from_self_ref(self));
}

uint8_t CancelToken_x3a_x3aActive_x3a_x3ais_x5fcancelled(void* self) {
    return (uint8_t)cursive_cancel_token_is_cancelled(
        c0_cancel_token_from_self_ref(self));
}

C0CancelId CancelToken_x3a_x3aActive_x3a_x3achild(void* self) {
    C0CancelId parent = c0_cancel_token_from_self_ref(self);
    C0CancelId child = C0_CANCEL_INVALID_ID;
    if (!c0_cancel_registry_ready()) {
        return C0_CANCEL_INVALID_ID;
    }

    cursive_rt_mutex_lock(&c0_cancel_registry.lock);
    if (c0_cancel_registry_valid_id_locked(parent)) {
        child = c0_cancel_registry_new_locked(parent);
    }
    cursive_rt_mutex_unlock(&c0_cancel_registry.lock);
    return child;
}

void CancelToken_x3a_x3aActive_x3a_x3await_x5fcancelled(void* out, void* self) {
    if (!out) {
        return;
    }
    C0AsyncResumeValue* async_out = (C0AsyncResumeValue*)out;
    C0CancelId token_id = c0_cancel_token_from_self_ref(self);
    if (token_id == C0_CANCEL_INVALID_ID || c0_token_is_cancelled(token_id)) {
        c0_cancel_wait_write_completed(async_out);
        return;
    }

    C0CancelWaitFrame* frame =
        (C0CancelWaitFrame*)c0_heap_alloc_raw(sizeof(C0CancelWaitFrame));
    if (!frame) {
        // Preserve progress on allocation failure by producing a completed
        // async value rather than an invalid suspended state.
        c0_cancel_wait_write_completed(async_out);
        return;
    }

    frame->resume_state = 0;
    frame->resume_fn = (void*)&c0_cancel_wait_resume;
    frame->hosted_env = NULL;
    frame->token_id = token_id;
    c0_cancel_wait_write_suspended(async_out, frame);
}
