// UVX Extension: Structured Concurrency Runtime Support (§18)
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
// Note: <string.h> is NOT included - we use uv_memset/uv_memcpy from rt_internal.h

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
typedef struct UVGpuDispatchState UVGpuDispatchState;
typedef struct UVGpuWorkItemState UVGpuWorkItemState;
typedef size_t UVCancelId;
static void uv_parallel_context_panic(ParallelContext* ctx, uint32_t code);
void uv_parallel_work_panic(void* ctx_ptr, uint32_t code);
static uv_rt_u32_t uv_worker_thread_proc(void* param);
static void uv_start_worker_threads(WorkerPool* pool);

// -----------------------------------------------------------------------------
// Panic boundary + TLS state
// -----------------------------------------------------------------------------

#define UV_PANIC_REDUCED_EMPTY_DISPATCH 2862u
#define UV_PANIC_GPU_DISPATCH_LAUNCH_FAILED 2863u

typedef struct {
    ParallelContext* ctx;
    WorkItem* item;
    UVGpuWorkItemState* gpu_item;
} UVThreadState;
static uv_rt_once_t uv_tls_once = UV_RT_ONCE_INIT;
static uv_rt_tls_key_t uv_tls_index = UV_RT_TLS_KEY_INVALID;
static UVThreadState uv_tls_fallback = {0};
static volatile uv_rt_i64_t uv_next_task_id = 0;
static volatile uv_rt_i64_t uv_next_completion_seq = 0;

static uv_rt_bool_t uv_tls_init(uv_rt_once_t* init_once,
                                     void* param,
                                     void** context) {
    (void)init_once;
    (void)param;
    (void)context;
    uv_rt_tls_key_t idx = uv_rt_tls_key_create();
    if (idx == UV_RT_TLS_KEY_INVALID) {
        return UV_RT_FALSE;
    }
    uv_tls_index = idx;
    return UV_RT_TRUE;
}

static UVThreadState* uv_tls_state(void) {
    if (!uv_rt_once_execute(&uv_tls_once, uv_tls_init, NULL, NULL)) {
        return &uv_tls_fallback;
    }
    UVThreadState* state = (UVThreadState*)uv_rt_tls_get(uv_tls_index);
    if (!state) {
        state = (UVThreadState*)uv_heap_alloc_raw(sizeof(UVThreadState));
        if (!state) {
            return &uv_tls_fallback;
        }
        state->ctx = NULL;
        state->item = NULL;
        state->gpu_item = NULL;
        uv_rt_tls_set(uv_tls_index, state);
    }
    return state;
}

static uint64_t uv_fresh_task_id(void) {
    return (uint64_t)uv_rt_atomic_increment64(&uv_next_task_id);
}

static uint64_t uv_fresh_completion_seq(void) {
    return (uint64_t)uv_rt_atomic_increment64(&uv_next_completion_seq);
}

static ParallelContext* uv_current_ctx(void) {
    return uv_tls_state()->ctx;
}

static WorkItem* uv_current_item(void) {
    return uv_tls_state()->item;
}

static void uv_set_current_ctx(ParallelContext* ctx) {
    uv_tls_state()->ctx = ctx;
}

static void uv_set_current_item(WorkItem* item) {
    uv_tls_state()->item = item;
}

static UVUsize3 uv_gpu_triplet(uint64_t x, uint64_t y, uint64_t z) {
    UVUsize3 out;
    out.x = x;
    out.y = y;
    out.z = z;
    return out;
}

static uint64_t uv_gpu_workgroup_volume(UVUsize3 workgroup_size) {
    if (workgroup_size.x == 0 || workgroup_size.y == 0 ||
        workgroup_size.z == 0) {
        return 0;
    }
    return workgroup_size.x * workgroup_size.y * workgroup_size.z;
}

static UVUsize3 uv_gpu_local_id_from_linear(uint64_t linear,
                                            UVUsize3 workgroup_size) {
    if (workgroup_size.x == 0 || workgroup_size.y == 0) {
        return uv_gpu_triplet(0, 0, 0);
    }
    return uv_gpu_triplet(
        linear % workgroup_size.x,
        (linear / workgroup_size.x) % workgroup_size.y,
        (linear / (workgroup_size.x * workgroup_size.y)) %
            workgroup_size.z);
}

static UVUsize3 uv_gpu_workgroup_id_from_linear(uint64_t linear,
                                                uint64_t workgroup_volume) {
    if (workgroup_volume == 0) {
        return uv_gpu_triplet(0, 0, 0);
    }
    return uv_gpu_triplet(linear / workgroup_volume, 0, 0);
}

static UVUsize3 uv_gpu_global_id(UVUsize3 local_id,
                                 UVUsize3 workgroup_id,
                                 UVUsize3 workgroup_size) {
    return uv_gpu_triplet(
        local_id.x + workgroup_id.x * workgroup_size.x,
        local_id.y + workgroup_id.y * workgroup_size.y,
        local_id.z + workgroup_id.z * workgroup_size.z);
}

static void uv_gpu_memory_fence(void) {
    static volatile uv_rt_i64_t fence_counter = 0;
    (void)uv_rt_atomic_increment64(&fence_counter);
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
    uv_rt_handle_t done_event;       // Signaled on completion
    SpawnHandle* handle;     // Owning handle
};

// §18.4.2 Spawned runtime representation (internal: SpawnHandle)
struct SpawnHandle {
    uint64_t id;
    SpawnHandleStateTag state;
    WorkItem* item;
    SpawnHandle* next;
};

typedef enum {
    GPU_WORK_PENDING,
    GPU_WORK_RUNNING,
    GPU_WORK_AT_BARRIER,
    GPU_WORK_DONE
} UVGpuWorkItemStatus;

typedef struct UVGpuWorkgroupState {
    uv_rt_mutex_t lock;
    uv_rt_condition_t barrier_cv;
    uint64_t expected_count;
    uint64_t barrier_count;
    uint64_t barrier_generation;
    int launch_ready;
    int launch_aborted;
} UVGpuWorkgroupState;

struct UVGpuDispatchState {
    UVUsize3 workgroup_size;
    UVUsize3 num_workgroups;
    UVUsize3 global_size;
    uint64_t workgroup_volume;
    uint64_t start;
    uint64_t end;
    size_t workgroup_count;
    UVGpuWorkgroupState* workgroups;
};

struct UVGpuWorkItemState {
    UVGpuDispatchState* dispatch;
    uint64_t linear_id;
    size_t workgroup_index;
    UVUsize3 local_id;
    UVUsize3 workgroup_id;
    UVUsize3 global_id;
    UVGpuWorkItemStatus status;
    void* private_mem;
};

enum {
    UV_CANCEL_STATUS_ACTIVE = 0,
    UV_CANCEL_STATUS_CANCELLED = 1
};

#define UV_CANCEL_INVALID_ID ((UVCancelId)SIZE_MAX)

typedef struct {
    UVCancelId parent;
    uint8_t status;
    uint8_t _pad[7];
} UVCancelStateEntry;

typedef struct {
    uv_rt_mutex_t lock;
    UVCancelStateEntry* entries;
    size_t count;
    size_t capacity;
} UVCancelRegistry;

static uv_rt_once_t uv_cancel_registry_once = UV_RT_ONCE_INIT;
static UVCancelRegistry uv_cancel_registry = {0};
static int uv_token_is_cancelled(UVCancelId token_id);

typedef struct UVCancelWaitFrame {
    uint64_t resume_state;
    void* resume_fn;
    void* hosted_env;
    UVCancelId token_id;
} UVCancelWaitFrame;

enum {
    UV_ASYNC_DISC_SUSPENDED_LOCAL = 0,
    UV_ASYNC_DISC_COMPLETED_LOCAL = 1,
    UV_ASYNC_DISC_FAILED_LOCAL = 2,
    UV_ASYNC_PAYLOAD_FRAME_PTR_OFFSET_LOCAL = 8,
};

static void uv_cancel_wait_write_completed(UVAsyncResumeValue* out) {
    if (!out) {
        return;
    }
    uv_memset(out, 0, sizeof(*out));
    out->disc = UV_ASYNC_DISC_COMPLETED_LOCAL;
}

static void uv_cancel_wait_write_suspended(UVAsyncResumeValue* out,
                                           UVCancelWaitFrame* frame) {
    if (!out || !frame) {
        return;
    }
    uv_memset(out, 0, sizeof(*out));
    out->disc = UV_ASYNC_DISC_SUSPENDED_LOCAL;
    void* frame_ptr = frame;
    uv_memcpy(out->payload + UV_ASYNC_PAYLOAD_FRAME_PTR_OFFSET_LOCAL,
              &frame_ptr,
              sizeof(frame_ptr));
}

static void uv_cancel_wait_resume(void* hosted_env,
                                  UVAsyncResumeValue* out,
                                  void* frame_ptr,
                                  void* input,
                                  void* panic_out) {
    (void)hosted_env;
    (void)input;
    (void)panic_out;
    UVCancelWaitFrame* frame = (UVCancelWaitFrame*)frame_ptr;
    if (!frame || frame->token_id == UV_CANCEL_INVALID_ID) {
        uv_cancel_wait_write_completed(out);
        if (frame) {
            uv_heap_free_raw(frame);
        }
        return;
    }
    if (uv_token_is_cancelled(frame->token_id)) {
        uv_cancel_wait_write_completed(out);
        uv_heap_free_raw(frame);
        return;
    }
    uv_cancel_wait_write_suspended(out, frame);
}

typedef struct {
    uint8_t panic;
    uint8_t _pad[3];
    uint32_t code;
} UVPanicRecord;

// Worker pool for parallel execution
struct WorkerPool {
    int num_workers;
    int active_workers;
    WorkItem* queue_head;
    WorkItem* queue_tail;
    uv_rt_handle_t* threads;
    uv_rt_mutex_t lock;
    uv_rt_condition_t work_cv;
    uv_rt_condition_t done_cv;
    volatile int shutdown;
    uint8_t threads_started;
    size_t pending_count;
    UVCancelId cancel_token;
};

