#include "../internal/rt_internal.h"

enum {
  C0_ASYNC_DISC_SUSPENDED = 0,
  C0_ASYNC_DISC_COMPLETED = 1,
};

enum { C0_ASYNC_PAYLOAD_FRAME_PTR_OFFSET = 8 };

typedef struct C0AsyncFrameHeader {
  uint64_t resume_state;
  void* resume_fn;
  void* hosted_env;
  void* key_snapshot;
} C0AsyncFrameHeader;

typedef void (*C0AsyncResumeFn)(void* hosted_env,
                                C0AsyncResumeValue* out,
                                void* frame,
                                void* input,
                                void* panic_out);

typedef struct C0AsyncTakeFrame {
  C0AsyncFrameHeader header;
  C0AsyncResumeValue source;
  uint64_t remaining;
} C0AsyncTakeFrame;

static void c0_async_store_frame(C0AsyncResumeValue* value, void* frame) {
  if (!value) {
    return;
  }
  c0_memcpy(value->payload + C0_ASYNC_PAYLOAD_FRAME_PTR_OFFSET,
            &frame,
            sizeof(frame));
}

static C0AsyncResumeValue c0_async_completed_unit(void) {
  C0AsyncResumeValue out;
  c0_memset(&out, 0, sizeof(out));
  out.disc = C0_ASYNC_DISC_COMPLETED;
  return out;
}

static void c0_async_take_resume(void* hosted_env,
                                 C0AsyncResumeValue* out,
                                 void* frame,
                                 void* input,
                                 void* panic_out) {
  (void)hosted_env;
  if (!out) {
    return;
  }
  c0_memset(out, 0, sizeof(*out));
  C0AsyncTakeFrame* take_frame = (C0AsyncTakeFrame*)frame;
  if (!take_frame) {
    return;
  }

  if (take_frame->remaining == 0) {
    *out = c0_async_completed_unit();
    cursive_x3a_x3aruntime_x3a_x3aasync_x3a_x3afree_x5fframe(frame);
    return;
  }

  C0AsyncResumeValue resumed =
      cursive_x3a_x3aruntime_x3a_x3aasync_x3a_x3aresume(
          &take_frame->source,
          input,
          panic_out);
  take_frame->source = resumed;
  if (resumed.disc == C0_ASYNC_DISC_SUSPENDED) {
    take_frame->remaining = take_frame->remaining - 1;
    *out = resumed;
    c0_async_store_frame(out, frame);
    return;
  }

  *out = resumed;
  cursive_x3a_x3aruntime_x3a_x3aasync_x3a_x3afree_x5fframe(frame);
}

void* cursive_x3a_x3aruntime_x3a_x3aasync_x3a_x3aalloc_x5fframe(uint64_t size,
                                                               uint64_t align) {
  c0_trace_emit_rule("BuiltinSym-Async-AllocFrame");
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

  void* base = c0_heap_alloc_raw((size_t)total);
  if (!base) {
    return NULL;
  }

  uintptr_t raw = (uintptr_t)base + (uintptr_t)sizeof(void*);
  uintptr_t aligned = (raw + (uintptr_t)(align - 1)) & ~(uintptr_t)(align - 1);
  ((void**)aligned)[-1] = base;
  return (void*)aligned;
}

void cursive_x3a_x3aruntime_x3a_x3aasync_x3a_x3afree_x5fframe(void* frame) {
  c0_trace_emit_rule("BuiltinSym-Async-FreeFrame");
  if (!frame) {
    return;
  }
  {
    C0AsyncFrameHeader header;
    c0_memcpy(&header, frame, sizeof(header));
    if (header.key_snapshot) {
      cursive_key_release_snapshot_discard(header.key_snapshot);
      header.key_snapshot = NULL;
      c0_memcpy(frame, &header, sizeof(header));
    }
  }
  void* base = ((void**)frame)[-1];
  c0_heap_free_raw(base);
}

C0AsyncResumeValue cursive_x3a_x3aruntime_x3a_x3aasync_x3a_x3aresume(
    const C0AsyncResumeValue* suspended,
    const void* input,
    void* panic_out) {
  c0_trace_emit_rule("BuiltinSym-Async-Resume");

  C0AsyncResumeValue out;
  c0_memset(&out, 0, sizeof(out));
  if (!suspended) {
    return out;
  }
  out = *suspended;

  if (suspended->disc != C0_ASYNC_DISC_SUSPENDED) {
    return out;
  }

  void* frame = NULL;
  c0_memcpy(&frame,
            suspended->payload + C0_ASYNC_PAYLOAD_FRAME_PTR_OFFSET,
            sizeof(frame));
  if (!frame) {
    return out;
  }

  C0AsyncFrameHeader header;
  c0_memcpy(&header, frame, sizeof(header));
  if (!header.resume_fn) {
    return out;
  }

  C0AsyncResumeFn resume_fn = (C0AsyncResumeFn)header.resume_fn;
  resume_fn(header.hosted_env, &out, frame, (void*)input, panic_out);
  return out;
}

C0AsyncResumeValue cursive_x3a_x3aruntime_x3a_x3aasync_x3a_x3atake(
    const C0AsyncResumeValue* source,
    uint64_t count,
    void* panic_out) {
  (void)panic_out;
  C0AsyncResumeValue out;
  c0_memset(&out, 0, sizeof(out));
  if (!source) {
    return out;
  }
  if (count == 0) {
    return c0_async_completed_unit();
  }
  if (source->disc != C0_ASYNC_DISC_SUSPENDED) {
    return *source;
  }

  C0AsyncTakeFrame* frame =
      (C0AsyncTakeFrame*)cursive_x3a_x3aruntime_x3a_x3aasync_x3a_x3aalloc_x5fframe(
          (uint64_t)sizeof(C0AsyncTakeFrame),
          (uint64_t)sizeof(void*));
  if (!frame) {
    return *source;
  }

  c0_memset(frame, 0, sizeof(*frame));
  frame->header.resume_fn = (void*)&c0_async_take_resume;
  frame->source = *source;
  frame->remaining = count - 1;

  out = *source;
  c0_async_store_frame(&out, frame);
  return out;
}
