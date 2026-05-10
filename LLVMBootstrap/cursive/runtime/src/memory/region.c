#include "../internal/rt_internal.h"

enum {
  C0_REGION_ACTIVE = 0,
  C0_REGION_FROZEN = 1,
  C0_REGION_FREED = 2,
};

typedef struct C0RegionBlock {
  struct C0RegionBlock* prev;
  void* base;
  uint8_t* data;
  size_t size;
  size_t used;
  size_t align;
} C0RegionBlock;

typedef struct C0RegionAlloc {
  void* addr;
} C0RegionAlloc;

typedef struct C0RegionArena {
  uint64_t handle;
  size_t prealloc;
  C0RegionBlock* head;
  C0RegionAlloc* allocs;
  size_t alloc_count;
  size_t alloc_cap;
} C0RegionArena;

typedef struct C0RegionSlot {
  uint64_t handle;
  C0RegionArena* arena;
} C0RegionSlot;

typedef struct C0RegionEntry {
  uint64_t tag;
  uint64_t target;
  uint64_t scope;
  uint64_t mark;
  uint8_t has_mark;
  uint8_t _pad[7];
} C0RegionEntry;

typedef struct C0AddrTag {
  void* addr;
  uint64_t tag;
} C0AddrTag;

typedef struct C0RegionState {
  C0RegionSlot* slots;
  size_t count;
  size_t cap;
  C0RegionEntry* region_stack;
  size_t region_count;
  size_t region_cap;
  uint64_t* scope_stack;
  size_t scope_count;
  size_t scope_cap;
  C0AddrTag* addr_tags;
  size_t addr_count;
  size_t addr_cap;
  uint64_t next_region_token;
  cursive_platform_rwlock_t lock;
} C0RegionState;

static C0RegionState g_region_state;
static cursive_platform_once_t g_region_once = CURSIVE_PLATFORM_ONCE_INIT;

static const uint64_t C0_SCOPE_TAG_MASK = 0x8000000000000000ull;

static bool c0_tag_is_scope(uint64_t tag) {
  return (tag & C0_SCOPE_TAG_MASK) != 0;
}

static uint64_t c0_scope_tag(uint64_t scope_id) {
  if (scope_id == 0) {
    return 0;
  }
  return C0_SCOPE_TAG_MASK | (scope_id & ~C0_SCOPE_TAG_MASK);
}

static uint64_t c0_scope_id_from_tag(uint64_t tag) {
  return tag & ~C0_SCOPE_TAG_MASK;
}

static cursive_platform_bool_t c0_region_init_once(
    cursive_platform_once_t* init_once,
    void* param,
    void** context) {
  (void)init_once;
  (void)param;
  (void)context;
  c0_memset(&g_region_state, 0, sizeof(g_region_state));
  g_region_state.next_region_token = 1;
  cursive_platform_rwlock_init(&g_region_state.lock);
  return CURSIVE_PLATFORM_TRUE;
}

static C0RegionState* c0_region_state(void) {
  cursive_platform_once_execute(&g_region_once, c0_region_init_once, NULL, NULL);
  return &g_region_state;
}

static C0Region c0_region_make(uint8_t disc, uint64_t handle) {
  C0Region region;
  c0_memset(&region, 0, sizeof(C0Region));
  region.disc = disc;
  region.handle = handle;
  return region;
}

static bool c0_region_reserve_slots(C0RegionState* state, size_t needed) {
  if (needed <= state->cap) {
    return true;
  }
  size_t new_cap = state->cap ? state->cap : 8;
  while (new_cap < needed) {
    new_cap *= 2;
  }
  C0RegionSlot* next =
      (C0RegionSlot*)c0_heap_alloc_raw(new_cap * sizeof(C0RegionSlot));
  if (!next) {
    return false;
  }
  if (state->slots && state->count > 0) {
    c0_memcpy(next, state->slots, state->count * sizeof(C0RegionSlot));
  }
  c0_heap_free_raw(state->slots);
  state->slots = next;
  state->cap = new_cap;
  return true;
}

