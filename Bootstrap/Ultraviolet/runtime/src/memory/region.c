#include "../internal/rt_internal.h"

enum {
  UV_REGION_ACTIVE = 0,
  UV_REGION_FROZEN = 1,
  UV_REGION_FREED = 2,
};

typedef struct UVRegionBlock {
  struct UVRegionBlock* prev;
  void* base;
  uint8_t* data;
  size_t size;
  size_t used;
  size_t align;
} UVRegionBlock;

typedef struct UVRegionAlloc {
  void* addr;
} UVRegionAlloc;

typedef struct UVRegionArena {
  uint64_t handle;
  size_t prealloc;
  UVRegionBlock* head;
  UVRegionAlloc* allocs;
  size_t alloc_count;
  size_t alloc_cap;
} UVRegionArena;

typedef struct UVRegionSlot {
  uint64_t handle;
  UVRegionArena* arena;
} UVRegionSlot;

typedef struct UVRegionEntry {
  uint64_t tag;
  uint64_t target;
  uint64_t scope;
  uint64_t mark;
  uint8_t has_mark;
  uint8_t _pad[7];
} UVRegionEntry;

typedef struct UVAddrTag {
  void* addr;
  uint64_t tag;
} UVAddrTag;

typedef struct UVRegionState {
  UVRegionSlot* slots;
  size_t count;
  size_t cap;
  UVRegionEntry* region_stack;
  size_t region_count;
  size_t region_cap;
  uint64_t* scope_stack;
  size_t scope_count;
  size_t scope_cap;
  UVAddrTag* addr_tags;
  size_t addr_count;
  size_t addr_cap;
  uint64_t next_region_token;
  uv_platform_rwlock_t lock;
} UVRegionState;

static UVRegionState g_region_state;
static uv_platform_once_t g_region_once = UV_PLATFORM_ONCE_INIT;

static const uint64_t UV_SCOPE_TAG_MASK = 0x8000000000000000ull;

static bool uv_tag_is_scope(uint64_t tag) {
  return (tag & UV_SCOPE_TAG_MASK) != 0;
}

static uint64_t uv_scope_tag(uint64_t scope_id) {
  if (scope_id == 0) {
    return 0;
  }
  return UV_SCOPE_TAG_MASK | (scope_id & ~UV_SCOPE_TAG_MASK);
}

static uint64_t uv_scope_id_from_tag(uint64_t tag) {
  return tag & ~UV_SCOPE_TAG_MASK;
}

static uv_platform_bool_t uv_region_init_once(
    uv_platform_once_t* init_once,
    void* param,
    void** context) {
  (void)init_once;
  (void)param;
  (void)context;
  uv_memset(&g_region_state, 0, sizeof(g_region_state));
  g_region_state.next_region_token = 1;
  uv_platform_rwlock_init(&g_region_state.lock);
  return UV_PLATFORM_TRUE;
}

static UVRegionState* uv_region_state(void) {
  uv_platform_once_execute(&g_region_once, uv_region_init_once, NULL, NULL);
  return &g_region_state;
}

static UVRegion uv_region_make(uint8_t disc, uint64_t handle) {
  UVRegion region;
  uv_memset(&region, 0, sizeof(UVRegion));
  region.disc = disc;
  region.handle = handle;
  return region;
}

static bool uv_region_reserve_slots(UVRegionState* state, size_t needed) {
  if (needed <= state->cap) {
    return true;
  }
  size_t new_cap = state->cap ? state->cap : 8;
  while (new_cap < needed) {
    new_cap *= 2;
  }
  UVRegionSlot* next =
      (UVRegionSlot*)uv_heap_alloc_raw(new_cap * sizeof(UVRegionSlot));
  if (!next) {
    return false;
  }
  if (state->slots && state->count > 0) {
    uv_memcpy(next, state->slots, state->count * sizeof(UVRegionSlot));
  }
  uv_heap_free_raw(state->slots);
  state->slots = next;
  state->cap = new_cap;
  return true;
}

