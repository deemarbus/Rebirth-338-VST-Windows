/* Read-only x86 ReBirth 2.0.1 ReWire metadata probe.
 * Never calls EngineOpen, TopHatOpenDeviceProc, or any audio/render function.
 */
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
#error This diagnostic must be built as a 32-bit x86 Windows executable.
#endif
_Static_assert(sizeof(void *) == 4, "The legacy device ABI uses 32-bit pointers.");

enum { INFO_BYTES = 0x8d8, NAME_BYTES = 32, CHANNELS = 18 };
static const char expected_sha256[] =
    "4c3a6441a2cd50f797d99b1c5b6cf0aaf198c7ad8ca2648196b530c2e3e58b12";
static const char *const expected_channels[CHANNELS] = {
    "Mix-L", "Mix-R", "303-1", "303-2", "808-Mix", "808-BD", "808-SD",
    "808-Tom", "808-Per", "808-HH", "808-CY", "909-Mix", "909-BD",
    "909-SD", "909-Tom", "909-Per", "909-HH", "909-CY"
};

typedef void (__cdecl *NameVersionProc)(uint32_t *, char *);
typedef void (__cdecl *DeviceInfoProc)(void *);
typedef struct GuardedBuffer {
    BYTE *reservation, *page, *data;
    SIZE_T page_bytes, data_bytes;
} GuardedBuffer;

static BOOL guarded_create(GuardedBuffer *buffer, SIZE_T bytes) {
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    buffer->page_bytes = system.dwPageSize;
    buffer->data_bytes = bytes;
    if (bytes > buffer->page_bytes - 64) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    buffer->reservation = VirtualAlloc(NULL, buffer->page_bytes * 3, MEM_RESERVE, PAGE_NOACCESS);
    if (!buffer->reservation) return FALSE;
    buffer->page = VirtualAlloc(buffer->reservation + buffer->page_bytes,
                               buffer->page_bytes, MEM_COMMIT, PAGE_READWRITE);
    if (!buffer->page) return FALSE;
    memset(buffer->page, 0xa5, buffer->page_bytes);
    buffer->data = buffer->page + 32;
    memset(buffer->data, 0xcc, bytes);
    return TRUE;
}

static BOOL guarded_valid(const GuardedBuffer *buffer) {
    SIZE_T i;
    for (i = 0; i < 32; ++i) if (buffer->page[i] != 0xa5) return FALSE;
    for (i = 32 + buffer->data_bytes; i < buffer->page_bytes; ++i)
        if (buffer->page[i] != 0xa5) return FALSE;
    return TRUE;
}

static void guarded_free(GuardedBuffer *buffer) {
    if (buffer->reservation) VirtualFree(buffer->reservation, 0, MEM_RELEASE);
    memset(buffer, 0, sizeof(*buffer));
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
    if (!CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0) || length != sizeof(digest)) goto done;
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