static bool c0_region_reserve_entries(C0RegionState* state, size_t needed) {
  if (needed <= state->region_cap) {
    return true;
  }
  size_t new_cap = state->region_cap ? state->region_cap : 16;
  while (new_cap < needed) {
    new_cap *= 2;
  }
  C0RegionEntry* next =
      (C0RegionEntry*)c0_heap_alloc_raw(new_cap * sizeof(C0RegionEntry));
  if (!next) {
    return false;
  }
  if (state->region_stack && state->region_count > 0) {
    c0_memcpy(next, state->region_stack, state->region_count * sizeof(C0RegionEntry));
  }
  c0_heap_free_raw(state->region_stack);
  state->region_stack = next;
  state->region_cap = new_cap;
  return true;
}

static bool c0_region_reserve_scope_stack(C0RegionState* state, size_t needed) {
  if (needed <= state->scope_cap) {
    return true;
  }
  size_t new_cap = state->scope_cap ? state->scope_cap : 16;
  while (new_cap < needed) {
    new_cap *= 2;
  }
  uint64_t* next = (uint64_t*)c0_heap_alloc_raw(new_cap * sizeof(uint64_t));
  if (!next) {
    return false;
  }
  if (state->scope_stack && state->scope_count > 0) {
    c0_memcpy(next, state->scope_stack, state->scope_count * sizeof(uint64_t));
  }
  c0_heap_free_raw(state->scope_stack);
  state->scope_stack = next;
  state->scope_cap = new_cap;
  return true;
}

static bool c0_region_reserve_addr_tags(C0RegionState* state, size_t needed) {
  if (needed <= state->addr_cap) {
    return true;
  }
  size_t new_cap = state->addr_cap ? state->addr_cap : 16;
  while (new_cap < needed) {
    new_cap *= 2;
  }
  C0AddrTag* next = (C0AddrTag*)c0_heap_alloc_raw(new_cap * sizeof(C0AddrTag));
  if (!next) {
    return false;
  }
  if (state->addr_tags && state->addr_count > 0) {
    c0_memcpy(next, state->addr_tags, state->addr_count * sizeof(C0AddrTag));
  }
  c0_heap_free_raw(state->addr_tags);
  state->addr_tags = next;
  state->addr_cap = new_cap;
  return true;
}

static bool c0_region_arena_reserve_allocs(C0RegionArena* arena, size_t needed) {
  if (needed <= arena->alloc_cap) {
    return true;
  }
  size_t new_cap = arena->alloc_cap ? arena->alloc_cap : 16;
  while (new_cap < needed) {
    new_cap *= 2;
  }
  C0RegionAlloc* next =
      (C0RegionAlloc*)c0_heap_alloc_raw(new_cap * sizeof(C0RegionAlloc));
  if (!next) {
    return false;
  }
  if (arena->allocs && arena->alloc_count > 0) {
    c0_memcpy(next, arena->allocs, arena->alloc_count * sizeof(C0RegionAlloc));
  }
  c0_heap_free_raw(arena->allocs);
  arena->allocs = next;
  arena->alloc_cap = new_cap;
  return true;
}

static C0RegionArena* c0_region_find(C0RegionState* state, uint64_t handle) {
  for (size_t i = 0; i < state->count; ++i) {
    if (state->slots[i].handle == handle) {
      return state->slots[i].arena;
    }
  }
  return NULL;
}

static bool c0_region_insert(C0RegionState* state, C0RegionArena* arena) {
  if (!arena) {
    return false;
  }
  if (!c0_region_reserve_slots(state, state->count + 1)) {
    return false;
  }
  state->slots[state->count].handle = arena->handle;
  state->slots[state->count].arena = arena;
  state->count += 1;
  return true;
}

static const C0RegionEntry* c0_region_active_entry(const C0RegionState* state) {
  if (!state || state->region_count == 0) {
    return NULL;
  }
  return &state->region_stack[state->region_count - 1];
}

