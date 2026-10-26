/* x86 ReBirth 2.0.1 lifecycle-only diagnostic. No audio-drive calls. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(_WIN32) || !defined(__i386__)
#error Build this diagnostic as a 32-bit x86 Windows executable.
#endif
_Static_assert(sizeof(void *) == 4, "The legacy ABI requires x86 pointers.");

static const char expected_sha256[] =
    "4c3a6441a2cd50f797d99b1c5b6cf0aaf198c7ad8ca2648196b530c2e3e58b12";
typedef uint32_t (__cdecl *OpenProc)(void);
typedef unsigned char (__cdecl *CloseOKProc)(void);
typedef void (__cdecl *ServiceProc)(void);
static const char *stage = "arguments";
static uintptr_t module_address;
static BOOL owned_device;

static DWORD WINAPI watchdog_thread(void *finished) {
    if (WaitForSingleObject((HANDLE)finished, 15000) == WAIT_TIMEOUT) {
        fprintf(stderr, "lifecycle_watchdog_timeout_at=%s\n", stage);
        fflush(stderr);
        /* Only this diagnostic process is terminated. Do not search for or
         * terminate any ReBirth panel or unrelated process. Avoid DLL detach
         * on a potentially hung loader/engine thread. */
        TerminateProcess(GetCurrentProcess(), 124);
    }
    return 0;
}

static void trace_stage(const char *value) {
    stage = value;
    fprintf(stderr, "lifecycle_stage=%s\n", stage);
    fflush(stderr);
}

static LONG WINAPI fatal_exception(EXCEPTION_POINTERS *exception) {
    uintptr_t address = (uintptr_t)exception->ExceptionRecord->ExceptionAddress;
    printf("{\"status\":\"failed\",\"stage\":\"%s\",\"exception_code\":\"0x%08lx\","
           "\"exception_address\":\"0x%08lx\",\"module_base\":\"0x%08lx\","
           "\"device_owned\":%s,\"audio_called\":false}\n", stage,
           (unsigned long)exception->ExceptionRecord->ExceptionCode,
           (unsigned long)address, (unsigned long)module_address,
           owned_device ? "true" : "false");
    fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}

static BOOL file_sha256(HANDLE file, char output[65]) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    LARGE_INTEGER start;
    BYTE block[16384], digest[32];
    DWORD count, length = sizeof(digest), error = ERROR_SUCCESS;
    BOOL result = FALSE;
    unsigned int i;
    start.QuadPart = 0;
    if (!SetFilePointerEx(file, start, NULL, FILE_BEGIN)) return FALSE;
    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES,
                              CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) return FALSE;
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) goto done;
    for (;;) {
        if (!ReadFile(file, block, sizeof(block), &count, NULL)) goto done;
        if (!count) break;
        if (!CryptHashData(hash, block, count, 0)) goto done;
    }
    if (!CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0)) goto done;
    if (length != sizeof(digest)) { SetLastError(ERROR_INVALID_DATA); goto done; }
    for (i = 0; i < sizeof(digest); ++i) sprintf(output + i * 2, "%02x", digest[i]);
    output[64] = 0;
    result = TRUE;
done:
    if (!result) error = GetLastError();
    if (hash) CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (!result) SetLastError(error);
    return result;
}

