#include "../internal/rt_internal.h"

#if defined(UV_RT_PLATFORM_LINUX)
#include <time.h>
#elif defined(UV_RT_PLATFORM_WINDOWS)
#include <float.h>
#endif

static UVU128 uv_u128_zero(void) {
  UVU128 out;
  out.lo = 0;
  out.hi = 0;
  return out;
}

static UVU128 uv_u128_from_u64(uint64_t value) {
  UVU128 out;
  out.lo = value;
  out.hi = 0;
  return out;
}

static int uv_u128_is_zero(UVU128 value) {
  return value.lo == 0 && value.hi == 0;
}

static int uv_u128_cmp(UVU128 lhs, UVU128 rhs) {
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

static UVU128 uv_u128_sub(UVU128 lhs, UVU128 rhs) {
  UVU128 out;
  out.lo = lhs.lo - rhs.lo;
  out.hi = lhs.hi - rhs.hi - (lhs.lo < rhs.lo ? 1u : 0u);
  return out;
}

static UVU128 uv_u128_max(UVU128 lhs, UVU128 rhs) {
  return uv_u128_cmp(lhs, rhs) >= 0 ? lhs : rhs;
}

static UVU128 uv_u128_floor_to_resolution(UVU128 value, UVU128 resolution) {
  if (uv_u128_is_zero(resolution) ||
      (resolution.hi == 0 && resolution.lo <= 1)) {
    return value;
  }
  if (resolution.hi != 0) {
    return uv_u128_cmp(value, resolution) < 0 ? uv_u128_zero() : value;
  }
  value.lo = (value.lo / resolution.lo) * resolution.lo;
  return value;
}

static UVTimeState* uv_time_from_dyn(const UVDynObject* cap) {
  if (!cap) {
    return NULL;
  }
  return (UVTimeState*)cap->data;
}

static UVDynObject uv_time_dyn(UVTimeState* state) {
  UVDynObject out;
  out.data = state;
  out.vtable = NULL;
  return out;
}

static UVTimeState* uv_time_child(UVTimeState* parent,
                                  uint8_t kind,
                                  UVU128 resolution) {
  UVTimeState* state = (UVTimeState*)uv_heap_alloc_raw(sizeof(UVTimeState));
  if (!state) {
    return NULL;
  }
  state->parent = parent;
  state->kind = kind;
  state->domain = parent ? parent->domain : 1;
  state->resolution = resolution;
  return state;
}

static int uv_platform_monotonic_now_ns(uint64_t* out) {
  if (!out) {
    return 0;
  }
#if defined(UV_RT_PLATFORM_LINUX)
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  *out = ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
  return 1;
#elif defined(UV_RT_PLATFORM_WINDOWS)
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

static int uv_platform_monotonic_resolution_ns(uint64_t* out) {
  if (!out) {
    return 0;
  }
#if defined(UV_RT_PLATFORM_LINUX)
  struct timespec ts;
  if (clock_getres(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  *out = ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
  if (*out == 0) {
    *out = 1;
  }
  return 1;
#elif defined(UV_RT_PLATFORM_WINDOWS)
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

static int uv_platform_wall_now_unix_ns(UVI128* out) {
  if (!out) {
    return 0;
  }
#if defined(UV_RT_PLATFORM_LINUX)
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0;
  }
  out->lo = ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
  out->hi = 0;
  return 1;
#elif defined(UV_RT_PLATFORM_WINDOWS)
  static const uint64_t kUnixEpochFiletime = 116444736000000000ULL;
  uv_rt_filetime_t ft;
  uint64_t ticks;
  uv_rt_system_time_filetime(&ft);
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

static int uv_platform_wall_resolution_ns(UVU128* out) {
  if (!out) {
    return 0;
  }
#if defined(UV_RT_PLATFORM_LINUX)
  struct timespec ts;
  if (clock_getres(CLOCK_REALTIME, &ts) != 0) {
    return 0;
  }
  *out = uv_u128_from_u64(((uint64_t)ts.tv_sec * 1000000000ULL) +
                          (uint64_t)ts.tv_nsec);
  if (uv_u128_is_zero(*out)) {
    *out = uv_u128_from_u64(1);
  }
  return 1;
#elif defined(UV_RT_PLATFORM_WINDOWS)
  *out = uv_u128_from_u64(100);
  return 1;
#else
  return 0;
#endif
}

static void uv_duration_write(UVDuration* out, UVU128 value) {
  if (out) {
    out->nanoseconds = value;
  }
}

static void uv_duration_ok(UVUnion_Duration_TimeError* out, UVU128 value) {
  if (!out) {
    return;
  }
  out->disc = 0;
  out->payload.value.nanoseconds = value;
}

static void uv_duration_err(UVUnion_Duration_TimeError* out, UVTimeError err) {
  if (!out) {
    return;
  }
  out->disc = 1;
  out->payload.time_error = err;
}

static void uv_dyn_ok(UVUnion_DynObject_TimeError* out, UVDynObject value) {
  if (!out) {
    return;
  }
  out->disc = 0;
  out->payload.value = value;
}

static void uv_dyn_err(UVUnion_DynObject_TimeError* out, UVTimeError err) {
  if (!out) {
    return;
  }
  out->disc = 1;
  out->payload.time_error = err;
}

static void uv_utc_ok(UVUnion_UtcInstant_TimeError* out, UVI128 value) {
  if (!out) {
    return;
  }
  out->disc = 0;
  out->payload.value.unix_nanoseconds = value;
}

static void uv_utc_err(UVUnion_UtcInstant_TimeError* out, UVTimeError err) {
  if (!out) {
    return;
  }
  out->disc = 1;
  out->payload.time_error = err;
}

UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic(
    UVDynObject self) {
  UVTimeState* root = uv_time_from_dyn(&self);
  uint64_t resolution_ns = 1;
  UVTimeState* state;
  if (!root) {
    return uv_time_dyn(NULL);
  }
  if (!uv_platform_monotonic_resolution_ns(&resolution_ns)) {
    resolution_ns = 1;
  }
  state = uv_time_child(root, UV_TIME_STATE_MONOTONIC,
                        uv_u128_from_u64(resolution_ns));
  return uv_time_dyn(state);
}

UVDynObject ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3awall(
    UVDynObject self) {
  UVTimeState* root = uv_time_from_dyn(&self);
  UVU128 resolution = uv_u128_from_u64(1);
  UVTimeState* state;
  if (!root) {
    return uv_time_dyn(NULL);
  }
  (void)uv_platform_wall_resolution_ns(&resolution);
  state = uv_time_child(root, UV_TIME_STATE_WALL, resolution);
  return uv_time_dyn(state);
}

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5fnow(
    UVMonotonicInstant* out,
    UVDynObject self) {
  UVTimeState* state = uv_time_from_dyn(&self);
  uint64_t ticks = 0;
  UVU128 value;
  if (!out) {
    return;
  }
  if (!state || state->kind != UV_TIME_STATE_MONOTONIC ||
      !uv_platform_monotonic_now_ns(&ticks)) {
    out->domain = 0;
    out->ticks = uv_u128_zero();
    return;
  }
  value = uv_u128_floor_to_resolution(uv_u128_from_u64(ticks),
                                      state->resolution);
  out->domain = state->domain;
  out->ticks = value;
}

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5fresolution(
    UVDuration* out,
    UVDynObject self) {
  UVTimeState* state = uv_time_from_dyn(&self);
  if (!state || state->kind != UV_TIME_STATE_MONOTONIC) {
    uv_duration_write(out, uv_u128_zero());
    return;
  }
  uv_duration_write(out, state->resolution);
}

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5felapsed(
    UVUnion_Duration_TimeError* out,
    UVDynObject self,
    const UVMonotonicInstant* start,
    const UVMonotonicInstant* end) {
  UVTimeState* state = uv_time_from_dyn(&self);
  if (!state || state->kind != UV_TIME_STATE_MONOTONIC || !start || !end ||
      start->domain != state->domain || end->domain != state->domain) {
    uv_duration_err(out, UV_TIME_CLOCK_MISMATCH);
    return;
  }
  if (uv_u128_cmp(end->ticks, start->ticks) < 0) {
    uv_duration_err(out, UV_TIME_OUT_OF_RANGE);
    return;
  }
  uv_duration_ok(out, uv_u128_sub(end->ticks, start->ticks));
}

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3amonotonic_x5fcoarsen(
    UVUnion_DynObject_TimeError* out,
    UVDynObject self,
    const UVDuration* resolution) {
  UVTimeState* state = uv_time_from_dyn(&self);
  UVU128 requested;
  UVTimeState* child;
  if (!state || state->kind != UV_TIME_STATE_MONOTONIC || !resolution) {
    uv_dyn_err(out, UV_TIME_CLOCK_UNAVAILABLE);
    return;
  }
  requested = resolution->nanoseconds;
  if (uv_u128_is_zero(requested)) {
    uv_dyn_err(out, UV_TIME_INVALID_RESOLUTION);
    return;
  }
  child = uv_time_child(state,
                       UV_TIME_STATE_MONOTONIC,
                       uv_u128_max(state->resolution, requested));
  if (!child) {
    uv_dyn_err(out, UV_TIME_UNSUPPORTED);
    return;
  }
  uv_dyn_ok(out, uv_time_dyn(child));
}

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3awall_x5fnow_x5futc(
    UVUnion_UtcInstant_TimeError* out,
    UVDynObject self) {
  UVTimeState* state = uv_time_from_dyn(&self);
  UVI128 value;
  if (!state || state->kind != UV_TIME_STATE_WALL) {
    uv_utc_err(out, UV_TIME_CLOCK_UNAVAILABLE);
    return;
  }
  if (!uv_platform_wall_now_unix_ns(&value)) {
    uv_utc_err(out, UV_TIME_CLOCK_UNAVAILABLE);
    return;
  }
  value = uv_u128_floor_to_resolution(value, state->resolution);
  uv_utc_ok(out, value);
}

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3awall_x5fresolution(
    UVUnion_Duration_TimeError* out,
    UVDynObject self) {
  UVTimeState* state = uv_time_from_dyn(&self);
  if (!state || state->kind != UV_TIME_STATE_WALL) {
    uv_duration_err(out, UV_TIME_CLOCK_UNAVAILABLE);
    return;
  }
  uv_duration_ok(out, state->resolution);
}

void ultraviolet_x3a_x3aruntime_x3a_x3atime_x3a_x3awall_x5fcoarsen(
    UVUnion_DynObject_TimeError* out,
    UVDynObject self,
    const UVDuration* resolution) {
  UVTimeState* state = uv_time_from_dyn(&self);
  UVU128 requested;
  UVTimeState* child;
  if (!state || state->kind != UV_TIME_STATE_WALL || !resolution) {
    uv_dyn_err(out, UV_TIME_CLOCK_UNAVAILABLE);
    return;
  }
  requested = resolution->nanoseconds;
  if (uv_u128_is_zero(requested)) {
    uv_dyn_err(out, UV_TIME_INVALID_RESOLUTION);
    return;
  }
  child = uv_time_child(state,
                       UV_TIME_STATE_WALL,
                       uv_u128_max(state->resolution, requested));
  if (!child) {
    uv_dyn_err(out, UV_TIME_UNSUPPORTED);
    return;
  }
  uv_dyn_ok(out, uv_time_dyn(child));
}