// §18.1 Parallel context
struct ParallelContext {
    WorkerPool* pool;
    uint32_t domain_kind;
    uint8_t owns_pool;
    size_t pending_count;
    uv_rt_condition_t done_cv;
    UVCancelId cancel_token;
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

static void uv_lock_parallel_ctx(ParallelContext* ctx) {
    if (ctx && ctx->pool) {
        uv_rt_mutex_lock(&ctx->pool->lock);
    }
}

static void uv_unlock_parallel_ctx(ParallelContext* ctx) {
    if (ctx && ctx->pool) {
        uv_rt_mutex_unlock(&ctx->pool->lock);
    }
}

static void uv_record_item_settlement(ParallelContext* ctx, WorkItem* item) {
    if (!item) {
        return;
    }
    if (item->state != WORK_COMPLETED && item->state != WORK_CANCELLED &&
        item->state != WORK_PANICKED) {
        return;
    }

    uv_lock_parallel_ctx(ctx);
    if (item->completion_seq == 0) {
        item->completion_seq = uv_fresh_completion_seq();
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
    uv_unlock_parallel_ctx(ctx);
}

static void uv_register_spawn_handle(ParallelContext* ctx, SpawnHandle* handle) {
    if (!ctx || !handle) {
        return;
    }
    uv_lock_parallel_ctx(ctx);
    handle->next = NULL;
    if (ctx->handles_tail) {
        ctx->handles_tail->next = handle;
        ctx->handles_tail = handle;
    } else {
        ctx->handles_head = handle;
        ctx->handles_tail = handle;
    }
    uv_unlock_parallel_ctx(ctx);
}

static uv_rt_bool_t uv_cancel_registry_init(uv_rt_once_t* init_once,
                                                 void* param,
                                                 void** context) {
    (void)init_once;
    (void)param;
    (void)context;
    uv_rt_mutex_init(&uv_cancel_registry.lock);
    uv_cancel_registry.entries = NULL;
    uv_cancel_registry.count = 0;
    uv_cancel_registry.capacity = 0;
    return UV_RT_TRUE;
}

static int uv_cancel_registry_ready(void) {
    return uv_rt_once_execute(&uv_cancel_registry_once,
                                   uv_cancel_registry_init,
                                   NULL,
                                   NULL)
               ? 1
               : 0;
}

static int uv_cancel_registry_valid_id_locked(UVCancelId id) {
    return id != UV_CANCEL_INVALID_ID && id < uv_cancel_registry.count;
}

static int uv_cancel_registry_reserve_locked(size_t needed) {
    if (needed <= uv_cancel_registry.capacity) {
        return 1;
    }

    size_t new_capacity = uv_cancel_registry.capacity ? uv_cancel_registry.capacity : 16u;
    while (new_capacity < needed) {
        if (new_capacity > (SIZE_MAX / 2u)) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2u;
    }

    const size_t bytes = new_capacity * sizeof(UVCancelStateEntry);
    UVCancelStateEntry* new_entries =
        (UVCancelStateEntry*)uv_heap_alloc_raw(bytes);
    if (!new_entries) {
        return 0;
    }

    uv_memset(new_entries, 0, bytes);
    if (uv_cancel_registry.entries && uv_cancel_registry.count > 0) {
        uv_memcpy(new_entries,
                  uv_cancel_registry.entries,
                  uv_cancel_registry.count * sizeof(UVCancelStateEntry));
        uv_heap_free_raw(uv_cancel_registry.entries);
    }

    uv_cancel_registry.entries = new_entries;
    uv_cancel_registry.capacity = new_capacity;
    return 1;
}

static UVCancelId uv_cancel_registry_new_locked(UVCancelId parent) {
    const size_t next = uv_cancel_registry.count;
    if (!uv_cancel_registry_reserve_locked(next + 1u)) {
        return UV_CANCEL_INVALID_ID;
    }

    uv_cancel_registry.entries[next].parent = parent;
    uv_cancel_registry.entries[next].status = UV_CANCEL_STATUS_ACTIVE;
    uv_cancel_registry.count = next + 1u;
    return (UVCancelId)next;
}

static int uv_cancel_registry_descendant_locked(UVCancelId root,
                                                UVCancelId candidate) {
    if (!uv_cancel_registry_valid_id_locked(root) ||
        !uv_cancel_registry_valid_id_locked(candidate)) {
        return 0;
    }

    UVCancelId current = candidate;
    for (;;) {
        if (current == root) {
            return 1;
        }
        if (!uv_cancel_registry_valid_id_locked(current)) {
            return 0;
        }
        const UVCancelId parent = uv_cancel_registry.entries[current].parent;
        if (parent == UV_CANCEL_INVALID_ID) {
            return 0;
        }
        current = parent;
    }
}

static void uv_cancel_registry_cancel_locked(UVCancelId id) {
    if (!uv_cancel_registry_valid_id_locked(id)) {
        return;
    }

    for (size_t i = 0; i < uv_cancel_registry.count; ++i) {
        if (uv_cancel_registry_descendant_locked(id, (UVCancelId)i)) {
            uv_cancel_registry.entries[i].status = UV_CANCEL_STATUS_CANCELLED;
        }
    }
}

// Thread-local parallel context tracking (for nested parallel support)

static int uv_token_is_cancelled(UVCancelId token_id) {
    int cancelled = 0;
    if (!uv_cancel_registry_ready()) {
        return 0;
    }

    uv_rt_mutex_lock(&uv_cancel_registry.lock);
    if (uv_cancel_registry_valid_id_locked(token_id)) {
        cancelled =
            uv_cancel_registry.entries[token_id].status == UV_CANCEL_STATUS_CANCELLED;
    }
    uv_rt_mutex_unlock(&uv_cancel_registry.lock);
    return cancelled;
}

static uint32_t uv_u64_to_dec(uint64_t value, char* out) {
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

static const char* uv_gpu_status_name(UVGpuWorkItemStatus status) {
    switch (status) {
        case GPU_WORK_PENDING:
            return "Pending";
        case GPU_WORK_RUNNING:
            return "Running";
        case GPU_WORK_AT_BARRIER:
            return "AtBarrier";
        case GPU_WORK_DONE:
            return "Done";
        default:
            return "Unknown";
    }
}

static void uv_gpu_trace_payload_append(char* payload,
                                        uint64_t* payload_len,
                                        uint64_t payload_cap,
                                        const char* text) {
    if (!payload || !payload_len || payload_cap == 0 || !text) {
        return;
    }
    while (*text != 0 && *payload_len + 1 < payload_cap) {
        payload[*payload_len] = *text;
        *payload_len += 1;
        text += 1;
    }
}

static void uv_gpu_trace_payload_append_u64(char* payload,
                                            uint64_t* payload_len,
                                            uint64_t payload_cap,
                                            const char* key,
                                            uint64_t value) {
    char decimal[32];
    const uint32_t decimal_len = uv_u64_to_dec(value, decimal);
    uv_gpu_trace_payload_append(payload, payload_len, payload_cap, key);
    uv_gpu_trace_payload_append(payload, payload_len, payload_cap, "=");
    for (uint32_t i = 0; i < decimal_len && *payload_len + 1 < payload_cap; ++i) {
        payload[*payload_len] = decimal[i];
        *payload_len += 1;
    }
    uv_gpu_trace_payload_append(payload, payload_len, payload_cap, ";");
}

static void uv_gpu_trace_payload_append_triplet(char* payload,
                                                uint64_t* payload_len,
                                                uint64_t payload_cap,
                                                const char* key,
                                                UVUsize3 value) {
    char axis_key[64];
    const char axes[3] = {'x', 'y', 'z'};
    const uint64_t values[3] = {value.x, value.y, value.z};
    for (uint32_t axis = 0; axis < 3; ++axis) {
        uint64_t axis_len = 0;
        uv_gpu_trace_payload_append(axis_key, &axis_len, sizeof(axis_key), key);
        uv_gpu_trace_payload_append(axis_key, &axis_len, sizeof(axis_key), "_");
        if (axis_len + 1 < (uint64_t)sizeof(axis_key)) {
            axis_key[axis_len] = axes[axis];
            axis_len += 1;
        }
        if (axis_len < (uint64_t)sizeof(axis_key)) {
            axis_key[axis_len] = 0;
        } else {
            axis_key[sizeof(axis_key) - 1] = 0;
        }
        uv_gpu_trace_payload_append_u64(
            payload,
            payload_len,
            payload_cap,
            axis_key,
            values[axis]);
    }
}

#define UV_GPU_TRACE_NO_GENERATION ((uint64_t)~(uint64_t)0)

static void uv_gpu_trace_emit_with_transition(const char* rule_id,
                                              const char* event,
                                              const char* transition,
                                              const UVGpuWorkItemState* item,
                                              const UVGpuWorkgroupState* group,
                                              uint64_t generation_before,
                                              uint64_t generation_after) {
    if (!rule_id || !event) {
        return;
    }

    char payload_buf[640];
    uint64_t payload_len = 0;
    uv_gpu_trace_payload_append(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "category=runtime;level=trace;component=GpuRuntimeState;event=");
    uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), event);
    uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), ";");
    if (transition && transition[0] != 0) {
        uv_gpu_trace_payload_append(
            payload_buf,
            &payload_len,
            sizeof(payload_buf),
            "transition=");
        uv_gpu_trace_payload_append(
            payload_buf,
            &payload_len,
            sizeof(payload_buf),
            transition);
        uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), ";");
    }

    if (item) {
        uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), "status=");
        uv_gpu_trace_payload_append(
            payload_buf,
            &payload_len,
            sizeof(payload_buf),
            uv_gpu_status_name(item->status));
        uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), ";");
        uv_gpu_trace_payload_append_u64(
            payload_buf,
            &payload_len,
            sizeof(payload_buf),
            "linear_id",
            item->linear_id);
        uv_gpu_trace_payload_append_u64(
            payload_buf,
            &payload_len,
            sizeof(payload_buf),
            "workgroup_index",
            (uint64_t)item->workgroup_index);
        uv_gpu_trace_payload_append_triplet(
            payload_buf,
            &payload_len,
            sizeof(payload_buf),
            "local_id",
            item->local_id);
        uv_gpu_trace_payload_append_triplet(
            payload_buf,
            &payload_len,
            sizeof(payload_buf),
            "workgroup_id",
            item->workgroup_id);
        uv_gpu_trace_payload_append_triplet(
            payload_buf,
            &payload_len,
            sizeof(payload_buf),
            "global_id",
            item->global_id);

        if (item->dispatch) {
            uv_gpu_trace_payload_append_triplet(
                payload_buf,
                &payload_len,
                sizeof(payload_buf),
                "workgroup_size",
                item->dispatch->workgroup_size);
            uv_gpu_trace_payload_append_triplet(
                payload_buf,
                &payload_len,
                sizeof(payload_buf),
                "global_size",
                item->dispatch->global_size);
            uv_gpu_trace_payload_append_triplet(
                payload_buf,
                &payload_len,
                sizeof(payload_buf),
                "num_workgroups",
                item->dispatch->num_workgroups);
            uv_gpu_trace_payload_append_u64(
                payload_buf,
                &payload_len,
                sizeof(payload_buf),
                "workgroup_volume",
                item->dispatch->workgroup_volume);
        }
    }

    if (group) {
        uv_gpu_trace_payload_append_u64(
            payload_buf,
            &payload_len,
            sizeof(payload_buf),
            "barrier_count",
            group->barrier_count);
        uv_gpu_trace_payload_append_u64(
            payload_buf,
            &payload_len,
            sizeof(payload_buf),
            "expected_count",
            group->expected_count);
        uv_gpu_trace_payload_append_u64(
            payload_buf,
            &payload_len,
            sizeof(payload_buf),
            "barrier_generation",
            group->barrier_generation);
    }

    if (generation_before != UV_GPU_TRACE_NO_GENERATION) {
        uv_gpu_trace_payload_append_u64(
            payload_buf,
            &payload_len,
            sizeof(payload_buf),
            "generation_before",
            generation_before);
    }
    if (generation_after != UV_GPU_TRACE_NO_GENERATION) {
        uv_gpu_trace_payload_append_u64(
            payload_buf,
            &payload_len,
            sizeof(payload_buf),
            "generation_after",
            generation_after);
    }

    UVStringView rule_view;
    rule_view.data = (const uint8_t*)rule_id;
    rule_view.len = uv_cstr_len(rule_id);

    UVStringView file_view;
    file_view.data = NULL;
    file_view.len = 0;

    UVStringView payload_view;
    payload_view.data = (const uint8_t*)payload_buf;
    payload_view.len = payload_len;

    ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit(
        rule_view,
        file_view,
        0,
        0,
        0,
        0,
        payload_view);
}

static void uv_gpu_trace_emit(const char* rule_id,
                              const char* event,
                              const UVGpuWorkItemState* item,
                              const UVGpuWorkgroupState* group,
                              uint64_t generation_before,
                              uint64_t generation_after) {
    uv_gpu_trace_emit_with_transition(
        rule_id,
        event,
        NULL,
        item,
        group,
        generation_before,
        generation_after);
}

static int uv_gpu_workgroup_volume_within_limit(UVUsize3 workgroup_size,
                                                uint64_t limit) {
    if (workgroup_size.x == 0 || workgroup_size.y == 0 ||
        workgroup_size.z == 0) {
        return 0;
    }
    if (workgroup_size.x > limit) {
        return 0;
    }
    const uint64_t xy_limit = limit / workgroup_size.x;
    if (workgroup_size.y > xy_limit) {
        return 0;
    }
    const uint64_t xyz_limit = xy_limit / workgroup_size.y;
    return workgroup_size.z <= xyz_limit;
}

static int uv_gpu_topology_axis_valid(uint64_t global_size,
                                      uint64_t workgroup_size,
                                      uint64_t num_workgroups) {
    if (workgroup_size == 0) {
        return 0;
    }
    if (num_workgroups != 0 && workgroup_size > UINT64_MAX / num_workgroups) {
        return 0;
    }
    return global_size == workgroup_size * num_workgroups;
}

static int uv_gpu_topology_valid(const UVGpuDispatchState* state) {
    if (!state) {
        return 0;
    }
    if (!uv_gpu_workgroup_volume_within_limit(state->workgroup_size, 1024)) {
        return 0;
    }
    return uv_gpu_topology_axis_valid(
               state->global_size.x,
               state->workgroup_size.x,
               state->num_workgroups.x) &&
           uv_gpu_topology_axis_valid(
               state->global_size.y,
               state->workgroup_size.y,
               state->num_workgroups.y) &&
           uv_gpu_topology_axis_valid(
               state->global_size.z,
               state->workgroup_size.z,
               state->num_workgroups.z);
}

static void uv_gpu_trace_emit_dispatch(const char* rule_id,
                                       const char* event,
                                       const UVGpuDispatchState* state,
                                       uint64_t range_count,
                                       uint64_t work_item_count) {
    if (!rule_id || !event || !state) {
        return;
    }

    char payload_buf[640];
    uint64_t payload_len = 0;
    uv_gpu_trace_payload_append(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "category=runtime;level=trace;component=GpuRuntimeState;event=");
    uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), event);
    uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), ";");
    uv_gpu_trace_payload_append(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "source=uv_gpu_dispatch_run;");
    uv_gpu_trace_payload_append_triplet(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "workgroup_size",
        state->workgroup_size);
    uv_gpu_trace_payload_append_triplet(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "global_size",
        state->global_size);
    uv_gpu_trace_payload_append_triplet(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "num_workgroups",
        state->num_workgroups);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "workgroup_volume",
        state->workgroup_volume);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "range_count",
        range_count);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "work_item_count",
        work_item_count);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "topology_valid",
        (uint64_t)(uv_gpu_topology_valid(state) ? 1 : 0));

    UVStringView rule_view;
    rule_view.data = (const uint8_t*)rule_id;
    rule_view.len = uv_cstr_len(rule_id);

    UVStringView file_view;
    file_view.data = NULL;
    file_view.len = 0;

    UVStringView payload_view;
    payload_view.data = (const uint8_t*)payload_buf;
    payload_view.len = payload_len;

    ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit(
        rule_view,
        file_view,
        0,
        0,
        0,
        0,
        payload_view);
}

static void uv_gpu_trace_emit_memory_forms(const UVGpuWorkItemState* item) {
    if (!item || !item->dispatch) {
        return;
    }

    char payload_buf[768];
    uint64_t payload_len = 0;
    uv_gpu_trace_payload_append(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "category=runtime;level=trace;component=GpuMemoryForms;"
        "event=gpu_memory_forms_created;source=uv_gpu_prepare_work_item;"
        "forms=GlobalMem,SharedMem,PrivateMem;"
        "address_spaces=Global,Shared,Private;"
        "global_scope=dispatch;shared_scope=workgroup;"
        "private_scope=work_item;");
    uv_gpu_trace_payload_append_triplet(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "workgroup_id",
        item->workgroup_id);
    uv_gpu_trace_payload_append_triplet(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "global_id",
        item->global_id);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "linear_id",
        item->linear_id);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "workgroup_index",
        (uint64_t)item->workgroup_index);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "workgroup_count",
        (uint64_t)item->dispatch->workgroup_count);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "work_item_count",
        item->dispatch->end - item->dispatch->start);

    UVStringView rule_view;
    rule_view.data = (const uint8_t*)"def.20.GpuMemoryForms";
    rule_view.len = uv_cstr_len("def.20.GpuMemoryForms");

    UVStringView file_view;
    file_view.data = NULL;
    file_view.len = 0;

    UVStringView payload_view;
    payload_view.data = (const uint8_t*)payload_buf;
    payload_view.len = payload_len;

    ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit(
        rule_view,
        file_view,
        0,
        0,
        0,
        0,
        payload_view);
}

static void uv_gpu_trace_emit_parallel(const char* rule_id,
                                       const char* event,
                                       const ParallelContext* ctx,
                                       uint32_t panic_code) {
    if (!rule_id || !event || !ctx) {
        return;
    }

    char payload_buf[320];
    uint64_t payload_len = 0;
    uv_gpu_trace_payload_append(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "category=runtime;level=trace;component=GpuRuntimeState;event=");
    uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), event);
    uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), ";");
    uv_gpu_trace_payload_append(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "source=uv_parallel_join;");
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "domain_kind",
        ctx->domain_kind);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "panic_code",
        panic_code);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "has_pool",
        (uint64_t)(ctx->pool ? 1 : 0));

    UVStringView rule_view;
    rule_view.data = (const uint8_t*)rule_id;
    rule_view.len = uv_cstr_len(rule_id);

    UVStringView file_view;
    file_view.data = NULL;
    file_view.len = 0;

    UVStringView payload_view;
    payload_view.data = (const uint8_t*)payload_buf;
    payload_view.len = payload_len;

    ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit(
        rule_view,
        file_view,
        0,
        0,
        0,
        0,
        payload_view);
}

