#ifndef UV_RT_PLATFORM_LINUX_COMPAT_H
#define UV_RT_PLATFORM_LINUX_COMPAT_H

#ifndef UV_RT_PLATFORM_LINUX
#define UV_RT_PLATFORM_LINUX
#endif

typedef void (*uv_rt_panic_boundary_body_t)(void* context);

#include "rt_platform_linux.h"

#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int BOOL;
typedef uint32_t DWORD;
typedef uintptr_t DWORD_PTR;
typedef int32_t LONG;
typedef int64_t LONG64;
typedef int64_t LONGLONG;
typedef unsigned int UINT;
typedef uintptr_t ULONG_PTR;
typedef void* PVOID;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef const char* LPCCH;
typedef wchar_t WCHAR;
typedef WCHAR* LPWSTR;
typedef const WCHAR* LPCWSTR;
typedef void* HMODULE;

typedef uv_platform_handle_t HANDLE;

typedef struct SECURITY_ATTRIBUTES {
  DWORD nLength;
  LPVOID lpSecurityDescriptor;
  BOOL bInheritHandle;
} SECURITY_ATTRIBUTES;
typedef SECURITY_ATTRIBUTES* PSECURITY_ATTRIBUTES;
typedef SECURITY_ATTRIBUTES* LPSECURITY_ATTRIBUTES;

typedef struct STARTUPINFOW {
  DWORD cb;
  DWORD dwFlags;
  HANDLE hStdInput;
  HANDLE hStdOutput;
  HANDLE hStdError;
} STARTUPINFOW;
typedef STARTUPINFOW* LPSTARTUPINFOW;

typedef struct PROCESS_INFORMATION {
  HANDLE hProcess;
  HANDLE hThread;
  DWORD dwProcessId;
  DWORD dwThreadId;
} PROCESS_INFORMATION;
typedef PROCESS_INFORMATION* LPPROCESS_INFORMATION;

typedef struct LARGE_INTEGER {
  LONGLONG QuadPart;
} LARGE_INTEGER;
typedef LARGE_INTEGER* PLARGE_INTEGER;

typedef union ULARGE_INTEGER {
  struct {
    DWORD LowPart;
    DWORD HighPart;
  };
  uint64_t QuadPart;
} ULARGE_INTEGER;
typedef ULARGE_INTEGER* PULARGE_INTEGER;

typedef struct FILETIME {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME;
typedef FILETIME* PFILETIME;

typedef struct WIN32_FIND_DATAW {
  DWORD dwFileAttributes;
  WCHAR cFileName[4096];
} WIN32_FIND_DATAW;
typedef WIN32_FIND_DATAW* PWIN32_FIND_DATAW;

typedef uv_platform_mutex_t CRITICAL_SECTION;
typedef uv_platform_condition_t CONDITION_VARIABLE;
typedef uv_platform_rwlock_t SRWLOCK;

typedef uv_platform_once_t INIT_ONCE;
typedef INIT_ONCE* PINIT_ONCE;

typedef BOOL (*PINIT_ONCE_FN)(PINIT_ONCE init_once,
                              PVOID parameter,
                              PVOID* context);
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID parameter);

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define CALLBACK
#define WINAPI
#define MAX_PATH 260
#define CP_UTF8 65001u
#define MB_ERR_INVALID_CHARS 0x00000008u
#define TLS_OUT_OF_INDEXES ((DWORD)0xFFFFFFFFu)
#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0u
#define WAIT_FAILED 0xFFFFFFFFu
#define WAIT_TIMEOUT 258u
#define STILL_ACTIVE 259u
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)
#define FILE_BEGIN 0u
#define FILE_CURRENT 1u
#define FILE_END 2u
#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u
#define FILE_SHARE_DELETE 0x00000004u
#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define TRUNCATE_EXISTING 5u
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_APPEND_DATA 0x00000004u
#define PAGE_READWRITE 0x00000004u
#define FILE_MAP_ALL_ACCESS 0x000F001Fu
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_ERROR_RETURN 0x7FFFFFFFu

