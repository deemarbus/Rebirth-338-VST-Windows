/* Experimental x86 host for one exact ReBirth 2.0.1 ReWire engine.
 * This is an explicit-offset experiment, not a recovered public SDK or a VST.
 * Engine calls and all buffer access run on the main thread. The watchdog
 * can terminate ONLY this host process; it never calls an engine export. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <math.h>
#include <limits.h>

#if !defined(_WIN32) || !defined(__i386__)
#error Build this experiment as a 32-bit x86 Windows executable.
#endif
_Static_assert(sizeof(void *) == 4, "The legacy ABI requires x86 pointers.");
_Static_assert(sizeof(float) == 4, "The engine writes IEEE float32 samples.");

enum { DRIVE_SIZE = 0x11c8, FRAMES = 64, CHANNELS = 2, SAMPLE_RATE = 44100,
       TEMPO = 140000, PPQ = 192, CAPTURE_FRAMES = 441000,
       CAPTURE_BLOCKS = 6891, CANARY_SIZE = 64, SMOKE_BLOCKS = 8,
       NATIVE_TIMEOUT_MS = 10000, SETUP_TIMEOUT_MS = 300000,
       RENDER_TIMEOUT_MS = 60000, CLOSE_TIMEOUT_MS = 180000 };
enum { O_FLAGS = 0x804, O_POINTERS = 0x844, O_FRAMES = 0x944,
       O_POSITION = 0x94c, O_TEMPO = 0x950, O_PLAY = 0x954,
       O_SELECTOR_POSITION = 0x958, O_SELECTOR_LOOP = 0x95c,
       O_PLAY_REQUEST = 0x96c, O_SEEK_REQUEST = 0x96d,
       O_SEEK_POSITION = 0x970, O_TEMPO_FEEDBACK = 0x974,
       O_LOOP_START = 0x978, O_LOOP_END = 0x97c, O_LOOP_ENABLED = 0x980,
       O_OUTPUT_COUNT = 0x1184, O_SERVED = 0x1188 };
enum Stage { ST_ARGUMENTS, ST_LOCK, ST_PREHASH, ST_MUTEX, ST_LOAD,
             ST_RESOLVE, ST_OPEN, ST_IDLE, ST_DRIVE, ST_CLOSE_QUERY,
             ST_CLOSE, ST_UNLOAD, ST_POSTHASH, ST_WAVE, ST_DONE };
static const char *const stage_names[] = {
    "arguments", "read_only_lock", "pre_sha256", "ReWi_mutex", "load",
    "resolve", "open", "idle", "drive", "IsCloseOK", "close", "unload",
    "post_sha256", "wave_write", "done"
};
static const char expected_sha256[] =
    "4c3a6441a2cd50f797d99b1c5b6cf0aaf198c7ad8ca2648196b530c2e3e58b12";
static const uint32_t sample_sentinel = UINT32_C(0x7fc5c3e1);
typedef uint32_t (__cdecl *OpenProc)(void);
/* This export returns a byte in AL, not a 32-bit BOOL. */
typedef unsigned char (__cdecl *CloseOKProc)(void);
typedef void (__cdecl *ServiceProc)(void);
typedef void (__cdecl *DriveProc)(void *);
typedef struct {
    BYTE *allocation, *writable, *data;
    SIZE_T allocation_size, writable_size, data_size;
} Guarded;
typedef struct {
    Guarded drive, audio[CHANNELS];
    BYTE before[DRIVE_SIZE];
    OpenProc open;
    CloseOKProc close_ok;
    ServiceProc idle, close;
    DriveProc render;
    BOOL buffers_safe, fatal_integrity, drive_enabled, owned, closed, unloaded;
    uint32_t open_result;
    unsigned long drive_calls, idle_calls, stereo_blocks, no_song_blocks, capture_calls;
    unsigned long play_requests, stop_requests, seek_requests, tempo_differences;
    uint32_t last_tempo;
    int last_loop;
    int latest_served;
    double peak[CHANNELS], squares[CHANNELS];
    uint64_t written_frames, checked_samples, nonfinite, unchanged_samples;
    uint64_t no_song_sentinel_checks, capture_frames_validated;
} Host;
static volatile LONG current_stage = ST_ARGUMENTS, native_active, native_start;
static volatile LONG cancellation, device_owned;
static uintptr_t module_address;

