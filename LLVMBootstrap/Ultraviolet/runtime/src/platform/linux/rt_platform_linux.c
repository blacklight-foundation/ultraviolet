#define _GNU_SOURCE

#include "rt_platform_linux_compat.h"
#include "../../internal/rt_startup.h"

#ifndef _WIN32

#include <ctype.h>
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <linux/futex.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <unicode/putil.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define UV_HEAP_MAGIC 0xC0DEC0DE51A7EULL

enum UVRtHandleKind {
  UV_RT_HANDLE_HEAP = 1,
  UV_RT_HANDLE_PROCESS = 2,
  UV_RT_HANDLE_THREAD = 3,
  UV_RT_HANDLE_EVENT = 4,
  UV_RT_HANDLE_FILE = 5,
  UV_RT_HANDLE_FIND = 6,
  UV_RT_HANDLE_MAPPING = 7,
  UV_RT_HANDLE_TOKEN = 8,
  UV_RT_HANDLE_PSEUDO_THREAD = 9,
};

typedef struct UVHeapBlockHeader {
  uint64_t magic;
  size_t size;
  size_t mapping_size;
} UVHeapBlockHeader;

typedef struct UVMapViewNode {
  void* address;
  size_t size;
  struct UVMapViewNode* next;
} UVMapViewNode;

struct UVRtHandleBase {
  int kind;
  int owned;
  union {
    struct {
      pid_t pid;
      int waited;
      DWORD exit_code;
    } process;
    struct {
      pthread_t thread;
      pid_t tid;
      CRITICAL_SECTION lock;
      CONDITION_VARIABLE cond;
      DWORD logical_id;
      DWORD exit_code;
      int finished;
      int joined;
      int priority;
    } thread;
    struct {
      CRITICAL_SECTION mutex;
      CONDITION_VARIABLE cond;
      int manual_reset;
      int signaled;
    } event;
    struct {
      int fd;
    } file;
    struct {
      DIR* dir;
      char* dir_path;
      char* pattern;
    } find;
    struct {
      int fd;
      size_t size;
    } mapping;
  } u;
};

static struct UVRtHandleBase uv_rt_linux_process_heap_handle = { UV_RT_HANDLE_HEAP, 0, { 0 } };
static struct UVRtHandleBase uv_rt_linux_pseudo_thread_handle = { UV_RT_HANDLE_PSEUDO_THREAD, 0, { 0 } };
static struct UVRtHandleBase uv_rt_linux_stdin_handle = { UV_RT_HANDLE_FILE, 0, { .file = { STDIN_FILENO } } };
static struct UVRtHandleBase uv_rt_linux_stdout_handle = { UV_RT_HANDLE_FILE, 0, { .file = { STDOUT_FILENO } } };
static struct UVRtHandleBase uv_rt_linux_stderr_handle = { UV_RT_HANDLE_FILE, 0, { .file = { STDERR_FILENO } } };

static HANDLE uv_rt_linux_std_input = &uv_rt_linux_stdin_handle;
static HANDLE uv_rt_linux_std_output = &uv_rt_linux_stdout_handle;
static HANDLE uv_rt_linux_std_error = &uv_rt_linux_stderr_handle;

static CRITICAL_SECTION uv_rt_linux_std_handle_lock = { 0u };
static CRITICAL_SECTION uv_rt_linux_tls_slot_lock = { 0u };
static CRITICAL_SECTION uv_rt_linux_map_view_lock = { 0u };
static int uv_rt_linux_tls_slot_used[128] = { 0 };
static UVMapViewNode* uv_rt_linux_map_views = NULL;
static atomic_uint uv_rt_linux_next_thread_id = ATOMIC_VAR_INIT(1u);

typedef struct UVRtLinuxPanicScope {
  jmp_buf jump_env;
  struct UVRtLinuxPanicScope* prev;
  uv_platform_u32_t panic_code;
} UVRtLinuxPanicScope;

typedef struct UVRtLinuxThreadState {
  pid_t kernel_tid;
  DWORD logical_id;
  int priority;
  DWORD last_error;
  void* tls_values[128];
  UVRtLinuxPanicScope* panic_scope;
  int active;
} UVRtLinuxThreadState;

static CRITICAL_SECTION uv_rt_linux_thread_state_lock = { 0u };
static UVRtLinuxThreadState uv_rt_linux_thread_states[256] = { 0 };
static atomic_int uv_rt_linux_icu_data_configured = ATOMIC_VAR_INIT(0);

static void uv_rt_linux_set_errno_error(int err);

static pid_t uv_rt_linux_current_kernel_tid(void) {
  return (pid_t)syscall(SYS_gettid);
}

static UVRtLinuxThreadState* uv_rt_linux_find_thread_state_locked(
    pid_t kernel_tid) {
  size_t i;
  for (i = 0u; i < (sizeof(uv_rt_linux_thread_states) /
                     sizeof(uv_rt_linux_thread_states[0]));
       ++i) {
    if (uv_rt_linux_thread_states[i].active &&
        uv_rt_linux_thread_states[i].kernel_tid == kernel_tid) {
      return &uv_rt_linux_thread_states[i];
    }
  }
  return NULL;
}

static UVRtLinuxThreadState* uv_rt_linux_current_thread_state(
    int create_if_missing) {
  UVRtLinuxThreadState* state;
  pid_t kernel_tid;
  size_t i;

  kernel_tid = uv_rt_linux_current_kernel_tid();
  EnterCriticalSection(&uv_rt_linux_thread_state_lock);
  state = uv_rt_linux_find_thread_state_locked(kernel_tid);
  if (state || !create_if_missing) {
    LeaveCriticalSection(&uv_rt_linux_thread_state_lock);
    return state;
  }

  for (i = 0u; i < (sizeof(uv_rt_linux_thread_states) /
                     sizeof(uv_rt_linux_thread_states[0]));
       ++i) {
    if (!uv_rt_linux_thread_states[i].active) {
      state = &uv_rt_linux_thread_states[i];
      memset(state, 0, sizeof(*state));
      state->active = 1;
      state->kernel_tid = kernel_tid;
      state->priority = THREAD_PRIORITY_NORMAL;
      state->last_error = ERROR_SUCCESS;
      LeaveCriticalSection(&uv_rt_linux_thread_state_lock);
      return state;
    }
  }

  LeaveCriticalSection(&uv_rt_linux_thread_state_lock);
  return NULL;
}

static void uv_rt_linux_release_current_thread_state(void) {
  UVRtLinuxThreadState* state;

  EnterCriticalSection(&uv_rt_linux_thread_state_lock);
  state = uv_rt_linux_find_thread_state_locked(
      uv_rt_linux_current_kernel_tid());
  if (state) {
    memset(state, 0, sizeof(*state));
  }
  LeaveCriticalSection(&uv_rt_linux_thread_state_lock);
}

static int uv_rt_linux_file_exists(const char* path) {
  struct stat st;
  if (!path || path[0] == '\0') {
    return 0;
  }
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int uv_rt_linux_data_directory_from_module(char* out_dir,
                                                       size_t out_dir_size) {
  Dl_info info;
  char module_path[PATH_MAX];
  char* slash = NULL;
  const char* candidates[3];
  char candidate_path[PATH_MAX];
  size_t i = 0u;

  if (!out_dir || out_dir_size == 0u) {
    return 0;
  }

  memset(&info, 0, sizeof(info));
  memset(module_path, 0, sizeof(module_path));
  if (dladdr((void*)&uv_platform_backend_icu_data_configure, &info) != 0 &&
      info.dli_fname && info.dli_fname[0] != '\0') {
    strncpy(module_path, info.dli_fname, sizeof(module_path) - 1u);
  } else {
    const ssize_t length =
        readlink("/proc/self/exe", module_path, sizeof(module_path) - 1u);
    if (length <= 0) {
      return 0;
    }
    module_path[(size_t)length] = '\0';
  }

  slash = strrchr(module_path, '/');
  if (!slash) {
    return 0;
  }
  *slash = '\0';

  candidates[0] = module_path;
  if (snprintf(candidate_path,
               sizeof(candidate_path),
               "%s/lib",
               module_path) < 0) {
    return 0;
  }
  candidates[1] = candidate_path;

  for (i = 0u; i < 2u; ++i) {
    char data_path[PATH_MAX];
    if (!candidates[i] || candidates[i][0] == '\0') {
      continue;
    }
    if (snprintf(data_path,
                 sizeof(data_path),
                 "%s/icudt72l.dat",
                 candidates[i]) < 0) {
      continue;
    }
    if (!uv_rt_linux_file_exists(data_path)) {
      continue;
    }
    strncpy(out_dir, candidates[i], out_dir_size - 1u);
    out_dir[out_dir_size - 1u] = '\0';
    return 1;
  }

  return 0;
}

static size_t uv_rt_linux_page_size(void) {
  static size_t cached_page_size = 0u;
  if (cached_page_size == 0u) {
    long value = sysconf(_SC_PAGESIZE);
    cached_page_size = value > 0 ? (size_t)value : 4096u;
  }
  return cached_page_size;
}

static int uv_rt_linux_size_add(size_t lhs, size_t rhs, size_t* out) {
  if (!out || lhs > (SIZE_MAX - rhs)) {
    return 0;
  }
  *out = lhs + rhs;
  return 1;
}

static int uv_rt_linux_size_mul(size_t lhs, size_t rhs, size_t* out) {
  if (!out) {
    return 0;
  }
  if (lhs != 0u && rhs > (SIZE_MAX / lhs)) {
    return 0;
  }
  *out = lhs * rhs;
  return 1;
}

static size_t uv_rt_linux_heap_payload_size(size_t requested) {
  return requested == 0u ? 1u : requested;
}

static int uv_rt_linux_round_mapping_size(size_t bytes, size_t* out) {
  const size_t page_size = uv_rt_linux_page_size();
  const size_t remainder = bytes % page_size;
  size_t rounded = bytes;
  if (!out) {
    return 0;
  }
  if (remainder != 0u) {
    if (!uv_rt_linux_size_add(bytes, page_size - remainder, &rounded)) {
      return 0;
    }
  }
  *out = rounded;
  return 1;
}

static UVHeapBlockHeader* uv_rt_linux_heap_header(const void* memory) {
  if (!memory) {
    return NULL;
  }
  return ((UVHeapBlockHeader*)memory) - 1;
}

static int uv_rt_linux_heap_header_valid(const UVHeapBlockHeader* header) {
  size_t minimum_payload = 0u;
  size_t minimum_mapping = 0u;
  if (!header || header->magic != UV_HEAP_MAGIC) {
    return 0;
  }
  minimum_payload = uv_rt_linux_heap_payload_size(header->size);
  if (!uv_rt_linux_size_add(sizeof(UVHeapBlockHeader),
                                 minimum_payload,
                                 &minimum_mapping)) {
    return 0;
  }
  return header->mapping_size >= minimum_mapping;
}

static void* uv_rt_linux_heap_alloc_unchecked(size_t bytes) {
  const size_t payload_size = uv_rt_linux_heap_payload_size(bytes);
  size_t minimum_mapping = 0u;
  size_t mapping_size = 0u;
  UVHeapBlockHeader* header = NULL;
  if (!uv_rt_linux_size_add(sizeof(UVHeapBlockHeader),
                                 payload_size,
                                 &minimum_mapping) ||
      !uv_rt_linux_round_mapping_size(minimum_mapping, &mapping_size)) {
    return NULL;
  }

  header = (UVHeapBlockHeader*)mmap(NULL,
                                    mapping_size,
                                    PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS,
                                    -1,
                                    0);
  if (header == MAP_FAILED) {
    return NULL;
  }
  header->magic = UV_HEAP_MAGIC;
  header->size = bytes;
  header->mapping_size = mapping_size;
  return (void*)(header + 1);
}

static void* uv_rt_linux_heap_calloc_unchecked(size_t count, size_t size) {
  size_t bytes = 0u;
  if (!uv_rt_linux_size_mul(count, size, &bytes)) {
    return NULL;
  }
  return uv_rt_linux_heap_alloc_unchecked(bytes);
}

static void uv_rt_linux_heap_free_unchecked(void* memory) {
  UVHeapBlockHeader* header = uv_rt_linux_heap_header(memory);
  if (!header || !uv_rt_linux_heap_header_valid(header)) {
    return;
  }
  header->magic = 0u;
  munmap(header, header->mapping_size);
}

static int uv_rt_linux_futex_wait(volatile uint32_t* address,
                                       uint32_t expected,
                                       const struct timespec* timeout) {
  return (int)syscall(SYS_futex,
                      (uint32_t*)address,
                      FUTEX_WAIT_PRIVATE,
                      expected,
                      timeout,
                      NULL,
                      0);
}

static int uv_rt_linux_futex_wake(volatile uint32_t* address,
                                       int wake_count) {
  return (int)syscall(SYS_futex,
                      (uint32_t*)address,
                      FUTEX_WAKE_PRIVATE,
                      wake_count,
                      NULL,
                      NULL,
                      0);
}

static int uv_rt_linux_timespec_from_millis(DWORD milliseconds,
                                                 struct timespec* timeout) {
  if (!timeout) {
    return 0;
  }
  timeout->tv_sec = (time_t)(milliseconds / 1000u);
  timeout->tv_nsec = (long)((milliseconds % 1000u) * 1000000u);
  return 1;
}

static void uv_rt_linux_mutex_lock(CRITICAL_SECTION* section) {
  uint32_t previous = 0u;
  if (!section) {
    return;
  }
  if (__atomic_compare_exchange_n(&section->state,
                                  &previous,
                                  1u,
                                  0,
                                  __ATOMIC_ACQUIRE,
                                  __ATOMIC_RELAXED)) {
    return;
  }
  for (;;) {
    previous = __atomic_exchange_n(&section->state, 2u, __ATOMIC_ACQUIRE);
    if (previous == 0u) {
      return;
    }
    while (__atomic_load_n(&section->state, __ATOMIC_RELAXED) == 2u) {
      if (uv_rt_linux_futex_wait(&section->state, 2u, NULL) < 0 &&
          errno != EAGAIN &&
          errno != EINTR) {
        break;
      }
    }
  }
}

static void uv_rt_linux_mutex_unlock(CRITICAL_SECTION* section) {
  if (!section) {
    return;
  }
  if (__atomic_fetch_sub(&section->state, 1u, __ATOMIC_RELEASE) != 1u) {
    __atomic_store_n(&section->state, 0u, __ATOMIC_RELEASE);
    uv_rt_linux_futex_wake(&section->state, 1);
  }
}

static BOOL uv_rt_linux_condition_wait(CONDITION_VARIABLE* condition,
                                            CRITICAL_SECTION* section,
                                            DWORD milliseconds) {
  uint32_t sequence = 0u;
  int rc = 0;
  struct timespec timeout;
  struct timespec* timeout_ptr = NULL;
  if (!condition || !section) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  sequence = __atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE);
  uv_rt_linux_mutex_unlock(section);
  if (milliseconds != INFINITE) {
    uv_rt_linux_timespec_from_millis(milliseconds, &timeout);
    timeout_ptr = &timeout;
  }
  rc = uv_rt_linux_futex_wait(&condition->sequence, sequence, timeout_ptr);
  uv_rt_linux_mutex_lock(section);
  if (rc == 0 || errno == EAGAIN || errno == EINTR) {
    SetLastError(ERROR_SUCCESS);
    return TRUE;
  }
  if (errno == ETIMEDOUT) {
    SetLastError(ERROR_SUCCESS);
    return FALSE;
  }
  uv_rt_linux_set_errno_error(errno);
  return FALSE;
}