#define ERROR_SUCCESS 0u
#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_PATH_NOT_FOUND 3u
#define ERROR_ACCESS_DENIED 5u
#define ERROR_INVALID_HANDLE 6u
#define ERROR_NOT_ENOUGH_MEMORY 8u
#define ERROR_INVALID_DRIVE 15u
#define ERROR_NO_MORE_FILES 18u
#define ERROR_WRITE_PROTECT 19u
#define ERROR_BAD_PATHNAME 161u
#define ERROR_ALREADY_EXISTS 183u
#define ERROR_ENVVAR_NOT_FOUND 203u
#define ERROR_FILENAME_EXCED_RANGE 206u
#define ERROR_DIRECTORY 267u
#define ERROR_PRIVILEGE_NOT_HELD 1314u
#define ERROR_FILE_EXISTS 80u
#define ERROR_BUSY 170u
#define ERROR_SHARING_VIOLATION 32u
#define ERROR_LOCK_VIOLATION 33u
#define ERROR_PIPE_BUSY 231u
#define ERROR_INVALID_NAME 123u
#define ERROR_INVALID_PARAMETER 87u
#define ERROR_INSUFFICIENT_BUFFER 122u

#define INIT_ONCE_STATIC_INIT { 0, NULL }
#define SRWLOCK_INIT { { 0u }, { 0u }, 0u, 0u, 0u }
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

DWORD GetLastError(void);
void SetLastError(DWORD error_code);

HANDLE GetProcessHeap(void);
LPVOID HeapAlloc(HANDLE heap, DWORD flags, size_t bytes);
BOOL HeapFree(HANDLE heap, DWORD flags, LPVOID memory);
BOOL HeapValidate(HANDLE heap, DWORD flags, LPCVOID memory);

void ExitProcess(UINT exit_code);

int MultiByteToWideChar(UINT code_page,
                        DWORD flags,
                        LPCCH source,
                        int source_length,
                        LPWSTR destination,
                        int destination_length);
int WideCharToMultiByte(UINT code_page,
                        DWORD flags,
                        LPCWSTR source,
                        int source_length,
                        char* destination,
                        int destination_length,
                        const char* default_char,
                        BOOL* used_default_char);

DWORD GetEnvironmentVariableW(LPCWSTR name, LPWSTR buffer, DWORD size);
DWORD GetEnvironmentVariableA(const char* name, char* buffer, DWORD size);

BOOL CreateProcessW(LPCWSTR application_name,
                    LPWSTR command_line,
                    LPSECURITY_ATTRIBUTES process_attributes,
                    LPSECURITY_ATTRIBUTES thread_attributes,
                    BOOL inherit_handles,
                    DWORD creation_flags,
                    LPVOID environment,
                    LPCWSTR current_directory,
                    LPSTARTUPINFOW startup_info,
                    LPPROCESS_INFORMATION process_information);
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
DWORD WaitForMultipleObjects(DWORD count,
                             const HANDLE* handles,
                             BOOL wait_all,
                             DWORD milliseconds);
BOOL GetExitCodeProcess(HANDLE handle, DWORD* exit_code);
BOOL GetExitCodeThread(HANDLE handle, DWORD* exit_code);

DWORD TlsAlloc(void);
LPVOID TlsGetValue(DWORD index);
BOOL TlsSetValue(DWORD index, LPVOID value);

BOOL InitOnceExecuteOnce(PINIT_ONCE init_once,
                         PINIT_ONCE_FN init_fn,
                         PVOID parameter,
                         PVOID* context);

void InitializeCriticalSection(CRITICAL_SECTION* section);
void DeleteCriticalSection(CRITICAL_SECTION* section);
void EnterCriticalSection(CRITICAL_SECTION* section);
void LeaveCriticalSection(CRITICAL_SECTION* section);

void InitializeConditionVariable(CONDITION_VARIABLE* condition);
BOOL SleepConditionVariableCS(CONDITION_VARIABLE* condition,
                              CRITICAL_SECTION* section,
                              DWORD milliseconds);
void WakeConditionVariable(CONDITION_VARIABLE* condition);
void WakeAllConditionVariable(CONDITION_VARIABLE* condition);

void InitializeSRWLock(SRWLOCK* lock);
void AcquireSRWLockExclusive(SRWLOCK* lock);
void ReleaseSRWLockExclusive(SRWLOCK* lock);
void AcquireSRWLockShared(SRWLOCK* lock);
void ReleaseSRWLockShared(SRWLOCK* lock);