static LONG atomic_read(volatile LONG *value) {
    return InterlockedCompareExchange(value, 0, 0);
}
static const char *stage_name(void) {
    LONG value = atomic_read(&current_stage);
    return (value >= ST_ARGUMENTS && value <= ST_DONE) ? stage_names[value] : "unknown";
}
static void begin_native(enum Stage stage) {
    InterlockedExchange(&current_stage, stage);
    InterlockedExchange(&native_start, (LONG)GetTickCount());
    InterlockedExchange(&native_active, 1);
}
static void end_native(void) { InterlockedExchange(&native_active, 0); }
static BOOL expired(DWORD start, DWORD duration) {
    return (DWORD)(GetTickCount() - start) >= duration;
}
static void terminate_only_this_host(UINT code) {
    /* ExitProcess can deadlock on DLL detach if a hung main thread held the
     * loader lock. This handle names ONLY this diagnostic, never the panel. */
    TerminateProcess(GetCurrentProcess(), code);
    ExitProcess(code); /* Fallback only if the hard self-termination failed. */
}
static DWORD WINAPI watchdog_thread(void *finished) {
    while (WaitForSingleObject((HANDLE)finished, 100) == WAIT_TIMEOUT) {
        if (atomic_read(&native_active) &&
            expired((DWORD)atomic_read(&native_start), NATIVE_TIMEOUT_MS)) {
            fprintf(stderr, "native_call_timeout stage=%s; exiting only this host\n", stage_name());
            printf("{\"status\":\"failed_native_timeout\",\"stage\":\"%s\","
                   "\"device_owned\":%s,\"clean_shutdown\":false}\n", stage_name(),
                   atomic_read(&device_owned) ? "true" : "false");
            fflush(NULL);
            /* No panel lookup, panel termination, forced Close or FreeLibrary. */
            terminate_only_this_host(124);
        }
    }
    return 0;
}
static LONG WINAPI fatal_exception(EXCEPTION_POINTERS *exception) {
    printf("{\"status\":\"failed_native_exception\",\"stage\":\"%s\","
           "\"exception_code\":\"0x%08lx\",\"exception_address\":\"0x%08lx\","
           "\"module_base\":\"0x%08lx\",\"device_owned\":%s,"
           "\"clean_shutdown\":false}\n", stage_name(),
           (unsigned long)exception->ExceptionRecord->ExceptionCode,
           (unsigned long)(uintptr_t)exception->ExceptionRecord->ExceptionAddress,
           (unsigned long)module_address, atomic_read(&device_owned) ? "true" : "false");
    fflush(NULL);
    terminate_only_this_host(125);
    return EXCEPTION_EXECUTE_HANDLER;
}
static BOOL WINAPI control_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        InterlockedExchange(&cancellation, 1);
        return TRUE;
    }
    return FALSE;
}