static void uv_parallel_trace_emit(const char* rule_id,
                                   const char* event,
                                   const char* source,
                                   const ParallelContext* ctx,
                                   const WorkItem* item,
                                   uint32_t panic_code) {
    if (!rule_id || !event || !source) {
        return;
    }

    char payload_buf[384];
    uint64_t payload_len = 0;
    uv_gpu_trace_payload_append(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "category=runtime;level=trace;component=StructuredParallelRuntime;event=");
    uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), event);
    uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), ";");
    uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), "source=");
    uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), source);
    uv_gpu_trace_payload_append(payload_buf, &payload_len, sizeof(payload_buf), ";");
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "domain_kind",
        ctx ? ctx->domain_kind : 0);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "task_id",
        item ? item->task_id : 0);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "work_state",
        item ? (uint64_t)item->state : 0);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "completion_seq",
        item ? item->completion_seq : 0);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "panic_code",
        panic_code);
    uv_gpu_trace_payload_append_u64(
        payload_buf,
        &payload_len,
        sizeof(payload_buf),
        "has_cancel_token",
        (uint64_t)(ctx && ctx->cancel_token != UV_CANCEL_INVALID_ID ? 1 : 0));

    UVStringView rule_view;
    rule_view.data = (const uint8_t*)rule_id;
    rule_view.len = uv_cstr_len(rule_id);

    UVStringView file_view;
    file_view.data = NULL;
    file_view.len = 0;

    UVStringView payload_view;
    payload_view.data = (const uint8_t*)payload_buf;
    payload_view.len = payload_len;

    ultraviolet_x3a_x3aruntime_x3a_x3aconformance_x3a_x3aemit(
        rule_view,
        file_view,
        0,
        0,
        0,
        0,
        payload_view);
}

static int32_t uv_priority_rank(int32_t priority_hint) {
    if (priority_hint <= 0) {
        return 0;
    }
    if (priority_hint == 1) {
        return 1;
    }
    return 2;
}

static int32_t uv_effective_priority_rank(ParallelContext* ctx, int32_t priority_hint) {
    if (priority_hint < 0) {
        return ctx ? ctx->domain_priority_hint : 1;
    }
    return uv_priority_rank(priority_hint);
}

static int uv_thread_priority_from_rank(int32_t rank) {
    switch (rank) {
        case 0:
            return UV_RT_THREAD_PRIORITY_LOW;
        case 2:
            return UV_RT_THREAD_PRIORITY_HIGH;
        case 1:
        default:
            return UV_RT_THREAD_PRIORITY_NORMAL;
    }
}

typedef struct {
    uv_rt_uptr_t prev_affinity;
    int prev_priority;
    int affinity_changed;
    int priority_changed;
} UVWorkHintScope;

static void uv_apply_work_hints(const WorkItem* item, UVWorkHintScope* scope) {
    if (!scope) {
        return;
    }

    scope->prev_affinity = 0;
    scope->prev_priority = UV_RT_THREAD_PRIORITY_NORMAL;
    scope->affinity_changed = 0;
    scope->priority_changed = 0;

    if (!item) {
        return;
    }

    if (item->affinity_mask != 0) {
        uv_rt_uptr_t mask = (uv_rt_uptr_t)item->affinity_mask;
        if (mask != 0) {
            scope->prev_affinity =
                uv_rt_thread_affinity_set(uv_rt_current_thread(), mask);
            if (scope->prev_affinity != 0) {
                scope->affinity_changed = 1;
            }
        }
    }

    const int desired_priority =
        uv_thread_priority_from_rank(uv_priority_rank(item->priority_hint));
    if (desired_priority != UV_RT_THREAD_PRIORITY_NORMAL) {
        int prev = uv_rt_thread_priority_get(uv_rt_current_thread());
        if (prev != UV_RT_THREAD_PRIORITY_INVALID &&
            uv_rt_thread_priority_set(uv_rt_current_thread(), desired_priority)) {
            scope->prev_priority = prev;
            scope->priority_changed = 1;
        }
    }
}

static void uv_restore_work_hints(const UVWorkHintScope* scope) {
    if (!scope) {
        return;
    }
    if (scope->priority_changed) {
        uv_rt_thread_priority_set(uv_rt_current_thread(),
                                       scope->prev_priority);
    }
    if (scope->affinity_changed && scope->prev_affinity != 0) {
        uv_rt_thread_affinity_set(uv_rt_current_thread(),
                                       scope->prev_affinity);
    }
}