static uint64_t c0_region_active_target(const C0RegionState* state) {
  const C0RegionEntry* entry = c0_region_active_entry(state);
  if (!entry) {
    return 0;
  }
  return entry->target;
}

static const C0RegionEntry* c0_region_resolve_entry(const C0RegionState* state,
                                                    uint64_t target) {
  const C0RegionEntry* active = c0_region_active_entry(state);
  if (active && c0_region_active_target(state) == target) {
    return active;
  }
  if (!state) {
    return NULL;
  }
  for (size_t i = state->region_count; i > 0; --i) {
    if (state->region_stack[i - 1].target == target) {
      return &state->region_stack[i - 1];
    }
  }
  return NULL;
}

static void c0_region_remove(C0RegionState* state, uint64_t handle) {
  size_t out = 0;
  for (size_t i = 0; i < state->count; ++i) {
    if (state->slots[i].handle == handle) {
      continue;
    }
    if (out != i) {
      state->slots[out] = state->slots[i];
    }
    ++out;
  }
  state->count = out;
}

static uint64_t c0_scope_current(const C0RegionState* state) {
  if (!state || state->scope_count == 0) {
    return 0;
  }
  return state->scope_stack[state->scope_count - 1];
}

static bool c0_scope_active(const C0RegionState* state, uint64_t scope_id) {
  if (!state || scope_id == 0) {
    return false;
  }
  for (size_t i = 0; i < state->scope_count; ++i) {
    if (state->scope_stack[i] == scope_id) {
      return true;
    }
  }
  return false;
}

static void c0_scope_enter(C0RegionState* state, uint64_t scope_id) {
  if (!state || scope_id == 0) {
    return;
  }
  if (!c0_region_reserve_scope_stack(state, state->scope_count + 1)) {
    return;
  }
  state->scope_stack[state->scope_count] = scope_id;
  state->scope_count += 1;
}

static void c0_scope_exit(C0RegionState* state, uint64_t scope_id) {
  if (!state || scope_id == 0 || state->scope_count == 0) {
    return;
  }
  for (size_t i = state->scope_count; i > 0; --i) {
    if (state->scope_stack[i - 1] != scope_id) {
      continue;
    }
    if (i < state->scope_count) {
      c0_memmove(&state->scope_stack[i - 1],
                 &state->scope_stack[i],
                 (state->scope_count - i) * sizeof(uint64_t));
    }
    state->scope_count -= 1;
    return;
  }
}

static void c0_region_addr_tag_set(C0RegionState* state, void* addr, uint64_t tag) {
  if (!state || !addr) {
    return;
  }
  for (size_t i = 0; i < state->addr_count; ++i) {
    if (state->addr_tags[i].addr == addr) {
      state->addr_tags[i].tag = tag;
      return;
    }
  }
  if (!c0_region_reserve_addr_tags(state, state->addr_count + 1)) {
    return;
  }
  state->addr_tags[state->addr_count].addr = addr;
  state->addr_tags[state->addr_count].tag = tag;
  state->addr_count += 1;
}

static uint64_t c0_region_addr_tag_get(const C0RegionState* state, const void* addr) {
  if (!state || !addr) {
    return 0;
  }
  for (size_t i = 0; i < state->addr_count; ++i) {
    if (state->addr_tags[i].addr == addr) {
      return state->addr_tags[i].tag;
    }
  }
  return 0;
}

static uint64_t c0_region_fresh_token(C0RegionState* state) {
  uint64_t token = 0;
  if (!state) {
    return 0;
  }
  do {
    token = state->next_region_token++;
  } while (token == 0 || c0_tag_is_scope(token));
  return token;
}

static bool c0_region_stack_push(C0RegionState* state,
                                 uint64_t tag,
                                 uint64_t target,
                                 uint64_t scope,
                                 uint64_t mark,
                                 int has_mark) {
  if (!state) {
    return false;
  }
  if (!c0_region_reserve_entries(state, state->region_count + 1)) {
    return false;
  }
  state->region_stack[state->region_count].tag = tag;
  state->region_stack[state->region_count].target = target;
  state->region_stack[state->region_count].scope = scope;
  state->region_stack[state->region_count].mark = mark;
  state->region_stack[state->region_count].has_mark = has_mark ? 1 : 0;
  c0_memset(state->region_stack[state->region_count]._pad, 0,
            sizeof(state->region_stack[state->region_count]._pad));
  state->region_count += 1;
  return true;
}