static void uv_rt_linux_condition_wake(CONDITION_VARIABLE* condition,
                                            int wake_count) {
  if (!condition) {
    return;
  }
  __atomic_add_fetch(&condition->sequence, 1u, __ATOMIC_RELEASE);
  uv_rt_linux_futex_wake(&condition->sequence, wake_count);
}

#define malloc(bytes) uv_rt_linux_heap_alloc_unchecked((bytes))
#define calloc(count, size) uv_rt_linux_heap_calloc_unchecked((count), (size))
#define free(memory) uv_rt_linux_heap_free_unchecked((memory))

static void uv_rt_linux_assign_current_thread_id(DWORD logical_id) {
  UVRtLinuxThreadState* state;
  if (logical_id == 0u) {
    return;
  }
  state = uv_rt_linux_current_thread_state(1);
  if (state) {
    state->logical_id = logical_id;
  }
}

static DWORD uv_rt_linux_ensure_current_thread_id(void) {
  UVRtLinuxThreadState* state = uv_rt_linux_current_thread_state(1);
  if (!state) {
    return 0u;
  }
  if (state->logical_id == 0u) {
    state->logical_id = atomic_fetch_add(&uv_rt_linux_next_thread_id, 1u);
    if (state->logical_id == 0u) {
      state->logical_id =
          atomic_fetch_add(&uv_rt_linux_next_thread_id, 1u);
    }
  }
  return state->logical_id;
}

static DWORD uv_rt_linux_error_from_errno(int err) {
  switch (err) {
    case 0:
      return ERROR_SUCCESS;
    case ENOENT:
      return ERROR_FILE_NOT_FOUND;
    case ENOTDIR:
      return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:
      return ERROR_ACCESS_DENIED;
    case EBADF:
      return ERROR_INVALID_HANDLE;
    case ENOMEM:
      return ERROR_NOT_ENOUGH_MEMORY;
    case EEXIST:
      return ERROR_ALREADY_EXISTS;
    case ENAMETOOLONG:
      return ERROR_FILENAME_EXCED_RANGE;
    case EINVAL:
      return ERROR_INVALID_PARAMETER;
    case EBUSY:
      return ERROR_BUSY;
    case EROFS:
      return ERROR_WRITE_PROTECT;
    case ENOTEMPTY:
      return ERROR_BUSY;
    default:
      return ERROR_INVALID_PARAMETER;
  }
}

static void uv_rt_linux_set_errno_error(int err) {
  SetLastError(uv_rt_linux_error_from_errno(err));
}

static size_t uv_rt_linux_utf8_cstr_len(const char* text) {
  return text ? strlen(text) : 0u;
}

static const char* uv_rt_linux_process_env_utf8_value(const char* name) {
  const uv_rt_process_start_t* start = uv_rt_startup_current();
  char** envp = start ? start->envp : NULL;
  size_t name_len;
#if !defined(__APPLE__)
  extern char** environ;
#endif

  if (!name || name[0] == '\0') {
    return NULL;
  }

  if (!envp) {
#if !defined(__APPLE__)
    envp = environ;
#endif
  }
  if (!envp) {
    return NULL;
  }

  name_len = strlen(name);
  for (; *envp != NULL; ++envp) {
    const char* entry = *envp;
    if (!entry) {
      continue;
    }
    if (strncmp(entry, name, name_len) == 0 && entry[name_len] == '=') {
      return entry + name_len + 1u;
    }
  }
  return NULL;
}

static size_t uv_rt_linux_wide_cstr_len(LPCWSTR text) {
  return text ? wcslen(text) : 0u;
}

static HANDLE uv_rt_linux_alloc_handle(int kind) {
  struct UVRtHandleBase* handle =
      (struct UVRtHandleBase*)calloc(1u, sizeof(struct UVRtHandleBase));
  if (!handle) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  handle->kind = kind;
  handle->owned = 1;
  SetLastError(ERROR_SUCCESS);
  return handle;
}

static int uv_rt_linux_handle_fd(HANDLE handle, int* out_fd) {
  if (!handle || handle == INVALID_HANDLE_VALUE || !out_fd) {
    SetLastError(ERROR_INVALID_HANDLE);
    return 0;
  }
  if (((struct UVRtHandleBase*)handle)->kind != UV_RT_HANDLE_FILE) {
    SetLastError(ERROR_INVALID_HANDLE);
    return 0;
  }
  *out_fd = ((struct UVRtHandleBase*)handle)->u.file.fd;
  return 1;
}

static int uv_rt_linux_utf8_decode_one(const char* source,
                              size_t source_len,
                              size_t* index,
                              uint32_t* out_cp) {
  size_t i;
  uint32_t codepoint;
  unsigned char b0;
  unsigned char b1;
  unsigned char b2;
  unsigned char b3;
  if (!source || !index || !out_cp || *index >= source_len) {
    return 0;
  }
  i = *index;
  b0 = (unsigned char)source[i];
  if (b0 < 0x80u) {
    *out_cp = (uint32_t)b0;
    *index = i + 1u;
    return 1;
  }
  if ((b0 & 0xE0u) == 0xC0u) {
    if (i + 1u >= source_len) {
      return 0;
    }
    b1 = (unsigned char)source[i + 1u];
    if ((b1 & 0xC0u) != 0x80u) {
      return 0;
    }
    codepoint = ((uint32_t)(b0 & 0x1Fu) << 6u) | (uint32_t)(b1 & 0x3Fu);
    if (codepoint < 0x80u) {
      return 0;
    }
    *out_cp = codepoint;
    *index = i + 2u;
    return 1;
  }
  if ((b0 & 0xF0u) == 0xE0u) {
    if (i + 2u >= source_len) {
      return 0;
    }
    b1 = (unsigned char)source[i + 1u];
    b2 = (unsigned char)source[i + 2u];
    if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) {
      return 0;
    }
    codepoint = ((uint32_t)(b0 & 0x0Fu) << 12u) |
                ((uint32_t)(b1 & 0x3Fu) << 6u) |
                (uint32_t)(b2 & 0x3Fu);
    if (codepoint < 0x800u || (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
      return 0;
    }
    *out_cp = codepoint;
    *index = i + 3u;
    return 1;
  }
  if ((b0 & 0xF8u) == 0xF0u) {
    if (i + 3u >= source_len) {
      return 0;
    }
    b1 = (unsigned char)source[i + 1u];
    b2 = (unsigned char)source[i + 2u];
    b3 = (unsigned char)source[i + 3u];
    if ((b1 & 0xC0u) != 0x80u ||
        (b2 & 0xC0u) != 0x80u ||
        (b3 & 0xC0u) != 0x80u) {
      return 0;
    }
    codepoint = ((uint32_t)(b0 & 0x07u) << 18u) |
                ((uint32_t)(b1 & 0x3Fu) << 12u) |
                ((uint32_t)(b2 & 0x3Fu) << 6u) |
                (uint32_t)(b3 & 0x3Fu);
    if (codepoint < 0x10000u || codepoint > 0x10FFFFu) {
      return 0;
    }
    *out_cp = codepoint;
    *index = i + 4u;
    return 1;
  }
  return 0;
}

static size_t uv_rt_linux_utf8_encode_one(uint32_t codepoint, char* dest) {
  if (codepoint <= 0x7Fu) {
    if (dest) {
      dest[0] = (char)codepoint;
    }
    return 1u;
  }
  if (codepoint <= 0x7FFu) {
    if (dest) {
      dest[0] = (char)(0xC0u | (codepoint >> 6u));
      dest[1] = (char)(0x80u | (codepoint & 0x3Fu));
    }
    return 2u;
  }
  if (codepoint <= 0xFFFFu) {
    if (dest) {
      dest[0] = (char)(0xE0u | (codepoint >> 12u));
      dest[1] = (char)(0x80u | ((codepoint >> 6u) & 0x3Fu));
      dest[2] = (char)(0x80u | (codepoint & 0x3Fu));
    }
    return 3u;
  }
  if (dest) {
    dest[0] = (char)(0xF0u | (codepoint >> 18u));
    dest[1] = (char)(0x80u | ((codepoint >> 12u) & 0x3Fu));
    dest[2] = (char)(0x80u | ((codepoint >> 6u) & 0x3Fu));
    dest[3] = (char)(0x80u | (codepoint & 0x3Fu));
  }
  return 4u;
}
static wchar_t* uv_rt_linux_wide_from_utf8_alloc(const char* source,
                                        size_t source_len,
                                        int add_nul,
                                        size_t* out_len) {
  size_t count = 0u;
  size_t index = 0u;
  wchar_t* output;
  size_t out_index = 0u;
  if (out_len) {
    *out_len = 0u;
  }
  if (!source && source_len != 0u) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  while (index < source_len) {
    uint32_t codepoint = 0u;
    if (!uv_rt_linux_utf8_decode_one(source, source_len, &index, &codepoint)) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return NULL;
    }
    ++count;
  }
  output = (wchar_t*)malloc(sizeof(wchar_t) * (count + (add_nul ? 1u : 0u)));
  if (!output) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  index = 0u;
  while (index < source_len) {
    uint32_t codepoint = 0u;
    if (!uv_rt_linux_utf8_decode_one(source, source_len, &index, &codepoint)) {
      free(output);
      SetLastError(ERROR_INVALID_PARAMETER);
      return NULL;
    }
    output[out_index++] = (wchar_t)codepoint;
  }
  if (add_nul) {
    output[out_index] = 0;
  }
  if (out_len) {
    *out_len = count;
  }
  SetLastError(ERROR_SUCCESS);
  return output;
}

static char* uv_rt_linux_utf8_from_wide_alloc(LPCWSTR source,
                                     size_t source_len,
                                     int add_nul,
                                     size_t* out_len) {
  size_t count = 0u;
  size_t i;
  char* output;
  size_t out_index = 0u;
  if (out_len) {
    *out_len = 0u;
  }
  if (!source && source_len != 0u) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  for (i = 0u; i < source_len; ++i) {
    uint32_t codepoint = (uint32_t)source[i];
    if ((codepoint >= 0xD800u && codepoint <= 0xDFFFu) || codepoint > 0x10FFFFu) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return NULL;
    }
    count += uv_rt_linux_utf8_encode_one(codepoint, NULL);
  }
  output = (char*)malloc(count + (add_nul ? 1u : 0u));
  if (!output) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  for (i = 0u; i < source_len; ++i) {
    out_index += uv_rt_linux_utf8_encode_one((uint32_t)source[i], output + out_index);
  }
  if (add_nul) {
    output[out_index] = '\0';
  }
  if (out_len) {
    *out_len = count;
  }
  SetLastError(ERROR_SUCCESS);
  return output;
}

