#include <stdint.h>

#ifdef _WIN32
/*
 * Floating-point code generation references this CRT sentinel symbol.
 * Provide it in the runtime so /NODEFAULTLIB links still succeed.
 */
int _fltused = 0;
#endif

int _RTC_InitBase(void) {
  return 0;
}

int _RTC_Shutdown(void) {
  return 0;
}

void _RTC_CheckStackVars(void* frame, void* desc) {
  (void)frame;
  (void)desc;
}

void _RTC_CheckStackVars2(void* frame, void* desc) {
  (void)frame;
  (void)desc;
}

/*
 * Debug runtime checks may emit a call to _RTC_UninitUse when MSVC instruments
 * reads from potentially uninitialized locals. Provide a no-op stub so the
 * CRT-free runtime archive remains self-contained under /NODEFAULTLIB.
 */
void __cdecl _RTC_UninitUse(const char* varname) {
  (void)varname;
}

/*
 * MSVC may emit SEH unwind metadata that references __C_specific_handler.
 * Provide a minimal CRT-free stub so linking succeeds under /NODEFAULTLIB.
 */
int __cdecl __C_specific_handler(void* exception_record,
                                 void* establisher_frame,
                                 void* context_record,
                                 void* dispatcher_context) {
  (void)exception_record;
  (void)establisher_frame;
  (void)context_record;
  (void)dispatcher_context;
  return 0;
}