static uint32_t uv_copy_cstr(char* out, const char* text) {
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

static int uv_debug_flag_enabled(const char* name) {
    if (!name || name[0] == '\0') {
        return 0;
    }
    char probe[2];
    uv_rt_u32_t len =
        uv_rt_env_query_utf8(name, probe, (uv_rt_u32_t)sizeof(probe));
    return len > 0;
}

static uv_rt_handle_t uv_debug_stderr_handle(void) {
    return uv_rt_std_stream(UV_RT_STD_STREAM_ERROR);
}

static void uv_debug_write(uv_rt_handle_t handle,
                           const char* text,
                           uint32_t len) {
    uv_rt_u32_t written = 0;
    uv_rt_handle_write(handle, text, (uv_rt_u32_t)len, &written);
}

static void uv_debug_write_spawn_result(const WorkItem* item) {
    if (!item || !item->result || item->result_size == 0) {
        return;
    }
    uv_rt_handle_t h = uv_debug_stderr_handle();
    if (h == NULL || h == UV_RT_INVALID_HANDLE) {
        return;
    }

    uint32_t first_u32 = 0;
    if (item->result_size >= 4) {
        first_u32 = *(const uint32_t*)item->result;
    }

    char dbg_result[128];
    uint32_t pos = 0;
    pos += uv_copy_cstr(dbg_result + pos, "[SPAWN-RESULT size=");
    pos += uv_u64_to_dec((uint64_t)item->result_size, dbg_result + pos);
    pos += uv_copy_cstr(dbg_result + pos, " first_u32=");
    pos += uv_u64_to_dec((uint64_t)first_u32, dbg_result + pos);
    dbg_result[pos++] = ']';
    dbg_result[pos++] = '\n';

    uv_debug_write(h, dbg_result, pos);
}

static void uv_debug_write_wait_result(const WorkItem* item) {
    if (!item || !item->result || item->result_size == 0) {
        return;
    }
    uv_rt_handle_t h = uv_debug_stderr_handle();
    if (h == NULL || h == UV_RT_INVALID_HANDLE) {
        return;
    }

    uint32_t first_u32 = 0;
    if (item->result_size >= 4) {
        first_u32 = *(const uint32_t*)item->result;
    }

    char dbg_result[128];
    uint32_t pos = 0;
    pos += uv_copy_cstr(dbg_result + pos, "[WAIT-RESULT size=");
    pos += uv_u64_to_dec((uint64_t)item->result_size, dbg_result + pos);
    pos += uv_copy_cstr(dbg_result + pos, " first_u32=");
    pos += uv_u64_to_dec((uint64_t)first_u32, dbg_result + pos);
    dbg_result[pos++] = ']';
    dbg_result[pos++] = '\n';

    uv_debug_write(h, dbg_result, pos);
}

static void uv_debug_write_dispatch_range(UVRange range,
                                          uint64_t start,
                                          uint64_t end,
                                          size_t elem_size,
                                          size_t result_size,
                                          int ordered,
                                          size_t chunk_size) {
    if (!uv_debug_flag_enabled("UV_DEBUG_DISPATCH_RANGE_RUNTIME")) {
        return;
    }
    uv_rt_handle_t h = uv_debug_stderr_handle();
    if (h == NULL || h == UV_RT_INVALID_HANDLE) {
        return;
    }

    char dbg[256];
    uint32_t pos = 0;
    pos += uv_copy_cstr(dbg + pos, "[DISPATCH tag=");
    pos += uv_u64_to_dec((uint64_t)range.tag, dbg + pos);
    pos += uv_copy_cstr(dbg + pos, " lo=");
    pos += uv_u64_to_dec(range.lo, dbg + pos);
    pos += uv_copy_cstr(dbg + pos, " hi=");
    pos += uv_u64_to_dec(range.hi, dbg + pos);
    pos += uv_copy_cstr(dbg + pos, " start=");
    pos += uv_u64_to_dec(start, dbg + pos);
    pos += uv_copy_cstr(dbg + pos, " end=");
    pos += uv_u64_to_dec(end, dbg + pos);
    pos += uv_copy_cstr(dbg + pos, " elem_size=");
    pos += uv_u64_to_dec((uint64_t)elem_size, dbg + pos);
    pos += uv_copy_cstr(dbg + pos, " result_size=");
    pos += uv_u64_to_dec((uint64_t)result_size, dbg + pos);
    pos += uv_copy_cstr(dbg + pos, " ordered=");
    pos += uv_u64_to_dec((uint64_t)(ordered ? 1 : 0), dbg + pos);
    pos += uv_copy_cstr(dbg + pos, " chunk=");
    pos += uv_u64_to_dec((uint64_t)chunk_size, dbg + pos);
    dbg[pos++] = ']';
    dbg[pos++] = '\n';

    uv_debug_write(h, dbg, pos);
}

static void uv_debug_write_dispatch_chunk_value(const char* label,
                                                uint64_t start,
                                                uint64_t end,
                                                const void* result_ptr,
                                                size_t result_size) {
    if (!label || !result_ptr || result_size == 0) {
        return;
    }
    uv_rt_handle_t h = uv_debug_stderr_handle();
    if (h == NULL || h == UV_RT_INVALID_HANDLE) {
        return;
    }
    uint32_t first_u32 = 0;
    if (result_size >= 4) {
        first_u32 = *(const uint32_t*)result_ptr;
    }
    char dbg[192];
    uint32_t pos = 0;
    pos += uv_copy_cstr(dbg + pos, "[DISPATCH-CHUNK ");
    pos += uv_copy_cstr(dbg + pos, label);
    pos += uv_copy_cstr(dbg + pos, " start=");
    pos += uv_u64_to_dec(start, dbg + pos);
    pos += uv_copy_cstr(dbg + pos, " end=");
    pos += uv_u64_to_dec(end, dbg + pos);
    pos += uv_copy_cstr(dbg + pos, " size=");
    pos += uv_u64_to_dec((uint64_t)result_size, dbg + pos);
    pos += uv_copy_cstr(dbg + pos, " first_u32=");
    pos += uv_u64_to_dec((uint64_t)first_u32, dbg + pos);
    dbg[pos++] = ']';
    dbg[pos++] = '\n';
    uv_debug_write(h, dbg, pos);
}

static void uv_debug_write_cancel_state(const char* stage,
                                        const ParallelContext* ctx,
                                        const WorkItem* item,
                                        UVCancelId token_id,
                                        int cancelled) {
    if (!uv_debug_flag_enabled("UV_DEBUG_PARALLEL_RUNTIME")) {
        return;
    }
    uv_rt_handle_t h = uv_debug_stderr_handle();
    if (h == NULL || h == UV_RT_INVALID_HANDLE) {
        return;
    }
    if (!stage) {
        stage = "unknown";
    }

    char dbg[256];
    uint32_t pos = 0;
    pos += uv_copy_cstr(dbg + pos, "[CANCEL stage=");
    pos += uv_copy_cstr(dbg + pos, stage);
    pos += uv_copy_cstr(dbg + pos, " ctx=");
    pos += uv_u64_to_dec((uint64_t)(uintptr_t)ctx, dbg + pos);
    pos += uv_copy_cstr(dbg + pos, " item=");
    pos += uv_u64_to_dec((uint64_t)(uintptr_t)item, dbg + pos);
    pos += uv_copy_cstr(dbg + pos, " token=");
    if (token_id == UV_CANCEL_INVALID_ID) {
        pos += uv_copy_cstr(dbg + pos, "none");
    } else {
        pos += uv_u64_to_dec((uint64_t)token_id, dbg + pos);
    }
    pos += uv_copy_cstr(dbg + pos, " cancelled=");
    pos += uv_u64_to_dec((uint64_t)(cancelled ? 1 : 0), dbg + pos);
    dbg[pos++] = ']';
    dbg[pos++] = '\n';
    uv_debug_write(h, dbg, pos);
}

typedef struct UVRunItemBoundaryContext {
    ParallelContext* ctx;
    WorkItem* item;
    UVPanicRecord* panic_record;
} UVRunItemBoundaryContext;

typedef struct UVReduceBoundaryContext {
    void* hosted_env;
    void* left_result;
    void* right_result;
    void* out_result;
    void (*reduce_fn)(void* hosted_env,
                      void* lhs,
                      void* rhs,
                      void* out,
                      void* panic_out);
    UVPanicRecord* panic_record;
} UVReduceBoundaryContext;

static void uv_parallel_run_item_body(void* context) {
    UVRunItemBoundaryContext* boundary = (UVRunItemBoundaryContext*)context;
    ParallelContext* ctx;
    WorkItem* item;
    UVPanicRecord* panic_record;
    int has_cancel_token;
    int is_cancelled;

    if (!boundary) {
        return;
    }

    ctx = boundary->ctx;
    item = boundary->item;
    panic_record = boundary->panic_record;
    has_cancel_token = ctx && ctx->cancel_token != UV_CANCEL_INVALID_ID;
    is_cancelled = has_cancel_token ? uv_token_is_cancelled(ctx->cancel_token)
                                    : 0;

    if (has_cancel_token && is_cancelled) {
        uv_debug_write_cancel_state("run_item-cancel", ctx, item,
                                    ctx->cancel_token, 1);
        item->state = WORK_CANCELLED;
        if (item->result && item->result_size > 0) {
            uv_memset(item->result, 0, item->result_size);
        }
        uv_parallel_trace_emit(
            "requirement.20.CooperativeCancellationBehavior",
            "queued_cancel_suppressed",
            "uv_parallel_run_item_body",
            ctx,
            item,
            0);
        return;
    }

    if (ctx) {
        uv_debug_write_cancel_state("run_item-start", ctx, item,
                                    ctx->cancel_token, is_cancelled);
    } else {
        uv_debug_write_cancel_state("run_item-start", ctx, item,
                                    UV_CANCEL_INVALID_ID, 0);
    }

    item->state = WORK_RUNNING;
    if (item->body) {
        item->body(item->hosted_env, item->captured_env, item->result,
                   panic_record);
        if (item->result && item->result_size > 0 &&
            uv_debug_flag_enabled("UV_DEBUG_SPAWN_RESULT_RUNTIME")) {
            uv_debug_write_spawn_result(item);
        }
    }
    if (panic_record->panic) {
        uv_parallel_work_panic(ctx, panic_record->code);
    }
    if (item->state == WORK_RUNNING) {
        item->state = WORK_COMPLETED;
    }
}

static void uv_parallel_run_reduce_body(void* context) {
    UVReduceBoundaryContext* boundary = (UVReduceBoundaryContext*)context;
    if (!boundary || !boundary->reduce_fn) {
        return;
    }
    boundary->reduce_fn(boundary->hosted_env,
                        boundary->left_result,
                        boundary->right_result,
                        boundary->out_result,
                        boundary->panic_record);
}

static void uv_run_item(ParallelContext* ctx, WorkItem* item) {
    UVRunItemBoundaryContext boundary;
    uv_rt_u32_t panic_code = 0u;

    if (!item) {
        return;
    }
    UVThreadState* state = uv_tls_state();
    ParallelContext* prev_ctx = state->ctx;
    WorkItem* prev_item = state->item;
    UVPanicRecord panic_record;

    panic_record.panic = 0;
    panic_record.code = 0;
    boundary.ctx = ctx;
    boundary.item = item;
    boundary.panic_record = &panic_record;

    state->ctx = ctx;
    state->item = item;
    if (!uv_rt_panic_boundary_run(uv_parallel_run_item_body,
                                       &boundary,
                                       &panic_code)) {
        uv_parallel_work_panic(ctx, panic_code);
    }
    uv_record_item_settlement(ctx, item);
    state->item = prev_item;
    state->ctx = prev_ctx;
}

static SpawnHandle* uv_await_spawned(ParallelContext* ctx) {
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
            UVWorkHintScope hints;
            uv_apply_work_hints(item, &hints);
            uv_run_item(item->owner_ctx, item);
            uv_restore_work_hints(&hints);
            if (item->done_event) {
                uv_rt_event_set(item->done_event);
            }
        }

        if (item->done_event) {
            uv_rt_wait(item->done_event, UV_RT_WAIT_FOREVER);
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
    if (failed && failed->item) {
        uv_parallel_trace_emit(
            "def.20.FirstCompletedFailure",
            "first_completed_failure_selected",
            "uv_await_spawned",
            ctx,
            failed->item,
            failed->item->panic_code);
    }
    return failed;
}

static void uv_enqueue_item(WorkerPool* pool, WorkItem* item) {
    if (!pool || !item) {
        return;
    }
    uv_start_worker_threads(pool);
    uv_rt_mutex_lock(&pool->lock);
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
        uv_rt_condition_wake_one(&pool->work_cv);
        uv_rt_mutex_unlock(&pool->lock);
}

static WorkItem* uv_dequeue_item_locked(WorkerPool* pool) {
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

int uv_parallel_in_panic_scope(void) {
    return uv_rt_panic_boundary_active() != UV_RT_FALSE;
}

void uv_parallel_raise_panic(uint32_t code) {
    if (!uv_parallel_in_panic_scope()) {
        uv_panic(code);
        return;
    }
    uv_rt_panic_boundary_raise(code);
}
static uv_rt_u32_t uv_worker_thread_proc(void* param) {
    WorkerPool* pool = (WorkerPool*)param;
    for (;;) {
        uv_rt_mutex_lock(&pool->lock);
        while (!pool->shutdown && pool->queue_head == NULL) {
            uv_rt_condition_wait(&pool->work_cv, &pool->lock,
                                      UV_RT_WAIT_FOREVER);
        }
        if (pool->shutdown) {
            uv_rt_mutex_unlock(&pool->lock);
            return 0;
        }

        WorkItem* item = uv_dequeue_item_locked(pool);
        pool->active_workers += 1;
        uv_rt_mutex_unlock(&pool->lock);

        if (item) {
            UVWorkHintScope hints;
            uv_apply_work_hints(item, &hints);
            uv_run_item(item->owner_ctx, item);
            uv_restore_work_hints(&hints);
            uv_rt_event_set(item->done_event);
        }

        uv_rt_mutex_lock(&pool->lock);
        pool->active_workers -= 1;
        if (pool->pending_count > 0) {
            pool->pending_count -= 1;
        }
        if (item && item->owner_ctx && item->owner_ctx->pending_count > 0) {
            item->owner_ctx->pending_count -= 1;
            if (item->owner_ctx->pending_count == 0) {
                uv_rt_condition_wake_all(&item->owner_ctx->done_cv);
            }
        }
        if (pool->pending_count == 0) {
            uv_rt_condition_wake_all(&pool->done_cv);
        }
        uv_rt_mutex_unlock(&pool->lock);
    }
}

static void uv_start_worker_threads(WorkerPool* pool) {
    if (!pool) {
        return;
    }

    uv_rt_mutex_lock(&pool->lock);
    if (pool->threads_started || pool->shutdown) {
        uv_rt_mutex_unlock(&pool->lock);
        return;
    }

    pool->threads_started = 1;
    pool->threads =
        (uv_rt_handle_t*)uv_heap_alloc_raw(
            sizeof(uv_rt_handle_t) * pool->num_workers);
    if (!pool->threads) {
        pool->threads_started = 0;
        uv_rt_mutex_unlock(&pool->lock);
        return;
    }

    for (int i = 0; i < pool->num_workers; ++i) {
        pool->threads[i] =
            uv_rt_thread_spawn(NULL, 0, uv_worker_thread_proc,
                                    pool, 0, NULL);
    }
    uv_rt_mutex_unlock(&pool->lock);
}

// §18.1.1 Begin parallel block
// runtime_parallel_begin(domain) -> ParallelContext*
void* uv_parallel_begin(UVDynObject domain,
                             UVCancelId cancel_token,
                             const char* name) {
    const UVExecutionDomain* dom = (const UVExecutionDomain*)domain.data;
    const uint32_t domain_kind = dom ? dom->kind : (uint32_t)UV_DOMAIN_CPU;
    const int inline_domain = dom && dom->kind == UV_DOMAIN_INLINE;
    const uint64_t domain_affinity_mask = dom ? dom->affinity_mask : 0;
    const int32_t domain_priority_hint = dom ? uv_priority_rank(dom->priority_hint) : 1;
    ParallelContext* prev_ctx = uv_current_ctx();
    if (uv_debug_flag_enabled("UV_DEBUG_PARALLEL_RUNTIME")) {
        char dbg[256];
        uint32_t pos = 0;
        pos += uv_copy_cstr(dbg + pos, "[PAR-BEGIN domain_data=");
        pos += uv_u64_to_dec((uint64_t)(uintptr_t)domain.data, dbg + pos);
        pos += uv_copy_cstr(dbg + pos, " domain_vtable=");
        pos += uv_u64_to_dec((uint64_t)(uintptr_t)domain.vtable, dbg + pos);
        pos += uv_copy_cstr(dbg + pos, " cancel=");
        if (cancel_token == UV_CANCEL_INVALID_ID) {
            pos += uv_copy_cstr(dbg + pos, "none");
        } else {
            pos += uv_u64_to_dec((uint64_t)cancel_token, dbg + pos);
        }
        pos += uv_copy_cstr(dbg + pos, " inline=");
        pos += uv_u64_to_dec((uint64_t)(inline_domain ? 1 : 0), dbg + pos);
        if (dom) {
            pos += uv_copy_cstr(dbg + pos, " kind=");
            pos += uv_u64_to_dec((uint64_t)dom->kind, dbg + pos);
            pos += uv_copy_cstr(dbg + pos, " max=");
            pos += uv_u64_to_dec((uint64_t)dom->max_concurrency, dbg + pos);
            pos += uv_copy_cstr(dbg + pos, " affinity=");
            pos += uv_u64_to_dec(dom->affinity_mask, dbg + pos);
            pos += uv_copy_cstr(dbg + pos, " priority=");
            pos += uv_u64_to_dec((uint64_t)uv_priority_rank(dom->priority_hint), dbg + pos);
        } else {
            pos += uv_copy_cstr(dbg + pos, " kind=null");
        }
        dbg[pos++] = ']';
        dbg[pos++] = '\n';
        uv_rt_handle_write(uv_rt_std_stream(UV_RT_STD_STREAM_ERROR),
                                dbg,
                                (uv_rt_u32_t)pos,
                                NULL);
    }

    ParallelContext* ctx =
        (ParallelContext*)uv_heap_alloc_raw(sizeof(ParallelContext));
    if (!ctx) return NULL;

    ctx->pool = NULL;
    ctx->domain_kind = domain_kind;
    ctx->owns_pool = 0;
    ctx->pending_count = 0;
    uv_rt_condition_init(&ctx->done_cv);
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
        if (domain_kind == (uint32_t)UV_DOMAIN_CPU && prev_ctx &&
            !prev_ctx->inline_domain &&
            prev_ctx->domain_kind == (uint32_t)UV_DOMAIN_CPU &&
            prev_ctx->pool) {
            ctx->pool = prev_ctx->pool;
        } else {
            ctx->pool = (WorkerPool*)uv_heap_alloc_raw(sizeof(WorkerPool));
            if (!ctx->pool) {
                uv_heap_free_raw(ctx);
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
            uv_rt_mutex_init(&ctx->pool->lock);
            uv_rt_condition_init(&ctx->pool->work_cv);
            uv_rt_condition_init(&ctx->pool->done_cv);
        }
    }

    uv_set_current_ctx(ctx);

    return ctx;
}

// §18.1.2 Join parallel block
// Waits for all work to complete and propagates first panic
int uv_parallel_join(void* ctx_ptr) {
    if (uv_debug_flag_enabled("UV_DEBUG_PARALLEL_RUNTIME")) {
        const char* dbg1 = "[JOIN] enter\n";
        uv_rt_handle_write(uv_rt_std_stream(UV_RT_STD_STREAM_ERROR),
                                dbg1,
                                13,
                                NULL);
    }
    
    ParallelContext* ctx = (ParallelContext*)ctx_ptr;
    if (!ctx) {
        if (uv_debug_flag_enabled("UV_DEBUG_PARALLEL_RUNTIME")) {
            const char* dbg2 = "[JOIN] null ctx\n";
            uv_rt_handle_write(uv_rt_std_stream(UV_RT_STD_STREAM_ERROR),
                                    dbg2,
                                    15,
                                    NULL);
        }
        return 0;
    }
    
    if (ctx->pool) {
        // §18.7.1 Wait for all work to complete
        uv_rt_mutex_lock(&ctx->pool->lock);
        while (ctx->pending_count > 0) {
            uv_rt_condition_wait(&ctx->done_cv, &ctx->pool->lock,
                                      UV_RT_WAIT_FOREVER);
        }
        if (ctx->owns_pool) {
            while (ctx->pool->pending_count > 0) {
                uv_rt_condition_wait(&ctx->pool->done_cv,
                                          &ctx->pool->lock,
                                          UV_RT_WAIT_FOREVER);
            }
            ctx->pool->shutdown = 1;
            uv_rt_condition_wake_all(&ctx->pool->work_cv);
        }
        uv_rt_mutex_unlock(&ctx->pool->lock);

        if (ctx->owns_pool && ctx->pool->threads) {
            for (int i = 0; i < ctx->pool->num_workers; ++i) {
                if (ctx->pool->threads[i]) {
                    uv_rt_wait(ctx->pool->threads[i],
                                    UV_RT_WAIT_FOREVER);
                    uv_rt_handle_release(ctx->pool->threads[i]);
                }
            }
            uv_heap_free_raw(ctx->pool->threads);
            ctx->pool->threads = NULL;
        }
    }
    
    SpawnHandle* failed_spawn = uv_await_spawned(ctx);
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
    if (had_panic) {
        uv_trace_emit_rule("requirement.20.ParallelPanicPropagationReference");
    }
    if (!had_panic && ctx->domain_kind == (uint32_t)UV_DOMAIN_GPU) {
        uv_gpu_trace_emit_parallel(
            "rule.20.EvalSigma-GPU-Parallel",
            "gpu_parallel_join",
            ctx,
            panic_code);
    }
    
    // Cleanup
    WorkItem* item = ctx->all_items;
    while (item) {
        WorkItem* next = item->all_next;
        if (item->captured_env) {
            uv_heap_free_raw(item->captured_env);
        }
        if (item->result) {
            uv_heap_free_raw(item->result);
        }
        if (item->done_event) {
                    uv_rt_handle_release(item->done_event);
        }
        if (item->handle) {
            uv_heap_free_raw(item->handle);
        }
        uv_heap_free_raw(item);
        item = next;
    }

    if (ctx->pool && ctx->owns_pool) {
            uv_rt_mutex_destroy(&ctx->pool->lock);
        uv_heap_free_raw(ctx->pool);
    }
    uv_set_current_ctx(ctx->prev_ctx);
    uv_heap_free_raw(ctx);
    
    // §18.7.1 Report panic-at-boundary to caller.
    // The caller is responsible for re-emitting panic in the active boundary
    // mechanism (panic record / catch-zero at FFI boundary).
    if (had_panic && uv_debug_flag_enabled("UV_DEBUG_PARALLEL_RUNTIME")) {
        const char* dbg = "[JOIN] propagating panic\n";
        uv_rt_handle_write(uv_rt_std_stream(UV_RT_STD_STREAM_ERROR),
                                dbg,
                                24,
                                NULL);
    }
    
    if (uv_debug_flag_enabled("UV_DEBUG_PARALLEL_RUNTIME")) {
        const char* dbg_exit = "[JOIN] done\n";
        uv_rt_handle_write(uv_rt_std_stream(UV_RT_STD_STREAM_ERROR),
                                dbg_exit,
                                12,
                                NULL);
    }
    return (int)panic_code;
}

// §18.4.2 Create spawn handle
// Returns Spawned<T>@Pending (or @Ready if body is NULL for inline execution)
void* uv_spawn_create(void* env, size_t env_size,
                            void (*body)(void* hosted_env, void* env, void* result, void* panic_out),
                            void* hosted_env,
                            size_t result_size,
                            uint64_t affinity_mask,
                            int32_t priority_hint) {
    ParallelContext* current_ctx = uv_current_ctx();
    if (uv_debug_flag_enabled("UV_DEBUG_PARALLEL_RUNTIME")) {
        static int spawn_count = 0;
        spawn_count++;
        char dbg[32];
        dbg[0] = '['; dbg[1] = 'S'; dbg[2] = 'P'; dbg[3] = 'A'; dbg[4] = 'W'; dbg[5] = 'N';
        dbg[6] = ' '; dbg[7] = '#'; dbg[8] = '0' + (spawn_count % 10); dbg[9] = ']'; dbg[10] = '\n';
        uv_rt_handle_write(uv_rt_std_stream(UV_RT_STD_STREAM_ERROR),
                                dbg,
                                11,
                                NULL);
    }
    SpawnHandle* handle =
        (SpawnHandle*)uv_heap_alloc_raw(sizeof(SpawnHandle));
    if (!handle) return NULL;
    
    WorkItem* item = (WorkItem*)uv_heap_alloc_raw(sizeof(WorkItem));
    if (!item) {
        uv_heap_free_raw(handle);
        return NULL;
    }
    
    // Copy captured environment
    item->captured_env = NULL;
    if (env && env_size > 0) {
        item->captured_env = uv_heap_alloc_raw(env_size);
        if (item->captured_env) {
            uv_memcpy(item->captured_env, env, env_size);
        }
    }

    item->done_event =
        uv_rt_event_open(NULL, UV_RT_TRUE, UV_RT_FALSE, NULL);

    item->state = WORK_PENDING;
    item->task_id = uv_fresh_task_id();
    item->completion_seq = 0;
    item->owner_ctx = current_ctx;
    item->hosted_env = hosted_env;
    item->body = body;
    item->result = result_size > 0 ? uv_heap_alloc_raw(result_size) : NULL;
    item->result_size = result_size;
    item->affinity_mask = affinity_mask != 0
                              ? affinity_mask
                              : (current_ctx ? current_ctx->domain_affinity_mask : 0);
    item->priority_hint = uv_effective_priority_rank(current_ctx, priority_hint);
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
        uv_record_item_settlement(current_ctx, item);
        if (item->done_event) {
            uv_rt_event_set(item->done_event);
        }
    } else if (current_ctx && current_ctx->pool) {
        uv_enqueue_item(current_ctx->pool, item);
    } else if (!current_ctx) {
        // Defensive fallback for invalid outside-parallel runtime entry.
        UVWorkHintScope hints;
        uv_apply_work_hints(item, &hints);
        uv_run_item(item->owner_ctx, item);
        uv_restore_work_hints(&hints);
        if (item->done_event) {
            uv_rt_event_set(item->done_event);
        }
    } else {
        // Inline/no-pool parallel domains keep spawned work pending until wait/join.
    }

    if (current_ctx) {
        uv_register_spawn_handle(current_ctx, handle);
        item->all_next = current_ctx->all_items;
        current_ctx->all_items = item;
    }
    
    return handle;
}

// §10.3 Wait for spawn result
// Blocks until handle is ready, returns extracted value
void* uv_spawn_wait(void* handle_ptr) {
    SpawnHandle* handle = (SpawnHandle*)handle_ptr;
    if (!handle || !handle->item) {
        if (uv_debug_flag_enabled("UV_DEBUG_PARALLEL_RUNTIME")) {
            const char* dbg = "[WAIT] null handle\n";
            uv_rt_handle_write(uv_rt_std_stream(UV_RT_STD_STREAM_ERROR),
                                    dbg,
                                    18,
                                    NULL);
        }
        return NULL;
    }
    
    WorkItem* item = handle->item;
    
    if (item->state == WORK_PENDING &&
        (!item->done_event || (item->owner_ctx && !item->owner_ctx->pool))) {
        uv_run_item(item->owner_ctx, item);
        if (item->done_event) {
            uv_rt_event_set(item->done_event);
        }
    } else if (item->done_event) {
        uv_rt_wait(item->done_event, UV_RT_WAIT_FOREVER);
    }

    if (uv_debug_flag_enabled("UV_DEBUG_PARALLEL_RUNTIME")) {
        char dbg[32];
        dbg[0] = '['; dbg[1] = 'W'; dbg[2] = 'A'; dbg[3] = 'I'; dbg[4] = 'T';
        dbg[5] = ' '; dbg[6] = 's'; dbg[7] = 't'; dbg[8] = '=';
        dbg[9] = '0' + item->state; dbg[10] = ']'; dbg[11] = '\n';
        uv_rt_handle_write(uv_rt_std_stream(UV_RT_STD_STREAM_ERROR),
                                dbg,
                                12,
                                NULL);
    }
    
    // §18.7.1 Panic is propagated at the enclosing parallel boundary after
    // all started work settles. wait returns the current value slot and does
    // not directly abort/rethrow.
    if (item->state == WORK_PANICKED) {
        if (uv_debug_flag_enabled("UV_DEBUG_PARALLEL_RUNTIME")) {
            const char* dbg2 = "[WAIT] propagating panic\n";
            uv_rt_handle_write(uv_rt_std_stream(UV_RT_STD_STREAM_ERROR),
                                    dbg2,
                                    24,
                                    NULL);
        }
        if (item->result && item->result_size > 0) {
            uv_memset(item->result, 0, item->result_size);
        }
    }

    if (uv_debug_flag_enabled("UV_DEBUG_SPAWN_RESULT_RUNTIME")) {
        uv_debug_write_wait_result(item);
    }
    
    return item->result;
}

static UVAsyncResumeValue uv_reactor_resolve_future(const void* future) {
    UVAsyncResumeValue current;
    uv_memset(&current, 0, sizeof(current));
    if (!future) {
        return current;
    }

    uv_memcpy(&current, future, sizeof(current));
    for (uint32_t step = 0; step < 1024 &&
                          current.disc == UV_ASYNC_DISC_SUSPENDED_LOCAL;
         ++step) {
        uint64_t panic_record[2];
        uv_memset(panic_record, 0, sizeof(panic_record));
        UVAsyncResumeValue next =
            ultraviolet_x3a_x3aruntime_x3a_x3aasync_x3a_x3aresume(
                &current,
                NULL,
                panic_record);
        if (((uint8_t*)panic_record)[0] != 0) {
            break;
        }
        if (next.disc == UV_ASYNC_DISC_SUSPENDED_LOCAL) {
            void* next_frame = NULL;
            uv_memcpy(&next_frame,
                      next.payload + UV_ASYNC_PAYLOAD_FRAME_PTR_OFFSET_LOCAL,
                      sizeof(next_frame));
            current = next;
            if (!next_frame) {
                break;
            }
            continue;
        }
        current = next;
    }
    return current;
}

// Reactor::run runtime hook.
// ABI is type-erased at this boundary: the reactor capability object is passed
// by reference and the future is passed by pointer.
UVAsyncResumeValue ultraviolet_x3a_x3aruntime_x3a_x3areactor_x3a_x3arun(
    const UVDynObject* reactor,
    const void* future) {
    (void)reactor;
    uv_trace_emit_rule("BuiltinSym-Reactor-Run");
    return uv_reactor_resolve_future(future);
}

// Reactor::register runtime hook.
// ABI is type-erased at this boundary: the reactor capability object is passed
// by reference, the future is passed by pointer, and the returned tracked
// handle is the same opaque spawn handle used by wait.
void* ultraviolet_x3a_x3aruntime_x3a_x3areactor_x3a_x3aregister(
    const UVDynObject* reactor,
    const void* future) {
    (void)reactor;
    uv_trace_emit_rule("BuiltinSym-Reactor-Register");

    SpawnHandle* handle =
        (SpawnHandle*)uv_spawn_create(NULL, 0, NULL, NULL, 8, 0, 1);
    if (!handle || !handle->item || !handle->item->result) {
        return handle;
    }

    uint8_t* out = (uint8_t*)handle->item->result;
    uv_memset(out, 0, 8);

    const UVAsyncResumeValue resolved = uv_reactor_resolve_future(future);
    const uint8_t* future_payload = resolved.payload;

    // Async@Completed -> union success arm
    if (resolved.disc == UV_ASYNC_DISC_COMPLETED_LOCAL) {
        out[0] = 1;
        uv_memcpy(out + 4, future_payload, 4);
        return handle;
    }

    // Async@Failed -> union error arm
    if (resolved.disc == UV_ASYNC_DISC_FAILED_LOCAL) {
        out[0] = 0;
        out[4] = future_payload[0];
        return handle;
    }

    // An unresolved suspension cannot be observed as a successful tracked value.
    out[0] = 0;
    out[4] = 0;
    return handle;
}

static int64_t uv_read_int(const void* ptr, size_t size) {
    if (!ptr) {
        return 0;
    }
    switch (size) {
        case 1: {
            int8_t value = 0;
            uv_memcpy(&value, ptr, sizeof(value));
            return value;
        }
        case 2: {
            int16_t value = 0;
            uv_memcpy(&value, ptr, sizeof(value));
            return value;
        }
        case 4: {
            int32_t value = 0;
            uv_memcpy(&value, ptr, sizeof(value));
            return value;
        }
        case 8: {
            int64_t value = 0;
            uv_memcpy(&value, ptr, sizeof(value));
            return value;
        }
        default:
            return 0;
    }
}

static void uv_write_int(void* ptr, size_t size, int64_t value) {
    if (!ptr) {
        return;
    }
    switch (size) {
        case 1: {
            int8_t narrowed = (int8_t)value;
            uv_memcpy(ptr, &narrowed, sizeof(narrowed));
            break;
        }
        case 2: {
            int16_t narrowed = (int16_t)value;
            uv_memcpy(ptr, &narrowed, sizeof(narrowed));
            break;
        }
        case 4: {
            int32_t narrowed = (int32_t)value;
            uv_memcpy(ptr, &narrowed, sizeof(narrowed));
            break;
        }
        case 8: {
            int64_t narrowed = value;
            uv_memcpy(ptr, &narrowed, sizeof(narrowed));
            break;
        }
        default:
            break;
    }
}

static int64_t uv_int_max_for_size(size_t size) {
    switch (size) {
        case 1: return INT8_MAX;
        case 2: return INT16_MAX;
        case 4: return INT32_MAX;
        case 8: return INT64_MAX;
        default: return INT64_MAX;
    }
}

static int64_t uv_int_min_for_size(size_t size) {
    switch (size) {
        case 1: return INT8_MIN;
        case 2: return INT16_MIN;
        case 4: return INT32_MIN;
        case 8: return INT64_MIN;
        default: return INT64_MIN;
    }
}

static int uv_reduce_has_op(UVStringView op) {
    return op.data && op.len > 0;
}

static char uv_reduce_char(UVStringView op, size_t idx) {
    if (!op.data || idx >= op.len) {
        return 0;
    }
    return (char)op.data[idx];
}

static void uv_reduce_init(UVStringView op, void* accum, size_t size) {
    if (!uv_reduce_has_op(op) || !accum) {
        return;
    }
    const char c0 = uv_reduce_char(op, 0);
    const char c1 = uv_reduce_char(op, 1);
    if (c0 == '*') {
        uv_write_int(accum, size, 1);
        return;
    }
    if (c0 == 'a') {
        uv_write_int(accum, size, 1);
        return;
    }
    if (c0 == 'm' && c1 == 'i') {
        uv_write_int(accum, size, uv_int_max_for_size(size));
        return;
    }
    if (c0 == 'm' && c1 == 'a') {
        uv_write_int(accum, size, uv_int_min_for_size(size));
        return;
    }
    uv_write_int(accum, size, 0);
}

static void uv_reduce_apply(UVStringView op, void* accum, const void* value, size_t size) {
    if (!uv_reduce_has_op(op) || !accum || !value) {
        return;
    }
    int64_t a = uv_read_int(accum, size);
    int64_t b = uv_read_int(value, size);
    const char c0 = uv_reduce_char(op, 0);
    const char c1 = uv_reduce_char(op, 1);
    if (c0 == '+') {
        uv_write_int(accum, size, a + b);
        return;
    }
    if (c0 == '*') {
        uv_write_int(accum, size, a * b);
        return;
    }
    if (c0 == 'm' && c1 == 'i') {
        uv_write_int(accum, size, a < b ? a : b);
        return;
    }
    if (c0 == 'm' && c1 == 'a') {
        uv_write_int(accum, size, a > b ? a : b);
        return;
    }
    if (c0 == 'a') {
        uv_write_int(accum, size, (a != 0 && b != 0) ? 1 : 0);
        return;
    }
    if (c0 == 'o') {
        uv_write_int(accum, size, (a != 0 || b != 0) ? 1 : 0);
        return;
    }
    uv_write_int(accum, size, b);
}

typedef struct {
    uint64_t start;
    uint64_t end;
    size_t elem_size;
    size_t result_size;
    void (*body)(void* hosted_env, void* elem, void* captured, void* result, void* panic_out);
    void* hosted_env;
    void* captured_env;
    UVStringView reduce_op;
    void (*reduce_fn)(void* hosted_env, void* lhs, void* rhs, void* out, void* panic_out);
} DispatchChunkEnv;

typedef struct {
    ParallelContext* ctx;
    WorkItem item;
    UVGpuWorkItemState gpu_item;
    DispatchChunkEnv* dispatch;
    uint8_t* result;
    uint8_t index_storage[sizeof(UVU128)];
} UVGpuWorkThreadEnv;

static void uv_gpu_init_workgroups(UVGpuDispatchState* state) {
    if (!state || !state->workgroups) {
        return;
    }
    for (size_t i = 0; i < state->workgroup_count; ++i) {
        UVGpuWorkgroupState* group = &state->workgroups[i];
        uv_rt_mutex_init(&group->lock);
        uv_rt_condition_init(&group->barrier_cv);
        group->barrier_count = 0;
        group->barrier_generation = 0;
        group->launch_ready = 0;
        group->launch_aborted = 0;
        const uint64_t group_start =
            state->start + (uint64_t)i * state->workgroup_volume;
        uint64_t group_end = group_start + state->workgroup_volume;
        if (group_end > state->end) {
            group_end = state->end;
        }
        group->expected_count =
            group_end > group_start ? group_end - group_start : 0;
    }
}

static int uv_gpu_wait_for_launch(UVGpuWorkItemState* item) {
    UVGpuWorkgroupState* group;
    if (!item || !item->dispatch ||
        item->workgroup_index >= item->dispatch->workgroup_count) {
        return 0;
    }
    group = &item->dispatch->workgroups[item->workgroup_index];
    uv_rt_mutex_lock(&group->lock);
    while (!group->launch_ready) {
        uv_rt_condition_wait(&group->barrier_cv,
                             &group->lock,
                             UV_RT_WAIT_FOREVER);
    }
    const int can_run = !group->launch_aborted;
    uv_rt_mutex_unlock(&group->lock);
    return can_run;
}

static void uv_gpu_release_workgroup_launch(UVGpuDispatchState* state,
                                            size_t group_index,
                                            int aborted) {
    if (!state || group_index >= state->workgroup_count) {
        return;
    }
    UVGpuWorkgroupState* group = &state->workgroups[group_index];
    uv_rt_mutex_lock(&group->lock);
    group->launch_aborted = aborted ? 1 : 0;
    group->launch_ready = 1;
    uv_rt_condition_wake_all(&group->barrier_cv);
    uv_rt_mutex_unlock(&group->lock);
}

static void uv_gpu_barrier_wait(void) {
    UVThreadState* thread_state = uv_tls_state();
    UVGpuWorkItemState* item = thread_state->gpu_item;
    if (!item || !item->dispatch ||
        item->workgroup_index >= item->dispatch->workgroup_count) {
        uv_gpu_memory_fence();
        return;
    }

    UVGpuWorkgroupState* group =
        &item->dispatch->workgroups[item->workgroup_index];
    uv_rt_mutex_lock(&group->lock);
    const uint64_t generation = group->barrier_generation;
    item->status = GPU_WORK_AT_BARRIER;
    uv_gpu_trace_emit_with_transition(
        "def.20.GpuRuntimeState",
        "status_at_barrier",
        "Running->AtBarrier",
        item,
        group,
        generation,
        UV_GPU_TRACE_NO_GENERATION);
    uv_gpu_trace_emit_with_transition(
        "rule.20.EvalSigma-GpuBarrier",
        "status_at_barrier",
        "Running->AtBarrier",
        item,
        group,
        generation,
        UV_GPU_TRACE_NO_GENERATION);
    group->barrier_count += 1;
    uv_gpu_trace_emit(
        "requirement.20.GpuBarrierWait",
        "barrier_arrive",
        item,
        group,
        generation,
        UV_GPU_TRACE_NO_GENERATION);
    if (group->barrier_count >= group->expected_count) {
        group->barrier_count = 0;
        group->barrier_generation += 1;
        const uint64_t generation_after = group->barrier_generation;
        uv_gpu_memory_fence();
        uv_gpu_trace_emit(
            "def.20.GpuMemoryVisibility",
            "barrier_fence",
            item,
            group,
            generation,
            generation_after);
        uv_gpu_trace_emit(
            "rule.20.GpuBarrier-Sync",
            "barrier_generation_advance",
            item,
            group,
            generation,
            generation_after);
        uv_rt_condition_wake_all(&group->barrier_cv);
    } else {
        while (generation == group->barrier_generation) {
            uv_rt_condition_wait(&group->barrier_cv, &group->lock,
                                 UV_RT_WAIT_FOREVER);
        }
    }
    item->status = GPU_WORK_RUNNING;
    uv_gpu_trace_emit_with_transition(
        "def.20.GpuRuntimeState",
        "status_resume_running",
        "AtBarrier->Running",
        item,
        group,
        generation,
        group->barrier_generation);
    uv_gpu_trace_emit(
        "requirement.20.GpuBarrierWait",
        "barrier_release",
        item,
        group,
        generation,
        group->barrier_generation);
    uv_rt_mutex_unlock(&group->lock);
}

static void uv_gpu_work_item_body(void* hosted_env,
                                  void* env_ptr,
                                  void* result_ptr,
                                  void* panic_out) {
    (void)hosted_env;
    (void)result_ptr;
    UVGpuWorkThreadEnv* thread_env = (UVGpuWorkThreadEnv*)env_ptr;
    if (!thread_env || !thread_env->dispatch || !thread_env->dispatch->body) {
        return;
    }

    UVThreadState* thread_state = uv_tls_state();
    UVGpuWorkItemState* previous_gpu_item = thread_state->gpu_item;
    UVPanicRecord local_panic;
    local_panic.panic = 0;
    local_panic.code = 0;
    UVPanicRecord* panic_record =
        panic_out ? (UVPanicRecord*)panic_out : &local_panic;

    thread_state->gpu_item = &thread_env->gpu_item;
    if (!uv_gpu_wait_for_launch(&thread_env->gpu_item)) {
        thread_env->gpu_item.status = GPU_WORK_DONE;
        uv_gpu_trace_emit_with_transition(
            "def.20.GpuRuntimeState",
            "status_done",
            "Pending->Done",
            &thread_env->gpu_item,
            NULL,
            UV_GPU_TRACE_NO_GENERATION,
            UV_GPU_TRACE_NO_GENERATION);
        thread_state->gpu_item = previous_gpu_item;
        return;
    }
    thread_env->gpu_item.status = GPU_WORK_RUNNING;
    uv_gpu_trace_emit_with_transition(
        "def.20.GpuRuntimeState",
        "status_running",
        "Pending->Running",
        &thread_env->gpu_item,
        NULL,
        UV_GPU_TRACE_NO_GENERATION,
        UV_GPU_TRACE_NO_GENERATION);
    uv_gpu_trace_emit_with_transition(
        "rule.20.GpuExecute-Step",
        "status_running",
        "Pending->Running",
        &thread_env->gpu_item,
        NULL,
        UV_GPU_TRACE_NO_GENERATION,
        UV_GPU_TRACE_NO_GENERATION);
    thread_env->dispatch->body(thread_env->dispatch->hosted_env,
                               thread_env->index_storage,
                               thread_env->dispatch->captured_env,
                               thread_env->result,
                               panic_record);
    if (panic_record->panic) {
        uv_parallel_work_panic(uv_current_ctx(), panic_record->code);
    }
    thread_env->gpu_item.status = GPU_WORK_DONE;
    uv_gpu_trace_emit_with_transition(
        "def.20.GpuRuntimeState",
        "status_done",
        "Running->Done",
        &thread_env->gpu_item,
        NULL,
        UV_GPU_TRACE_NO_GENERATION,
        UV_GPU_TRACE_NO_GENERATION);
    thread_state->gpu_item = previous_gpu_item;
}

static uv_rt_u32_t uv_gpu_work_thread_proc(void* param) {
    UVGpuWorkThreadEnv* thread_env = (UVGpuWorkThreadEnv*)param;
    if (!thread_env) {
        return 0;
    }
    uv_run_item(thread_env->ctx, &thread_env->item);
    return 0;
}

static void uv_gpu_prepare_work_item(UVGpuWorkThreadEnv* thread_env,
                                     DispatchChunkEnv* dispatch,
                                     UVGpuDispatchState* gpu_state,
                                     ParallelContext* ctx,
                                     uint64_t index_value,
                                     uint64_t work_item_linear,
                                     size_t group_index,
                                     size_t result_size) {
    if (!thread_env || !dispatch || !gpu_state) {
        return;
    }
    uv_memset(thread_env, 0, sizeof(*thread_env));
    thread_env->ctx = ctx;
    thread_env->dispatch = dispatch;

    const size_t copy =
        dispatch->elem_size < sizeof(index_value) ? dispatch->elem_size
                                                  : sizeof(index_value);
    uv_memcpy(thread_env->index_storage, &index_value, copy);

    thread_env->gpu_item.dispatch = gpu_state;
    thread_env->gpu_item.linear_id = work_item_linear;
    thread_env->gpu_item.workgroup_index = group_index;
    thread_env->gpu_item.local_id =
        uv_gpu_local_id_from_linear(work_item_linear,
                                    gpu_state->workgroup_size);
    thread_env->gpu_item.workgroup_id =
        uv_gpu_workgroup_id_from_linear(work_item_linear,
                                        gpu_state->workgroup_volume);
    thread_env->gpu_item.global_id =
        uv_gpu_global_id(thread_env->gpu_item.local_id,
                         thread_env->gpu_item.workgroup_id,
                         gpu_state->workgroup_size);
    thread_env->gpu_item.status = GPU_WORK_PENDING;
    thread_env->gpu_item.private_mem = NULL;
    uv_gpu_trace_emit_memory_forms(&thread_env->gpu_item);
    uv_gpu_trace_emit(
        "def.20.GpuRuntimeState",
        "status_pending",
        &thread_env->gpu_item,
        NULL,
        UV_GPU_TRACE_NO_GENERATION,
        UV_GPU_TRACE_NO_GENERATION);
    uv_gpu_trace_emit(
        "def.20.GpuExecutionTopology",
        "work_item_topology_created",
        &thread_env->gpu_item,
        NULL,
        UV_GPU_TRACE_NO_GENERATION,
        UV_GPU_TRACE_NO_GENERATION);
    uv_gpu_trace_emit(
        "rule.20.EvalSigma-GPU-Dispatch",
        "gpu_dispatch_work_item_created",
        &thread_env->gpu_item,
        NULL,
        UV_GPU_TRACE_NO_GENERATION,
        UV_GPU_TRACE_NO_GENERATION);

    if (result_size > 0) {
        thread_env->result = (uint8_t*)uv_heap_alloc_raw(result_size);
        if (thread_env->result) {
            uv_memset(thread_env->result, 0, result_size);
        }
    }

    thread_env->item.state = WORK_PENDING;
    thread_env->item.task_id = uv_fresh_task_id();
    thread_env->item.completion_seq = 0;
    thread_env->item.owner_ctx = ctx;
    thread_env->item.captured_env = thread_env;
    thread_env->item.hosted_env = NULL;
    thread_env->item.body = uv_gpu_work_item_body;
    thread_env->item.result = thread_env->result;
    thread_env->item.result_size = result_size;
    thread_env->item.affinity_mask = ctx ? ctx->domain_affinity_mask : 0;
    thread_env->item.priority_hint = ctx ? ctx->domain_priority_hint : 1;
    thread_env->item.panic_code = 0;
    thread_env->item.next = NULL;
    thread_env->item.all_next = NULL;
    thread_env->item.done_event = NULL;
    thread_env->item.handle = NULL;
}

static void uv_gpu_reduce_result(DispatchChunkEnv* dispatch,
                                 ParallelContext* ctx,
                                 void* reduce_result,
                                 UVGpuWorkThreadEnv* thread_env,
                                 int* has_accum) {
    if (!dispatch || !thread_env || !thread_env->result ||
        dispatch->result_size == 0 || !reduce_result) {
        return;
    }

    if (dispatch->reduce_fn) {
        if (!*has_accum) {
            uv_memcpy(reduce_result, thread_env->result, dispatch->result_size);
            *has_accum = 1;
            return;
        }

        UVPanicRecord panic_record;
        panic_record.panic = 0;
        panic_record.code = 0;
        dispatch->reduce_fn(dispatch->hosted_env,
                            reduce_result,
                            thread_env->result,
                            reduce_result,
                            &panic_record);
        if (panic_record.panic) {
            uv_parallel_work_panic(ctx, panic_record.code);
        }
        return;
    }

    if (uv_reduce_has_op(dispatch->reduce_op)) {
        uv_reduce_apply(dispatch->reduce_op,
                        reduce_result,
                        thread_env->result,
                        dispatch->result_size);
    }
}

static void uv_gpu_dispatch_run(const UVRange* range,
                                size_t elem_size,
                                size_t result_size,
                                void (*body)(void* hosted_env,
                                             void* elem,
                                             void* captured,
                                             void* result,
                                             void* panic_out),
                                void* hosted_env,
                                void* captured_env,
                                UVStringView reduce_op,
                                void* reduce_result,
                                void (*reduce_fn)(void* hosted_env,
                                                  void* lhs,
                                                  void* rhs,
                                                  void* out,
                                                  void* panic_out),
                                UVUsize3 workgroup_size,
                                uint64_t start,
                                uint64_t end) {
    ParallelContext* ctx = uv_current_ctx();
    const uint64_t count = end - start;
    uint64_t volume = uv_gpu_workgroup_volume(workgroup_size);
    if (volume == 0) {
        workgroup_size = uv_gpu_triplet(64, 1, 1);
        volume = uv_gpu_workgroup_volume(workgroup_size);
    }
    const uint64_t groups = (count + volume - 1) / volume;

    UVGpuDispatchState gpu_state;
    gpu_state.workgroup_size = workgroup_size;
    gpu_state.num_workgroups = uv_gpu_triplet(groups, 1, 1);
    gpu_state.global_size =
        uv_gpu_triplet(workgroup_size.x * groups,
                       workgroup_size.y,
                       workgroup_size.z);
    gpu_state.workgroup_volume = volume;
    gpu_state.start = start;
    gpu_state.end = end;
    gpu_state.workgroup_count = (size_t)groups;
    uv_gpu_trace_emit_dispatch(
        "def.20.DispatchGpuTopologyComputation",
        "dispatch_topology_computed",
        &gpu_state,
        count,
        count);
    if (uv_gpu_topology_valid(&gpu_state)) {
        uv_gpu_trace_emit_dispatch(
            "def.20.GpuTopologyValidity",
            "dispatch_topology_valid",
            &gpu_state,
            count,
            count);
    }
    gpu_state.workgroups = (UVGpuWorkgroupState*)uv_heap_alloc_raw(
        sizeof(UVGpuWorkgroupState) * gpu_state.workgroup_count);
    if (!gpu_state.workgroups) {
        uv_parallel_context_panic(ctx, UV_PANIC_GPU_DISPATCH_LAUNCH_FAILED);
        return;
    }
    uv_memset(gpu_state.workgroups,
              0,
              sizeof(UVGpuWorkgroupState) * gpu_state.workgroup_count);
    uv_gpu_init_workgroups(&gpu_state);
    uv_gpu_trace_emit_dispatch(
        "rule.20.EvalSigma-GPU-Dispatch",
        "gpu_dispatch_created",
        &gpu_state,
        count,
        count);

    DispatchChunkEnv dispatch;
    dispatch.start = start;
    dispatch.end = end;
    dispatch.elem_size = elem_size;
    dispatch.result_size = result_size;
    dispatch.body = body;
    dispatch.hosted_env = hosted_env;
    dispatch.captured_env = captured_env;
    dispatch.reduce_op = reduce_op;
    dispatch.reduce_fn = reduce_fn;

    const int use_builtin = uv_reduce_has_op(reduce_op);
    const int has_reduce = reduce_result && result_size > 0;
    int has_accum = 0;
    if (use_builtin && has_reduce) {
        uv_reduce_init(reduce_op, reduce_result, result_size);
    }

    int dispatch_aborted = 0;
    for (size_t group = 0; group < gpu_state.workgroup_count; ++group) {
        const uint64_t group_start = start + (uint64_t)group * volume;
        uint64_t group_end = group_start + volume;
        if (group_end > end) {
            group_end = end;
        }
        const size_t active_count = (size_t)(group_end - group_start);
        if (active_count == 0) {
            continue;
        }

        UVGpuWorkThreadEnv* thread_envs =
            (UVGpuWorkThreadEnv*)uv_heap_alloc_raw(
                sizeof(UVGpuWorkThreadEnv) * active_count);
        uv_rt_handle_t* handles = (uv_rt_handle_t*)uv_heap_alloc_raw(
            sizeof(uv_rt_handle_t) * active_count);
        if (!thread_envs || !handles) {
            if (thread_envs) {
                uv_heap_free_raw(thread_envs);
            }
            if (handles) {
                uv_heap_free_raw(handles);
            }
            uv_parallel_context_panic(ctx, UV_PANIC_GPU_DISPATCH_LAUNCH_FAILED);
            dispatch_aborted = 1;
            break;
        }
        uv_memset(handles, 0, sizeof(uv_rt_handle_t) * active_count);
        int launch_failed = 0;

        for (size_t item_index = 0; item_index < active_count; ++item_index) {
            const uint64_t index_value = group_start + (uint64_t)item_index;
            const uint64_t work_item_linear = index_value - start;
            uv_gpu_prepare_work_item(&thread_envs[item_index],
                                     &dispatch,
                                     &gpu_state,
                                     ctx,
                                     index_value,
                                     work_item_linear,
                                     group,
                                     has_reduce ? result_size : 0);
            if (has_reduce && !thread_envs[item_index].result) {
                launch_failed = 1;
            }
        }

        if (!launch_failed) {
            for (size_t item_index = 0; item_index < active_count; ++item_index) {
                handles[item_index] = uv_rt_thread_spawn(
                    NULL,
                    0,
                    uv_gpu_work_thread_proc,
                    &thread_envs[item_index],
                    0,
                    NULL);
                if (!handles[item_index] ||
                    handles[item_index] == UV_RT_INVALID_HANDLE) {
                    launch_failed = 1;
                    handles[item_index] = NULL;
                }
            }
        }

        uv_gpu_release_workgroup_launch(&gpu_state, group, launch_failed);

        for (size_t item_index = 0; item_index < active_count; ++item_index) {
            if (handles[item_index] &&
                handles[item_index] != UV_RT_INVALID_HANDLE) {
                uv_rt_wait(handles[item_index], UV_RT_WAIT_FOREVER);
                uv_rt_handle_release(handles[item_index]);
            }
        }

        if (launch_failed) {
            uv_parallel_context_panic(ctx, UV_PANIC_GPU_DISPATCH_LAUNCH_FAILED);
            dispatch_aborted = 1;
        }

        if (!launch_failed && has_reduce) {
            for (size_t item_index = 0; item_index < active_count; ++item_index) {
                uv_gpu_reduce_result(&dispatch,
                                     ctx,
                                     reduce_result,
                                     &thread_envs[item_index],
                                     &has_accum);
            }
        }

        for (size_t item_index = 0; item_index < active_count; ++item_index) {
            if (thread_envs[item_index].result) {
                uv_heap_free_raw(thread_envs[item_index].result);
            }
        }
        uv_heap_free_raw(handles);
        uv_heap_free_raw(thread_envs);
        if (dispatch_aborted) {
            break;
        }
    }

    if (!dispatch_aborted && reduce_fn && has_reduce && !has_accum) {
        uv_memset(reduce_result, 0, result_size);
    }
    for (size_t group = 0; group < gpu_state.workgroup_count; ++group) {
        uv_rt_mutex_destroy(&gpu_state.workgroups[group].lock);
    }
    uv_heap_free_raw(gpu_state.workgroups);
    (void)range;
}

static void uv_dispatch_chunk(void* hosted_env, void* env_ptr, void* result_ptr, void* panic_out) {
    (void)hosted_env;
    DispatchChunkEnv* env = (DispatchChunkEnv*)env_ptr;
    if (!env || !env->body) {
        return;
    }
    UVPanicRecord local_panic;
    local_panic.panic = 0;
    local_panic.code = 0;
    UVPanicRecord* panic_record =
        panic_out ? (UVPanicRecord*)panic_out : &local_panic;
    union {
        UVU128 aligned;
        uint8_t bytes[sizeof(UVU128)];
    } index_storage;
    uint8_t* iter_result = NULL;
    if (env->result_size > 0) {
        iter_result = (uint8_t*)uv_heap_alloc_raw(env->result_size);
    }
    const int has_reduce = result_ptr && env->result_size > 0;
    const int use_custom = env->reduce_fn != NULL;
    const int use_builtin = uv_reduce_has_op(env->reduce_op);
    int has_accum = 0;
    if (use_builtin && has_reduce) {
        uv_reduce_init(env->reduce_op, result_ptr, env->result_size);
    }
    for (uint64_t i = env->start; i < env->end; ++i) {
        uv_memset(index_storage.bytes, 0, sizeof(index_storage.bytes));
        const size_t copy = env->elem_size < sizeof(i) ? env->elem_size : sizeof(i);
        uv_memcpy(index_storage.bytes, &i, copy);
        void* out_ptr = iter_result ? (void*)iter_result : result_ptr;
        env->body(env->hosted_env, index_storage.bytes, env->captured_env, out_ptr, panic_record);
        if (panic_record->panic) {
            uv_parallel_work_panic(uv_current_ctx(), panic_record->code);
            break;
        }
        if (has_reduce && iter_result) {
            if (use_custom) {
                if (!has_accum) {
                    uv_memcpy(result_ptr, iter_result, env->result_size);
                    has_accum = 1;
                } else {
                    env->reduce_fn(env->hosted_env, result_ptr, iter_result, result_ptr, panic_record);
                    if (panic_record->panic) {
                        uv_parallel_work_panic(uv_current_ctx(), panic_record->code);
                        break;
                    }
                }
            } else if (use_builtin) {
                uv_reduce_apply(env->reduce_op, result_ptr, iter_result, env->result_size);
            }
        }
    }
    if (use_custom && has_reduce && !has_accum) {
        uv_memset(result_ptr, 0, env->result_size);
    }
    if (uv_debug_flag_enabled("UV_DEBUG_DISPATCH_RESULT_RUNTIME")) {
        uv_debug_write_dispatch_chunk_value("local",
                                            env->start,
                                            env->end,
                                            result_ptr,
                                            env->result_size);
    }
    if (iter_result) {
        uv_heap_free_raw(iter_result);
    }
}

// §18.5.2 Dispatch iteration
// Executes body for each element in range with optional reduction
void uv_dispatch_run(const UVRange* range, size_t elem_size, size_t result_size,
                           void (*body)(void* hosted_env, void* elem, void* captured, void* result, void* panic_out),
                           void* hosted_env,
                           void* captured_env,
                           UVStringView reduce_op,
                           void* reduce_result,
                           void (*reduce_fn)(void* hosted_env, void* lhs, void* rhs, void* out, void* panic_out),
                           int ordered,
                           size_t chunk_size,
                           UVUsize3 workgroup_size) {
    if (!body) return;
    if (!range) return;

    uint64_t start = 0;
    uint64_t end = 0;
    const UVRange range_value = *range;
    switch (range_value.tag) {
        case 0:  // To
            start = 0;
            end = range_value.hi;
            break;
        case 1:  // ToInclusive
            start = 0;
            end = range_value.hi + 1;
            break;
        case 2:  // Full
            start = range_value.lo;
            end = range_value.hi;
            break;
        case 3:  // From
            start = range_value.lo;
            end = range_value.hi;
            break;
        case 4:  // Exclusive
            start = range_value.lo;
            end = range_value.hi;
            break;
        case 5:  // Inclusive
            start = range_value.lo;
            end = range_value.hi + 1;
            break;
        default:
            uv_debug_write_dispatch_range(range_value,
                                          0,
                                          0,
                                          elem_size,
                                          result_size,
                                          ordered,
                                          chunk_size);
            return;
    }
    uv_debug_write_dispatch_range(range_value,
                                  start,
                                  end,
                                  elem_size,
                                  result_size,
                                  ordered,
                                  chunk_size);
    if (end <= start) {
        if ((reduce_fn || uv_reduce_has_op(reduce_op)) && result_size > 0) {
            ParallelContext* ctx = uv_current_ctx();
            if (ctx) {
                if (uv_current_item()) {
                    uv_parallel_work_panic(ctx, UV_PANIC_REDUCED_EMPTY_DISPATCH);
                } else {
                    uv_parallel_context_panic(ctx, UV_PANIC_REDUCED_EMPTY_DISPATCH);
                }
            } else {
                uv_panic(UV_PANIC_REDUCED_EMPTY_DISPATCH);
            }
        }
        return;
    }

    ParallelContext* ctx = uv_current_ctx();
    if (ctx && ctx->domain_kind == (uint32_t)UV_DOMAIN_GPU) {
        (void)ordered;
        (void)chunk_size;
        uv_gpu_dispatch_run(range,
                            elem_size,
                            result_size,
                            body,
                            hosted_env,
                            captured_env,
                            reduce_op,
                            reduce_result,
                            reduce_fn,
                            workgroup_size,
                            start,
                            end);
        return;
    }

    // Spec permits concurrency but does not require it; execute dispatch
    // deterministically in-process to preserve result correctness.
    const int use_threaded_dispatch = 0;
    if (!use_threaded_dispatch || !uv_current_ctx() || !uv_current_ctx()->pool || ordered) {
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
    WorkItem* item = (WorkItem*)uv_heap_alloc_raw(sizeof(WorkItem));
        if (!item) {
            uv_dispatch_chunk(NULL, &env, reduce_result, NULL);
            return;
        }
        item->state = WORK_PENDING;
        item->task_id = uv_fresh_task_id();
        item->completion_seq = 0;
        item->owner_ctx = uv_current_ctx();
        item->captured_env = &env;
        item->hosted_env = NULL;
        item->body = uv_dispatch_chunk;
        item->result = reduce_result;
        item->result_size = result_size;
        item->affinity_mask = uv_current_ctx() ? uv_current_ctx()->domain_affinity_mask : 0;
        item->priority_hint = uv_current_ctx() ? uv_current_ctx()->domain_priority_hint : 1;
        item->panic_code = 0;
        item->next = NULL;
        item->all_next = NULL;
        item->handle = NULL;
        item->done_event = NULL;
        if (uv_current_ctx()) {
            item->all_next = uv_current_ctx()->all_items;
            uv_current_ctx()->all_items = item;
        }
        uv_run_item(item->owner_ctx, item);
        item->captured_env = NULL;
        item->result = NULL;
        item->result_size = 0;
        if (!uv_current_ctx()) {
        uv_heap_free_raw(item);
        }
        return;
    }

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
        (WorkItem**)uv_heap_alloc_raw(sizeof(WorkItem*) * num_chunks);
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
        uv_dispatch_chunk(NULL, &env, reduce_result, NULL);
        return;
    }

    for (size_t c = 0; c < num_chunks; ++c) {
        uint64_t chunk_start = start + (uint64_t)c * (uint64_t)chunk_size;
        uint64_t chunk_end = chunk_start + (uint64_t)chunk_size;
        if (chunk_end > end) {
            chunk_end = end;
        }
        DispatchChunkEnv* env =
            (DispatchChunkEnv*)uv_heap_alloc_raw(sizeof(DispatchChunkEnv));
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

        WorkItem* item = (WorkItem*)uv_heap_alloc_raw(sizeof(WorkItem));
        if (!item) {
            uv_heap_free_raw(env);
            items[c] = NULL;
            continue;
        }
        item->state = WORK_PENDING;
        item->task_id = uv_fresh_task_id();
        item->completion_seq = 0;
        item->owner_ctx = ctx;
        item->captured_env = env;
        item->hosted_env = NULL;
        item->body = uv_dispatch_chunk;
        item->result = (reduce_fn || uv_reduce_has_op(reduce_op)) && result_size > 0
                           ? uv_heap_alloc_raw(result_size)
                           : NULL;
        item->result_size = result_size;
        item->affinity_mask = ctx ? ctx->domain_affinity_mask : 0;
        item->priority_hint = ctx ? ctx->domain_priority_hint : 1;
        item->panic_code = 0;
        item->next = NULL;
        item->all_next = NULL;
        item->handle = NULL;
        item->done_event = uv_rt_event_open(
            NULL, UV_RT_TRUE, UV_RT_FALSE, NULL);

        items[c] = item;
        if (ctx) {
            item->all_next = ctx->all_items;
            ctx->all_items = item;
        }
        uv_enqueue_item(pool, item);
    }

    const int use_custom = reduce_fn != NULL;
    const int use_builtin = uv_reduce_has_op(reduce_op);
    int has_accum = 0;
    WorkItem* reduce_item = NULL;
    if (use_builtin && reduce_result && result_size > 0) {
        uv_reduce_init(reduce_op, reduce_result, result_size);
    }

    for (size_t c = 0; c < num_chunks; ++c) {
        WorkItem* item = items[c];
        if (!item) {
            continue;
        }
        if (item->done_event) {
            uv_rt_wait(item->done_event, UV_RT_WAIT_FOREVER);
        }
        if (reduce_result && item->result && result_size > 0) {
            if (uv_debug_flag_enabled("UV_DEBUG_DISPATCH_RESULT_RUNTIME")) {
                uint64_t chunk_start = start + (uint64_t)c * (uint64_t)chunk_size;
                uint64_t chunk_end = chunk_start + (uint64_t)chunk_size;
                if (chunk_end > end) {
                    chunk_end = end;
                }
                uv_debug_write_dispatch_chunk_value("merge-in",
                                                    chunk_start,
                                                    chunk_end,
                                                    item->result,
                                                    result_size);
            }
            if (use_custom) {
                if (!has_accum) {
                    uv_memcpy(reduce_result, item->result, result_size);
                    has_accum = 1;
                } else {
                    if (!reduce_item && ctx) {
                        reduce_item =
                            (WorkItem*)uv_heap_alloc_raw(sizeof(WorkItem));
                        if (reduce_item) {
                            uv_memset(reduce_item, 0, sizeof(WorkItem));
                            reduce_item->state = WORK_RUNNING;
                            reduce_item->all_next = ctx->all_items;
                            ctx->all_items = reduce_item;
                        }
                    }
                    if (!reduce_item) {
                        UVPanicRecord panic_record;
                        panic_record.panic = 0;
                        panic_record.code = 0;
                        reduce_fn(hosted_env, reduce_result, item->result, reduce_result, &panic_record);
                        if (panic_record.panic) {
                            uv_parallel_work_panic(ctx, panic_record.code);
                            break;
                        }
                    } else {
                        UVReduceBoundaryContext boundary;
                        uv_rt_u32_t panic_code = 0u;
                        UVPanicRecord panic_record;
                        panic_record.panic = 0;
                        panic_record.code = 0;
                        UVThreadState* state = uv_tls_state();
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
                        if (!uv_rt_panic_boundary_run(
                                uv_parallel_run_reduce_body,
                                &boundary,
                                &panic_code)) {
                            uv_parallel_work_panic(ctx, panic_code);
                            state->ctx = prev_ctx;
                            state->item = prev_item;
                            break;
                        }
                        if (panic_record.panic) {
                            uv_parallel_work_panic(ctx, panic_record.code);
                        }
                        state->ctx = prev_ctx;
                        state->item = prev_item;
                    }
                }
            } else if (use_builtin) {
                uv_reduce_apply(reduce_op, reduce_result, item->result, result_size);
            }
        }
    }

    if (uv_debug_flag_enabled("UV_DEBUG_DISPATCH_RESULT_RUNTIME")) {
        uv_debug_write_dispatch_chunk_value("final",
                                            start,
                                            end,
                                            reduce_result,
                                            result_size);
    }

    uv_heap_free_raw(items);
}