static char* uv_rt_linux_path_from_wide_alloc(LPCWSTR path) {
  size_t len = uv_rt_linux_wide_cstr_len(path);
  size_t utf8_len = 0u;
  char* utf8 = uv_rt_linux_utf8_from_wide_alloc(path, len, 1, &utf8_len);
  size_t i;
  if (!utf8) {
    return NULL;
  }
  for (i = 0u; i < utf8_len; ++i) {
    if (utf8[i] == '\\') {
      utf8[i] = '/';
    }
  }
  return utf8;
}

static char* uv_rt_linux_strdup_or_null(const char* text) {
  size_t len;
  char* copy;
  if (!text) {
    return NULL;
  }
  len = strlen(text);
  copy = (char*)malloc(len + 1u);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, text, len + 1u);
  return copy;
}

static char* uv_rt_linux_join_path_utf8(const char* lhs, const char* rhs) {
  size_t lhs_len = lhs ? strlen(lhs) : 0u;
  size_t rhs_len = rhs ? strlen(rhs) : 0u;
  int need_sep = lhs_len != 0u && lhs[lhs_len - 1u] != '/';
  char* path = (char*)malloc(lhs_len + (need_sep ? 1u : 0u) + rhs_len + 1u);
  if (!path) {
    return NULL;
  }
  if (lhs_len != 0u) {
    memcpy(path, lhs, lhs_len);
  }
  if (need_sep) {
    path[lhs_len++] = '/';
  }
  if (rhs_len != 0u) {
    memcpy(path + lhs_len, rhs, rhs_len);
  }
  path[lhs_len + rhs_len] = '\0';
  return path;
}

void SetLastError(DWORD error_code) {
  UVRtLinuxThreadState* state = uv_rt_linux_current_thread_state(1);
  if (state) {
    state->last_error = error_code;
  }
}

DWORD GetLastError(void) {
  UVRtLinuxThreadState* state = uv_rt_linux_current_thread_state(0);
  return state ? state->last_error : ERROR_SUCCESS;
}

HANDLE GetProcessHeap(void) {
  return &uv_rt_linux_process_heap_handle;
}

LPVOID HeapAlloc(HANDLE heap, DWORD flags, size_t bytes) {
  UVHeapBlockHeader* block;
  (void)heap;
  (void)flags;
  block = (UVHeapBlockHeader*)malloc(bytes);
  if (!block) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  SetLastError(ERROR_SUCCESS);
  return (LPVOID)block;
}

