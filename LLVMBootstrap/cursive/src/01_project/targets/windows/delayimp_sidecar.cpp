#ifdef _WIN32
// Build a compiler-packaged delay-load helper archive from the MSVC source
// with CFG disabled. This sidecar is for the compiler's own Windows binary and
// bundled support surface; raw-dylib user programs are resolved through the
// runtime-owned lazy resolver rather than PE /DELAYLOAD.
//
// The stock delayimp.lib on current MSVC toolchains expects vcruntime load
// config / CFG support symbols such as _load_config_used and
// __guard_dispatch_icall_fptr. User programs linked by Cursive intentionally do
// not pull the full CRT, so the packaged helper must also remain CRT-free.
//
// MSVC ships delayhlp.cpp explicitly for this purpose. We compile that source
// into the bundled delayimp.lib sidecar with /guard:cf- so the helper does not
// require the CFG support globals that are absent under /NODEFAULTLIB.
#define WIN32_LEAN_AND_MEAN
#define STRICT
#include <windows.h>
#include <delayimp.h>

extern "C" __declspec(selectany) const PfnDliHook __pfnDliNotifyHook2 = nullptr;
extern "C" __declspec(selectany) const PfnDliHook __pfnDliFailureHook2 = nullptr;
extern "C" __declspec(selectany) const BOOL __bChangeProtectionOfWholeDloadSection =
    FALSE;

// A minimal load-config directory is sufficient for the delay helper. The
// helper only needs the symbol to exist; with CFG disabled for this sidecar,
// the remaining fields may stay zero under the compiler's /NODEFAULTLIB model.
extern "C" __declspec(selectany) IMAGE_LOAD_CONFIG_DIRECTORY _load_config_used = {
    sizeof(_load_config_used)
};

#include <delayhlp.cpp>
#endif