static size_t c0_region_find_mark_entry_index(const C0RegionState* state,
                                              uint64_t target,
                                              uint64_t mark) {
  if (!state) {
    return SIZE_MAX;
  }
  for (size_t i = state->region_count; i > 0; --i) {
    const C0RegionEntry* entry = &state->region_stack[i - 1];
    if (entry->target == target && entry->has_mark != 0 && entry->mark == mark) {
      return i - 1;
    }
  }
  return SIZE_MAX;
}

static bool c0_region_tag_active(const C0RegionState* state, uint64_t tag) {
  if (tag == 0) {
    return true;
  }
  if (c0_tag_is_scope(tag)) {
    return c0_scope_active(state, c0_scope_id_from_tag(tag));
  }
  for (size_t i = 0; i < state->region_count; ++i) {
    if (state->region_stack[i].tag == tag) {
      return true;
    }
  }
  return false;
}

static void c0_region_stack_remove_index(C0RegionState* state, size_t index) {
  if (!state || index >= state->region_count) {
    return;
  }
  if (index + 1 < state->region_count) {
    c0_memmove(&state->region_stack[index],
               &state->region_stack[index + 1],
               (state->region_count - index - 1) * sizeof(C0RegionEntry));
  }
  state->region_count -= 1;
}

static void c0_region_stack_pop_scope(C0RegionState* state, uint64_t scope) {
  if (!state) {
    return;
  }
  for (size_t i = state->region_count; i > 0; --i) {
    if (state->region_stack[i - 1].scope != scope) {
      continue;
    }
    c0_region_stack_remove_index(state, i - 1);
    return;
  }
}

static void c0_region_stack_pop_target(C0RegionState* state, uint64_t target) {
  if (!state) {
    return;
  }
  size_t out = 0;
  for (size_t i = 0; i < state->region_count; ++i) {
    if (state->region_stack[i].target == target) {
      continue;
    }
    if (out != i) {
      state->region_stack[out] = state->region_stack[i];
    }
    ++out;
  }
  state->region_count = out;
}

static size_t c0_region_count_target_entries(const C0RegionState* state, uint64_t target) {
  size_t count = 0;
  if (!state) {
    return 0;
  }
  for (size_t i = 0; i < state->region_count; ++i) {
    if (state->region_stack[i].target == target) {
      count += 1;
    }
  }
  return count;
}

static C0RegionBlock* c0_region_block_new(size_t size, size_t align) {
  if (align == 0) {
    align = 1;
  }
  const size_t total = sizeof(C0RegionBlock) + size + align;
  uint8_t* raw = (uint8_t*)c0_heap_alloc_raw(total);
  if (!raw) {
    return NULL;
  }
  C0RegionBlock* block = (C0RegionBlock*)raw;
  uint8_t* data_start = raw + sizeof(C0RegionBlock);
  uintptr_t aligned = (uintptr_t)c0_align_up((uint64_t)(uintptr_t)data_start,
                                             (uint64_t)align);
  block->prev = NULL;
  block->base = raw;
  block->data = (uint8_t*)aligned;
  block->size = size;
  block->used = 0;
  block->align = align;
  return block;
}

static void c0_region_block_free(C0RegionBlock* block) {
  if (!block) {
    return;
  }
  c0_heap_free_raw(block->base);
}

static void c0_region_free_blocks(C0RegionArena* arena) {
  if (!arena) {
    return;
  }
  C0RegionBlock* block = arena->head;
  while (block) {
    C0RegionBlock* prev = block->prev;
    c0_region_block_free(block);
    block = prev;
  }
  arena->head = NULL;
}