int wmain(int argc, wchar_t **argv) {
    WCHAR path[32768];
    DWORD path_length, error = ERROR_SUCCESS;
    LARGE_INTEGER length;
    HANDLE file = INVALID_HANDLE_VALUE;
    HMODULE module = NULL;
    HANDLE finished_event = NULL, watchdog = NULL;
    OpenProc open_device;
    CloseOKProc close_ok;
    ServiceProc idle, close_device;
    union { FARPROC raw; OpenProc open; CloseOKProc query; ServiceProc service; } symbol;
    uint32_t open_result = 0;
    unsigned int idle_calls = 0;
    BOOL closed = FALSE, unloaded = FALSE;
    const char *status = "failed";
    char digest[65];
    int result = 1;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(fatal_exception);
    trace_stage("watchdog_setup");
    finished_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!finished_event) { error = GetLastError(); goto done; }
    watchdog = CreateThread(NULL, 0, watchdog_thread, finished_event, 0, NULL);
    if (!watchdog) { error = GetLastError(); goto done; }
    trace_stage("arguments");
    if (argc != 2) { error = ERROR_INVALID_PARAMETER; goto done; }
    trace_stage("absolute_path");
    path_length = GetFullPathNameW(argv[1], sizeof(path) / sizeof(path[0]), path, NULL);
    if (!path_length || path_length >= sizeof(path) / sizeof(path[0])) {
        error = ERROR_INVALID_NAME; goto done;
    }
    trace_stage("read_only_lock");
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) { error = GetLastError(); goto done; }
    trace_stage("original_size");
    if (!GetFileSizeEx(file, &length)) { error = GetLastError(); goto done; }
    if (length.QuadPart != 2472960) { error = ERROR_BAD_EXE_FORMAT; goto done; }
    trace_stage("original_sha256");
    if (!file_sha256(file, digest)) { error = GetLastError(); goto done; }
    if (strcmp(digest, expected_sha256)) { error = ERROR_CRC; goto done; }
    trace_stage("load_validated_original");
    module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) { error = GetLastError(); goto done; }
    module_address = (uintptr_t)module;
    trace_stage("resolve_lifecycle_exports");
    symbol.raw = GetProcAddress(module, "TopHatOpenDeviceProc");
    if (!symbol.raw) { error = GetLastError(); goto done; }
    open_device = symbol.open;
    symbol.raw = GetProcAddress(module, "TopHatIsCloseOKProc");
    if (!symbol.raw) { error = GetLastError(); goto done; }
    close_ok = symbol.query;
    symbol.raw = GetProcAddress(module, "TopHatIdleProc");
    if (!symbol.raw) { error = GetLastError(); goto done; }
    idle = symbol.service;
    symbol.raw = GetProcAddress(module, "TopHatCloseDeviceProc");
    if (!symbol.raw) { error = GetLastError(); goto done; }
    close_device = symbol.service;

    trace_stage("open_device");
    open_result = open_device();
    if (open_result != 1) {
        if (open_result == 4) {
            stage = "device_in_use"; status = "refused"; result = 2; error = ERROR_BUSY;
        } else {
            error = ERROR_GEN_FAILURE;
        }
        goto done;
    }
    owned_device = TRUE;
    trace_stage("idle_owned_device");
    for (idle_calls = 0; idle_calls < 10; ++idle_calls) { idle(); Sleep(10); }
    trace_stage("query_close_permission");
    if (close_ok() != 1) {
        stage = "close_refused"; status = "refused"; result = 2; error = ERROR_BUSY;
        goto done;
    }
    trace_stage("close_owned_device");
    close_device();
    owned_device = FALSE;
    closed = TRUE;
    trace_stage("unload_closed_device");
    if (!FreeLibrary(module)) { error = GetLastError(); goto done; }
    module = NULL;
    unloaded = TRUE;
    trace_stage("original_unchanged");
    if (!file_sha256(file, digest)) { error = GetLastError(); goto done; }
    if (strcmp(digest, expected_sha256)) { error = ERROR_CRC; goto done; }
    status = "passed";
    result = 0;
done:
    /* Only a successful Open gives this process an engine to close. If CloseOK
     * refuses, do not explicitly unload an active engine or touch any panel.
     * Report the refusal and end this diagnostic process; OS cleanup follows. */
    if (module && !owned_device) {
        if (FreeLibrary(module)) unloaded = TRUE;
        else if (!error) { error = GetLastError(); result = 1; status = "failed"; }
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (finished_event) SetEvent(finished_event);
    if (watchdog) { WaitForSingleObject(watchdog, 5000); CloseHandle(watchdog); }
    if (finished_event) CloseHandle(finished_event);
    printf("{\"status\":\"%s\",\"scope\":\"lifecycle_only\",\"stage\":\"%s\","
           "\"win32_error\":%lu,\"open_result\":%lu,\"idle_calls\":%u,"
           "\"device_owned\":%s,\"close_called\":%s,\"dll_unloaded\":%s,"
           "\"audio_called\":false", status, stage, (unsigned long)error,
           (unsigned long)open_result, idle_calls, owned_device ? "true" : "false",
           closed ? "true" : "false", unloaded ? "true" : "false");
    if (!result) printf(",\"original_unchanged\":true,\"dll_sha256\":\"%s\"", digest);
    printf("}\n");
    fflush(stdout);
    if (owned_device) ExitProcess((UINT)result);
    return result;
}