static uint32_t get32(const BYTE *p, SIZE_T offset) {
    uint32_t value;
    memcpy(&value, p + offset, sizeof(value));
    return value;
}
static void put32(BYTE *p, SIZE_T offset, uint32_t value) {
    memcpy(p + offset, &value, sizeof(value));
}
static void put16(BYTE *p, SIZE_T offset, uint16_t value) {
    memcpy(p + offset, &value, sizeof(value));
}
static BOOL guarded_allocate(Guarded *guard, SIZE_T size) {
    SYSTEM_INFO system;
    SIZE_T page, writable;
    GetSystemInfo(&system);
    page = system.dwPageSize;
    writable = ((size + CANARY_SIZE * 2 + page - 1) / page) * page;
    guard->allocation_size = writable + page * 2;
    guard->allocation = VirtualAlloc(NULL, guard->allocation_size, MEM_RESERVE, PAGE_NOACCESS);
    if (!guard->allocation) return FALSE;
    guard->writable = VirtualAlloc(guard->allocation + page, writable, MEM_COMMIT, PAGE_READWRITE);
    if (!guard->writable) {
        VirtualFree(guard->allocation, 0, MEM_RELEASE);
        memset(guard, 0, sizeof(*guard));
        return FALSE;
    }
    guard->writable_size = writable;
    guard->data = guard->writable + CANARY_SIZE;
    guard->data_size = size;
    memset(guard->writable, 0xa5, writable);
    memset(guard->data, 0, size);
    return TRUE;
}
static void guarded_reset_canaries(Guarded *guard) {
    SIZE_T prefix = (SIZE_T)(guard->data - guard->writable);
    memset(guard->writable, 0xa5, prefix);
    memset(guard->data + guard->data_size, 0xa5,
           guard->writable_size - prefix - guard->data_size);
}
static BOOL guarded_valid(const Guarded *guard) {
    SIZE_T i, prefix = (SIZE_T)(guard->data - guard->writable);
    for (i = 0; i < prefix; ++i) if (guard->writable[i] != 0xa5) return FALSE;
    for (i = prefix + guard->data_size; i < guard->writable_size; ++i)
        if (guard->writable[i] != 0xa5) return FALSE;
    return TRUE;
}
static void guarded_free(Guarded *guard) {
    if (guard->allocation) VirtualFree(guard->allocation, 0, MEM_RELEASE);
    memset(guard, 0, sizeof(*guard));
}
static BOOL output_byte(SIZE_T offset) {
    return offset == 0x96c || offset == 0x96d ||
           (offset >= 0x970 && offset <= 0x980) ||
           offset == 0x982 || offset == 0x983 ||
           (offset >= 0x1184 && offset < DRIVE_SIZE);
}
static BOOL integrity(Host *host) {
    SIZE_T i;
    unsigned int channel;
    if (!guarded_valid(&host->drive)) goto damaged;
    for (channel = 0; channel < CHANNELS; ++channel)
        if (!guarded_valid(&host->audio[channel])) goto damaged;
    for (i = 0; i < DRIVE_SIZE; ++i) {
        if (!output_byte(i) && host->before[i] != host->drive.data[i]) {
            fprintf(stderr, "immutable_drive_byte_changed offset=0x%04lx before=%02x after=%02x\n",
                    (unsigned long)i, host->before[i], host->drive.data[i]);
            goto damaged;
        }
    }
    return TRUE;
damaged:
    host->buffers_safe = FALSE;
    host->fatal_integrity = TRUE;
    host->drive_enabled = FALSE;
    fprintf(stderr, "buffer_integrity_failed; no further engine calls or DLL unload are permitted\n");
    return FALSE;
}
static void snapshot(Host *host) { memcpy(host->before, host->drive.data, DRIVE_SIZE); }
static BOOL initialize_buffers(Host *host) {
    unsigned int channel;
    if (!guarded_allocate(&host->drive, DRIVE_SIZE)) return FALSE;
    for (channel = 0; channel < CHANNELS; ++channel) {
        uintptr_t address;
        if (!guarded_allocate(&host->audio[channel], FRAMES * sizeof(float))) return FALSE;
        address = (uintptr_t)host->audio[channel].data;
        if (address > UINT32_MAX) { SetLastError(ERROR_ARITHMETIC_OVERFLOW); return FALSE; }
        host->drive.data[O_FLAGS + channel] = 1;
        put32(host->drive.data, O_POINTERS + channel * 4, (uint32_t)address);
    }
    put32(host->drive.data, O_FRAMES, FRAMES);
    put32(host->drive.data, O_TEMPO, TEMPO);
    /* All other bytes, including both selectors and all event regions, stay 0. */
    host->buffers_safe = TRUE;
    snapshot(host);
    return TRUE;
}
static BOOL call_idle(Host *host) {
    MSG message;
    unsigned int count = 0;
    if (host->fatal_integrity) return FALSE;
    snapshot(host);
    begin_native(ST_IDLE);
    while (count++ < 32 && PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) InterlockedExchange(&cancellation, 1);
        else { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    host->idle();
    end_native();
    ++host->idle_calls;
    return !host->buffers_safe || integrity(host);
}
static BOOL call_close_ok(Host *host, BOOL *allowed) {
    unsigned char result;
    if (host->fatal_integrity) return FALSE;
    snapshot(host);
    begin_native(ST_CLOSE_QUERY);
    result = host->close_ok();
    end_native();
    *allowed = result != 0;
    return !host->buffers_safe || integrity(host);
}
/* Exact integer frame-clock conversion avoids accumulating rounded tick steps. */
static BOOL frame_position(uint64_t frames, int32_t *position) {
    const uint64_t multiplier = (uint64_t)TEMPO * PPQ;
    uint64_t ticks;
    if (frames > UINT64_MAX / multiplier) return FALSE;
    ticks = frames * multiplier / ((uint64_t)60000 * SAMPLE_RATE);
    if (ticks > INT32_MAX) return FALSE;
    *position = (int32_t)ticks;
    return TRUE;
}
/* Returns 0 for no song, 2 for validated stereo, -1 for any hard failure.
 * Every served sample is checked, including the final 24 frames not written. */
static int drive_block(Host *host, BOOL play, uint64_t frames) {
    int32_t position;
    unsigned int channel, sample;
    int served = 0;
    if (!host->drive_enabled || !host->buffers_safe || !frame_position(frames, &position)) goto failed;
    if (!integrity(host)) goto failed;
    guarded_reset_canaries(&host->drive);
    for (channel = 0; channel < CHANNELS; ++channel) {
        guarded_reset_canaries(&host->audio[channel]);
        for (sample = 0; sample < FRAMES; ++sample)
            put32(host->audio[channel].data, sample * 4, sample_sentinel);
    }
    put32(host->drive.data, O_FRAMES, FRAMES);
    put32(host->drive.data, O_POSITION, (uint32_t)position);
    put32(host->drive.data, O_TEMPO, TEMPO);
    put32(host->drive.data, O_PLAY, play ? 1 : 0);
    /* Clear only established one-shot request outputs. Persistent feedback
     * and native-cleared served/count fields are not invented host inputs. */
    host->drive.data[O_PLAY_REQUEST] = 0;
    host->drive.data[O_SEEK_REQUEST] = 0;
    put32(host->drive.data, O_SEEK_POSITION, 0);
    snapshot(host);
    begin_native(ST_DRIVE);
    host->render(host->drive.data);
    end_native();
    ++host->drive_calls;
    if (play) ++host->capture_calls;
    if (!integrity(host)) goto failed;
    if (get32(host->drive.data, O_OUTPUT_COUNT) != 0) {
        fprintf(stderr, "unexpected_output_event_count\n");
        goto failed;
    }
    for (channel = 0; channel < 64; ++channel) {
        BYTE flag = host->drive.data[O_SERVED + channel];
        if ((channel >= CHANNELS && flag != 0) || flag > 1) {
            fprintf(stderr, "unexpected_served_flag channel=%u flag=%u\n", channel, flag);
            goto failed;
        }
        if (channel < CHANNELS) served += flag;
    }
    if (served != 0 && served != CHANNELS) {
        fprintf(stderr, "partial_stereo_served channels=%d\n", served);
        goto failed;
    }
    for (channel = 0; channel < CHANNELS; ++channel) {
        for (sample = 0; sample < FRAMES; ++sample) {
            uint32_t bits = get32(host->audio[channel].data, sample * 4);
            float value;
            if (!served) {
                if (bits != sample_sentinel) {
                    fprintf(stderr, "unserved_sample_written channel=%u frame=%u\n", channel, sample);
                    goto failed;
                }
                ++host->no_song_sentinel_checks;
                continue;
            }
            ++host->checked_samples;
            if (bits == sample_sentinel) {
                ++host->unchanged_samples;
                fprintf(stderr, "served_sample_unchanged channel=%u frame=%u\n", channel, sample);
                goto failed;
            }
            memcpy(&value, &bits, sizeof(value));
            if (!isfinite(value)) {
                ++host->nonfinite;
                fprintf(stderr, "nonfinite_sample channel=%u frame=%u\n", channel, sample);
                goto failed;
            }
        }
    }
    host->latest_served = served;
    if (served) ++host->stereo_blocks;
    else ++host->no_song_blocks;
    if (play && served == CHANNELS) host->capture_frames_validated += FRAMES;
    return served;
failed:
    host->drive_enabled = FALSE;
    return -1;
}
static BOOL feedback(Host *host, BOOL capturing) {
    BYTE *p = host->drive.data;
    BYTE play = p[O_PLAY_REQUEST], seek = p[O_SEEK_REQUEST];
    if (play > 2) {
        fprintf(stderr, "invalid_play_request=%u\n", play);
        return FALSE;
    }
    if (play == 1) ++host->play_requests;
    if (play == 2) ++host->stop_requests;
    if (play) fprintf(stderr, "feedback play_request=%u policy=%s\n", play,
                      capturing ? "fixed_host_play; unexpected_stop_aborts" : "hold_stopped");
    if (seek) {
        int32_t requested = (int32_t)get32(p, O_SEEK_POSITION);
        ++host->seek_requests;
        fprintf(stderr, "feedback seek_flag=%u position=%ld policy=%s\n", seek,
                (long)requested, capturing ? "abort_capture" : "hold_position_zero");
        if (requested < 0 || capturing) return FALSE;
    }
    if (capturing && play == 2) return FALSE;
    if (host->latest_served == CHANNELS) {
        uint32_t tempo = get32(p, O_TEMPO_FEEDBACK);
        BYTE loop = p[O_LOOP_ENABLED];
        if (tempo != TEMPO) ++host->tempo_differences;
        if (tempo != host->last_tempo) {
            fprintf(stderr, "feedback tempo=%lu fixed_host_tempo=%u policy=host_140_BPM\n",
                    (unsigned long)tempo, TEMPO);
            host->last_tempo = tempo;
        }
        if ((int)loop != host->last_loop) {
            fprintf(stderr, "feedback loop_enabled=%u start=%ld end=%ld\n", loop,
                    (long)(int32_t)get32(p, O_LOOP_START),
                    (long)(int32_t)get32(p, O_LOOP_END));
            host->last_loop = loop;
        }
        if (loop > 1 || (capturing && loop != 0)) return FALSE;
    }
    return TRUE;
}

static int drive_checked(Host *host, BOOL play, uint64_t frames, int expected_served) {
    int served = drive_block(host, play, frames);
    if (served < 0 || (expected_served >= 0 && served != expected_served) ||
        !feedback(host, play)) {
        host->drive_enabled = FALSE;
        fprintf(stderr, "Drive validation failed; expected_served=%d actual=%d; Drive disabled\n",
                expected_served, served);
        return -1;
    }
    return served;
}
/* Console events are consumed only when Peek reports available input.
 * No line read or input wait can stall engine servicing. */
static BOOL poll_console(HANDLE input, BOOL *enter, BOOL *escape) {
    INPUT_RECORD events[32];
    DWORD available, count, i;
    *enter = FALSE;
    *escape = FALSE;
    if (!PeekConsoleInputW(input, events, 32, &available)) return FALSE;
    if (!available) return TRUE;
    if (!ReadConsoleInputW(input, events, available, &count)) return FALSE;
    for (i = 0; i < count; ++i) {
        if (events[i].EventType != KEY_EVENT || !events[i].Event.KeyEvent.bKeyDown) continue;
        if (events[i].Event.KeyEvent.wVirtualKeyCode == VK_RETURN) *enter = TRUE;
        if (events[i].Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE) *escape = TRUE;
    }
    return TRUE;
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
static BOOL absolute_path(const WCHAR *argument, WCHAR output[32768]) {
    DWORD length;
    /* Restrict to explicit DOS-drive absolute paths. Reject device/UNC names,
     * streams, wildcard names and ambiguous trailing spaces/dots. */
    const WCHAR *part, *end;
    if (!argument || wcslen(argument) < 4 ||
        !((argument[0] >= L'A' && argument[0] <= L'Z') ||
          (argument[0] >= L'a' && argument[0] <= L'z')) ||
        argument[1] != L':' || argument[2] != L'\\') return FALSE;
    for (part = argument + 3; *part; ++part)
        if (*part < 32 || wcschr(L":*?\"<>|/", *part)) return FALSE;
    for (part = argument + 3; *part; part = *end ? end + 1 : end) {
        WCHAR base[8];
        SIZE_T i, stem;
        end = wcschr(part, L'\\');
        if (!end) end = part + wcslen(part);
        if (end == part || end[-1] == L'.' || end[-1] == L' ') return FALSE;
        stem = 0;
        while (part + stem < end && part[stem] != L'.') ++stem;
        while (stem && part[stem - 1] == L' ') --stem;
        if (stem <= 7) {
            for (i = 0; i < stem; ++i) base[i] = (WCHAR)towupper(part[i]);
            base[stem] = 0;
            if (!wcscmp(base, L"CON") || !wcscmp(base, L"PRN") ||
                !wcscmp(base, L"AUX") || !wcscmp(base, L"NUL") ||
                !wcscmp(base, L"CONIN$") || !wcscmp(base, L"CONOUT$") ||
                (stem == 4 && (!wcsncmp(base, L"COM", 3) || !wcsncmp(base, L"LPT", 3)) &&
                 ((base[3] >= L'1' && base[3] <= L'9') ||
                  base[3] == L'\x00b9' || base[3] == L'\x00b2' || base[3] == L'\x00b3'))) return FALSE;
        }
    }
    length = GetFullPathNameW(argument, 32768, output, NULL);
    return length > 0 && length < 32768;
}
static BOOL write_exact(HANDLE file, const void *bytes, DWORD length) {
    DWORD written;
    if (!WriteFile(file, bytes, length, &written, NULL)) return FALSE;
    if (written != length) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
    return TRUE;
}
static BOOL wave_disposition(HANDLE file, BOOL delete_pending) {
    FILE_DISPOSITION_INFO disposition;
    disposition.DeleteFile = delete_pending;
    return SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
}
/* WAVEFORMATEX(18), fact and data: 58 bytes, IEEE float format tag 3.
 * The bounded frame count is known before rendering; incomplete files are
 * deleted through their own open handle instead of published as finished WAVs. */
static BOOL wave_header(HANDLE file) {
    BYTE header[58] = {0};
    const uint32_t data_size = CAPTURE_FRAMES * CHANNELS * sizeof(float);
    memcpy(header, "RIFF", 4); put32(header, 4, 50 + data_size);
    memcpy(header + 8, "WAVEfmt ", 8); put32(header, 16, 18);
    put16(header, 20, 3); put16(header, 22, CHANNELS);
    put32(header, 24, SAMPLE_RATE); put32(header, 28, SAMPLE_RATE * CHANNELS * sizeof(float));
    put16(header, 32, CHANNELS * sizeof(float)); put16(header, 34, 32);
    put16(header, 36, 0);
    memcpy(header + 38, "fact", 4); put32(header, 42, 4); put32(header, 46, CAPTURE_FRAMES);
    memcpy(header + 50, "data", 4); put32(header, 54, data_size);
    return write_exact(file, header, sizeof(header));
}
static BOOL write_audio(Host *host, HANDLE file, unsigned int frames) {
    float interleaved[FRAMES * CHANNELS];
    double peak[CHANNELS] = {0, 0}, squares[CHANNELS] = {0, 0};
    unsigned int sample, channel;
    for (sample = 0; sample < frames; ++sample) {
        for (channel = 0; channel < CHANNELS; ++channel) {
            float value;
            double magnitude;
            memcpy(&value, host->audio[channel].data + sample * sizeof(float), sizeof(value));
            interleaved[sample * CHANNELS + channel] = value;
            magnitude = fabs((double)value);
            if (magnitude > peak[channel]) peak[channel] = magnitude;
            squares[channel] += (double)value * value;
        }
    }
    InterlockedExchange(&current_stage, ST_WAVE);
    if (!write_exact(file, interleaved, frames * CHANNELS * sizeof(float))) return FALSE;
    for (channel = 0; channel < CHANNELS; ++channel) {
        if (peak[channel] > host->peak[channel]) host->peak[channel] = peak[channel];
        host->squares[channel] += squares[channel];
    }
    host->written_frames += frames;
    return TRUE;
}

/* Normal panel shutdown remains a live STOP+Idle service loop. A protocol or
 * sample failure disables Drive and permits Idle/IsCloseOK only. Any memory
 * corruption is fatal: no further engine call or DLL unload is permitted. */
static BOOL shutdown_host(Host *host, BOOL smoke) {
    DWORD started = GetTickCount();
    BOOL allowed = FALSE;
    BOOL valid = TRUE;
    DWORD timeout = smoke ? 5000 : CLOSE_TIMEOUT_MS;
    fprintf(stderr, "shutdown: Quit the original ReBirth panel normally; keep this console open.\n");
    for (;;) {
        if (!call_idle(host)) return FALSE;
        if (host->drive_enabled && host->buffers_safe) {
            int served = drive_checked(host, FALSE, 0, smoke ? 0 : -1);
            if (host->fatal_integrity) return FALSE;
            if (served < 0) {
                host->drive_enabled = FALSE;
                valid = FALSE;
                fprintf(stderr, "shutdown: Drive disabled after validation failure; Idle/IsCloseOK only.\n");
            }
        }
        if (!call_close_ok(host, &allowed)) return FALSE;
        if (allowed) {
            snapshot(host);
            begin_native(ST_CLOSE);
            host->close();
            end_native();
            host->owned = FALSE;
            InterlockedExchange(&device_owned, 0);
            host->closed = TRUE;
            if (host->buffers_safe && !integrity(host)) valid = FALSE;
            return valid;
        }
        if (expired(started, timeout)) {
            fprintf(stderr, "normal_panel_close_timeout; refusing forced Close/FreeLibrary\n");
            return FALSE;
        }
        Sleep(10);
    }
}

int wmain(int argc, WCHAR **argv) {
    Host host = {0};
    WCHAR dll_path[32768], wave_path[32768];
    HMODULE module = NULL;
    HANDLE original = INVALID_HANDLE_VALUE, wave = INVALID_HANDLE_VALUE, mutex = NULL;
    HANDLE finished = NULL, watchdog = NULL, input = INVALID_HANDLE_VALUE;
    LARGE_INTEGER size;
    DWORD error = ERROR_SUCCESS, console_mode, started;
    char digest[65] = "";
    const char *status = "failed", *phase = "initialization", *outcome_stage = "arguments";
    BOOL smoke = FALSE, retain_overload = FALSE, complete_wave = FALSE;
    BOOL keep_wave = FALSE, wave_delete_pending = FALSE;
    BOOL original_unchanged = FALSE, validated_input = FALSE, shutdown_ok = FALSE;
    BOOL enter = FALSE, escape = FALSE;
    int result = 1, served;
    unsigned int block, channel;
    double rms[CHANNELS] = {0, 0};
    union { FARPROC raw; OpenProc open; CloseOKProc query;
            ServiceProc service; DriveProc drive; } symbol;

    host.last_loop = -1;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(fatal_exception);
    if (!SetConsoleCtrlHandler(control_handler, TRUE)) { error = GetLastError(); goto done; }
    finished = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!finished) { error = GetLastError(); goto done; }
    watchdog = CreateThread(NULL, 0, watchdog_thread, finished, 0, NULL);
    if (!watchdog) { error = GetLastError(); goto done; }
    if (argc == 3 && !wcscmp(argv[1], L"--no-panel-smoke")) smoke = TRUE;
    else if ((argc == 4 || argc == 5) && !wcscmp(argv[1], L"--capture")) {
        if (argc == 5) {
            if (wcscmp(argv[4], L"--retain-overload")) { error = ERROR_INVALID_PARAMETER; goto done; }
            retain_overload = TRUE;
        }
        if (!absolute_path(argv[3], wave_path) || wcslen(wave_path) < 4 ||
            _wcsicmp(wave_path + wcslen(wave_path) - 4, L".wav")) {
            error = ERROR_INVALID_NAME; goto done;
        }
        input = GetStdHandle(STD_INPUT_HANDLE);
        if (!GetConsoleMode(input, &console_mode)) {
            fprintf(stderr, "interactive capture requires an actual console input handle\n");
            error = ERROR_INVALID_HANDLE; goto done;
        }
    } else {
        fprintf(stderr, "usage: render-probe --no-panel-smoke C:\\absolute\\ReBirth.dll\n"
                        "   or: render-probe --capture C:\\absolute\\ReBirth.dll C:\\new\\capture.wav [--retain-overload]\n");
        error = ERROR_INVALID_PARAMETER; goto done;
    }
    if (!absolute_path(argv[2], dll_path)) { error = ERROR_INVALID_NAME; goto done; }
    InterlockedExchange(&current_stage, ST_LOCK);
    original = CreateFileW(dll_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (original == INVALID_HANDLE_VALUE) { error = GetLastError(); goto done; }
    if (!GetFileSizeEx(original, &size)) { error = GetLastError(); goto done; }
    if (size.QuadPart != 2472960) { error = ERROR_BAD_EXE_FORMAT; goto done; }
    InterlockedExchange(&current_stage, ST_PREHASH);
    if (!file_sha256(original, digest)) { error = GetLastError(); goto done; }
    if (strcmp(digest, expected_sha256)) { error = ERROR_CRC; goto done; }
    validated_input = TRUE;
    if (!initialize_buffers(&host)) { error = GetLastError(); goto done; }
    InterlockedExchange(&current_stage, ST_MUTEX);
    SetLastError(ERROR_SUCCESS);
    mutex = CreateMutexA(NULL, FALSE, "ReWi");
    error = GetLastError();
    if (!mutex) goto done;
    if (error == ERROR_ALREADY_EXISTS) {
        status = "refused_existing_mixer"; result = 2; goto done;
    }
    error = ERROR_SUCCESS;
    begin_native(ST_LOAD);
    module = LoadLibraryExW(dll_path, NULL,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    end_native();
    if (!module) { error = GetLastError(); goto done; }
    module_address = (uintptr_t)module;
    InterlockedExchange(&current_stage, ST_RESOLVE);
#define RESOLVE(name, field, member) do { \
    symbol.raw = GetProcAddress(module, name); \
    if (!symbol.raw) { error = GetLastError(); goto done; } \
    host.field = symbol.member; \
} while (0)
    RESOLVE("TopHatOpenDeviceProc", open, open);
    RESOLVE("TopHatIsCloseOKProc", close_ok, query);
    RESOLVE("TopHatIdleProc", idle, service);
    RESOLVE("TopHatCloseDeviceProc", close, service);
    RESOLVE("TopHatDriveAudioProc", render, drive);
#undef RESOLVE
    snapshot(&host);
    begin_native(ST_OPEN);
    host.open_result = host.open();
    end_native();
    if (host.open_result != 1) {
        status = host.open_result == 4 ? "refused_existing_engine" : "failed_open";
        error = host.open_result == 4 ? ERROR_BUSY : ERROR_GEN_FAILURE;
        result = host.open_result == 4 ? 2 : 1;
        goto done;
    }
    host.owned = TRUE;
    InterlockedExchange(&device_owned, 1);
    if (!integrity(&host)) { error = ERROR_INVALID_DATA; goto done; }
    fprintf(stderr, "owned engine open; actual ReWi host mutex retained; fixed 140 BPM,192 PPQ,44100 Hz\n");
    if (smoke) {
        BOOL close_allowed = FALSE;
        phase = "no_panel_smoke";
        if (!call_close_ok(&host, &close_allowed)) { error = ERROR_INVALID_DATA; goto done; }
        if (!close_allowed) {
            status = "refused_panel_connected_before_smoke";
            result = 2; error = ERROR_BUSY; goto done;
        }
        host.drive_enabled = TRUE;
        for (block = 0; block < SMOKE_BLOCKS; ++block) {
            if (!call_idle(&host) || drive_checked(&host, FALSE, 0, 0) < 0) {
                error = ERROR_INVALID_DATA; goto done;
            }
            Sleep(10);
        }
        status = "passed_no_song_smoke";
        result = 0;
        goto done;
    }
    host.drive_enabled = TRUE;
    phase = "panel_setup";
    fprintf(stderr, "Now open your ORIGINAL working ReBirth application normally.\n"
                    "Load a known audible demo, turn Loop OFF, stop and rewind to the beginning.\n"
                    "The host keeps transport stopped and services Idle+Drive while you work.\n"
                    "Press Enter here only when ready; stereo served and Loop OFF are required.\n"
                    "Escape/Ctrl+C cancels capture, then Quit ReBirth normally. Setup timeout:300s.\n");
    started = GetTickCount();
    for (;;) {
        if (atomic_read(&cancellation)) { status = "cancelled"; error = ERROR_CANCELLED; goto done; }
        if (expired(started, SETUP_TIMEOUT_MS)) { status = "setup_timeout"; error = WAIT_TIMEOUT; goto done; }
        if (!call_idle(&host)) { error = ERROR_INVALID_DATA; goto done; }
        served = drive_checked(&host, FALSE, 0, -1);
        if (served < 0) { error = ERROR_INVALID_DATA; goto done; }
        if (!poll_console(input, &enter, &escape)) { error = GetLastError(); goto done; }
        if (escape) { status = "cancelled"; error = ERROR_CANCELLED; goto done; }
        if (enter) {
            if (served != CHANNELS || host.last_loop != 0) {
                fprintf(stderr, "not_ready: need actual stereo served and panel Loop OFF; keep preparing, then press Enter again\n");
            } else break;
        }
        Sleep(10);
    }
    /* A distinct stopped priming block follows readiness, before play=1/pos=0. */
    phase = "stopped_prime";
    if (!call_idle(&host) || drive_checked(&host, FALSE, 0, CHANNELS) < 0 ||
        host.last_loop != 0) { error = ERROR_INVALID_DATA; goto done; }
    InterlockedExchange(&current_stage, ST_WAVE);
    wave = CreateFileW(wave_path, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (wave == INVALID_HANDLE_VALUE) { error = GetLastError(); goto done; }
    if (GetFileType(wave) != FILE_TYPE_DISK) { error = ERROR_INVALID_HANDLE; goto done; }
    /* Arm deletion before the first header/sample write. If a native call
     * faults/hangs and the watchdog kills this host, OS handle cleanup deletes
     * the incomplete file. Cancel this disposition only at final commit. */
    if (!wave_disposition(wave, TRUE)) { error = GetLastError(); goto done; }
    wave_delete_pending = TRUE;
    if (!wave_header(wave)) { error = GetLastError(); goto done; }
    phase = "capture";
    fprintf(stderr, "capture:6891 native blocks;441024 checked frames;441000 written frames;no normalization or autoplay\n");
    started = GetTickCount();
    for (block = 0; block < CAPTURE_BLOCKS; ++block) {
        unsigned int write_frames = FRAMES;
        uint64_t rendered_frames = (uint64_t)block * FRAMES;
        if (atomic_read(&cancellation)) { status = "cancelled"; error = ERROR_CANCELLED; goto done; }
        if (expired(started, RENDER_TIMEOUT_MS)) { status = "render_timeout"; error = WAIT_TIMEOUT; goto done; }
        if (!call_idle(&host) || drive_checked(&host, TRUE, rendered_frames, CHANNELS) < 0) {
            error = ERROR_INVALID_DATA; goto done;
        }
        if (rendered_frames + FRAMES > CAPTURE_FRAMES)
            write_frames = (unsigned int)(CAPTURE_FRAMES - rendered_frames);
        if (!write_audio(&host, wave, write_frames)) { error = GetLastError(); goto done; }
        if (!(block % 16)) {
            if (!poll_console(input, &enter, &escape)) { error = GetLastError(); goto done; }
            if (escape) { status = "cancelled"; error = ERROR_CANCELLED; goto done; }
        }
    }
    if (host.written_frames != CAPTURE_FRAMES) { error = ERROR_INVALID_DATA; goto done; }
    if (!FlushFileBuffers(wave)) { error = GetLastError(); goto done; }
    complete_wave = TRUE;
    result = 0;
    status = "passed_stereo_capture";
    keep_wave = TRUE;
    for (channel = 0; channel < CHANNELS; ++channel) {
        rms[channel] = sqrt(host.squares[channel] / (double)host.written_frames);
        if (host.peak[channel] > 1.0) {
            status = "inconclusive_overload";
            result = 3;
            keep_wave = retain_overload;
        }
    }
    if (host.peak[0] <= 0.001 || host.peak[1] <= 0.001 ||
        rms[0] <= 0.0001 || rms[1] <= 0.0001) {
        status = "inconclusive_quiet_audio";
        result = 3;
        keep_wave = FALSE;
    }
done:
    outcome_stage = stage_name();
    if (host.owned && !host.fatal_integrity) {
        shutdown_ok = shutdown_host(&host, smoke);
        if (!shutdown_ok) {
            status = "failed_incomplete_shutdown";
            phase = "normal_panel_shutdown";
            outcome_stage = stage_name();
            result = 1;
            if (!error) error = ERROR_BUSY;
            keep_wave = FALSE;
        }
    }
    if (host.fatal_integrity) {
        status = "failed_buffer_integrity";
        result = 1; error = ERROR_INVALID_DATA; keep_wave = FALSE;
    }
    if (module && !host.owned && !host.fatal_integrity) {
        begin_native(ST_UNLOAD);
        host.unloaded = FreeLibrary(module);
        end_native();
        if (host.unloaded) module = NULL;
        else { if (!error) error = GetLastError(); status = "failed_unload"; result = 1; keep_wave = FALSE; outcome_stage = "unload"; }
    }
    if (original != INVALID_HANDLE_VALUE && validated_input) {
        InterlockedExchange(&current_stage, ST_POSTHASH);
        original_unchanged = file_sha256(original, digest) && !strcmp(digest, expected_sha256);
        if (!original_unchanged) {
            if (!error) error = ERROR_CRC;
            status = "failed_posthash"; result = 1; keep_wave = FALSE; outcome_stage = "post_sha256";
        }
    }
    /* Release and verify coordination before committing output. A handle
     * retained after incomplete shutdown is closed only by self-termination. */
    if (mutex && !host.owned && !host.fatal_integrity) {
        if (!CloseHandle(mutex)) {
            if (!error) error = GetLastError();
            status = "failed_coordination_release";
            result = 1; keep_wave = FALSE; outcome_stage = "ReWi_mutex";
        }
        mutex = NULL;
    }
    if (wave != INVALID_HANDLE_VALUE) {
        if (keep_wave && complete_wave) {
            if (!wave_disposition(wave, FALSE)) {
                if (!error) error = GetLastError();
                result = 1; status = "failed_output_commit"; keep_wave = FALSE;
                /* Reassert deletion even if a failed cancellation left the
                 * filesystem disposition uncertain. */
                wave_delete_pending = FALSE;
                outcome_stage = "wave_commit";
            } else wave_delete_pending = FALSE;
        } else keep_wave = FALSE;
        if (!keep_wave && !wave_delete_pending) {
            if (!wave_disposition(wave, TRUE)) {
                fprintf(stderr, "could_not_delete_own_incomplete_or_rejected_WAV error=%lu\n", (unsigned long)GetLastError());
                if (!error) error = GetLastError();
                result = 1; status = "failed_delete_own_output";
            } else wave_delete_pending = TRUE;
        }
        if (!CloseHandle(wave)) { if (!error) error = GetLastError(); result = 1; status = "failed_output_close"; }
    }
    /* The coordination handle is retained until panel/device shutdown. Never
     * explicitly release it while an engine we opened is still active. */
    if (!host.owned && !host.fatal_integrity) {
        guarded_free(&host.drive);
        for (channel = 0; channel < CHANNELS; ++channel) guarded_free(&host.audio[channel]);
    }
    if (original != INVALID_HANDLE_VALUE) CloseHandle(original);
    if (finished) SetEvent(finished);
    if (watchdog) { WaitForSingleObject(watchdog, 5000); CloseHandle(watchdog); }
    if (finished) CloseHandle(finished);
    for (channel = 0; channel < CHANNELS; ++channel)
        if (host.written_frames) rms[channel] = sqrt(host.squares[channel] / (double)host.written_frames);
    printf("{\"status\":\"%s\",\"mode\":\"%s\",\"phase\":\"%s\",\"stage\":\"%s\","
           "\"win32_error\":%lu,\"open_result\":%lu,\"device_owned\":%s,"
           "\"close_called\":%s,\"dll_unloaded\":%s,\"original_unchanged\":%s,"
           "\"input_and_canary_integrity\":%s,\"drive_calls\":%lu,\"idle_calls\":%lu,"
           "\"stereo_blocks\":%lu,\"no_song_blocks\":%lu,\"frames_written\":%llu,"
           "\"capture_calls\":%lu,\"capture_frames_validated\":%llu,\"last_served_channels\":%d,"
           "\"sample_rate\":%u,\"tempo_BPM\":140,\"PPQ\":%u,"
           "\"L_peak\":%.9g,\"R_peak\":%.9g,\"L_RMS\":%.9g,\"R_RMS\":%.9g,"
           "\"checked_samples\":%llu,\"nonfinite\":%llu,\"unchanged_served_samples\":%llu,"
           "\"unserved_sentinel_checks\":%llu,"
           "\"play_requests\":%lu,\"stop_requests\":%lu,\"seek_requests\":%lu,"
           "\"fixed_tempo_differences\":%lu,\"WAV_retained\":%s,"
           "\"audio_verified\":%s,\"music_identity_verified\":false,\"clean_shutdown\":%s}\n",
           status, smoke ? "no_panel_smoke" : "interactive_capture", phase, outcome_stage,
           (unsigned long)error, (unsigned long)host.open_result, host.owned ? "true" : "false",
           host.closed ? "true" : "false", host.unloaded ? "true" : "false",
           original_unchanged ? "true" : "false", host.buffers_safe ? "true" : "false",
           host.drive_calls, host.idle_calls, host.stereo_blocks, host.no_song_blocks,
           (unsigned long long)host.written_frames, host.capture_calls,
           (unsigned long long)host.capture_frames_validated, host.latest_served, SAMPLE_RATE, PPQ,
           host.peak[0], host.peak[1], rms[0], rms[1],
           (unsigned long long)host.checked_samples, (unsigned long long)host.nonfinite,
           (unsigned long long)host.unchanged_samples,
           (unsigned long long)host.no_song_sentinel_checks, host.play_requests, host.stop_requests,
           host.seek_requests, host.tempo_differences, keep_wave ? "true" : "false",
           (!smoke && result == 0) ? "true" : "false",
           (host.closed && host.unloaded) ? "true" : "false");
    fflush(NULL);
    if (host.owned || host.fatal_integrity) terminate_only_this_host((UINT)result);
    return result;
}
