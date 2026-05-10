#include "../../internal/rt_startup.h"

static cursive_rt_process_start_t g_cursive_rt_process_start = {0};

void cursive_rt_startup_record(const cursive_rt_process_start_t* start) {
  if (!start) {
    g_cursive_rt_process_start.argc = 0;
    g_cursive_rt_process_start.argv = NULL;
    g_cursive_rt_process_start.envp = NULL;
    g_cursive_rt_process_start.auxv = NULL;
    return;
  }

  g_cursive_rt_process_start = *start;
}

const cursive_rt_process_start_t* cursive_rt_startup_current(void) {
  return &g_cursive_rt_process_start;
}
