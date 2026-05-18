#if !defined(_WIN32) && defined(__x86_64__)

#include "../../internal/rt_startup.h"

#include <stdint.h>

extern int main(void);

int uv_rt_linux_startup_entry(void* initial_stack) {
  uintptr_t* words = (uintptr_t*)initial_stack;
  uv_rt_process_start_t start;
  char** argv = NULL;
  char** envp = NULL;
  char** auxv = NULL;

  start.argc = 0;
  start.argv = NULL;
  start.envp = NULL;
  start.auxv = NULL;

  if (words != NULL) {
    start.argc = (int)words[0];
    if (start.argc < 0) {
      start.argc = 0;
    }
    argv = (char**)&words[1];
    envp = argv + start.argc + 1;
    auxv = envp;
    while (*auxv != NULL) {
      ++auxv;
    }
    start.argv = argv;
    start.envp = envp;
    start.auxv = (const void*)(auxv + 1);
  }

  uv_rt_startup_record(&start);
  return main();
}

__asm__(
    ".text\n"
    ".globl _start\n"
    ".type _start,@function\n"
    "_start:\n"
    "  xor %ebp, %ebp\n"
    "  mov %rsp, %rdi\n"
    "  andq $-16, %rsp\n"
    "  call uv_rt_linux_startup_entry\n"
    "  mov %eax, %edi\n"
    "  call ExitProcess\n"
    "  ud2\n"
    ".size _start, .-_start\n");

#endif