UVUsize3 ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3aglobal_x5fid(void) {
    UVGpuWorkItemState* item = uv_tls_state()->gpu_item;
    if (item) {
        uv_gpu_trace_emit(
            "def.20.GpuIntrinsicTable",
            "intrinsic_gpu_global_id",
            item,
            NULL,
            UV_GPU_TRACE_NO_GENERATION,
            UV_GPU_TRACE_NO_GENERATION);
    }
    return item ? item->global_id : uv_gpu_triplet(0, 0, 0);
}

UVUsize3 ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3alocal_x5fid(void) {
    UVGpuWorkItemState* item = uv_tls_state()->gpu_item;
    if (item) {
        uv_gpu_trace_emit(
            "def.20.GpuIntrinsicTable",
            "intrinsic_gpu_local_id",
            item,
            NULL,
            UV_GPU_TRACE_NO_GENERATION,
            UV_GPU_TRACE_NO_GENERATION);
    }
    return item ? item->local_id : uv_gpu_triplet(0, 0, 0);
}

UVUsize3 ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3aworkgroup_x5fid(void) {
    UVGpuWorkItemState* item = uv_tls_state()->gpu_item;
    if (item) {
        uv_gpu_trace_emit(
            "def.20.GpuIntrinsicTable",
            "intrinsic_gpu_workgroup_id",
            item,
            NULL,
            UV_GPU_TRACE_NO_GENERATION,
            UV_GPU_TRACE_NO_GENERATION);
    }
    return item ? item->workgroup_id : uv_gpu_triplet(0, 0, 0);
}

