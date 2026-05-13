#include "../internal/rt_internal.h"

#if defined(CURSIVE_RT_PLATFORM_LINUX)
#include <time.h>
#elif defined(CURSIVE_RT_PLATFORM_WINDOWS)
#include <float.h>
#endif

static C0U128 c0_u128_zero(void) {
  C0U128 out;
  out.lo = 0;
  out.hi = 0;
  return out;
}

static C0U128 c0_u128_from_u64(uint64_t value) {
  C0U128 out;
  out.lo = value;
  out.hi = 0;
  return out;
}

static int c0_u128_is_zero(C0U128 value) {
  return value.lo == 0 && value.hi == 0;
}

static int c0_u128_cmp(C0U128 lhs, C0U128 rhs) {
  if (lhs.hi < rhs.hi) {
    return -1;
  }
  if (lhs.hi > rhs.hi) {
    return 1;
  }
  if (lhs.lo < rhs.lo) {
    return -1;
  }
  if (lhs.lo > rhs.lo) {
    return 1;
  }
  return 0;
}

static C0U128 c0_u128_sub(C0U128 lhs, C0U128 rhs) {
  C0U128 out;
  out.lo = lhs.lo - rhs.lo;
  out.hi = lhs.hi - rhs.hi - (lhs.lo < rhs.lo ? 1u : 0u);
  return out;
}

static C0U128 c0_u128_max(C0U128 lhs, C0U128 rhs) {
  return c0_u128_cmp(lhs, rhs) >= 0 ? lhs : rhs;
}

static C0U128 c0_u128_floor_to_resolution(C0U128 value, C0U128 resolution) {
  if (c0_u128_is_zero(resolution) ||
      (resolution.hi == 0 && resolution.lo <= 1)) {
    return value;
  }
  if (resolution.hi != 0) {
    return c0_u128_cmp(value, resolution) < 0 ? c0_u128_zero() : value;
  }
  value.lo = (value.lo / resolution.lo) * resolution.lo;
  return value;
}

static C0TimeState* c0_time_from_dyn(const C0DynObject* cap) {
  if (!cap) {
    return NULL;
  }
  return (C0TimeState*)cap->data;
}

static C0DynObject c0_time_dyn(C0TimeState* state) {
  C0DynObject out;
  out.data = state;
  out.vtable = NULL;
  return out;
}

static C0TimeState* c0_time_child(C0TimeState* parent,
                                  uint8_t kind,
                                  C0U128 resolution) {
  C0TimeState* state = (C0TimeState*)c0_heap_alloc_raw(sizeof(C0TimeState));
  if (!state) {
    return NULL;
  }
  state->parent = parent;
  state->kind = kind;
  state->domain = parent ? parent->domain : 1;
  state->resolution = resolution;
  return state;
}

static int c0_platform_monotonic_now_ns(uint64_t* out) {
  if (!out) {
    return 0;
  }
#if defined(CURSIVE_RT_PLATFORM_LINUX)
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  *out = ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
  return 1;
#elif defined(CURSIVE_RT_PLATFORM_WINDOWS)
  LARGE_INTEGER counter;
  LARGE_INTEGER frequency;
  long double ns;
  if (!QueryPerformanceCounter(&counter) ||
      !QueryPerformanceFrequency(&frequency) ||
      frequency.QuadPart <= 0) {
    return 0;
  }
  ns = ((long double)counter.QuadPart * 1000000000.0L) /
       (long double)frequency.QuadPart;
  if (ns < 0.0L || ns > (long double)UINT64_MAX) {
    return 0;
  }
  *out = (uint64_t)ns;
  return 1;
#else
  return 0;
#endif
}