static bool uv_region_reserve_entries(UVRegionState* state, size_t needed) {
  if (needed <= state->region_cap) {
    return true;
  }
  size_t new_cap = state->region_cap ? state->region_cap : 16;
  while (new_cap < needed) {
    new_cap *= 2;
  }
  UVRegionEntry* next =
      (UVRegionEntry*)uv_heap_alloc_raw(new_cap * sizeof(UVRegionEntry));
  if (!next) {
    return false;
  }
  if (state->region_stack && state->region_count > 0) {
    uv_memcpy(next, state->region_stack, state->region_count * sizeof(UVRegionEntry));
  }
  uv_heap_free_raw(state->region_stack);
  state->region_stack = next;
  state->region_cap = new_cap;
  return true;
}

static bool uv_region_reserve_scope_stack(UVRegionState* state, size_t needed) {
  if (needed <= state->scope_cap) {
    return true;
  }
  size_t new_cap = state->scope_cap ? state->scope_cap : 16;
  while (new_cap < needed) {
    new_cap *= 2;
  }
  uint64_t* next = (uint64_t*)uv_heap_alloc_raw(new_cap * sizeof(uint64_t));
  if (!next) {
    return false;
  }
  if (state->scope_stack && state->scope_count > 0) {
    uv_memcpy(next, state->scope_stack, state->scope_count * sizeof(uint64_t));
  }
  uv_heap_free_raw(state->scope_stack);
  state->scope_stack = next;
  state->scope_cap = new_cap;
  return true;
}

static bool uv_region_reserve_addr_tags(UVRegionState* state, size_t needed) {
  if (needed <= state->addr_cap) {
    return true;
  }
  size_t new_cap = state->addr_cap ? state->addr_cap : 16;
  while (new_cap < needed) {
    new_cap *= 2;
  }
  UVAddrTag* next = (UVAddrTag*)uv_heap_alloc_raw(new_cap * sizeof(UVAddrTag));
  if (!next) {
    return false;
  }
  if (state->addr_tags && state->addr_count > 0) {
    uv_memcpy(next, state->addr_tags, state->addr_count * sizeof(UVAddrTag));
  }
  uv_heap_free_raw(state->addr_tags);
  state->addr_tags = next;
  state->addr_cap = new_cap;
  return true;
}

static bool uv_region_arena_reserve_allocs(UVRegionArena* arena, size_t needed) {
  if (needed <= arena->alloc_cap) {
    return true;
  }
  size_t new_cap = arena->alloc_cap ? arena->alloc_cap : 16;
  while (new_cap < needed) {
    new_cap *= 2;
  }
  UVRegionAlloc* next =
      (UVRegionAlloc*)uv_heap_alloc_raw(new_cap * sizeof(UVRegionAlloc));
  if (!next) {
    return false;
  }
  if (arena->allocs && arena->alloc_count > 0) {
    uv_memcpy(next, arena->allocs, arena->alloc_count * sizeof(UVRegionAlloc));
  }
  uv_heap_free_raw(arena->allocs);
  arena->allocs = next;
  arena->alloc_cap = new_cap;
  return true;
}

static UVRegionArena* uv_region_find(UVRegionState* state, uint64_t handle) {
  for (size_t i = 0; i < state->count; ++i) {
    if (state->slots[i].handle == handle) {
      return state->slots[i].arena;
    }
  }
  return NULL;
}

static bool uv_region_insert(UVRegionState* state, UVRegionArena* arena) {
  if (!arena) {
    return false;
  }
  if (!uv_region_reserve_slots(state, state->count + 1)) {
    return false;
  }
  state->slots[state->count].handle = arena->handle;
  state->slots[state->count].arena = arena;
  state->count += 1;
  return true;
}

static const UVRegionEntry* uv_region_active_entry(const UVRegionState* state) {
  if (!state || state->region_count == 0) {
    return NULL;
  }
  return &state->region_stack[state->region_count - 1];
}

static uint64_t uv_region_active_target(const UVRegionState* state) {
  const UVRegionEntry* entry = uv_region_active_entry(state);
  if (!entry) {
    return 0;
  }
  return entry->target;
}

