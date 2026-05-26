#include "../../internal/rt_startup.h"

static uv_rt_process_start_t g_uv_rt_process_start = {0};

void uv_rt_startup_record(const uv_rt_process_start_t* start) {
  if (!start) {
    g_uv_rt_process_start.argc = 0;
    g_uv_rt_process_start.argv = NULL;
    g_uv_rt_process_start.envp = NULL;
    g_uv_rt_process_start.auxv = NULL;
    return;
  }

  g_uv_rt_process_start = *start;
}

const uv_rt_process_start_t* uv_rt_startup_current(void) {
  return &g_uv_rt_process_start;
}
