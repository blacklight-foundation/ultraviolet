#ifdef _WIN32
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

typedef int32_t (*UVInstallProbeFn)(int32_t*);
typedef int32_t (*UVIncrementFn)(int32_t);
typedef int32_t (*UVStateFn)(void);
typedef int32_t (*UVLinkedRunFn)(int32_t*);

static int fail_with_last_error(const char* action, const char* subject) {
    fprintf(stderr, "%s failed for %s: %lu\n", action, subject, GetLastError());
    return 1;
}

static int fail_value(const char* label, int32_t expected, int32_t actual) {
    fprintf(stderr, "%s expected %d but observed %d\n", label, expected, actual);
    return 1;
}

static FARPROC require_symbol(HMODULE module, const char* symbol) {
    FARPROC proc = GetProcAddress(module, symbol);
    if (proc == NULL) {
        fprintf(stderr, "GetProcAddress failed for %s: %lu\n", symbol, GetLastError());
    }
    return proc;
}

static HMODULE load_library_image(const char* path) {
    HMODULE module = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == NULL) {
        fail_with_last_error("LoadLibraryExA", path);
    }
    return module;
}

static int expect_i32(const char* label, int32_t expected, int32_t actual) {
    if (actual != expected) {
        return fail_value(label, expected, actual);
    }
    return 0;
}

static int exercise_raw_image(const char* provider_path) {
    int32_t destroy_count = 0;
    HMODULE provider = load_library_image(provider_path);
    if (provider == NULL) {
        return 1;
    }

    UVInstallProbeFn install_probe =
        (UVInstallProbeFn)require_symbol(provider, "uv_shared_lifecycle_install_raw_destroy_probe");
    UVIncrementFn increment =
        (UVIncrementFn)require_symbol(provider, "uv_shared_lifecycle_raw_increment");
    UVStateFn state = (UVStateFn)require_symbol(provider, "uv_shared_lifecycle_raw_state");
    if (install_probe == NULL || increment == NULL || state == NULL) {
        FreeLibrary(provider);
        return 1;
    }

    if (expect_i32("raw initial image state", 101, install_probe(&destroy_count)) ||
        expect_i32("raw first call state", 108, increment(7)) ||
        expect_i32("raw second call state", 111, increment(3)) ||
        expect_i32("raw reused image state", 111, state())) {
        FreeLibrary(provider);
        return 1;
    }

    if (!FreeLibrary(provider)) {
        return fail_with_last_error("FreeLibrary", provider_path);
    }
    if (expect_i32("raw image destroy count", 1, destroy_count)) {
        return 1;
    }

    provider = load_library_image(provider_path);
    if (provider == NULL) {
        return 1;
    }
    state = (UVStateFn)require_symbol(provider, "uv_shared_lifecycle_raw_state");
    if (state == NULL) {
        FreeLibrary(provider);
        return 1;
    }
    if (expect_i32("raw reloaded image state", 101, state())) {
        FreeLibrary(provider);
        return 1;
    }
    if (!FreeLibrary(provider)) {
        return fail_with_last_error("FreeLibrary", provider_path);
    }
    return 0;
}

static int exercise_linked_image_once(const char* consumer_path, const char* label) {
    int32_t destroy_count = 0;
    HMODULE consumer = load_library_image(consumer_path);
    if (consumer == NULL) {
        return 1;
    }

    UVLinkedRunFn run_linked =
        (UVLinkedRunFn)require_symbol(consumer, "uv_shared_lifecycle_run_linked_calls");
    if (run_linked == NULL) {
        FreeLibrary(consumer);
        return 1;
    }

    if (expect_i32(label, 0, run_linked(&destroy_count))) {
        FreeLibrary(consumer);
        return 1;
    }
    if (!FreeLibrary(consumer)) {
        return fail_with_last_error("FreeLibrary", consumer_path);
    }
    if (expect_i32("linked provider image destroy count", 1, destroy_count)) {
        return 1;
    }
    return 0;
}

static int exercise_linked_image(const char* consumer_path) {
    if (exercise_linked_image_once(consumer_path, "linked first image run")) {
        return 1;
    }
    return exercise_linked_image_once(consumer_path, "linked reloaded image run");
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <provider-dll> <consumer-dll>\n", argv[0]);
        return 2;
    }

    if (exercise_raw_image(argv[1])) {
        return 10;
    }
    if (exercise_linked_image(argv[2])) {
        return 20;
    }
    return 0;
}
#else
#include <stdio.h>

int main(void) {
    fputs("SharedLibraryImageLifecycleHarness requires Windows library image loading.\n", stderr);
    return 2;
}
#endif