static void c0_region_discard_arena(C0RegionArena* arena, int free_blocks) {
  if (!arena) {
    return;
  }
  if (free_blocks) {
    c0_region_free_blocks(arena);
  }
  c0_heap_free_raw(arena->allocs);
  c0_heap_free_raw(arena);
}

static void* c0_region_alloc_arena(C0RegionArena* arena, size_t size, size_t align) {
  if (!arena) {
    return NULL;
  }
  if (align == 0) {
    align = 1;
  }
  C0RegionBlock* block = arena->head;
  if (!block || align > block->align) {
    size_t block_size = size;
    if (arena->prealloc > block_size) {
      block_size = arena->prealloc;
    } else if (block && block->size > block_size) {
      block_size = block->size * 2;
    }
    if (block_size < size) {
      block_size = size;
    }
    C0RegionBlock* next = c0_region_block_new(block_size, align);
    if (!next) {
      return NULL;
    }
    next->prev = arena->head;
    arena->head = next;
    block = next;
  }

  for (;;) {
    size_t used = block->used;
    size_t aligned = (size_t)c0_align_up((uint64_t)used, (uint64_t)align);
    if (aligned > block->size || block->size - aligned < size) {
      size_t block_size = size;
      if (block->size > block_size) {
        block_size = block->size * 2;
      }
      if (block_size < size) {
        block_size = size;
      }
      C0RegionBlock* next = c0_region_block_new(block_size, align);
      if (!next) {
        return NULL;
      }
      next->prev = arena->head;
      arena->head = next;
      block = next;
      continue;
    }
    block->used = aligned + size;
    return block->data + aligned;
  }
}

void* cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3aalloc(
    const C0Region* self,
    uint64_t size,
    uint64_t align) {
  c0_trace_emit_rule("RegionSym-Alloc");
  if (!self) {
    return NULL;
  }

  C0RegionState* state = c0_region_state();
  cursive_platform_rwlock_lock_exclusive(&state->lock);

  C0RegionArena* arena = c0_region_find(state, self->handle);
  const C0RegionEntry* entry = c0_region_resolve_entry(state, self->handle);
  if (!arena || !entry) {
    cursive_platform_rwlock_unlock_exclusive(&state->lock);
    return NULL;
  }

  void* ptr = c0_region_alloc_arena(arena, (size_t)size, (size_t)align);
  if (ptr) {
    if (c0_region_arena_reserve_allocs(arena, arena->alloc_count + 1)) {
      arena->allocs[arena->alloc_count].addr = ptr;
      arena->alloc_count += 1;
      c0_region_addr_tag_set(state, ptr, entry->tag);
    } else {
      ptr = NULL;
    }
  }

  cursive_platform_rwlock_unlock_exclusive(&state->lock);
  return ptr;
}

uint64_t cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3amark(const C0Region* self) {
  c0_trace_emit_rule("RegionSym-Mark");
  if (!self) {
    return 0;
  }

  C0RegionState* state = c0_region_state();
  cursive_platform_rwlock_lock_exclusive(&state->lock);

  C0RegionArena* arena = c0_region_find(state, self->handle);
  if (!arena) {
    cursive_platform_rwlock_unlock_exclusive(&state->lock);
    return 0;
  }

  const uint64_t scope = c0_scope_current(state);
  const uint64_t mark = (uint64_t)arena->alloc_count;
  const uint64_t tag = c0_region_fresh_token(state);
  if (!c0_region_stack_push(state, tag, self->handle, scope, mark, 1)) {
    cursive_platform_rwlock_unlock_exclusive(&state->lock);
    return 0;
  }

  cursive_platform_rwlock_unlock_exclusive(&state->lock);
  return mark;
}

void cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3areset_x5fto(
    const C0Region* self,
    uint64_t mark_value) {
  c0_trace_emit_rule("RegionSym-ResetTo");
  if (!self) {
    return;
  }

  C0RegionState* state = c0_region_state();
  cursive_platform_rwlock_lock_exclusive(&state->lock);

  C0RegionArena* arena = c0_region_find(state, self->handle);
  size_t entry_index = c0_region_find_mark_entry_index(state, self->handle, mark_value);
  if (!arena || entry_index == SIZE_MAX) {
    cursive_platform_rwlock_unlock_exclusive(&state->lock);
    return;
  }

  if (mark_value <= (uint64_t)arena->alloc_count) {
    arena->alloc_count = (size_t)mark_value;
  }

  c0_region_stack_pop_scope(state, state->region_stack[entry_index].scope);
  cursive_platform_rwlock_unlock_exclusive(&state->lock);
}

C0Region cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3areset_x5funchecked(
    const C0Region* self) {
  c0_trace_emit_rule("Region-Reset-Proc");
  c0_trace_emit_rule("RegionSym-ResetUnchecked");
  if (!self) {
    return c0_region_make(C0_REGION_FREED, 0);
  }

  C0RegionState* state = c0_region_state();
  cursive_platform_rwlock_lock_exclusive(&state->lock);

  C0RegionArena* arena = c0_region_find(state, self->handle);
  if (arena) {
    const size_t tag_count = c0_region_count_target_entries(state, self->handle);
    uint64_t* fresh_tags = NULL;
    if (tag_count > 0) {
      fresh_tags = (uint64_t*)c0_heap_alloc_raw(tag_count * sizeof(uint64_t));
    }
    if (tag_count == 0 || fresh_tags) {
      size_t next = 0;
      arena->alloc_count = 0;
      for (size_t i = 0; i < tag_count; ++i) {
        fresh_tags[i] = c0_region_fresh_token(state);
      }
      for (size_t i = 0; i < state->region_count; ++i) {
        if (state->region_stack[i].target != self->handle) {
          continue;
        }
        state->region_stack[i].tag = fresh_tags[next++];
      }
    }
    c0_heap_free_raw(fresh_tags);
  }

  cursive_platform_rwlock_unlock_exclusive(&state->lock);
  return c0_region_make(C0_REGION_ACTIVE, self->handle);
}

C0Region cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3afreeze(const C0Region* self) {
  c0_trace_emit_rule("Region-Freeze-Proc");
  c0_trace_emit_rule("RegionSym-Freeze");
  return c0_region_make(C0_REGION_FROZEN, self ? self->handle : 0);
}

C0Region cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3athaw(const C0Region* self) {
  c0_trace_emit_rule("Region-Thaw-Proc");
  c0_trace_emit_rule("RegionSym-Thaw");
  return c0_region_make(C0_REGION_ACTIVE, self ? self->handle : 0);
}

C0Region cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3afree_x5funchecked(
    const C0Region* self) {
  c0_trace_emit_rule("Region-Free-Proc");
  c0_trace_emit_rule("RegionSym-FreeUnchecked");
  if (!self) {
    return c0_region_make(C0_REGION_FREED, 0);
  }

  C0RegionState* state = c0_region_state();
  cursive_platform_rwlock_lock_exclusive(&state->lock);

  C0RegionArena* arena = c0_region_find(state, self->handle);
  if (arena) {
    arena->alloc_count = 0;
    c0_region_stack_pop_target(state, self->handle);
    c0_region_remove(state, self->handle);
    // Keep published region blocks quarantined after free. The spec preserves
    // AddrTags across reset/free, so reusing a raw address would violate the
    // FreshAddr witness for later allocations.
    c0_region_discard_arena(arena, 0);
  }

  cursive_platform_rwlock_unlock_exclusive(&state->lock);
  return c0_region_make(C0_REGION_FREED, self->handle);
}

