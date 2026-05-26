#ifndef UV_RT_STARTUP_H
#define UV_RT_STARTUP_H

#include "rt_platform.h"

typedef struct uv_rt_process_start_t {
  int argc;
  char** argv;
  char** envp;
  const void* auxv;
} uv_rt_process_start_t;

void uv_rt_startup_record(const uv_rt_process_start_t* start);
const uv_rt_process_start_t* uv_rt_startup_current(void);

static __inline void uv_rt_startup_exit(int exit_code) {
  uv_rt_exit_process((uv_rt_uint_t)exit_code);
}

#endif  // UV_RT_STARTUP_H