UVUsize3 ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3aworkgroup_x5fsize(void) {
    UVGpuWorkItemState* item = uv_tls_state()->gpu_item;
    if (item) {
        uv_gpu_trace_emit(
            "def.20.GpuIntrinsicTable",
            "intrinsic_gpu_workgroup_size",
            item,
            NULL,
            UV_GPU_TRACE_NO_GENERATION,
            UV_GPU_TRACE_NO_GENERATION);
    }
    return item && item->dispatch ? item->dispatch->workgroup_size
                                  : uv_gpu_triplet(1, 1, 1);
}

UVUsize3 ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3aglobal_x5fsize(void) {
    UVGpuWorkItemState* item = uv_tls_state()->gpu_item;
    if (item) {
        uv_gpu_trace_emit(
            "def.20.GpuIntrinsicTable",
            "intrinsic_gpu_global_size",
            item,
            NULL,
            UV_GPU_TRACE_NO_GENERATION,
            UV_GPU_TRACE_NO_GENERATION);
    }
    return item && item->dispatch ? item->dispatch->global_size
                                  : uv_gpu_triplet(1, 1, 1);
}

UVUsize3 ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3anum_x5fworkgroups(void) {
    UVGpuWorkItemState* item = uv_tls_state()->gpu_item;
    if (item) {
        uv_gpu_trace_emit(
            "def.20.GpuIntrinsicTable",
            "intrinsic_gpu_num_workgroups",
            item,
            NULL,
            UV_GPU_TRACE_NO_GENERATION,
            UV_GPU_TRACE_NO_GENERATION);
    }
    return item && item->dispatch ? item->dispatch->num_workgroups
                                  : uv_gpu_triplet(1, 1, 1);
}

