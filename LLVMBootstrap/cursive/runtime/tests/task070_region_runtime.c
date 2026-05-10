#include "cursive_rt.h"

#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int report_failure(const char* label) {
  fprintf(stderr, "TASK-070 runtime regression failed: %s\n", label);
  return 1;
}

static C0Region make_region(void) {
  C0RegionOptions options;
  memset(&options, 0, sizeof(options));
  return cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3anew_x5fscoped(&options);
}

static int32_t* alloc_i32(const C0Region* region, int32_t value) {
  int32_t* ptr = (int32_t*)cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3aalloc(
      region, sizeof(int32_t), alignof(int32_t));
  if (ptr) {
    *ptr = value;
  }
  return ptr;
}

static int expect_active(const void* addr, int expected, const char* label) {
  const int actual =
      (int)cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3aaddr_x5fis_x5factive(addr);
  if (actual != expected) {
    return report_failure(label);
  }
  return 0;
}

static int expect_nonnull(const void* ptr, const char* label) {
  if (!ptr) {
    return report_failure(label);
  }
  return 0;
}

static int test_frame_reset_preserves_outer_and_expires_inner(void) {
  C0Region region = make_region();
  int32_t* outer = alloc_i32(&region, 11);
  if (expect_nonnull(outer, "frame-reset outer alloc")) {
    return 1;
  }

  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fenter(1001);
  const uint64_t mark =
      cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3amark(&region);
  int32_t* inner = alloc_i32(&region, 13);
  if (expect_nonnull(inner, "frame-reset inner alloc")) {
    return 1;
  }

  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3areset_x5fto(&region, mark);
  if (expect_active(outer, 1, "frame-reset outer stays active")) {
    return 1;
  }
  if (expect_active(inner, 0, "frame-reset inner expires")) {
    return 1;
  }

  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fexit(1001);

  // A newly opened region must not accidentally reactivate the expired inner
  // pointer by reusing its old region tag as the new base-region handle.
  C0Region next_region = make_region();
  if (expect_active(inner, 0, "new region does not reactivate expired frame ptr")) {
    return 1;
  }

  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3afree_x5funchecked(&next_region);
  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3afree_x5funchecked(&region);
  return 0;
}

static int test_reset_unchecked_retags_all_live_entries(void) {
  C0Region region = make_region();
  int32_t* outer = alloc_i32(&region, 17);
  if (expect_nonnull(outer, "reset-unchecked outer alloc")) {
    return 1;
  }

  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fenter(2001);
  const uint64_t mark =
      cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3amark(&region);
  int32_t* inner = alloc_i32(&region, 19);
  if (expect_nonnull(inner, "reset-unchecked inner alloc")) {
    return 1;
  }

  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3areset_x5funchecked(&region);
  if (expect_active(outer, 0, "reset-unchecked outer expires")) {
    return 1;
  }
  if (expect_active(inner, 0, "reset-unchecked inner expires")) {
    return 1;
  }

  int32_t* post_reset = alloc_i32(&region, 23);
  if (expect_nonnull(post_reset, "reset-unchecked post-reset alloc")) {
    return 1;
  }
  if (post_reset == outer || post_reset == inner) {
    return report_failure("reset-unchecked fresh alloc reused expired address");
  }
  if (expect_active(post_reset, 1, "reset-unchecked post-reset alloc active")) {
    return 1;
  }
  if (expect_active(outer, 0, "reset-unchecked outer stays expired after fresh alloc")) {
    return 1;
  }
  if (expect_active(inner, 0, "reset-unchecked inner stays expired after fresh alloc")) {
    return 1;
  }

  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3areset_x5fto(&region, mark);
  if (expect_active(post_reset, 0, "frame cleanup after reset-unchecked expires post-reset alloc")) {
    return 1;
  }
  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fexit(2001);
  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3afree_x5funchecked(&region);
  return 0;
}

static int test_free_unchecked_pops_all_region_entries(void) {
  C0Region region = make_region();
  int32_t* outer = alloc_i32(&region, 29);
  if (expect_nonnull(outer, "free-unchecked outer alloc")) {
    return 1;
  }

  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fenter(3001);
  (void)cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3amark(&region);
  int32_t* inner = alloc_i32(&region, 31);
  if (expect_nonnull(inner, "free-unchecked inner alloc")) {
    return 1;
  }

  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3afree_x5funchecked(&region);
  if (expect_active(outer, 0, "free-unchecked outer expires")) {
    return 1;
  }
  if (expect_active(inner, 0, "free-unchecked inner expires")) {
    return 1;
  }

  if (cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3aalloc(
          &region, sizeof(int32_t), alignof(int32_t)) != NULL) {
    return report_failure("free-unchecked rejects further alloc");
  }

  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3ascope_x5fexit(3001);
  return 0;
}

static int test_freeze_thaw_leave_runtime_sigma_unchanged(void) {
  C0Region region = make_region();
  int32_t* first = alloc_i32(&region, 37);
  if (expect_nonnull(first, "freeze first alloc")) {
    return 1;
  }

  C0Region frozen = cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3afreeze(&region);
  int32_t* second = alloc_i32(&region, 41);
  if (expect_nonnull(second, "freeze preserves runtime state")) {
    return 1;
  }

  C0Region thawed = cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3athaw(&frozen);
  int32_t* third = alloc_i32(&thawed, 43);
  if (expect_nonnull(third, "thaw preserves runtime state")) {
    return 1;
  }

  if (expect_active(first, 1, "freeze/thaw keep first alloc active")) {
    return 1;
  }
  if (expect_active(second, 1, "freeze/thaw keep second alloc active")) {
    return 1;
  }
  if (expect_active(third, 1, "freeze/thaw keep third alloc active")) {
    return 1;
  }

  cursive_x3a_x3aruntime_x3a_x3aregion_x3a_x3afree_x5funchecked(&region);
  if (expect_active(first, 0, "free after freeze/thaw expires first alloc")) {
    return 1;
  }
  if (expect_active(second, 0, "free after freeze/thaw expires second alloc")) {
    return 1;
  }
  if (expect_active(third, 0, "free after freeze/thaw expires third alloc")) {
    return 1;
  }
  return 0;
}

int main(void) {
  if (test_frame_reset_preserves_outer_and_expires_inner()) {
    return 1;
  }
  if (test_reset_unchecked_retags_all_live_entries()) {
    return 1;
  }
  if (test_free_unchecked_pops_all_region_entries()) {
    return 1;
  }
  if (test_freeze_thaw_leave_runtime_sigma_unchanged()) {
    return 1;
  }

  puts("TASK-070 runtime regression passed.");
  return 0;
}
