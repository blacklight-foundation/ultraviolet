#include "../internal/rt_internal.h"

enum {
  UV_ASYNC_DISC_SUSPENDED = 0,
  UV_ASYNC_DISC_COMPLETED = 1,
};

enum { UV_ASYNC_PAYLOAD_FRAME_PTR_OFFSET = 8 };

typedef struct UVAsyncFrameHeader {
  uint64_t resume_state;
  void* resume_fn;
  void* hosted_env;
  void* key_snapshot;
} UVAsyncFrameHeader;

typedef void (*UVAsyncResumeFn)(void* hosted_env,
                                UVAsyncResumeValue* out,
                                void* frame,
                                void* input,
                                void* panic_out);

typedef struct UVAsyncTakeFrame {
  UVAsyncFrameHeader header;
  UVAsyncResumeValue source;
  uint64_t remaining;
} UVAsyncTakeFrame;

static void uv_async_store_frame(UVAsyncResumeValue* value, void* frame) {
  if (!value) {
    return;
  }
  uv_memcpy(value->payload + UV_ASYNC_PAYLOAD_FRAME_PTR_OFFSET,
            &frame,
            sizeof(frame));
}

static UVAsyncResumeValue uv_async_completed_unit(void) {
  UVAsyncResumeValue out;
  uv_memset(&out, 0, sizeof(out));
  out.disc = UV_ASYNC_DISC_COMPLETED;
  return out;
}

static void uv_async_take_resume(void* hosted_env,
                                 UVAsyncResumeValue* out,
                                 void* frame,
                                 void* input,
                                 void* panic_out) {
  (void)hosted_env;
  if (!out) {
    return;
  }
  uv_memset(out, 0, sizeof(*out));
  UVAsyncTakeFrame* take_frame = (UVAsyncTakeFrame*)frame;
  if (!take_frame) {
    return;
  }

  if (take_frame->remaining == 0) {
    *out = uv_async_completed_unit();
    ultraviolet_x3a_x3aruntime_x3a_x3aasync_x3a_x3afree_x5fframe(frame);
    return;
  }

  UVAsyncResumeValue resumed =
      ultraviolet_x3a_x3aruntime_x3a_x3aasync_x3a_x3aresume(
          &take_frame->source,
          input,
          panic_out);
  take_frame->source = resumed;
  if (resumed.disc == UV_ASYNC_DISC_SUSPENDED) {
    take_frame->remaining = take_frame->remaining - 1;
    *out = resumed;
    uv_async_store_frame(out, frame);
    return;
  }

  *out = resumed;
  ultraviolet_x3a_x3aruntime_x3a_x3aasync_x3a_x3afree_x5fframe(frame);
}

void* ultraviolet_x3a_x3aruntime_x3a_x3aasync_x3a_x3aalloc_x5fframe(uint64_t size,
                                                               uint64_t align) {
  uv_trace_emit_rule("BuiltinSym-Async-AllocFrame");
  if (size == 0) {
    return NULL;
  }
  if (align == 0) {
    align = 1;
  }
  if (align < (uint64_t)sizeof(void*)) {
    align = (uint64_t)sizeof(void*);
  }
  if ((align & (align - 1)) != 0) {
    uint64_t pow2 = 1;
    while (pow2 < align) {
      pow2 <<= 1;
    }
    align = pow2;
  }

  uint64_t total = size + align - 1 + (uint64_t)sizeof(void*);
  if (total < size) {
    return NULL;
  }

  void* base = uv_heap_alloc_raw((size_t)total);
  if (!base) {
    return NULL;
  }

  uintptr_t raw = (uintptr_t)base + (uintptr_t)sizeof(void*);
  uintptr_t aligned = (raw + (uintptr_t)(align - 1)) & ~(uintptr_t)(align - 1);
  ((void**)aligned)[-1] = base;
  return (void*)aligned;
}

void ultraviolet_x3a_x3aruntime_x3a_x3aasync_x3a_x3afree_x5fframe(void* frame) {
  uv_trace_emit_rule("BuiltinSym-Async-FreeFrame");
  if (!frame) {
    return;
  }
  {
    UVAsyncFrameHeader header;
    uv_memcpy(&header, frame, sizeof(header));
    if (header.key_snapshot) {
      uv_key_release_snapshot_discard(header.key_snapshot);
      header.key_snapshot = NULL;
      uv_memcpy(frame, &header, sizeof(header));
    }
  }
  void* base = ((void**)frame)[-1];
  uv_heap_free_raw(base);
}

UVAsyncResumeValue ultraviolet_x3a_x3aruntime_x3a_x3aasync_x3a_x3aresume(
    const UVAsyncResumeValue* suspended,
    const void* input,
    void* panic_out) {
  uv_trace_emit_rule("BuiltinSym-Async-Resume");

  UVAsyncResumeValue out;
  uv_memset(&out, 0, sizeof(out));
  if (!suspended) {
    return out;
  }
  out = *suspended;

  if (suspended->disc != UV_ASYNC_DISC_SUSPENDED) {
    return out;
  }

  void* frame = NULL;
  uv_memcpy(&frame,
            suspended->payload + UV_ASYNC_PAYLOAD_FRAME_PTR_OFFSET,
            sizeof(frame));
  if (!frame) {
    return out;
  }

  UVAsyncFrameHeader header;
  uv_memcpy(&header, frame, sizeof(header));
  if (!header.resume_fn) {
    return out;
  }

  UVAsyncResumeFn resume_fn = (UVAsyncResumeFn)header.resume_fn;
  resume_fn(header.hosted_env, &out, frame, (void*)input, panic_out);
  return out;
}

UVAsyncResumeValue ultraviolet_x3a_x3aruntime_x3a_x3aasync_x3a_x3atake(
    const UVAsyncResumeValue* source,
    uint64_t count,
    void* panic_out) {
  (void)panic_out;
  UVAsyncResumeValue out;
  uv_memset(&out, 0, sizeof(out));
  if (!source) {
    return out;
  }
  if (count == 0) {
    return uv_async_completed_unit();
  }
  if (source->disc != UV_ASYNC_DISC_SUSPENDED) {
    return *source;
  }

  UVAsyncTakeFrame* frame =
      (UVAsyncTakeFrame*)ultraviolet_x3a_x3aruntime_x3a_x3aasync_x3a_x3aalloc_x5fframe(
          (uint64_t)sizeof(UVAsyncTakeFrame),
          (uint64_t)sizeof(void*));
  if (!frame) {
    return *source;
  }

  uv_memset(frame, 0, sizeof(*frame));
  frame->header.resume_fn = (void*)&uv_async_take_resume;
  frame->source = *source;
  frame->remaining = count - 1;

  out = *source;
  uv_async_store_frame(&out, frame);
  return out;
}