uint64_t ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3alinear_x5fid(void) {
    UVGpuWorkItemState* item = uv_tls_state()->gpu_item;
    if (item) {
        uv_gpu_trace_emit(
            "def.20.GpuIntrinsicTable",
            "intrinsic_gpu_linear_id",
            item,
            NULL,
            UV_GPU_TRACE_NO_GENERATION,
            UV_GPU_TRACE_NO_GENERATION);
    }
    return item ? item->linear_id : 0;
}

void ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3abarrier(void) {
    UVGpuWorkItemState* item = uv_tls_state()->gpu_item;
    if (item) {
        uv_gpu_trace_emit(
            "def.20.GpuIntrinsicTable",
            "intrinsic_gpu_barrier",
            item,
            NULL,
            UV_GPU_TRACE_NO_GENERATION,
            UV_GPU_TRACE_NO_GENERATION);
    }
    uv_gpu_barrier_wait();
}

void ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3amemory_x5fbarrier(void) {
    UVGpuWorkItemState* item = uv_tls_state()->gpu_item;
    if (item) {
        uv_gpu_trace_emit(
            "def.20.GpuIntrinsicTable",
            "intrinsic_gpu_memory_barrier",
            item,
            NULL,
            UV_GPU_TRACE_NO_GENERATION,
            UV_GPU_TRACE_NO_GENERATION);
    }
    uv_gpu_memory_fence();
    if (item) {
        uv_gpu_trace_emit(
            "def.20.GpuMemoryVisibility",
            "memory_barrier",
            item,
            NULL,
            UV_GPU_TRACE_NO_GENERATION,
            UV_GPU_TRACE_NO_GENERATION);
    }
}