static int c0_platform_monotonic_resolution_ns(uint64_t* out) {
  if (!out) {
    return 0;
  }
#if defined(CURSIVE_RT_PLATFORM_LINUX)
  struct timespec ts;
  if (clock_getres(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  *out = ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
  if (*out == 0) {
    *out = 1;
  }
  return 1;
#elif defined(CURSIVE_RT_PLATFORM_WINDOWS)
  LARGE_INTEGER frequency;
  if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
    return 0;
  }
  *out = (uint64_t)((1000000000ULL + (uint64_t)frequency.QuadPart - 1ULL) /
                    (uint64_t)frequency.QuadPart);
  if (*out == 0) {
    *out = 1;
  }
  return 1;
#else
  return 0;
#endif
}

static int c0_platform_wall_now_unix_ns(C0I128* out) {
  if (!out) {
    return 0;
  }
#if defined(CURSIVE_RT_PLATFORM_LINUX)
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0;
  }
  out->lo = ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
  out->hi = 0;
  return 1;
#elif defined(CURSIVE_RT_PLATFORM_WINDOWS)
  static const uint64_t kUnixEpochFiletime = 116444736000000000ULL;
  cursive_rt_filetime_t ft;
  uint64_t ticks;
  cursive_rt_system_time_filetime(&ft);
  ticks = ((uint64_t)ft.high_date_time << 32u) | (uint64_t)ft.low_date_time;
  if (ticks < kUnixEpochFiletime) {
    return 0;
  }
  ticks -= kUnixEpochFiletime;
  if (ticks > UINT64_MAX / 100ULL) {
    return 0;
  }
  out->lo = ticks * 100ULL;
  out->hi = 0;
  return 1;
#else
  return 0;
#endif
}

static int c0_platform_wall_resolution_ns(C0U128* out) {
  if (!out) {
    return 0;
  }
#if defined(CURSIVE_RT_PLATFORM_LINUX)
  struct timespec ts;
  if (clock_getres(CLOCK_REALTIME, &ts) != 0) {
    return 0;
  }
  *out = c0_u128_from_u64(((uint64_t)ts.tv_sec * 1000000000ULL) +
                          (uint64_t)ts.tv_nsec);
  if (c0_u128_is_zero(*out)) {
    *out = c0_u128_from_u64(1);
  }
  return 1;
#elif defined(CURSIVE_RT_PLATFORM_WINDOWS)
  *out = c0_u128_from_u64(100);
  return 1;
#else
  return 0;
#endif
}

static void c0_duration_write(C0Duration* out, C0U128 value) {
  if (out) {
    out->nanoseconds = value;
  }
}

static void c0_duration_ok(C0Union_Duration_TimeError* out, C0U128 value) {
  if (!out) {
    return;
  }
  out->disc = 0;
  out->payload.value.nanoseconds = value;
}

static void c0_duration_err(C0Union_Duration_TimeError* out, C0TimeError err) {
  if (!out) {
    return;
  }
  out->disc = 1;
  out->payload.time_error = err;
}

static void c0_dyn_ok(C0Union_DynObject_TimeError* out, C0DynObject value) {
  if (!out) {
    return;
  }
  out->disc = 0;
  out->payload.value = value;
}

static void c0_dyn_err(C0Union_DynObject_TimeError* out, C0TimeError err) {
  if (!out) {
    return;
  }
  out->disc = 1;
  out->payload.time_error = err;
}

static void c0_utc_ok(C0Union_UtcInstant_TimeError* out, C0I128 value) {
  if (!out) {
    return;
  }
  out->disc = 0;
  out->payload.value.unix_nanoseconds = value;
}

static void c0_utc_err(C0Union_UtcInstant_TimeError* out, C0TimeError err) {
  if (!out) {
    return;
  }
  out->disc = 1;
  out->payload.time_error = err;
}

C0DynObject cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic(
    const C0DynObject* self) {
  C0TimeState* root = c0_time_from_dyn(self);
  uint64_t resolution_ns = 1;
  C0TimeState* state;
  if (!root) {
    return c0_time_dyn(NULL);
  }
  if (!c0_platform_monotonic_resolution_ns(&resolution_ns)) {
    resolution_ns = 1;
  }
  state = c0_time_child(root, C0_TIME_STATE_MONOTONIC,
                        c0_u128_from_u64(resolution_ns));
  return c0_time_dyn(state);
}