static const UVRegionEntry* uv_region_resolve_entry(const UVRegionState* state,
                                                    uint64_t target) {
  const UVRegionEntry* active = uv_region_active_entry(state);
  if (active && uv_region_active_target(state) == target) {
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

static void uv_region_remove(UVRegionState* state, uint64_t handle) {
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

static uint64_t uv_scope_current(const UVRegionState* state) {
  if (!state || state->scope_count == 0) {
    return 0;
  }
  return state->scope_stack[state->scope_count - 1];
}

static bool uv_scope_active(const UVRegionState* state, uint64_t scope_id) {
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

static void uv_scope_enter(UVRegionState* state, uint64_t scope_id) {
  if (!state || scope_id == 0) {
    return;
  }
  if (!uv_region_reserve_scope_stack(state, state->scope_count + 1)) {
    return;
  }
  state->scope_stack[state->scope_count] = scope_id;
  state->scope_count += 1;
}

static void uv_scope_exit(UVRegionState* state, uint64_t scope_id) {
  if (!state || scope_id == 0 || state->scope_count == 0) {
    return;
  }
  for (size_t i = state->scope_count; i > 0; --i) {
    if (state->scope_stack[i - 1] != scope_id) {
      continue;
    }
    if (i < state->scope_count) {
      uv_memmove(&state->scope_stack[i - 1],
                 &state->scope_stack[i],
                 (state->scope_count - i) * sizeof(uint64_t));
    }
    state->scope_count -= 1;
    return;
  }
}

static void uv_region_addr_tag_set(UVRegionState* state, void* addr, uint64_t tag) {
  if (!state || !addr) {
    return;
  }
  for (size_t i = 0; i < state->addr_count; ++i) {
    if (state->addr_tags[i].addr == addr) {
      state->addr_tags[i].tag = tag;
      return;
    }
  }
  if (!uv_region_reserve_addr_tags(state, state->addr_count + 1)) {
    return;
  }
  state->addr_tags[state->addr_count].addr = addr;
  state->addr_tags[state->addr_count].tag = tag;
  state->addr_count += 1;
}

static uint64_t uv_region_addr_tag_get(const UVRegionState* state, const void* addr) {
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

static uint64_t uv_region_fresh_token(UVRegionState* state) {
  uint64_t token = 0;
  if (!state) {
    return 0;
  }
  do {
    token = state->next_region_token++;
  } while (token == 0 || uv_tag_is_scope(token));
  return token;
}

static bool uv_region_stack_push(UVRegionState* state,
                                 uint64_t tag,
                                 uint64_t target,
                                 uint64_t scope,
                                 uint64_t mark,
                                 int has_mark) {
  if (!state) {
    return false;
  }
  if (!uv_region_reserve_entries(state, state->region_count + 1)) {
    return false;
  }
  state->region_stack[state->region_count].tag = tag;
  state->region_stack[state->region_count].target = target;
  state->region_stack[state->region_count].scope = scope;
  state->region_stack[state->region_count].mark = mark;
  state->region_stack[state->region_count].has_mark = has_mark ? 1 : 0;
  uv_memset(state->region_stack[state->region_count]._pad, 0,
            sizeof(state->region_stack[state->region_count]._pad));
  state->region_count += 1;
  return true;
}

static size_t uv_region_find_mark_entry_index(const UVRegionState* state,
                                              uint64_t target,
                                              uint64_t mark) {
  if (!state) {
    return SIZE_MAX;
  }
  for (size_t i = state->region_count; i > 0; --i) {
    const UVRegionEntry* entry = &state->region_stack[i - 1];
    if (entry->target == target && entry->has_mark != 0 && entry->mark == mark) {
      return i - 1;
    }
  }
  return SIZE_MAX;
}

static bool uv_region_tag_active(const UVRegionState* state, uint64_t tag) {
  if (tag == 0) {
    return true;
  }
  if (uv_tag_is_scope(tag)) {
    return uv_scope_active(state, uv_scope_id_from_tag(tag));
  }
  for (size_t i = 0; i < state->region_count; ++i) {
    if (state->region_stack[i].tag == tag) {
      return true;
    }
  }
  return false;
}

static void uv_region_stack_remove_index(UVRegionState* state, size_t index) {
  if (!state || index >= state->region_count) {
    return;
  }
  if (index + 1 < state->region_count) {
    uv_memmove(&state->region_stack[index],
               &state->region_stack[index + 1],
               (state->region_count - index - 1) * sizeof(UVRegionEntry));
  }
  state->region_count -= 1;
}

static void uv_region_stack_pop_scope(UVRegionState* state, uint64_t scope) {
  if (!state) {
    return;
  }
  for (size_t i = state->region_count; i > 0; --i) {
    if (state->region_stack[i - 1].scope != scope) {
      continue;
    }
    uv_region_stack_remove_index(state, i - 1);
    return;
  }
}

static void uv_region_stack_pop_target(UVRegionState* state, uint64_t target) {
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

static size_t uv_region_count_target_entries(const UVRegionState* state, uint64_t target) {
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

static UVRegionBlock* uv_region_block_new(size_t size, size_t align) {
  if (align == 0) {
    align = 1;
  }
  const size_t total = sizeof(UVRegionBlock) + size + align;
  uint8_t* raw = (uint8_t*)uv_heap_alloc_raw(total);
  if (!raw) {
    return NULL;
  }
  UVRegionBlock* block = (UVRegionBlock*)raw;
  uint8_t* data_start = raw + sizeof(UVRegionBlock);
  uintptr_t aligned = (uintptr_t)uv_align_up((uint64_t)(uintptr_t)data_start,
                                             (uint64_t)align);
  block->prev = NULL;
  block->base = raw;
  block->data = (uint8_t*)aligned;
  block->size = size;
  block->used = 0;
  block->align = align;
  return block;
}

static void uv_region_block_free(UVRegionBlock* block) {
  if (!block) {
    return;
  }
  uv_heap_free_raw(block->base);
}

static void uv_region_free_blocks(UVRegionArena* arena) {
  if (!arena) {
    return;
  }
  UVRegionBlock* block = arena->head;
  while (block) {
    UVRegionBlock* prev = block->prev;
    uv_region_block_free(block);
    block = prev;
  }
  arena->head = NULL;
}

static void uv_region_discard_arena(UVRegionArena* arena, int free_blocks) {
  if (!arena) {
    return;
  }
  if (free_blocks) {
    uv_region_free_blocks(arena);
  }
  uv_heap_free_raw(arena->allocs);
  uv_heap_free_raw(arena);
}

static void* uv_region_alloc_arena(UVRegionArena* arena, size_t size, size_t align) {
  if (!arena) {
    return NULL;
  }
  if (align == 0) {
    align = 1;
  }
  UVRegionBlock* block = arena->head;
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
    UVRegionBlock* next = uv_region_block_new(block_size, align);
    if (!next) {
      return NULL;
    }
    next->prev = arena->head;
    arena->head = next;
    block = next;
  }

  for (;;) {
    size_t used = block->used;
    size_t aligned = (size_t)uv_align_up((uint64_t)used, (uint64_t)align);
    if (aligned > block->size || block->size - aligned < size) {
      size_t block_size = size;
      if (block->size > block_size) {
        block_size = block->size * 2;
      }
      if (block_size < size) {
        block_size = size;
      }
      UVRegionBlock* next = uv_region_block_new(block_size, align);
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

void* ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3aalloc(
    UVRegion self,
    uint64_t size,
    uint64_t align) {
  uv_trace_emit_rule("RegionSym-Alloc");
  if (self.handle == 0) {
    return NULL;
  }

  UVRegionState* state = uv_region_state();
  uv_platform_rwlock_lock_exclusive(&state->lock);

  UVRegionArena* arena = uv_region_find(state, self.handle);
  const UVRegionEntry* entry = uv_region_resolve_entry(state, self.handle);
  if (!arena || !entry) {
    uv_platform_rwlock_unlock_exclusive(&state->lock);
    return NULL;
  }

  void* ptr = uv_region_alloc_arena(arena, (size_t)size, (size_t)align);
  if (ptr) {
    if (uv_region_arena_reserve_allocs(arena, arena->alloc_count + 1)) {
      arena->allocs[arena->alloc_count].addr = ptr;
      arena->alloc_count += 1;
      uv_region_addr_tag_set(state, ptr, entry->tag);
    } else {
      ptr = NULL;
    }
  }

  uv_platform_rwlock_unlock_exclusive(&state->lock);
  return ptr;
}

uint64_t ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3amark(UVRegion self) {
  uv_trace_emit_rule("RegionSym-Mark");
  if (self.handle == 0) {
    return 0;
  }

  UVRegionState* state = uv_region_state();
  uv_platform_rwlock_lock_exclusive(&state->lock);

  UVRegionArena* arena = uv_region_find(state, self.handle);
  if (!arena) {
    uv_platform_rwlock_unlock_exclusive(&state->lock);
    return 0;
  }

  const uint64_t scope = uv_scope_current(state);
  const uint64_t mark = (uint64_t)arena->alloc_count;
  const uint64_t tag = uv_region_fresh_token(state);
  if (!uv_region_stack_push(state, tag, self.handle, scope, mark, 1)) {
    uv_platform_rwlock_unlock_exclusive(&state->lock);
    return 0;
  }

  uv_platform_rwlock_unlock_exclusive(&state->lock);
  return mark;
}

void ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3areset_x5fto(
    UVRegion self,
    uint64_t mark_value) {
  uv_trace_emit_rule("RegionSym-ResetTo");
  if (self.handle == 0) {
    return;
  }

  UVRegionState* state = uv_region_state();
  uv_platform_rwlock_lock_exclusive(&state->lock);

  UVRegionArena* arena = uv_region_find(state, self.handle);
  size_t entry_index = uv_region_find_mark_entry_index(state, self.handle, mark_value);
  if (!arena || entry_index == SIZE_MAX) {
    uv_platform_rwlock_unlock_exclusive(&state->lock);
    return;
  }

  if (mark_value <= (uint64_t)arena->alloc_count) {
    arena->alloc_count = (size_t)mark_value;
  }

  uv_region_stack_pop_scope(state, state->region_stack[entry_index].scope);
  uv_platform_rwlock_unlock_exclusive(&state->lock);
}

UVRegion ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3areset_x5funchecked(
    UVRegion self) {
  uv_trace_emit_rule("Region-Reset-Proc");
  uv_trace_emit_rule("RegionSym-ResetUnchecked");
  if (self.handle == 0) {
    return uv_region_make(UV_REGION_FREED, 0);
  }

  UVRegionState* state = uv_region_state();
  uv_platform_rwlock_lock_exclusive(&state->lock);

  UVRegionArena* arena = uv_region_find(state, self.handle);
  if (arena) {
    const size_t tag_count = uv_region_count_target_entries(state, self.handle);
    uint64_t* fresh_tags = NULL;
    if (tag_count > 0) {
      fresh_tags = (uint64_t*)uv_heap_alloc_raw(tag_count * sizeof(uint64_t));
    }
    if (tag_count == 0 || fresh_tags) {
      size_t next = 0;
      arena->alloc_count = 0;
      for (size_t i = 0; i < tag_count; ++i) {
        fresh_tags[i] = uv_region_fresh_token(state);
      }
      for (size_t i = 0; i < state->region_count; ++i) {
        if (state->region_stack[i].target != self.handle) {
          continue;
        }
        state->region_stack[i].tag = fresh_tags[next++];
      }
    }
    uv_heap_free_raw(fresh_tags);
  }

  uv_platform_rwlock_unlock_exclusive(&state->lock);
  return uv_region_make(UV_REGION_ACTIVE, self.handle);
}

UVRegion ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3afreeze(UVRegion self) {
  uv_trace_emit_rule("Region-Freeze-Proc");
  uv_trace_emit_rule("RegionSym-Freeze");
  return uv_region_make(UV_REGION_FROZEN, self.handle);
}

UVRegion ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3athaw(UVRegion self) {
  uv_trace_emit_rule("Region-Thaw-Proc");
  uv_trace_emit_rule("RegionSym-Thaw");
  return uv_region_make(UV_REGION_ACTIVE, self.handle);
}

UVRegion ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3afree_x5funchecked(
    UVRegion self) {
  uv_trace_emit_rule("Region-Free-Proc");
  uv_trace_emit_rule("RegionSym-FreeUnchecked");
  if (self.handle == 0) {
    return uv_region_make(UV_REGION_FREED, 0);
  }

  UVRegionState* state = uv_region_state();
  uv_platform_rwlock_lock_exclusive(&state->lock);

  UVRegionArena* arena = uv_region_find(state, self.handle);
  if (arena) {
    arena->alloc_count = 0;
    uv_region_stack_pop_target(state, self.handle);
    uv_region_remove(state, self.handle);
    // Keep published region blocks quarantined after free. The spec preserves
    // AddrTags across reset/free, so reusing a raw address would violate the
    // FreshAddr witness for later allocations.
    uv_region_discard_arena(arena, 0);
  }

  uv_platform_rwlock_unlock_exclusive(&state->lock);
  return uv_region_make(UV_REGION_FREED, self.handle);
}

uint8_t ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3aaddr_x5fis_x5factive(
    const void* addr) {
  uv_trace_emit_rule("RegionSym-AddrIsActive");
  UVRegionState* state = uv_region_state();
  uv_platform_rwlock_lock_exclusive(&state->lock);

  const uint64_t tag = uv_region_addr_tag_get(state, addr);
  const bool ok = (tag == 0) ? true : uv_region_tag_active(state, tag);
  if (tag == 0) {
    uv_trace_emit_rule("RegionAddrIsActive-TagZero");
  } else if (uv_tag_is_scope(tag)) {
    uv_trace_emit_rule(ok ? "RegionAddrIsActive-TagScope-Active"
                          : "RegionAddrIsActive-TagScope-Inactive");
  } else {
    uv_trace_emit_rule(ok ? "RegionAddrIsActive-TagRegion-Active"
                          : "RegionAddrIsActive-TagRegion-Inactive");
  }

  uv_platform_rwlock_unlock_exclusive(&state->lock);
  return ok ? 1 : 0;
}

void ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3aaddr_x5ftag_x5ffrom(
    const void* addr,
    const void* base) {
  uv_trace_emit_rule("RegionSym-AddrTagFrom");
  if (!addr || !base) {
    return;
  }

  UVRegionState* state = uv_region_state();
  uv_platform_rwlock_lock_exclusive(&state->lock);

  const uint64_t tag = uv_region_addr_tag_get(state, base);
  if (tag != 0) {
    uv_region_addr_tag_set(state, (void*)addr, tag);
  }

  uv_platform_rwlock_unlock_exclusive(&state->lock);
}

void ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fenter(
    uint64_t scope_id) {
  uv_trace_emit_rule("RegionSym-ScopeEnter");
  UVRegionState* state = uv_region_state();
  uv_platform_rwlock_lock_exclusive(&state->lock);
  uv_scope_enter(state, scope_id);
  uv_platform_rwlock_unlock_exclusive(&state->lock);
}

void ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fexit(
    uint64_t scope_id) {
  uv_trace_emit_rule("RegionSym-ScopeExit");
  UVRegionState* state = uv_region_state();
  uv_platform_rwlock_lock_exclusive(&state->lock);
  uv_scope_exit(state, scope_id);
  uv_platform_rwlock_unlock_exclusive(&state->lock);
}

void ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3aaddr_x5ftag_x5fscope(
    const void* addr,
    uint64_t scope_id) {
  uv_trace_emit_rule("RegionSym-AddrTagScope");
  if (!addr || scope_id == 0) {
    return;
  }

  UVRegionState* state = uv_region_state();
  uv_platform_rwlock_lock_exclusive(&state->lock);

  const uint64_t existing = uv_region_addr_tag_get(state, addr);
  if (existing == 0 || uv_tag_is_scope(existing)) {
    uv_region_addr_tag_set(state, (void*)addr, uv_scope_tag(scope_id));
  }

  uv_platform_rwlock_unlock_exclusive(&state->lock);
}

UVRegion ultraviolet_x3a_x3aruntime_x3a_x3aregion_x3a_x3anew_x5fscoped(
    const UVRegionOptions* options) {
  uv_trace_emit_rule("RegionSym-NewScoped");
  UVRegionState* state = uv_region_state();
  UVRegionArena* arena = (UVRegionArena*)uv_heap_alloc_raw(sizeof(UVRegionArena));
  if (!arena) {
    return uv_region_make(UV_REGION_FREED, 0);
  }
  uv_memset(arena, 0, sizeof(UVRegionArena));

  if (options) {
    arena->prealloc = (size_t)options->stack_size;
  }
  if (arena->prealloc > 0) {
    UVRegionBlock* block = uv_region_block_new(arena->prealloc, 8);
    if (block) {
      arena->head = block;
    }
  }

  uv_platform_rwlock_lock_exclusive(&state->lock);

  arena->handle = uv_region_fresh_token(state);
  if (!uv_region_insert(state, arena) ||
      !uv_region_stack_push(state,
                            arena->handle,
                            arena->handle,
                            uv_scope_current(state),
                            0,
                            0)) {
    uv_region_remove(state, arena->handle);
    uv_platform_rwlock_unlock_exclusive(&state->lock);
    uv_region_discard_arena(arena, 1);
    return uv_region_make(UV_REGION_FREED, 0);
  }

  uv_platform_rwlock_unlock_exclusive(&state->lock);
  return uv_region_make(UV_REGION_ACTIVE, arena->handle);
}