static uint32_t read_u32(const BYTE *bytes) {
    uint32_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static BOOL expected_string(const BYTE *bytes, const char *expected) {
    return memchr(bytes, 0, NAME_BYTES) != NULL && strcmp((const char *)bytes, expected) == 0;
}

int wmain(int argc, wchar_t **argv) {
    WCHAR path[32768];
    DWORD path_length, error = ERROR_SUCCESS;
    LARGE_INTEGER length;
    HANDLE file = INVALID_HANDLE_VALUE;
    HMODULE module = NULL;
    GuardedBuffer version = {0}, name = {0}, info = {0};
    NameVersionProc get_name;
    DeviceInfoProc get_info;
    union { FARPROC raw; NameVersionProc name; DeviceInfoProc info; } symbol;
    char digest[65];
    const char *stage = "arguments";
    int result = 1, i;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc != 2) { error = ERROR_INVALID_PARAMETER; goto done; }
    stage = "absolute_path";
    path_length = GetFullPathNameW(argv[1], sizeof(path) / sizeof(path[0]), path, NULL);
    if (!path_length || path_length >= sizeof(path) / sizeof(path[0])) {
        error = ERROR_INVALID_NAME; goto done;
    }
    stage = "read_only_lock";
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) { error = GetLastError(); goto done; }
    stage = "original_size";
    if (!GetFileSizeEx(file, &length)) { error = GetLastError(); goto done; }
    if (length.QuadPart != 2472960) { error = ERROR_BAD_EXE_FORMAT; goto done; }
    stage = "original_sha256";
    if (!file_sha256(file, digest)) { error = GetLastError(); goto done; }
    if (strcmp(digest, expected_sha256)) { error = ERROR_CRC; goto done; }

    stage = "guarded_buffers";
    if (!guarded_create(&version, 4) || !guarded_create(&name, NAME_BYTES) ||
        !guarded_create(&info, INFO_BYTES)) { error = GetLastError(); goto done; }
    stage = "load_validated_original";
    module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) { error = GetLastError(); goto done; }
    stage = "resolve_metadata_exports";
    symbol.raw = GetProcAddress(module, "TopHatGetDeviceNameAndVersionProc");
    if (!symbol.raw) { error = GetLastError(); goto done; }
    get_name = symbol.name;
    symbol.raw = GetProcAddress(module, "TopHatGetDeviceInfoProc");
    if (!symbol.raw) { error = GetLastError(); goto done; }
    get_info = symbol.info;

    /* Static call-site evidence establishes cdecl and these exact buffer sizes.
     * This invokes no engine lifecycle, panel IPC or render function. */
    stage = "name_version_call";
    get_name((uint32_t *)version.data, (char *)name.data);
    stage = "device_info_call";
    get_info(info.data);
    stage = "buffer_canaries";
    if (!guarded_valid(&version) || !guarded_valid(&name) || !guarded_valid(&info)) {
        error = ERROR_INVALID_DATA; goto done;
    }
    stage = "legacy_metadata_values";
    if (read_u32(version.data) != 8 || read_u32(info.data + 0x8c4) != 8 ||
        read_u32(info.data + 0x20) != CHANNELS ||
        !expected_string(name.data, "ReBirth Audio") || !expected_string(info.data, "ReBirth Audio")) {
        error = ERROR_INVALID_DATA; goto done;
    }
    for (i = 0; i < CHANNELS; ++i) {
        if (!expected_string(info.data + 0x24 + i * NAME_BYTES, expected_channels[i])) {
            error = ERROR_INVALID_DATA; goto done;
        }
    }
    stage = "unload_original";
    if (!FreeLibrary(module)) { error = GetLastError(); goto done; }
    module = NULL;
    stage = "original_unchanged";
    if (!file_sha256(file, digest)) { error = GetLastError(); goto done; }
    if (strcmp(digest, expected_sha256)) { error = ERROR_CRC; goto done; }
    result = 0;
done:
    if (module) FreeLibrary(module);
    guarded_free(&info);
    guarded_free(&name);
    guarded_free(&version);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (result) {
        printf("{\"status\":\"failed\",\"stage\":\"%s\",\"win32_error\":%lu}\n",
               stage, (unsigned long)error);
    } else {
        printf("{\"status\":\"passed\",\"scope\":\"metadata_only\","
               "\"architecture\":\"x86\",\"dll_sha256\":\"%s\","
               "\"api_version\":8,\"info_version\":8,\"device_name\":\"ReBirth Audio\","
               "\"info_bytes\":2264,\"buffer_canaries\":\"unchanged\",\"channel_count\":18,"
               "\"channels\":[", digest);
        for (i = 0; i < CHANNELS; ++i)
            printf("%s\"%s\"", i ? "," : "", expected_channels[i]);
        printf("],\"original_unchanged\":true,\"audio_called\":false,\"device_opened\":false}\n");
    }
    return result;
}