C0DynObject cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3awall(
    const C0DynObject* self) {
  C0TimeState* root = c0_time_from_dyn(self);
  C0U128 resolution = c0_u128_from_u64(1);
  C0TimeState* state;
  if (!root) {
    return c0_time_dyn(NULL);
  }
  (void)c0_platform_wall_resolution_ns(&resolution);
  state = c0_time_child(root, C0_TIME_STATE_WALL, resolution);
  return c0_time_dyn(state);
}

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5fnow(
    C0MonotonicInstant* out,
    const C0DynObject* self) {
  C0TimeState* state = c0_time_from_dyn(self);
  uint64_t ticks = 0;
  C0U128 value;
  if (!out) {
    return;
  }
  if (!state || state->kind != C0_TIME_STATE_MONOTONIC ||
      !c0_platform_monotonic_now_ns(&ticks)) {
    out->domain = 0;
    out->ticks = c0_u128_zero();
    return;
  }
  value = c0_u128_floor_to_resolution(c0_u128_from_u64(ticks),
                                      state->resolution);
  out->domain = state->domain;
  out->ticks = value;
}

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5fresolution(
    C0Duration* out,
    const C0DynObject* self) {
  C0TimeState* state = c0_time_from_dyn(self);
  if (!state || state->kind != C0_TIME_STATE_MONOTONIC) {
    c0_duration_write(out, c0_u128_zero());
    return;
  }
  c0_duration_write(out, state->resolution);
}

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5felapsed(
    C0Union_Duration_TimeError* out,
    const C0DynObject* self,
    const C0MonotonicInstant* start,
    const C0MonotonicInstant* end) {
  C0TimeState* state = c0_time_from_dyn(self);
  if (!state || state->kind != C0_TIME_STATE_MONOTONIC || !start || !end ||
      start->domain != state->domain || end->domain != state->domain) {
    c0_duration_err(out, C0_TIME_CLOCK_MISMATCH);
    return;
  }
  if (c0_u128_cmp(end->ticks, start->ticks) < 0) {
    c0_duration_err(out, C0_TIME_OUT_OF_RANGE);
    return;
  }
  c0_duration_ok(out, c0_u128_sub(end->ticks, start->ticks));
}

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5fcoarsen(
    C0Union_DynObject_TimeError* out,
    const C0DynObject* self,
    const C0Duration* resolution) {
  C0TimeState* state = c0_time_from_dyn(self);
  C0U128 requested;
  C0TimeState* child;
  if (!state || state->kind != C0_TIME_STATE_MONOTONIC || !resolution) {
    c0_dyn_err(out, C0_TIME_CLOCK_UNAVAILABLE);
    return;
  }
  requested = resolution->nanoseconds;
  if (c0_u128_is_zero(requested)) {
    c0_dyn_err(out, C0_TIME_INVALID_RESOLUTION);
    return;
  }
  child = c0_time_child(state,
                       C0_TIME_STATE_MONOTONIC,
                       c0_u128_max(state->resolution, requested));
  if (!child) {
    c0_dyn_err(out, C0_TIME_UNSUPPORTED);
    return;
  }
  c0_dyn_ok(out, c0_time_dyn(child));
}

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3awall_x5fnow_x5futc(
    C0Union_UtcInstant_TimeError* out,
    const C0DynObject* self) {
  C0TimeState* state = c0_time_from_dyn(self);
  C0I128 value;
  if (!state || state->kind != C0_TIME_STATE_WALL) {
    c0_utc_err(out, C0_TIME_CLOCK_UNAVAILABLE);
    return;
  }
  if (!c0_platform_wall_now_unix_ns(&value)) {
    c0_utc_err(out, C0_TIME_CLOCK_UNAVAILABLE);
    return;
  }
  value = c0_u128_floor_to_resolution(value, state->resolution);
  c0_utc_ok(out, value);
}

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3awall_x5fresolution(
    C0Union_Duration_TimeError* out,
    const C0DynObject* self) {
  C0TimeState* state = c0_time_from_dyn(self);
  if (!state || state->kind != C0_TIME_STATE_WALL) {
    c0_duration_err(out, C0_TIME_CLOCK_UNAVAILABLE);
    return;
  }
  c0_duration_ok(out, state->resolution);
}

void cursive_x3a_x3aruntime_x3a_x3atime_x3a_x3awall_x5fcoarsen(
    C0Union_DynObject_TimeError* out,
    const C0DynObject* self,
    const C0Duration* resolution) {
  C0TimeState* state = c0_time_from_dyn(self);
  C0U128 requested;
  C0TimeState* child;
  if (!state || state->kind != C0_TIME_STATE_WALL || !resolution) {
    c0_dyn_err(out, C0_TIME_CLOCK_UNAVAILABLE);
    return;
  }
  requested = resolution->nanoseconds;
  if (c0_u128_is_zero(requested)) {
    c0_dyn_err(out, C0_TIME_INVALID_RESOLUTION);
    return;
  }
  child = c0_time_child(state,
                       C0_TIME_STATE_WALL,
                       c0_u128_max(state->resolution, requested));
  if (!child) {
    c0_dyn_err(out, C0_TIME_UNSUPPORTED);
    return;
  }
  c0_dyn_ok(out, c0_time_dyn(child));
}