void ultraviolet_x3a_x3aruntime_x3a_x3agpu_x3a_x3aworkgroup_x5fbarrier(void) {
    UVGpuWorkItemState* item = uv_tls_state()->gpu_item;
    if (item) {
        uv_gpu_trace_emit(
            "def.20.GpuIntrinsicTable",
            "intrinsic_gpu_workgroup_barrier",
            item,
            NULL,
            UV_GPU_TRACE_NO_GENERATION,
            UV_GPU_TRACE_NO_GENERATION);
    }
    uv_gpu_barrier_wait();
}

// §18.6.1 Create cancellation token
UVCancelId uv_cancel_token_new(void) {
    UVCancelId token_id = UV_CANCEL_INVALID_ID;
    if (!uv_cancel_registry_ready()) {
        return UV_CANCEL_INVALID_ID;
    }

    uv_rt_mutex_lock(&uv_cancel_registry.lock);
    token_id = uv_cancel_registry_new_locked(UV_CANCEL_INVALID_ID);
    uv_rt_mutex_unlock(&uv_cancel_registry.lock);
    return token_id;
}

// §18.6.1 Request cancellation
void uv_cancel_token_cancel(UVCancelId token_id) {
    if (!uv_cancel_registry_ready()) {
        return;
    }

    uv_rt_mutex_lock(&uv_cancel_registry.lock);
    uv_cancel_registry_cancel_locked(token_id);
    uv_rt_mutex_unlock(&uv_cancel_registry.lock);
}

// §18.6.1 Check if cancelled
int uv_cancel_token_is_cancelled(UVCancelId token_id) {
    return uv_token_is_cancelled(token_id);
}

// §18.7 Record panic in work item
static void uv_parallel_context_panic(ParallelContext* ctx, uint32_t code) {
    if (!ctx) {
        uv_panic(code);
        return;
    }

    int request_cancel = 0;
    uv_lock_parallel_ctx(ctx);
    if (ctx->context_panic_code == 0 && !ctx->first_panic) {
        ctx->context_panic_code = code;
    }
    ctx->panic_count += 1;
    request_cancel =
        (ctx->cancel_token != UV_CANCEL_INVALID_ID && ctx->panic_count == 1);
    uv_unlock_parallel_ctx(ctx);

    if (request_cancel) {
        uv_cancel_token_cancel(ctx->cancel_token);
    }
}

void uv_parallel_work_panic(void* ctx_ptr, uint32_t code) {
    ParallelContext* ctx = (ParallelContext*)ctx_ptr;
    if (!ctx) {
        ctx = uv_current_ctx();
    }
    WorkItem* item = uv_current_item();
    if (!ctx) {
        uv_panic(code);
        return;
    }
    if (!item) {
        uv_parallel_context_panic(ctx, code);
        return;
    }

    int request_cancel = 0;
    uv_lock_parallel_ctx(ctx);
    item->state = WORK_PANICKED;
    item->panic_code = code;
    if (item->result && item->result_size > 0) {
        uv_memset(item->result, 0, item->result_size);
    }
    ctx->panic_count += 1;
    request_cancel =
        (ctx->cancel_token != UV_CANCEL_INVALID_ID && ctx->panic_count == 1);
    uv_unlock_parallel_ctx(ctx);

    uv_parallel_trace_emit(
        "requirement.20.ParallelWorkItemPanicSemantics",
        "work_item_panic_recorded",
        "uv_parallel_work_panic",
        ctx,
        item,
        code);
    uv_parallel_trace_emit(
        "rule.20.EvalSigma-Parallel-Spawn-Panic",
        "spawn_panic_recorded",
        "uv_parallel_work_panic",
        ctx,
        item,
        code);
    if (request_cancel) {
        uv_cancel_token_cancel(ctx->cancel_token);
        uv_parallel_trace_emit(
            "requirement.20.ParallelPanicCancellationRequest",
            "panic_requested_cancellation",
            "uv_parallel_work_panic",
            ctx,
            item,
            code);
    }
}

UVCancelId CancelToken_x3a_x3anew(void) {
    return uv_cancel_token_new();
}

static UVCancelId uv_cancel_token_from_self_ref(void* self_ref) {
    if (!self_ref) {
        return UV_CANCEL_INVALID_ID;
    }
    return *((const UVCancelId*)self_ref);
}

void CancelToken_x3a_x3aActive_x3a_x3acancel(void* self) {
    uv_cancel_token_cancel(uv_cancel_token_from_self_ref(self));
}

uint8_t CancelToken_x3a_x3aActive_x3a_x3ais_x5fcancelled(void* self) {
    return (uint8_t)uv_cancel_token_is_cancelled(
        uv_cancel_token_from_self_ref(self));
}

UVCancelId CancelToken_x3a_x3aActive_x3a_x3achild(void* self) {
    UVCancelId parent = uv_cancel_token_from_self_ref(self);
    UVCancelId child = UV_CANCEL_INVALID_ID;
    if (!uv_cancel_registry_ready()) {
        return UV_CANCEL_INVALID_ID;
    }

    uv_rt_mutex_lock(&uv_cancel_registry.lock);
    if (uv_cancel_registry_valid_id_locked(parent)) {
        child = uv_cancel_registry_new_locked(parent);
    }
    uv_rt_mutex_unlock(&uv_cancel_registry.lock);
    return child;
}

void CancelToken_x3a_x3aActive_x3a_x3await_x5fcancelled(void* out, void* self) {
    if (!out) {
        return;
    }
    UVAsyncResumeValue* async_out = (UVAsyncResumeValue*)out;
    UVCancelId token_id = uv_cancel_token_from_self_ref(self);
    if (token_id == UV_CANCEL_INVALID_ID || uv_token_is_cancelled(token_id)) {
        uv_cancel_wait_write_completed(async_out);
        return;
    }

    UVCancelWaitFrame* frame =
        (UVCancelWaitFrame*)uv_heap_alloc_raw(sizeof(UVCancelWaitFrame));
    if (!frame) {
        // Preserve progress on allocation failure by producing a completed
        // async value rather than an invalid suspended state.
        uv_cancel_wait_write_completed(async_out);
        return;
    }

    frame->resume_state = 0;
    frame->resume_fn = (void*)&uv_cancel_wait_resume;
    frame->hosted_env = NULL;
    frame->token_id = token_id;
    uv_cancel_wait_write_suspended(async_out, frame);
}