LONG InterlockedExchange(volatile LONG* target, LONG value);
LONG InterlockedCompareExchange(volatile LONG* target,
                                LONG exchange,
                                LONG comparand);
LONG64 InterlockedIncrement64(volatile LONG64* target);

DWORD GetCurrentProcessId(void);
DWORD GetCurrentThreadId(void);
HANDLE GetCurrentThread(void);
int GetThreadPriority(HANDLE thread);
BOOL SetThreadPriority(HANDLE thread, int priority);
DWORD_PTR SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES attributes,
                    size_t stack_size,
                    LPTHREAD_START_ROUTINE start_routine,
                    LPVOID parameter,
                    DWORD creation_flags,
                    DWORD* thread_id);

HANDLE CreateEvent(LPSECURITY_ATTRIBUTES attributes,
                   BOOL manual_reset,
                   BOOL initial_state,
                   LPCWSTR name);
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES attributes,
                    BOOL manual_reset,
                    BOOL initial_state,
                    LPCWSTR name);
BOOL SetEvent(HANDLE handle);
BOOL ResetEvent(HANDLE handle);

HANDLE GetStdHandle(DWORD std_handle_id);
BOOL SetStdHandle(DWORD std_handle_id, HANDLE handle);
BOOL GetConsoleMode(HANDLE handle, DWORD* mode);
BOOL WriteConsoleA(HANDLE handle,
                   LPCVOID buffer,
                   DWORD chars_to_write,
                   DWORD* chars_written,
                   LPVOID reserved);

HANDLE CreateFileW(LPCWSTR path,
                   DWORD desired_access,
                   DWORD share_mode,
                   LPSECURITY_ATTRIBUTES security_attributes,
                   DWORD creation_disposition,
                   DWORD flags_and_attributes,
                   HANDLE template_file);
BOOL ReadFile(HANDLE handle,
              LPVOID buffer,
              DWORD bytes_to_read,
              DWORD* bytes_read,
              LPVOID overlapped);
BOOL WriteFile(HANDLE handle,
               LPCVOID buffer,
               DWORD bytes_to_write,
               DWORD* bytes_written,
               LPVOID overlapped);
BOOL FlushFileBuffers(HANDLE handle);
BOOL CloseHandle(HANDLE handle);
BOOL GetFileSizeEx(HANDLE handle, LARGE_INTEGER* size_out);
BOOL SetFilePointerEx(HANDLE handle,
                      LARGE_INTEGER distance,
                      LARGE_INTEGER* new_position,
                      DWORD move_method);
DWORD GetFileAttributesW(LPCWSTR path);
BOOL DeleteFileW(LPCWSTR path);
BOOL RemoveDirectoryW(LPCWSTR path);
BOOL CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES security_attributes);
HANDLE FindFirstFileW(LPCWSTR pattern, WIN32_FIND_DATAW* find_data);
BOOL FindNextFileW(HANDLE handle, WIN32_FIND_DATAW* find_data);
BOOL FindClose(HANDLE handle);
DWORD GetTempPathW(DWORD buffer_length, LPWSTR buffer);
UINT GetTempFileNameW(LPCWSTR path_name,
                      LPCWSTR prefix_string,
                      UINT unique,
                      LPWSTR temp_file_name);

HANDLE CreateFileMappingW(HANDLE file,
                          LPSECURITY_ATTRIBUTES attributes,
                          DWORD protect,
                          DWORD maximum_size_high,
                          DWORD maximum_size_low,
                          LPCWSTR name);
LPVOID MapViewOfFile(HANDLE mapping,
                     DWORD desired_access,
                     DWORD file_offset_high,
                     DWORD file_offset_low,
                     size_t number_of_bytes_to_map);
BOOL UnmapViewOfFile(LPCVOID base_address);

HMODULE GetModuleHandleA(const char* name);
HMODULE LoadLibraryA(const char* name);
void* GetProcAddress(HMODULE module, const char* symbol_name);

void DebugBreak(void);
void GetSystemTimeAsFileTime(FILETIME* file_time);

#ifdef __cplusplus
}
#endif

#endif  // UV_RT_PLATFORM_LINUX_COMPAT_H