BOOL HeapFree(HANDLE heap, DWORD flags, LPVOID memory) {
  const UVHeapBlockHeader* block;
  (void)heap;
  (void)flags;
  if (!memory) {
    SetLastError(ERROR_SUCCESS);
    return TRUE;
  }
  block = uv_rt_linux_heap_header(memory);
  if (!uv_rt_linux_heap_header_valid(block)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  uv_rt_linux_heap_free_unchecked(memory);
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL HeapValidate(HANDLE heap, DWORD flags, LPCVOID memory) {
  const UVHeapBlockHeader* block;
  (void)heap;
  (void)flags;
  if (!memory) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  block = uv_rt_linux_heap_header(memory);
  if (!uv_rt_linux_heap_header_valid(block)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

void ExitProcess(UINT exit_code) {
  _exit((int)exit_code);
}

int MultiByteToWideChar(UINT code_page,
                        DWORD flags,
                        LPCCH source,
                        int source_length,
                        LPWSTR destination,
                        int destination_length) {
  size_t source_len;
  size_t required;
  size_t written;
  wchar_t* converted;
  (void)flags;
  if (code_page != CP_UTF8 || !source || source_length == 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  source_len = (source_length < 0) ? uv_rt_linux_utf8_cstr_len(source) : (size_t)source_length;
  converted = uv_rt_linux_wide_from_utf8_alloc(source, source_len, source_length < 0, &written);
  if (!converted) {
    return 0;
  }
  required = written + ((source_length < 0) ? 1u : 0u);
  if (!destination || destination_length == 0) {
    free(converted);
    return (int)required;
  }
  if ((size_t)destination_length < required) {
    free(converted);
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  memcpy(destination, converted, sizeof(wchar_t) * required);
  free(converted);
  SetLastError(ERROR_SUCCESS);
  return (int)required;
}

int WideCharToMultiByte(UINT code_page,
                        DWORD flags,
                        LPCWSTR source,
                        int source_length,
                        char* destination,
                        int destination_length,
                        const char* default_char,
                        BOOL* used_default_char) {
  size_t source_len;
  size_t required;
  size_t written;
  char* converted;
  (void)flags;
  (void)default_char;
  if (used_default_char) {
    *used_default_char = FALSE;
  }
  if (code_page != CP_UTF8 || !source || source_length == 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  source_len = (source_length < 0) ? uv_rt_linux_wide_cstr_len(source) : (size_t)source_length;
  converted = uv_rt_linux_utf8_from_wide_alloc(source, source_len, source_length < 0, &written);
  if (!converted) {
    return 0;
  }
  required = written + ((source_length < 0) ? 1u : 0u);
  if (!destination || destination_length == 0) {
    free(converted);
    return (int)required;
  }
  if ((size_t)destination_length < required) {
    free(converted);
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  memcpy(destination, converted, required);
  free(converted);
  SetLastError(ERROR_SUCCESS);
  return (int)required;
}

DWORD GetEnvironmentVariableW(LPCWSTR name, LPWSTR buffer, DWORD size) {
  size_t name_len = uv_rt_linux_wide_cstr_len(name);
  char* utf8_name = uv_rt_linux_utf8_from_wide_alloc(name, name_len, 1, NULL);
  const char* value;
  size_t value_len;
  wchar_t* wide_value;
  size_t wide_len;
  DWORD result;
  if (!utf8_name) {
    return 0u;
  }
  value = uv_rt_linux_process_env_utf8_value(utf8_name);
  free(utf8_name);
  if (!value) {
    SetLastError(ERROR_ENVVAR_NOT_FOUND);
    return 0u;
  }
  value_len = strlen(value);
  wide_value = uv_rt_linux_wide_from_utf8_alloc(value, value_len, 1, &wide_len);
  if (!wide_value) {
    return 0u;
  }
  if (!buffer || size == 0u) {
    free(wide_value);
    SetLastError(ERROR_SUCCESS);
    return (DWORD)(wide_len + 1u);
  }
  if ((size_t)size <= wide_len) {
    free(wide_value);
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return (DWORD)(wide_len + 1u);
  }
  memcpy(buffer, wide_value, sizeof(wchar_t) * (wide_len + 1u));
  free(wide_value);
  result = (DWORD)wide_len;
  SetLastError(ERROR_SUCCESS);
  return result;
}

DWORD GetEnvironmentVariableA(const char* name, char* buffer, DWORD size) {
  const char* value;
  size_t value_len;
  if (!name || name[0] == '\0') {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0u;
  }
  value = uv_rt_linux_process_env_utf8_value(name);
  if (!value) {
    SetLastError(ERROR_ENVVAR_NOT_FOUND);
    return 0u;
  }
  value_len = strlen(value);
  if (!buffer || size == 0u) {
    SetLastError(ERROR_SUCCESS);
    return (DWORD)(value_len + 1u);
  }
  if ((size_t)size <= value_len) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return (DWORD)(value_len + 1u);
  }
  memcpy(buffer, value, value_len + 1u);
  SetLastError(ERROR_SUCCESS);
  return (DWORD)value_len;
}

typedef struct UVThreadStartContext {
  HANDLE handle;
  LPTHREAD_START_ROUTINE start_routine;
  LPVOID parameter;
} UVThreadStartContext;

static void* uv_rt_linux_thread_start_trampoline(void* raw_context) {
  UVThreadStartContext* context = (UVThreadStartContext*)raw_context;
  struct UVRtHandleBase* handle = (struct UVRtHandleBase*)context->handle;
  DWORD exit_code = 0u;
  uv_rt_linux_assign_current_thread_id(handle->u.thread.logical_id);
  handle->u.thread.tid = (pid_t)syscall(SYS_gettid);
  {
    UVRtLinuxThreadState* state = uv_rt_linux_current_thread_state(1);
    if (state) {
      state->priority = handle->u.thread.priority;
    }
  }
  if (context->start_routine) {
    exit_code = context->start_routine(context->parameter);
  }
  EnterCriticalSection(&handle->u.thread.lock);
  handle->u.thread.exit_code = exit_code;
  handle->u.thread.finished = 1;
  WakeAllConditionVariable(&handle->u.thread.cond);
  LeaveCriticalSection(&handle->u.thread.lock);
  uv_rt_linux_release_current_thread_state();
  free(context);
  return NULL;
}
BOOL CreateProcessW(LPCWSTR application_name,
                    LPWSTR command_line,
                    LPSECURITY_ATTRIBUTES process_attributes,
                    LPSECURITY_ATTRIBUTES thread_attributes,
                    BOOL inherit_handles,
                    DWORD creation_flags,
                    LPVOID environment,
                    LPCWSTR current_directory,
                    LPSTARTUPINFOW startup_info,
                    LPPROCESS_INFORMATION process_information) {
  char* app_utf8 = NULL;
  char* cmd_utf8 = NULL;
  char* cwd_utf8 = NULL;
  pid_t pid;
  HANDLE process_handle;
  HANDLE thread_handle;
  (void)process_attributes;
  (void)thread_attributes;
  (void)inherit_handles;
  (void)creation_flags;
  (void)environment;
  (void)startup_info;
  if (!process_information) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (application_name) {
    app_utf8 = uv_rt_linux_path_from_wide_alloc(application_name);
    if (!app_utf8) {
      return FALSE;
    }
  }
  if (command_line) {
    cmd_utf8 = uv_rt_linux_path_from_wide_alloc(command_line);
    if (!cmd_utf8) {
      free(app_utf8);
      return FALSE;
    }
  }
  if (current_directory) {
    cwd_utf8 = uv_rt_linux_path_from_wide_alloc(current_directory);
    if (!cwd_utf8) {
      free(cmd_utf8);
      free(app_utf8);
      return FALSE;
    }
  }
  if (!app_utf8 && !cmd_utf8) {
    free(cwd_utf8);
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  pid = fork();
  if (pid < 0) {
    uv_rt_linux_set_errno_error(errno);
    free(cwd_utf8);
    free(cmd_utf8);
    free(app_utf8);
    return FALSE;
  }
  if (pid == 0) {
    if (cwd_utf8 && chdir(cwd_utf8) != 0) {
      _exit(127);
    }
    if (app_utf8 && !cmd_utf8) {
      execlp(app_utf8, app_utf8, (char*)NULL);
    } else if (app_utf8 && cmd_utf8) {
      execlp(app_utf8, app_utf8, cmd_utf8, (char*)NULL);
    } else {
      execl("/bin/sh", "sh", "-c", cmd_utf8, (char*)NULL);
    }
    _exit(127);
  }

  free(cwd_utf8);
  free(cmd_utf8);
  free(app_utf8);

  process_handle = uv_rt_linux_alloc_handle(UV_RT_HANDLE_PROCESS);
  if (!process_handle) {
    return FALSE;
  }
  ((struct UVRtHandleBase*)process_handle)->u.process.pid = pid;
  ((struct UVRtHandleBase*)process_handle)->u.process.waited = 0;
  ((struct UVRtHandleBase*)process_handle)->u.process.exit_code = STILL_ACTIVE;

  thread_handle = uv_rt_linux_alloc_handle(UV_RT_HANDLE_TOKEN);
  if (!thread_handle) {
    free(process_handle);
    return FALSE;
  }

  process_information->hProcess = process_handle;
  process_information->hThread = thread_handle;
  process_information->dwProcessId = (DWORD)pid;
  process_information->dwThreadId = (DWORD)pid;
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

static DWORD uv_rt_linux_wait_for_process(struct UVRtHandleBase* handle, DWORD milliseconds) {
  int status = 0;
  pid_t waited;
  (void)milliseconds;
  if (handle->u.process.waited) {
    SetLastError(ERROR_SUCCESS);
    return WAIT_OBJECT_0;
  }
  waited = waitpid(handle->u.process.pid, &status, 0);
  if (waited < 0) {
    uv_rt_linux_set_errno_error(errno);
    return WAIT_FAILED;
  }
  handle->u.process.waited = 1;
  if (WIFEXITED(status)) {
    handle->u.process.exit_code = (DWORD)WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    handle->u.process.exit_code = (DWORD)(128 + WTERMSIG(status));
  } else {
    handle->u.process.exit_code = 1u;
  }
  SetLastError(ERROR_SUCCESS);
  return WAIT_OBJECT_0;
}

static DWORD uv_rt_linux_wait_for_thread(struct UVRtHandleBase* handle, DWORD milliseconds) {
  EnterCriticalSection(&handle->u.thread.lock);
  if (handle->u.thread.joined) {
    LeaveCriticalSection(&handle->u.thread.lock);
    SetLastError(ERROR_SUCCESS);
    return WAIT_OBJECT_0;
  }
  if (milliseconds == INFINITE) {
    while (!handle->u.thread.finished) {
      SleepConditionVariableCS(&handle->u.thread.cond,
                               &handle->u.thread.lock,
                               INFINITE);
    }
  } else {
    while (!handle->u.thread.finished) {
      if (!SleepConditionVariableCS(&handle->u.thread.cond,
                                    &handle->u.thread.lock,
                                    milliseconds)) {
        LeaveCriticalSection(&handle->u.thread.lock);
        SetLastError(ERROR_SUCCESS);
        return WAIT_TIMEOUT;
      }
    }
  }
  handle->u.thread.joined = 1;
  LeaveCriticalSection(&handle->u.thread.lock);
  SetLastError(ERROR_SUCCESS);
  return WAIT_OBJECT_0;
}

static DWORD uv_rt_linux_wait_for_event(struct UVRtHandleBase* handle, DWORD milliseconds) {
  EnterCriticalSection(&handle->u.event.mutex);
  if (milliseconds == INFINITE) {
    while (!handle->u.event.signaled) {
      SleepConditionVariableCS(&handle->u.event.cond,
                               &handle->u.event.mutex,
                               INFINITE);
    }
  } else {
    while (!handle->u.event.signaled) {
      if (!SleepConditionVariableCS(&handle->u.event.cond,
                                    &handle->u.event.mutex,
                                    milliseconds)) {
        LeaveCriticalSection(&handle->u.event.mutex);
        SetLastError(ERROR_SUCCESS);
        return WAIT_TIMEOUT;
      }
    }
  }
  if (!handle->u.event.manual_reset) {
    handle->u.event.signaled = 0;
  }
  LeaveCriticalSection(&handle->u.event.mutex);
  SetLastError(ERROR_SUCCESS);
  return WAIT_OBJECT_0;
}

DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds) {
  struct UVRtHandleBase* base = (struct UVRtHandleBase*)handle;
  if (!base || handle == INVALID_HANDLE_VALUE) {
    SetLastError(ERROR_INVALID_HANDLE);
    return WAIT_FAILED;
  }
  switch (base->kind) {
    case UV_RT_HANDLE_PROCESS:
      return uv_rt_linux_wait_for_process(base, milliseconds);
    case UV_RT_HANDLE_THREAD:
      return uv_rt_linux_wait_for_thread(base, milliseconds);
    case UV_RT_HANDLE_EVENT:
      return uv_rt_linux_wait_for_event(base, milliseconds);
    case UV_RT_HANDLE_TOKEN:
      SetLastError(ERROR_SUCCESS);
      return WAIT_OBJECT_0;
    default:
      SetLastError(ERROR_INVALID_HANDLE);
      return WAIT_FAILED;
  }
}

DWORD WaitForMultipleObjects(DWORD count,
                             const HANDLE* handles,
                             BOOL wait_all,
                             DWORD milliseconds) {
  DWORD i;
  if (!handles || count == 0u) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return WAIT_FAILED;
  }
  if (wait_all) {
    for (i = 0u; i < count; ++i) {
      DWORD result = WaitForSingleObject(handles[i], milliseconds);
      if (result != WAIT_OBJECT_0) {
        return WAIT_FAILED;
      }
    }
    SetLastError(ERROR_SUCCESS);
    return WAIT_OBJECT_0;
  }
  if (milliseconds != INFINITE) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return WAIT_FAILED;
  }
  for (;;) {
    for (i = 0u; i < count; ++i) {
      struct UVRtHandleBase* base = (struct UVRtHandleBase*)handles[i];
      if (!base) {
        continue;
      }
      if (base->kind == UV_RT_HANDLE_EVENT && base->u.event.signaled) {
        return WAIT_OBJECT_0 + i;
      }
      if (base->kind == UV_RT_HANDLE_THREAD && base->u.thread.finished) {
        return WAIT_OBJECT_0 + i;
      }
      if (base->kind == UV_RT_HANDLE_PROCESS && base->u.process.waited) {
        return WAIT_OBJECT_0 + i;
      }
    }
    usleep(1000u);
  }
}

BOOL GetExitCodeProcess(HANDLE handle, DWORD* exit_code) {
  struct UVRtHandleBase* base = (struct UVRtHandleBase*)handle;
  int status = 0;
  pid_t result;
  if (!base || base->kind != UV_RT_HANDLE_PROCESS || !exit_code) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (base->u.process.waited) {
    *exit_code = base->u.process.exit_code;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
  }
  result = waitpid(base->u.process.pid, &status, WNOHANG);
  if (result == 0) {
    *exit_code = STILL_ACTIVE;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
  }
  if (result < 0) {
    uv_rt_linux_set_errno_error(errno);
    return FALSE;
  }
  base->u.process.waited = 1;
  base->u.process.exit_code = WIFEXITED(status)
      ? (DWORD)WEXITSTATUS(status)
      : (DWORD)(128 + WTERMSIG(status));
  *exit_code = base->u.process.exit_code;
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL GetExitCodeThread(HANDLE handle, DWORD* exit_code) {
  struct UVRtHandleBase* base = (struct UVRtHandleBase*)handle;
  if (!base || base->kind != UV_RT_HANDLE_THREAD || !exit_code) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  EnterCriticalSection(&base->u.thread.lock);
  *exit_code = base->u.thread.finished ? base->u.thread.exit_code : STILL_ACTIVE;
  LeaveCriticalSection(&base->u.thread.lock);
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

DWORD TlsAlloc(void) {
  DWORD index;
  EnterCriticalSection(&uv_rt_linux_tls_slot_lock);
  for (index = 0u; index < (DWORD)(sizeof(uv_rt_linux_tls_slot_used) / sizeof(uv_rt_linux_tls_slot_used[0])); ++index) {
    if (!uv_rt_linux_tls_slot_used[index]) {
      uv_rt_linux_tls_slot_used[index] = 1;
      LeaveCriticalSection(&uv_rt_linux_tls_slot_lock);
      SetLastError(ERROR_SUCCESS);
      return index;
    }
  }
  LeaveCriticalSection(&uv_rt_linux_tls_slot_lock);
  SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  return TLS_OUT_OF_INDEXES;
}

LPVOID TlsGetValue(DWORD index) {
  UVRtLinuxThreadState* state;
  if (index >= (DWORD)(sizeof(uv_rt_linux_tls_slot_used) / sizeof(uv_rt_linux_tls_slot_used[0])) ||
      !uv_rt_linux_tls_slot_used[index]) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  state = uv_rt_linux_current_thread_state(1);
  if (!state) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  SetLastError(ERROR_SUCCESS);
  return state->tls_values[index];
}

BOOL TlsSetValue(DWORD index, LPVOID value) {
  UVRtLinuxThreadState* state;
  if (index >= (DWORD)(sizeof(uv_rt_linux_tls_slot_used) / sizeof(uv_rt_linux_tls_slot_used[0])) ||
      !uv_rt_linux_tls_slot_used[index]) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  state = uv_rt_linux_current_thread_state(1);
  if (!state) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  state->tls_values[index] = value;
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL InitOnceExecuteOnce(PINIT_ONCE init_once,
                         PINIT_ONCE_FN init_fn,
                         PVOID parameter,
                         PVOID* context) {
  BOOL ok;
  if (!init_once || !init_fn) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  for (;;) {
    LONG state = __atomic_load_n(&init_once->state, __ATOMIC_ACQUIRE);
    if (state == 2) {
      ok = TRUE;
      break;
    }
    if (state == 0 &&
        __atomic_compare_exchange_n(&init_once->state,
                                    &state,
                                    1,
                                    0,
                                    __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE)) {
      PVOID local_context = init_once->context;
      ok = init_fn(init_once, parameter, &local_context);
      if (ok) {
        init_once->context = local_context;
      }
      __atomic_store_n(&init_once->state, ok ? 2 : 0, __ATOMIC_RELEASE);
      uv_rt_linux_futex_wake((volatile uint32_t*)&init_once->state, INT_MAX);
      break;
    }
    uv_rt_linux_futex_wait((volatile uint32_t*)&init_once->state, 1u, NULL);
  }
  if (context) {
    *context = init_once->context;
  }
  SetLastError(ok ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER);
  return ok;
}

void InitializeCriticalSection(CRITICAL_SECTION* section) {
  if (section) {
    __atomic_store_n(&section->state, 0u, __ATOMIC_RELAXED);
  }
}

void DeleteCriticalSection(CRITICAL_SECTION* section) {
  (void)section;
}

void EnterCriticalSection(CRITICAL_SECTION* section) {
  uv_rt_linux_mutex_lock(section);
}

void LeaveCriticalSection(CRITICAL_SECTION* section) {
  uv_rt_linux_mutex_unlock(section);
}

void InitializeConditionVariable(CONDITION_VARIABLE* condition) {
  if (condition) {
    __atomic_store_n(&condition->sequence, 0u, __ATOMIC_RELAXED);
  }
}

BOOL SleepConditionVariableCS(CONDITION_VARIABLE* condition,
                              CRITICAL_SECTION* section,
                              DWORD milliseconds) {
  return uv_rt_linux_condition_wait(condition, section, milliseconds);
}

void WakeConditionVariable(CONDITION_VARIABLE* condition) {
  uv_rt_linux_condition_wake(condition, 1);
}

void WakeAllConditionVariable(CONDITION_VARIABLE* condition) {
  uv_rt_linux_condition_wake(condition, INT_MAX);
}

void InitializeSRWLock(SRWLOCK* lock) {
  if (!lock) {
    return;
  }
  InitializeCriticalSection(&lock->mutex);
  InitializeConditionVariable(&lock->condition);
  __atomic_store_n(&lock->readers, 0u, __ATOMIC_RELAXED);
  __atomic_store_n(&lock->writer, 0u, __ATOMIC_RELAXED);
  __atomic_store_n(&lock->waiting_writers, 0u, __ATOMIC_RELAXED);
}

void AcquireSRWLockExclusive(SRWLOCK* lock) {
  if (!lock) {
    return;
  }
  EnterCriticalSection(&lock->mutex);
  __atomic_add_fetch(&lock->waiting_writers, 1u, __ATOMIC_RELAXED);
  while (__atomic_load_n(&lock->writer, __ATOMIC_ACQUIRE) != 0u ||
         __atomic_load_n(&lock->readers, __ATOMIC_ACQUIRE) != 0u) {
    SleepConditionVariableCS(&lock->condition, &lock->mutex, INFINITE);
  }
  __atomic_sub_fetch(&lock->waiting_writers, 1u, __ATOMIC_RELAXED);
  __atomic_store_n(&lock->writer, 1u, __ATOMIC_RELEASE);
  LeaveCriticalSection(&lock->mutex);
}

void ReleaseSRWLockExclusive(SRWLOCK* lock) {
  if (!lock) {
    return;
  }
  EnterCriticalSection(&lock->mutex);
  __atomic_store_n(&lock->writer, 0u, __ATOMIC_RELEASE);
  WakeAllConditionVariable(&lock->condition);
  LeaveCriticalSection(&lock->mutex);
}

void AcquireSRWLockShared(SRWLOCK* lock) {
  if (!lock) {
    return;
  }
  EnterCriticalSection(&lock->mutex);
  while (__atomic_load_n(&lock->writer, __ATOMIC_ACQUIRE) != 0u ||
         __atomic_load_n(&lock->waiting_writers, __ATOMIC_ACQUIRE) != 0u) {
    SleepConditionVariableCS(&lock->condition, &lock->mutex, INFINITE);
  }
  __atomic_add_fetch(&lock->readers, 1u, __ATOMIC_RELAXED);
  LeaveCriticalSection(&lock->mutex);
}

void ReleaseSRWLockShared(SRWLOCK* lock) {
  if (!lock) {
    return;
  }
  EnterCriticalSection(&lock->mutex);
  if (__atomic_sub_fetch(&lock->readers, 1u, __ATOMIC_RELAXED) == 0u) {
    WakeAllConditionVariable(&lock->condition);
  }
  LeaveCriticalSection(&lock->mutex);
}

LONG InterlockedExchange(volatile LONG* target, LONG value) {
  return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

LONG InterlockedCompareExchange(volatile LONG* target,
                                LONG exchange,
                                LONG comparand) {
  __atomic_compare_exchange_n(target,
                              &comparand,
                              exchange,
                              0,
                              __ATOMIC_SEQ_CST,
                              __ATOMIC_SEQ_CST);
  return comparand;
}

LONG64 InterlockedIncrement64(volatile LONG64* target) {
  return __atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST);
}

DWORD GetCurrentProcessId(void) {
  return (DWORD)getpid();
}

DWORD GetCurrentThreadId(void) {
  return uv_rt_linux_ensure_current_thread_id();
}

HANDLE GetCurrentThread(void) {
  uv_rt_linux_ensure_current_thread_id();
  return &uv_rt_linux_pseudo_thread_handle;
}

int GetThreadPriority(HANDLE thread) {
  struct UVRtHandleBase* base = (struct UVRtHandleBase*)thread;
  if (!thread || thread == &uv_rt_linux_pseudo_thread_handle) {
    UVRtLinuxThreadState* state = uv_rt_linux_current_thread_state(1);
    return state ? state->priority : THREAD_PRIORITY_ERROR_RETURN;
  }
  if (!base || base->kind != UV_RT_HANDLE_THREAD) {
    return THREAD_PRIORITY_ERROR_RETURN;
  }
  return base->u.thread.priority;
}

BOOL SetThreadPriority(HANDLE thread, int priority) {
  struct UVRtHandleBase* base = (struct UVRtHandleBase*)thread;
  if (!thread || thread == &uv_rt_linux_pseudo_thread_handle) {
    UVRtLinuxThreadState* state = uv_rt_linux_current_thread_state(1);
    if (!state) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return FALSE;
    }
    state->priority = priority;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
  }
  if (!base || base->kind != UV_RT_HANDLE_THREAD) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  base->u.thread.priority = priority;
  if (base->u.thread.tid == uv_rt_linux_current_kernel_tid()) {
    UVRtLinuxThreadState* state = uv_rt_linux_current_thread_state(1);
    if (state) {
      state->priority = priority;
    }
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

DWORD_PTR SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask) {
  pid_t thread_id = 0;
  cpu_set_t current_set;
  cpu_set_t desired_set;
  DWORD_PTR previous = 0u;
  int cpu_index;
  if (mask == 0u) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0u;
  }
  if (!thread || thread == &uv_rt_linux_pseudo_thread_handle) {
    thread_id = 0;
  } else if (((struct UVRtHandleBase*)thread)->kind == UV_RT_HANDLE_THREAD) {
    thread_id = ((struct UVRtHandleBase*)thread)->u.thread.tid;
    if (thread_id == 0) {
      SetLastError(ERROR_BUSY);
      return 0u;
    }
  } else {
    SetLastError(ERROR_INVALID_HANDLE);
    return 0u;
  }
  CPU_ZERO(&current_set);
  {
    if (sched_getaffinity(thread_id, sizeof(current_set), &current_set) != 0) {
      uv_rt_linux_set_errno_error(errno);
      return 0u;
    }
  }
  for (cpu_index = 0; cpu_index < (int)(sizeof(DWORD_PTR) * 8u) && cpu_index < CPU_SETSIZE; ++cpu_index) {
    if (CPU_ISSET(cpu_index, &current_set)) {
      previous |= ((DWORD_PTR)1u << cpu_index);
    }
  }
  CPU_ZERO(&desired_set);
  for (cpu_index = 0; cpu_index < (int)(sizeof(DWORD_PTR) * 8u) && cpu_index < CPU_SETSIZE; ++cpu_index) {
    if ((mask & ((DWORD_PTR)1u << cpu_index)) != 0u) {
      CPU_SET(cpu_index, &desired_set);
    }
  }
  {
    if (sched_setaffinity(thread_id, sizeof(desired_set), &desired_set) != 0) {
      uv_rt_linux_set_errno_error(errno);
      return 0u;
    }
  }
  SetLastError(ERROR_SUCCESS);
  return previous;
}

HANDLE CreateThread(LPSECURITY_ATTRIBUTES attributes,
                    size_t stack_size,
                    LPTHREAD_START_ROUTINE start_routine,
                    LPVOID parameter,
                    DWORD creation_flags,
                    DWORD* thread_id) {
  struct UVRtHandleBase* handle;
  UVThreadStartContext* context;
  pthread_attr_t attr;
  (void)attributes;
  (void)creation_flags;
  handle = (struct UVRtHandleBase*)uv_rt_linux_alloc_handle(UV_RT_HANDLE_THREAD);
  if (!handle) {
    return NULL;
  }
  InitializeCriticalSection(&handle->u.thread.lock);
  InitializeConditionVariable(&handle->u.thread.cond);
  handle->u.thread.logical_id = atomic_fetch_add(&uv_rt_linux_next_thread_id, 1u);
  handle->u.thread.exit_code = STILL_ACTIVE;
  handle->u.thread.priority = THREAD_PRIORITY_NORMAL;
  context = (UVThreadStartContext*)malloc(sizeof(UVThreadStartContext));
  if (!context) {
    free(handle);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  context->handle = handle;
  context->start_routine = start_routine;
  context->parameter = parameter;
  pthread_attr_init(&attr);
  if (stack_size != 0u) {
    pthread_attr_setstacksize(&attr, stack_size);
  }
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  {
    int rc = pthread_create(&handle->u.thread.thread, &attr, uv_rt_linux_thread_start_trampoline, context);
    if (rc != 0) {
      pthread_attr_destroy(&attr);
      free(context);
      free(handle);
      uv_rt_linux_set_errno_error(rc);
      return NULL;
    }
  }
  pthread_attr_destroy(&attr);
  if (thread_id) {
    *thread_id = handle->u.thread.logical_id;
  }
  SetLastError(ERROR_SUCCESS);
  return handle;
}

HANDLE CreateEvent(LPSECURITY_ATTRIBUTES attributes,
                   BOOL manual_reset,
                   BOOL initial_state,
                   LPCWSTR name) {
  struct UVRtHandleBase* handle;
  (void)attributes;
  (void)name;
  handle = (struct UVRtHandleBase*)uv_rt_linux_alloc_handle(UV_RT_HANDLE_EVENT);
  if (!handle) {
    return NULL;
  }
  InitializeCriticalSection(&handle->u.event.mutex);
  InitializeConditionVariable(&handle->u.event.cond);
  handle->u.event.manual_reset = manual_reset ? 1 : 0;
  handle->u.event.signaled = initial_state ? 1 : 0;
  SetLastError(ERROR_SUCCESS);
  return handle;
}

HANDLE CreateEventW(LPSECURITY_ATTRIBUTES attributes,
                    BOOL manual_reset,
                    BOOL initial_state,
                    LPCWSTR name) {
  return CreateEvent(attributes, manual_reset, initial_state, name);
}

BOOL SetEvent(HANDLE handle) {
  struct UVRtHandleBase* base = (struct UVRtHandleBase*)handle;
  if (!base || base->kind != UV_RT_HANDLE_EVENT) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  EnterCriticalSection(&base->u.event.mutex);
  base->u.event.signaled = 1;
  if (base->u.event.manual_reset) {
    WakeAllConditionVariable(&base->u.event.cond);
  } else {
    WakeConditionVariable(&base->u.event.cond);
  }
  LeaveCriticalSection(&base->u.event.mutex);
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL ResetEvent(HANDLE handle) {
  struct UVRtHandleBase* base = (struct UVRtHandleBase*)handle;
  if (!base || base->kind != UV_RT_HANDLE_EVENT) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  EnterCriticalSection(&base->u.event.mutex);
  base->u.event.signaled = 0;
  LeaveCriticalSection(&base->u.event.mutex);
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

HANDLE GetStdHandle(DWORD std_handle_id) {
  HANDLE handle = NULL;
  EnterCriticalSection(&uv_rt_linux_std_handle_lock);
  switch (std_handle_id) {
    case STD_INPUT_HANDLE:
      handle = uv_rt_linux_std_input;
      break;
    case STD_OUTPUT_HANDLE:
      handle = uv_rt_linux_std_output;
      break;
    case STD_ERROR_HANDLE:
      handle = uv_rt_linux_std_error;
      break;
    default:
      handle = NULL;
      break;
  }
  LeaveCriticalSection(&uv_rt_linux_std_handle_lock);
  if (!handle) {
    SetLastError(ERROR_INVALID_HANDLE);
  } else {
    SetLastError(ERROR_SUCCESS);
  }
  return handle;
}

BOOL SetStdHandle(DWORD std_handle_id, HANDLE handle) {
  EnterCriticalSection(&uv_rt_linux_std_handle_lock);
  switch (std_handle_id) {
    case STD_INPUT_HANDLE:
      uv_rt_linux_std_input = handle;
      break;
    case STD_OUTPUT_HANDLE:
      uv_rt_linux_std_output = handle;
      break;
    case STD_ERROR_HANDLE:
      uv_rt_linux_std_error = handle;
      break;
    default:
      LeaveCriticalSection(&uv_rt_linux_std_handle_lock);
      SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
  }
  LeaveCriticalSection(&uv_rt_linux_std_handle_lock);
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL GetConsoleMode(HANDLE handle, DWORD* mode) {
  int fd;
  if (!uv_rt_linux_handle_fd(handle, &fd)) {
    return FALSE;
  }
  if (!isatty(fd)) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (mode) {
    *mode = 1u;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL WriteConsoleA(HANDLE handle,
                   LPCVOID buffer,
                   DWORD chars_to_write,
                   DWORD* chars_written,
                   LPVOID reserved) {
  (void)reserved;
  return WriteFile(handle, buffer, chars_to_write, chars_written, NULL);
}

static int uv_rt_linux_open_flags_from_access(DWORD desired_access, DWORD creation_disposition) {
  int flags = 0;
  int wants_read = (desired_access & GENERIC_READ) != 0u;
  int wants_write = (desired_access & GENERIC_WRITE) != 0u ||
                    (desired_access & FILE_APPEND_DATA) != 0u;
  if (wants_read && wants_write) {
    flags |= O_RDWR;
  } else if (wants_write) {
    flags |= O_WRONLY;
  } else {
    flags |= O_RDONLY;
  }
  if ((desired_access & FILE_APPEND_DATA) != 0u) {
    flags |= O_APPEND;
  }
  switch (creation_disposition) {
    case CREATE_NEW:
      flags |= O_CREAT | O_EXCL;
      break;
    case CREATE_ALWAYS:
      flags |= O_CREAT | O_TRUNC;
      break;
    case OPEN_EXISTING:
      break;
    case OPEN_ALWAYS:
      flags |= O_CREAT;
      break;
    case TRUNCATE_EXISTING:
      flags |= O_TRUNC;
      break;
    default:
      break;
  }
  return flags;
}

static int uv_rt_linux_console_fd_from_name(LPCWSTR path) {
  if (!path) {
    return -1;
  }
  if (wcscasecmp(path, L"CONOUT$") == 0) {
    int tty_fd = open("/dev/tty", O_WRONLY);
    if (tty_fd >= 0) {
      return tty_fd;
    }
    return dup(STDOUT_FILENO);
  }
  if (wcscasecmp(path, L"CONIN$") == 0) {
    int tty_fd = open("/dev/tty", O_RDONLY);
    if (tty_fd >= 0) {
      return tty_fd;
    }
    return dup(STDIN_FILENO);
  }
  return -1;
}

HANDLE CreateFileW(LPCWSTR path,
                   DWORD desired_access,
                   DWORD share_mode,
                   LPSECURITY_ATTRIBUTES security_attributes,
                   DWORD creation_disposition,
                   DWORD flags_and_attributes,
                   HANDLE template_file) {
  char* utf8_path;
  int fd;
  HANDLE handle;
  (void)share_mode;
  (void)security_attributes;
  (void)flags_and_attributes;
  (void)template_file;
  fd = uv_rt_linux_console_fd_from_name(path);
  if (fd < 0) {
    utf8_path = uv_rt_linux_path_from_wide_alloc(path);
    if (!utf8_path) {
      return INVALID_HANDLE_VALUE;
    }
    fd = open(utf8_path,
              uv_rt_linux_open_flags_from_access(desired_access, creation_disposition),
              0666);
    free(utf8_path);
  }
  if (fd < 0) {
    uv_rt_linux_set_errno_error(errno);
    return INVALID_HANDLE_VALUE;
  }
  handle = uv_rt_linux_alloc_handle(UV_RT_HANDLE_FILE);
  if (!handle) {
    close(fd);
    return INVALID_HANDLE_VALUE;
  }
  ((struct UVRtHandleBase*)handle)->u.file.fd = fd;
  SetLastError(ERROR_SUCCESS);
  return handle;
}

BOOL ReadFile(HANDLE handle,
              LPVOID buffer,
              DWORD bytes_to_read,
              DWORD* bytes_read,
              LPVOID overlapped) {
  int fd;
  ssize_t rc;
  (void)overlapped;
  if (bytes_read) {
    *bytes_read = 0u;
  }
  if (!uv_rt_linux_handle_fd(handle, &fd)) {
    return FALSE;
  }
  do {
    rc = read(fd, buffer, (size_t)bytes_to_read);
  } while (rc < 0 && errno == EINTR);
  if (rc < 0) {
    uv_rt_linux_set_errno_error(errno);
    return FALSE;
  }
  if (bytes_read) {
    *bytes_read = (DWORD)rc;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL WriteFile(HANDLE handle,
               LPCVOID buffer,
               DWORD bytes_to_write,
               DWORD* bytes_written,
               LPVOID overlapped) {
  int fd;
  ssize_t rc;
  (void)overlapped;
  if (bytes_written) {
    *bytes_written = 0u;
  }
  if (!uv_rt_linux_handle_fd(handle, &fd)) {
    return FALSE;
  }
  do {
    rc = write(fd, buffer, (size_t)bytes_to_write);
  } while (rc < 0 && errno == EINTR);
  if (rc < 0) {
    uv_rt_linux_set_errno_error(errno);
    return FALSE;
  }
  if (bytes_written) {
    *bytes_written = (DWORD)rc;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL FlushFileBuffers(HANDLE handle) {
  int fd;
  if (!uv_rt_linux_handle_fd(handle, &fd)) {
    return FALSE;
  }
  if (fsync(fd) != 0 && errno != EINVAL && errno != ENOTSUP) {
    uv_rt_linux_set_errno_error(errno);
    return FALSE;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL GetFileSizeEx(HANDLE handle, LARGE_INTEGER* size_out) {
  int fd;
  struct stat st;
  if (!size_out || !uv_rt_linux_handle_fd(handle, &fd)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (fstat(fd, &st) != 0) {
    uv_rt_linux_set_errno_error(errno);
    return FALSE;
  }
  size_out->QuadPart = (LONGLONG)st.st_size;
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL SetFilePointerEx(HANDLE handle,
                      LARGE_INTEGER distance,
                      LARGE_INTEGER* new_position,
                      DWORD move_method) {
  int fd;
  int whence;
  off_t result;
  if (!uv_rt_linux_handle_fd(handle, &fd)) {
    return FALSE;
  }
  switch (move_method) {
    case FILE_BEGIN:
      whence = SEEK_SET;
      break;
    case FILE_CURRENT:
      whence = SEEK_CUR;
      break;
    case FILE_END:
      whence = SEEK_END;
      break;
    default:
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
  }
  result = lseek(fd, (off_t)distance.QuadPart, whence);
  if (result < 0) {
    uv_rt_linux_set_errno_error(errno);
    return FALSE;
  }
  if (new_position) {
    new_position->QuadPart = (LONGLONG)result;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

static DWORD uv_rt_linux_attributes_from_stat(const struct stat* st) {
  if (S_ISDIR(st->st_mode)) {
    return FILE_ATTRIBUTE_DIRECTORY;
  }
  return FILE_ATTRIBUTE_NORMAL;
}

DWORD GetFileAttributesW(LPCWSTR path) {
  char* utf8_path = uv_rt_linux_path_from_wide_alloc(path);
  struct stat st;
  if (!utf8_path) {
    return INVALID_FILE_ATTRIBUTES;
  }
  if (stat(utf8_path, &st) != 0) {
    uv_rt_linux_set_errno_error(errno);
    free(utf8_path);
    return INVALID_FILE_ATTRIBUTES;
  }
  free(utf8_path);
  SetLastError(ERROR_SUCCESS);
  return uv_rt_linux_attributes_from_stat(&st);
}

BOOL DeleteFileW(LPCWSTR path) {
  char* utf8_path = uv_rt_linux_path_from_wide_alloc(path);
  int rc;
  if (!utf8_path) {
    return FALSE;
  }
  rc = unlink(utf8_path);
  free(utf8_path);
  if (rc != 0) {
    uv_rt_linux_set_errno_error(errno);
    return FALSE;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL RemoveDirectoryW(LPCWSTR path) {
  char* utf8_path = uv_rt_linux_path_from_wide_alloc(path);
  int rc;
  if (!utf8_path) {
    return FALSE;
  }
  rc = rmdir(utf8_path);
  free(utf8_path);
  if (rc != 0) {
    uv_rt_linux_set_errno_error(errno);
    return FALSE;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES security_attributes) {
  char* utf8_path = uv_rt_linux_path_from_wide_alloc(path);
  int rc;
  (void)security_attributes;
  if (!utf8_path) {
    return FALSE;
  }
  rc = mkdir(utf8_path, 0777);
  free(utf8_path);
  if (rc != 0) {
    uv_rt_linux_set_errno_error(errno);
    return FALSE;
  }
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}
static int uv_rt_linux_fill_find_data(const char* dir_path,
                             const char* name,
                             WIN32_FIND_DATAW* find_data) {
  char* full_path = uv_rt_linux_join_path_utf8(dir_path, name);
  struct stat st;
  wchar_t* wide_name;
  size_t wide_len = 0u;
  if (!full_path) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
  if (stat(full_path, &st) != 0) {
    free(full_path);
    uv_rt_linux_set_errno_error(errno);
    return 0;
  }
  free(full_path);
  wide_name = uv_rt_linux_wide_from_utf8_alloc(name, strlen(name), 1, &wide_len);
  if (!wide_name) {
    return 0;
  }
  if (wide_len >= (sizeof(find_data->cFileName) / sizeof(find_data->cFileName[0]))) {
    free(wide_name);
    SetLastError(ERROR_FILENAME_EXCED_RANGE);
    return 0;
  }
  memcpy(find_data->cFileName, wide_name, sizeof(wchar_t) * (wide_len + 1u));
  find_data->dwFileAttributes = uv_rt_linux_attributes_from_stat(&st);
  free(wide_name);
  SetLastError(ERROR_SUCCESS);
  return 1;
}

static int uv_rt_linux_find_next_match(struct UVRtHandleBase* handle,
                              WIN32_FIND_DATAW* find_data) {
  struct dirent* entry;
  while ((entry = readdir(handle->u.find.dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    if (handle->u.find.pattern &&
        fnmatch(handle->u.find.pattern, entry->d_name, 0) != 0) {
      continue;
    }
    return uv_rt_linux_fill_find_data(handle->u.find.dir_path, entry->d_name, find_data);
  }
  SetLastError(ERROR_NO_MORE_FILES);
  return 0;
}

HANDLE FindFirstFileW(LPCWSTR pattern, WIN32_FIND_DATAW* find_data) {
  char* utf8_pattern = uv_rt_linux_path_from_wide_alloc(pattern);
  char* last_sep;
  char* dir_path;
  char* wildcard;
  DIR* dir;
  struct UVRtHandleBase* handle;
  if (!utf8_pattern || !find_data) {
    free(utf8_pattern);
    SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_HANDLE_VALUE;
  }
  last_sep = strrchr(utf8_pattern, '/');
  if (last_sep) {
    *last_sep = '\0';
    dir_path = uv_rt_linux_strdup_or_null(utf8_pattern[0] ? utf8_pattern : "/");
    wildcard = uv_rt_linux_strdup_or_null(last_sep + 1);
  } else {
    dir_path = uv_rt_linux_strdup_or_null(".");
    wildcard = uv_rt_linux_strdup_or_null(utf8_pattern);
  }
  free(utf8_pattern);
  if (!dir_path || !wildcard) {
    free(dir_path);
    free(wildcard);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return INVALID_HANDLE_VALUE;
  }
  dir = opendir(dir_path);
  if (!dir) {
    uv_rt_linux_set_errno_error(errno);
    free(dir_path);
    free(wildcard);
    return INVALID_HANDLE_VALUE;
  }
  handle = (struct UVRtHandleBase*)uv_rt_linux_alloc_handle(UV_RT_HANDLE_FIND);
  if (!handle) {
    closedir(dir);
    free(dir_path);
    free(wildcard);
    return INVALID_HANDLE_VALUE;
  }
  handle->u.find.dir = dir;
  handle->u.find.dir_path = dir_path;
  handle->u.find.pattern = wildcard;
  if (!uv_rt_linux_find_next_match(handle, find_data)) {
    closedir(dir);
    free(dir_path);
    free(wildcard);
    free(handle);
    return INVALID_HANDLE_VALUE;
  }
  SetLastError(ERROR_SUCCESS);
  return handle;
}

BOOL FindNextFileW(HANDLE handle, WIN32_FIND_DATAW* find_data) {
  struct UVRtHandleBase* base = (struct UVRtHandleBase*)handle;
  if (!base || base->kind != UV_RT_HANDLE_FIND || !find_data) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  return uv_rt_linux_find_next_match(base, find_data) ? TRUE : FALSE;
}

BOOL FindClose(HANDLE handle) {
  return CloseHandle(handle);
}

DWORD GetTempPathW(DWORD buffer_length, LPWSTR buffer) {
  const char* temp_dir = uv_rt_linux_process_env_utf8_value("TMPDIR");
  size_t wide_len = 0u;
  wchar_t* wide_temp;
  if (!temp_dir || temp_dir[0] == '\0') {
    temp_dir = uv_rt_linux_process_env_utf8_value("TEMP");
  }
  if (!temp_dir || temp_dir[0] == '\0') {
    temp_dir = uv_rt_linux_process_env_utf8_value("TMP");
  }
  if (!temp_dir || temp_dir[0] == '\0') {
    temp_dir = "/tmp";
  }
  wide_temp = uv_rt_linux_wide_from_utf8_alloc(temp_dir, strlen(temp_dir), 1, &wide_len);
  if (!wide_temp) {
    return 0u;
  }
  if (wide_len == 0u || wide_temp[wide_len - 1u] != L'/') {
    wchar_t* expanded = (wchar_t*)malloc(sizeof(wchar_t) * (wide_len + 2u));
    if (!expanded) {
      free(wide_temp);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return 0u;
    }
    memcpy(expanded, wide_temp, sizeof(wchar_t) * wide_len);
    expanded[wide_len++] = L'/';
    expanded[wide_len] = 0;
    free(wide_temp);
    wide_temp = expanded;
  }
  if (!buffer || buffer_length <= wide_len) {
    free(wide_temp);
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return (DWORD)(wide_len + 1u);
  }
  memcpy(buffer, wide_temp, sizeof(wchar_t) * (wide_len + 1u));
  free(wide_temp);
  SetLastError(ERROR_SUCCESS);
  return (DWORD)wide_len;
}

UINT GetTempFileNameW(LPCWSTR path_name,
                      LPCWSTR prefix_string,
                      UINT unique,
                      LPWSTR temp_file_name) {
  char* dir_path = uv_rt_linux_path_from_wide_alloc(path_name);
  char* prefix = uv_rt_linux_utf8_from_wide_alloc(prefix_string,
                                         uv_rt_linux_wide_cstr_len(prefix_string),
                                         1,
                                         NULL);
  char template_path[PATH_MAX];
  int fd;
  size_t wide_len = 0u;
  wchar_t* wide_path;
  (void)unique;
  if (!dir_path || !prefix || !temp_file_name) {
    free(dir_path);
    free(prefix);
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0u;
  }
  snprintf(template_path,
           sizeof(template_path),
           "%s/%sXXXXXX",
           dir_path,
           prefix[0] ? prefix : "tmp");
  fd = mkstemp(template_path);
  free(dir_path);
  free(prefix);
  if (fd < 0) {
    uv_rt_linux_set_errno_error(errno);
    return 0u;
  }
  close(fd);
  wide_path = uv_rt_linux_wide_from_utf8_alloc(template_path, strlen(template_path), 1, &wide_len);
  if (!wide_path) {
    unlink(template_path);
    return 0u;
  }
  memcpy(temp_file_name, wide_path, sizeof(wchar_t) * (wide_len + 1u));
  free(wide_path);
  SetLastError(ERROR_SUCCESS);
  return 1u;
}

HANDLE CreateFileMappingW(HANDLE file,
                          LPSECURITY_ATTRIBUTES attributes,
                          DWORD protect,
                          DWORD maximum_size_high,
                          DWORD maximum_size_low,
                          LPCWSTR name) {
  struct UVRtHandleBase* handle;
  uint64_t requested_size = ((uint64_t)maximum_size_high << 32u) |
                            (uint64_t)maximum_size_low;
  int fd = -1;
  char* shm_name = NULL;
  (void)attributes;
  (void)protect;
  if (file == INVALID_HANDLE_VALUE) {
    char* raw_name = uv_rt_linux_utf8_from_wide_alloc(name, uv_rt_linux_wide_cstr_len(name), 1, NULL);
    size_t i;
    if (!raw_name) {
      return NULL;
    }
    shm_name = (char*)malloc(strlen(raw_name) + 2u);
    if (!shm_name) {
      free(raw_name);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return NULL;
    }
    shm_name[0] = '/';
    strcpy(shm_name + 1, raw_name);
    for (i = 1u; shm_name[i] != '\0'; ++i) {
      if (!isalnum((unsigned char)shm_name[i])) {
        shm_name[i] = '_';
      }
    }
    free(raw_name);
    fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
    if (fd >= 0 && requested_size != 0u && ftruncate(fd, (off_t)requested_size) != 0) {
      close(fd);
      fd = -1;
    }
    free(shm_name);
  } else if (file && ((struct UVRtHandleBase*)file)->kind == UV_RT_HANDLE_FILE) {
    fd = dup(((struct UVRtHandleBase*)file)->u.file.fd);
    if (fd >= 0 && requested_size != 0u) {
      ftruncate(fd, (off_t)requested_size);
    }
  }
  if (fd < 0) {
    uv_rt_linux_set_errno_error(errno);
    return NULL;
  }
  handle = (struct UVRtHandleBase*)uv_rt_linux_alloc_handle(UV_RT_HANDLE_MAPPING);
  if (!handle) {
    close(fd);
    return NULL;
  }
  handle->u.mapping.fd = fd;
  handle->u.mapping.size = (size_t)requested_size;
  SetLastError(ERROR_SUCCESS);
  return handle;
}

LPVOID MapViewOfFile(HANDLE mapping,
                     DWORD desired_access,
                     DWORD file_offset_high,
                     DWORD file_offset_low,
                     size_t number_of_bytes_to_map) {
  struct UVRtHandleBase* base = (struct UVRtHandleBase*)mapping;
  off_t offset = (off_t)(((uint64_t)file_offset_high << 32u) | (uint64_t)file_offset_low);
  size_t map_size;
  int prot = PROT_READ;
  void* address;
  UVMapViewNode* node;
  if (!base || base->kind != UV_RT_HANDLE_MAPPING) {
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  map_size = number_of_bytes_to_map ? number_of_bytes_to_map : base->u.mapping.size;
  if ((desired_access & FILE_MAP_ALL_ACCESS) != 0u) {
    prot |= PROT_WRITE;
  }
  address = mmap(NULL, map_size, prot, MAP_SHARED, base->u.mapping.fd, offset);
  if (address == MAP_FAILED) {
    uv_rt_linux_set_errno_error(errno);
    return NULL;
  }
  node = (UVMapViewNode*)malloc(sizeof(UVMapViewNode));
  if (!node) {
    munmap(address, map_size);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  node->address = address;
  node->size = map_size;
  EnterCriticalSection(&uv_rt_linux_map_view_lock);
  node->next = uv_rt_linux_map_views;
  uv_rt_linux_map_views = node;
  LeaveCriticalSection(&uv_rt_linux_map_view_lock);
  SetLastError(ERROR_SUCCESS);
  return address;
}

BOOL UnmapViewOfFile(LPCVOID base_address) {
  UVMapViewNode** link;
  UVMapViewNode* node;
  if (!base_address) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  EnterCriticalSection(&uv_rt_linux_map_view_lock);
  link = &uv_rt_linux_map_views;
  while (*link && (*link)->address != base_address) {
    link = &(*link)->next;
  }
  if (!*link) {
    LeaveCriticalSection(&uv_rt_linux_map_view_lock);
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  node = *link;
  *link = node->next;
  LeaveCriticalSection(&uv_rt_linux_map_view_lock);
  munmap((void*)base_address, node->size);
  free(node);
  SetLastError(ERROR_SUCCESS);
  return TRUE;
}

BOOL CloseHandle(HANDLE handle) {
  struct UVRtHandleBase* base = (struct UVRtHandleBase*)handle;
  DWORD saved_error = GetLastError();
  if (!base || handle == INVALID_HANDLE_VALUE) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (!base->owned) {
    SetLastError(saved_error);
    return TRUE;
  }
  switch (base->kind) {
    case UV_RT_HANDLE_PROCESS:
      if (!base->u.process.waited) {
        waitpid(base->u.process.pid, NULL, WNOHANG);
      }
      break;
    case UV_RT_HANDLE_THREAD:
      DeleteCriticalSection(&base->u.thread.lock);
      break;
    case UV_RT_HANDLE_EVENT:
      DeleteCriticalSection(&base->u.event.mutex);
      break;
    case UV_RT_HANDLE_FILE:
      close(base->u.file.fd);
      break;
    case UV_RT_HANDLE_FIND:
      if (base->u.find.dir) {
        closedir(base->u.find.dir);
      }
      free(base->u.find.dir_path);
      free(base->u.find.pattern);
      break;
    case UV_RT_HANDLE_MAPPING:
      close(base->u.mapping.fd);
      break;
    case UV_RT_HANDLE_TOKEN:
      break;
    default:
      break;
  }
  free(base);
  SetLastError(saved_error);
  return TRUE;
}

HMODULE GetModuleHandleA(const char* name) {
  (void)name;
  SetLastError(ERROR_INVALID_PARAMETER);
  return NULL;
}

HMODULE LoadLibraryA(const char* name) {
  (void)name;
  SetLastError(ERROR_INVALID_PARAMETER);
  return NULL;
}

void* GetProcAddress(HMODULE module, const char* symbol_name) {
  (void)module;
  (void)symbol_name;
  SetLastError(ERROR_INVALID_PARAMETER);
  return NULL;
}

void DebugBreak(void) {
  raise(SIGTRAP);
}

void GetSystemTimeAsFileTime(FILETIME* file_time) {
  static const uint64_t kWindowsEpochOffsetSeconds = 11644473600ULL;
  struct timespec ts;
  uint64_t ticks;
  if (!file_time) {
    return;
  }
  clock_gettime(CLOCK_REALTIME, &ts);
  ticks = ((uint64_t)ts.tv_sec + kWindowsEpochOffsetSeconds) * 10000000ULL;
  ticks += (uint64_t)(ts.tv_nsec / 100u);
  file_time->dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFFu);
  file_time->dwHighDateTime = (DWORD)(ticks >> 32u);
}

uv_platform_u32_t uv_platform_backend_error_get(void) {
  return GetLastError();
}

void uv_platform_backend_error_set(uv_platform_u32_t error_code) {
  SetLastError(error_code);
}

uv_platform_handle_t uv_platform_backend_heap_handle(void) {
  return GetProcessHeap();
}

void* uv_platform_backend_heap_alloc(uv_platform_handle_t heap,
                                          uv_platform_u32_t flags,
                                          size_t bytes) {
  return HeapAlloc(heap, flags, bytes);
}

uv_platform_bool_t uv_platform_backend_heap_free(
    uv_platform_handle_t heap,
    uv_platform_u32_t flags,
    void* memory) {
  return HeapFree(heap, flags, memory);
}

uv_platform_bool_t uv_platform_backend_heap_validate(
    uv_platform_handle_t heap,
    uv_platform_u32_t flags,
    const void* memory) {
  return HeapValidate(heap, flags, memory);
}

void uv_platform_backend_exit_process(uv_platform_uint_t exit_code) {
  ExitProcess(exit_code);
}

uv_platform_u32_t uv_platform_backend_env_get_wide(
    const wchar_t* name,
    wchar_t* buffer,
    uv_platform_u32_t size) {
  return GetEnvironmentVariableW(name, buffer, size);
}

uv_platform_u32_t uv_platform_backend_env_get_utf8(
    const char* name,
    char* buffer,
    uv_platform_u32_t size) {
  return GetEnvironmentVariableA(name, buffer, size);
}

uv_platform_u32_t uv_platform_backend_executable_path_utf8(
    char* buffer,
    uv_platform_u32_t size) {
  char path[PATH_MAX + 1];
  ssize_t len = readlink("/proc/self/exe", path, PATH_MAX);

  if (len < 0) {
    const uv_rt_process_start_t* start = uv_rt_startup_current();
    if (!start || start->argc <= 0 || !start->argv || !start->argv[0]) {
      return 0u;
    }
    size_t fallback_len = uv_rt_linux_utf8_cstr_len(start->argv[0]);
    if (fallback_len >= (size_t)UINT32_MAX) {
      return 0u;
    }
    uv_platform_u32_t required =
        (uv_platform_u32_t)fallback_len + 1u;
    if (!buffer || size < required) {
      return required;
    }
    memcpy(buffer, start->argv[0], fallback_len);
    buffer[fallback_len] = '\0';
    return (uv_platform_u32_t)fallback_len;
  }

  if (len > PATH_MAX) {
    return 0u;
  }
  path[len] = '\0';

  uv_platform_u32_t required = (uv_platform_u32_t)len + 1u;
  if (!buffer || size < required) {
    return required;
  }
  memcpy(buffer, path, (size_t)len);
  buffer[len] = '\0';
  return (uv_platform_u32_t)len;
}

uv_platform_uptr_t uv_platform_backend_argument_count(void) {
  const uv_rt_process_start_t* start = uv_rt_startup_current();
  if (!start || start->argc <= 1 || !start->argv) {
    return 0u;
  }
  return (uv_platform_uptr_t)(start->argc - 1);
}

uv_platform_u32_t uv_platform_backend_argument_utf8(
    uv_platform_uptr_t index,
    char* buffer,
    uv_platform_u32_t size) {
  const uv_rt_process_start_t* start = uv_rt_startup_current();
  if (!start || start->argc <= 1 || !start->argv ||
      index >= (uv_platform_uptr_t)(start->argc - 1)) {
    return 0u;
  }

  const char* argument = start->argv[index + 1u];
  if (!argument) {
    return 0u;
  }

  size_t len = uv_rt_linux_utf8_cstr_len(argument);
  if (len >= (size_t)UINT32_MAX) {
    return 0u;
  }
  uv_platform_u32_t required = (uv_platform_u32_t)len + 1u;
  if (!buffer || size < required) {
    return required;
  }
  memcpy(buffer, argument, len);
  buffer[len] = '\0';
  return (uv_platform_u32_t)len;
}

uv_platform_u32_t uv_platform_backend_current_directory_utf8(
    char* buffer,
    uv_platform_u32_t size) {
  char cwd[PATH_MAX + 1];
  if (!getcwd(cwd, sizeof(cwd))) {
    return 0u;
  }

  size_t len = uv_rt_linux_utf8_cstr_len(cwd);
  if (len >= (size_t)UINT32_MAX) {
    return 0u;
  }
  uv_platform_u32_t required = (uv_platform_u32_t)len + 1u;
  if (!buffer || size < required) {
    return required;
  }
  memcpy(buffer, cwd, len);
  buffer[len] = '\0';
  return (uv_platform_u32_t)len;
}

void uv_platform_backend_icu_data_configure(void) {
  char data_dir[PATH_MAX];
  if (atomic_exchange(&uv_rt_linux_icu_data_configured, 1) != 0) {
    return;
  }
  if (getenv("ICU_DATA") != NULL) {
    return;
  }
  if (!uv_rt_linux_data_directory_from_module(data_dir, sizeof(data_dir))) {
    return;
  }
  u_setDataDirectory(data_dir);
}

int uv_platform_backend_utf8_to_wide_chars(const char* source,
                                                int source_length,
                                                wchar_t* destination,
                                                int destination_length) {
  return MultiByteToWideChar(65001u,
                             MB_ERR_INVALID_CHARS,
                             source,
                             source_length,
                             destination,
                             destination_length);
}

int uv_platform_backend_wide_to_utf8_chars(const wchar_t* source,
                                                int source_length,
                                                char* destination,
                                                int destination_length) {
  return WideCharToMultiByte(65001u,
                             0u,
                             source,
                             source_length,
                             destination,
                             destination_length,
                             NULL,
                             NULL);
}

uv_platform_bool_t uv_platform_backend_process_create(
    const wchar_t* application_name,
    wchar_t* command_line,
    void* process_attributes,
    void* thread_attributes,
    uv_platform_bool_t inherit_handles,
    uv_platform_u32_t creation_flags,
    void* environment,
    const wchar_t* current_directory,
    uv_platform_process_startup_t* startup_info,
    uv_platform_process_info_t* process_information) {
  STARTUPINFOW native_startup;
  PROCESS_INFORMATION native_process_info;
  STARTUPINFOW* native_startup_ptr = NULL;
  PROCESS_INFORMATION* native_process_info_ptr = NULL;

  if (startup_info) {
    native_startup.cb = startup_info->size_bytes;
    native_startup.dwFlags = startup_info->flags;
    native_startup.hStdInput = startup_info->stdin_handle;
    native_startup.hStdOutput = startup_info->stdout_handle;
    native_startup.hStdError = startup_info->stderr_handle;
    native_startup_ptr = &native_startup;
  }

  if (process_information) {
    native_process_info.hProcess = UV_PLATFORM_BACKEND_INVALID_HANDLE;
    native_process_info.hThread = UV_PLATFORM_BACKEND_INVALID_HANDLE;
    native_process_info.dwProcessId = 0;
    native_process_info.dwThreadId = 0;
    native_process_info_ptr = &native_process_info;
  }

  if (!CreateProcessW(application_name,
                      command_line,
                      process_attributes,
                      thread_attributes,
                      inherit_handles,
                      creation_flags,
                      environment,
                      current_directory,
                      native_startup_ptr,
                      native_process_info_ptr)) {
    return UV_PLATFORM_BACKEND_FALSE;
  }

  if (process_information) {
    process_information->process_handle = native_process_info.hProcess;
    process_information->thread_handle = native_process_info.hThread;
    process_information->process_id = native_process_info.dwProcessId;
    process_information->thread_id = native_process_info.dwThreadId;
  }
  return UV_PLATFORM_BACKEND_TRUE;
}

uv_platform_u32_t uv_platform_backend_wait_one(
    uv_platform_handle_t handle,
    uv_platform_u32_t milliseconds) {
  return WaitForSingleObject(handle, milliseconds);
}

uv_platform_bool_t uv_platform_backend_process_exit_code(
    uv_platform_handle_t handle,
    uv_platform_u32_t* exit_code) {
  return GetExitCodeProcess(handle, exit_code);
}

uv_platform_bool_t uv_platform_backend_close_handle(
    uv_platform_handle_t handle) {
  return CloseHandle(handle);
}

uv_platform_module_t uv_platform_backend_module_get(const char* name) {
  return GetModuleHandleA(name);
}

uv_platform_module_t uv_platform_backend_module_load(
    const char* name) {
  return LoadLibraryA(name);
}

void* uv_platform_backend_module_symbol(uv_platform_module_t module,
                                             const char* symbol_name) {
  return GetProcAddress(module, symbol_name);
}

uv_platform_bool_t uv_platform_backend_once_execute(
    uv_platform_once_t* init_once,
    uv_platform_once_callback_t init_fn,
    void* parameter,
    void** context) {
  return InitOnceExecuteOnce(init_once, init_fn, parameter, context);
}

uv_platform_tls_key_t uv_platform_backend_tls_key_create(void) {
  return TlsAlloc();
}

void* uv_platform_backend_tls_get(uv_platform_tls_key_t index) {
  return TlsGetValue(index);
}

uv_platform_bool_t uv_platform_backend_tls_set(
    uv_platform_tls_key_t index,
    void* value) {
  return TlsSetValue(index, value);
}

void uv_platform_backend_mutex_init(uv_platform_mutex_t* mutex) {
  InitializeCriticalSection(mutex);
}

void uv_platform_backend_mutex_destroy(uv_platform_mutex_t* mutex) {
  DeleteCriticalSection(mutex);
}

void uv_platform_backend_mutex_lock(uv_platform_mutex_t* mutex) {
  EnterCriticalSection(mutex);
}

void uv_platform_backend_mutex_unlock(uv_platform_mutex_t* mutex) {
  LeaveCriticalSection(mutex);
}

void uv_platform_backend_condition_init(
    uv_platform_condition_t* condition) {
  InitializeConditionVariable(condition);
}

uv_platform_bool_t uv_platform_backend_condition_wait(
    uv_platform_condition_t* condition,
    uv_platform_mutex_t* mutex,
    uv_platform_u32_t milliseconds) {
  return SleepConditionVariableCS(condition, mutex, milliseconds);
}

void uv_platform_backend_condition_wake_one(
    uv_platform_condition_t* condition) {
  WakeConditionVariable(condition);
}

void uv_platform_backend_condition_wake_all(
    uv_platform_condition_t* condition) {
  WakeAllConditionVariable(condition);
}

void uv_platform_backend_rwlock_init(uv_platform_rwlock_t* lock) {
  InitializeSRWLock(lock);
}

void uv_platform_backend_rwlock_lock_exclusive(
    uv_platform_rwlock_t* lock) {
  AcquireSRWLockExclusive(lock);
}

void uv_platform_backend_rwlock_unlock_exclusive(
    uv_platform_rwlock_t* lock) {
  ReleaseSRWLockExclusive(lock);
}

void uv_platform_backend_rwlock_lock_shared(
    uv_platform_rwlock_t* lock) {
  AcquireSRWLockShared(lock);
}

void uv_platform_backend_rwlock_unlock_shared(
    uv_platform_rwlock_t* lock) {
  ReleaseSRWLockShared(lock);
}

uv_platform_i32_t uv_platform_backend_atomic_exchange(
    volatile uv_platform_i32_t* target,
    uv_platform_i32_t value) {
  return InterlockedExchange(target, value);
}

uv_platform_i32_t uv_platform_backend_atomic_compare_exchange(
    volatile uv_platform_i32_t* target,
    uv_platform_i32_t exchange,
    uv_platform_i32_t comparand) {
  return InterlockedCompareExchange(target, exchange, comparand);
}

uv_platform_i64_t uv_platform_backend_atomic_increment64(
    volatile uv_platform_i64_t* target) {
  return InterlockedIncrement64(target);
}

uv_platform_thread_id_t uv_platform_backend_current_thread_id(void) {
  return GetCurrentThreadId();
}

uv_platform_u32_t uv_platform_backend_current_process_id(void) {
  return GetCurrentProcessId();
}

uv_platform_handle_t uv_platform_backend_current_thread(void) {
  return GetCurrentThread();
}

int uv_platform_backend_thread_priority_get(
    uv_platform_handle_t thread) {
  return GetThreadPriority(thread);
}

uv_platform_bool_t uv_platform_backend_thread_priority_set(
    uv_platform_handle_t thread,
    int priority) {
  return SetThreadPriority(thread, priority);
}

uv_platform_uptr_t uv_platform_backend_thread_affinity_set(
    uv_platform_handle_t thread,
    uv_platform_uptr_t mask) {
  return SetThreadAffinityMask(thread, mask);
}

uv_platform_handle_t uv_platform_backend_thread_create(
    void* attributes,
    size_t stack_size,
    uv_platform_thread_start_routine_t start_routine,
    void* parameter,
    uv_platform_u32_t creation_flags,
    uv_platform_u32_t* thread_id) {
  return CreateThread(attributes,
                      stack_size,
                      start_routine,
                      parameter,
                      creation_flags,
                      thread_id);
}

uv_platform_bool_t uv_platform_backend_thread_exit_code(
    uv_platform_handle_t handle,
    uv_platform_u32_t* exit_code) {
  return GetExitCodeThread(handle, exit_code);
}

uv_platform_handle_t uv_platform_backend_event_create(
    void* attributes,
    uv_platform_bool_t manual_reset,
    uv_platform_bool_t initial_state,
    const wchar_t* name) {
  return CreateEventW(attributes, manual_reset, initial_state, name);
}

uv_platform_bool_t uv_platform_backend_event_set(
    uv_platform_handle_t handle) {
  return SetEvent(handle);
}

uv_platform_bool_t uv_platform_backend_event_reset(
    uv_platform_handle_t handle) {
  return ResetEvent(handle);
}

uv_platform_handle_t uv_platform_backend_std_handle(
    uv_platform_u32_t std_handle_id) {
  return GetStdHandle(std_handle_id);
}

uv_platform_bool_t uv_platform_backend_std_handle_set(
    uv_platform_u32_t std_handle_id,
    uv_platform_handle_t handle) {
  return SetStdHandle(std_handle_id, handle);
}

uv_platform_bool_t uv_platform_backend_console_mode_get(
    uv_platform_handle_t handle,
    uv_platform_u32_t* mode) {
  return GetConsoleMode(handle, mode);
}

uv_platform_bool_t uv_platform_backend_console_write_utf8(
    uv_platform_handle_t handle,
    const void* buffer,
    uv_platform_u32_t chars_to_write,
    uv_platform_u32_t* chars_written) {
  return WriteConsoleA(handle, buffer, chars_to_write, chars_written, NULL);
}

uv_platform_bool_t uv_platform_backend_handle_write(
    uv_platform_handle_t handle,
    const void* buffer,
    uv_platform_u32_t bytes_to_write,
    uv_platform_u32_t* bytes_written) {
  return WriteFile(handle, buffer, bytes_to_write, bytes_written, NULL);
}

uv_platform_bool_t uv_platform_backend_handle_read(
    uv_platform_handle_t handle,
    void* buffer,
    uv_platform_u32_t bytes_to_read,
    uv_platform_u32_t* bytes_read) {
  return ReadFile(handle, buffer, bytes_to_read, bytes_read, NULL);
}

uv_platform_handle_t uv_platform_backend_file_open_wide(
    const wchar_t* path,
    uv_platform_u32_t desired_access,
    uv_platform_u32_t share_mode,
    void* security_attributes,
    uv_platform_u32_t creation_disposition,
    uv_platform_u32_t flags_and_attributes,
    uv_platform_handle_t template_file) {
  return CreateFileW(path,
                     desired_access,
                     share_mode,
                     security_attributes,
                     creation_disposition,
                     flags_and_attributes,
                     template_file);
}

uv_platform_bool_t uv_platform_backend_handle_flush(
    uv_platform_handle_t handle) {
  return FlushFileBuffers(handle);
}

uv_platform_bool_t uv_platform_backend_file_size_get(
    uv_platform_handle_t handle,
    uv_platform_large_integer_t* size_out) {
  LARGE_INTEGER size;
  const BOOL ok = GetFileSizeEx(handle, &size);
  if (ok && size_out) {
    size_out->quad_part = size.QuadPart;
  }
  return ok;
}

uv_platform_bool_t uv_platform_backend_file_pointer_set(
    uv_platform_handle_t handle,
    uv_platform_large_integer_t distance,
    uv_platform_large_integer_t* new_position,
    uv_platform_u32_t move_method) {
  LARGE_INTEGER native_distance;
  LARGE_INTEGER native_new_position;
  native_distance.QuadPart = distance.quad_part;
  const BOOL ok = SetFilePointerEx(handle,
                                   native_distance,
                                   new_position ? &native_new_position : NULL,
                                   move_method);
  if (ok && new_position) {
    new_position->quad_part = native_new_position.QuadPart;
  }
  return ok;
}

uv_platform_u32_t uv_platform_backend_file_attributes_get_wide(
    const wchar_t* path) {
  return GetFileAttributesW(path);
}

uv_platform_bool_t uv_platform_backend_file_delete_wide(
    const wchar_t* path) {
  return DeleteFileW(path);
}

uv_platform_bool_t uv_platform_backend_directory_remove_wide(
    const wchar_t* path) {
  return RemoveDirectoryW(path);
}

uv_platform_bool_t uv_platform_backend_directory_create_wide(
    const wchar_t* path,
    void* security_attributes) {
  return CreateDirectoryW(path, security_attributes);
}

uv_platform_u32_t uv_platform_backend_temp_path_get_wide(
    uv_platform_u32_t buffer_length,
    wchar_t* buffer) {
  return GetTempPathW(buffer_length, buffer);
}

uv_platform_uint_t uv_platform_backend_temp_file_name_wide(
    const wchar_t* path_name,
    const wchar_t* prefix_string,
    uv_platform_uint_t unique,
    wchar_t* temp_file_name) {
  return GetTempFileNameW(path_name, prefix_string, unique, temp_file_name);
}

uv_platform_handle_t uv_platform_backend_find_first_wide(
    const wchar_t* pattern,
    uv_platform_find_data_t* find_data) {
  WIN32_FIND_DATAW native_find_data;
  uv_platform_handle_t handle =
      FindFirstFileW(pattern, find_data ? &native_find_data : NULL);
  if (handle != UV_PLATFORM_BACKEND_INVALID_HANDLE && find_data) {
    find_data->file_attributes = native_find_data.dwFileAttributes;
    for (size_t i = 0;
         i < (sizeof(find_data->file_name) / sizeof(find_data->file_name[0]));
         ++i) {
      find_data->file_name[i] = native_find_data.cFileName[i];
      if (native_find_data.cFileName[i] == 0) {
        break;
      }
    }
  }
  return handle;
}

uv_platform_bool_t uv_platform_backend_find_next(
    uv_platform_handle_t handle,
    uv_platform_find_data_t* find_data) {
  WIN32_FIND_DATAW native_find_data;
  const BOOL ok = FindNextFileW(handle, find_data ? &native_find_data : NULL);
  if (ok && find_data) {
    find_data->file_attributes = native_find_data.dwFileAttributes;
    for (size_t i = 0;
         i < (sizeof(find_data->file_name) / sizeof(find_data->file_name[0]));
         ++i) {
      find_data->file_name[i] = native_find_data.cFileName[i];
      if (native_find_data.cFileName[i] == 0) {
        break;
      }
    }
  }
  return ok;
}

uv_platform_bool_t uv_platform_backend_find_close(
    uv_platform_handle_t handle) {
  return FindClose(handle);
}

uv_platform_handle_t uv_platform_backend_mapping_create(
    uv_platform_handle_t file,
    void* attributes,
    uv_platform_u32_t protect,
    uv_platform_u32_t maximum_size_high,
    uv_platform_u32_t maximum_size_low,
    const wchar_t* name) {
  return CreateFileMappingW(file,
                            attributes,
                            protect,
                            maximum_size_high,
                            maximum_size_low,
                            name);
}

void* uv_platform_backend_mapping_view(
    uv_platform_handle_t mapping,
    uv_platform_u32_t desired_access,
    uv_platform_u32_t file_offset_high,
    uv_platform_u32_t file_offset_low,
    size_t number_of_bytes_to_map) {
  return MapViewOfFile(mapping,
                       desired_access,
                       file_offset_high,
                       file_offset_low,
                       number_of_bytes_to_map);
}

uv_platform_bool_t uv_platform_backend_mapping_unview(
    const void* base_address) {
  return UnmapViewOfFile(base_address);
}

void uv_platform_backend_debug_break(void) {
  DebugBreak();
}

void uv_platform_backend_system_time_filetime(
    uv_platform_filetime_t* file_time) {
  GetSystemTimeAsFileTime((FILETIME*)file_time);
}

uv_platform_bool_t uv_platform_backend_panic_boundary_active(void) {
  UVRtLinuxThreadState* state = uv_rt_linux_current_thread_state(0);
  return (state && state->panic_scope != NULL) ? UV_PLATFORM_TRUE
                                               : UV_PLATFORM_FALSE;
}

uv_platform_bool_t uv_platform_backend_panic_boundary_run(
    uv_rt_panic_boundary_body_t body,
    void* context,
    uv_platform_u32_t* panic_code) {
  UVRtLinuxThreadState* state;
  UVRtLinuxPanicScope scope;

  if (panic_code) {
    *panic_code = 0u;
  }
  if (!body) {
    return UV_PLATFORM_FALSE;
  }

  state = uv_rt_linux_current_thread_state(1);
  if (!state) {
    return UV_PLATFORM_FALSE;
  }
  scope.prev = state->panic_scope;
  scope.panic_code = 0u;
  state->panic_scope = &scope;
  if (setjmp(scope.jump_env) == 0) {
    body(context);
    state->panic_scope = scope.prev;
    return UV_PLATFORM_TRUE;
  }

  state->panic_scope = scope.prev;
  if (panic_code) {
    *panic_code = scope.panic_code;
  }
  return UV_PLATFORM_FALSE;
}

void uv_platform_backend_panic_boundary_raise(
    uv_platform_u32_t panic_code) {
  UVRtLinuxThreadState* state = uv_rt_linux_current_thread_state(0);
  if (!state || !state->panic_scope) {
    ExitProcess((UINT)panic_code);
  }
  state->panic_scope->panic_code = panic_code;
  longjmp(state->panic_scope->jump_env, 1);
}

#endif  // !_WIN32