uint8_t cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3aaddr_x5fis_x5factive(
    const void* addr) {
  c0_trace_emit_rule("RegionSym-AddrIsActive");
  C0RegionState* state = c0_region_state();
  cursive_platform_rwlock_lock_exclusive(&state->lock);

  const uint64_t tag = c0_region_addr_tag_get(state, addr);
  const bool ok = (tag == 0) ? true : c0_region_tag_active(state, tag);
  if (tag == 0) {
    c0_trace_emit_rule("RegionAddrIsActive-TagZero");
  } else if (c0_tag_is_scope(tag)) {
    c0_trace_emit_rule(ok ? "RegionAddrIsActive-TagScope-Active"
                          : "RegionAddrIsActive-TagScope-Inactive");
  } else {
    c0_trace_emit_rule(ok ? "RegionAddrIsActive-TagRegion-Active"
                          : "RegionAddrIsActive-TagRegion-Inactive");
  }

  cursive_platform_rwlock_unlock_exclusive(&state->lock);
  return ok ? 1 : 0;
}

void cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3aaddr_x5ftag_x5ffrom(
    const void* addr,
    const void* base) {
  c0_trace_emit_rule("RegionSym-AddrTagFrom");
  if (!addr || !base) {
    return;
  }

  C0RegionState* state = c0_region_state();
  cursive_platform_rwlock_lock_exclusive(&state->lock);

  const uint64_t tag = c0_region_addr_tag_get(state, base);
  if (tag != 0) {
    c0_region_addr_tag_set(state, (void*)addr, tag);
  }

  cursive_platform_rwlock_unlock_exclusive(&state->lock);
}

void cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fenter(
    uint64_t scope_id) {
  c0_trace_emit_rule("RegionSym-ScopeEnter");
  C0RegionState* state = c0_region_state();
  cursive_platform_rwlock_lock_exclusive(&state->lock);
  c0_scope_enter(state, scope_id);
  cursive_platform_rwlock_unlock_exclusive(&state->lock);
}

void cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fexit(
    uint64_t scope_id) {
  c0_trace_emit_rule("RegionSym-ScopeExit");
  C0RegionState* state = c0_region_state();
  cursive_platform_rwlock_lock_exclusive(&state->lock);
  c0_scope_exit(state, scope_id);
  cursive_platform_rwlock_unlock_exclusive(&state->lock);
}

void cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3aaddr_x5ftag_x5fscope(
    const void* addr,
    uint64_t scope_id) {
  c0_trace_emit_rule("RegionSym-AddrTagScope");
  if (!addr || scope_id == 0) {
    return;
  }

  C0RegionState* state = c0_region_state();
  cursive_platform_rwlock_lock_exclusive(&state->lock);

  const uint64_t existing = c0_region_addr_tag_get(state, addr);
  if (existing == 0 || c0_tag_is_scope(existing)) {
    c0_region_addr_tag_set(state, (void*)addr, c0_scope_tag(scope_id));
  }

  cursive_platform_rwlock_unlock_exclusive(&state->lock);
}

C0Region cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3anew_x5fscoped(
    const C0RegionOptions* options) {
  c0_trace_emit_rule("RegionSym-NewScoped");
  C0RegionState* state = c0_region_state();
  C0RegionArena* arena = (C0RegionArena*)c0_heap_alloc_raw(sizeof(C0RegionArena));
  if (!arena) {
    return c0_region_make(C0_REGION_FREED, 0);
  }
  c0_memset(arena, 0, sizeof(C0RegionArena));

  if (options) {
    arena->prealloc = (size_t)options->stack_size;
  }
  if (arena->prealloc > 0) {
    C0RegionBlock* block = c0_region_block_new(arena->prealloc, 8);
    if (block) {
      arena->head = block;
    }
  }

  cursive_platform_rwlock_lock_exclusive(&state->lock);

  arena->handle = c0_region_fresh_token(state);
  if (!c0_region_insert(state, arena) ||
      !c0_region_stack_push(state,
                            arena->handle,
                            arena->handle,
                            c0_scope_current(state),
                            0,
                            0)) {
    c0_region_remove(state, arena->handle);
    cursive_platform_rwlock_unlock_exclusive(&state->lock);
    c0_region_discard_arena(arena, 1);
    return c0_region_make(C0_REGION_FREED, 0);
  }

  cursive_platform_rwlock_unlock_exclusive(&state->lock);
  return c0_region_make(C0_REGION_ACTIVE, arena->handle);
}
