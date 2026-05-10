#ifndef CURSIVE_RT_STARTUP_H
#define CURSIVE_RT_STARTUP_H

#include "rt_platform.h"

typedef struct cursive_rt_process_start_t {
  int argc;
  char** argv;
  char** envp;
  const void* auxv;
} cursive_rt_process_start_t;

void cursive_rt_startup_record(const cursive_rt_process_start_t* start);
const cursive_rt_process_start_t* cursive_rt_startup_current(void);

static __inline void cursive_rt_startup_exit(int exit_code) {
  cursive_rt_exit_process((cursive_rt_uint_t)exit_code);
}

#endif  // CURSIVE_RT_STARTUP_H
