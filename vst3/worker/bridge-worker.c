/* Persistent x86 backend. The native render, guard, hash, exception and
 * shutdown routines are exactly the diagnostic implementation included here.
 * The renamed diagnostic entry point is never called by this program. */
#define wmain rebirth_diagnostic_entry_unused
#include "../../bridge-prototype/render-probe.c"
#undef wmain
#include "../shared/bridge_ipc.h"
#include "worker-run.h"

static void worker_status(RBShared *shared, uint32_t state, DWORD error) {
    if (!shared) return;
    rb_store(&shared->error, error);
    rb_store(&shared->heartbeat, GetTickCount());
    rb_store(&shared->worker_state, state);
}
static uint32_t worker_ready_state(const Host *host) {
    if (host->latest_served != CHANNELS) return RB_NO_SONG;
    return host->last_loop == 0 ? RB_READY : RB_LOOP_ON;
}
/* The prototype accepts a linear play run only: frame zero, then exact
 * 64-frame advances. Epoch changes discard the old run; no untested seek
 * selector or position-chase flag is sent to the original engine. */
static BOOL worker_request_metadata_valid(const RBRequest *request) {
    if (!request->client_token || !request->epoch || request->reserved ||
        request->playing > 1 || request->stream_sample > UINT64_MAX - FRAMES ||
        request->tempo_milli < RB_MIN_TEMPO_MILLI ||
        request->tempo_milli > RB_MAX_TEMPO_MILLI || request->position_ticks < 0 ||
        request->engine_frame > UINT64_MAX - RB_BLOCK_FRAMES ||
        (!request->playing && request->engine_frame != 0)) return FALSE;
    return TRUE;
}
int wmain(int argc, WCHAR **argv) {
    Host host = {0};
    RBWorkerRun run = {0};
    WCHAR dll_path[32768];
    HANDLE original = INVALID_HANDLE_VALUE, mutex = NULL, mapping = NULL;
    HANDLE finished = NULL, watchdog = NULL, input = INVALID_HANDLE_VALUE;
    HMODULE module = NULL;
    RBShared *shared = NULL;
    LARGE_INTEGER size;
    char digest[65] = "";
    DWORD error = ERROR_SUCCESS, console_mode, last_job = GetTickCount();
    BOOL validated = FALSE, shutdown_ok = FALSE, unchanged = FALSE;
    BOOL smoke = FALSE;
    BOOL saw_panel = FALSE, enter = FALSE, escape = FALSE;
    const char *status = "failed_initialization";
    int result = 1;
    union { FARPROC raw; OpenProc open; CloseOKProc query;
            ServiceProc service; DriveProc drive; } symbol;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    host.last_loop = -1;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(fatal_exception);
    if (!SetConsoleCtrlHandler(control_handler, TRUE)) { error = GetLastError(); goto done; }
    finished = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!finished) { error = GetLastError(); goto done; }
    watchdog = CreateThread(NULL, 0, watchdog_thread, finished, 0, NULL);
    if (!watchdog) { error = GetLastError(); goto done; }
    smoke = argc == 3 && !wcscmp(argv[1], L"--no-panel-smoke");
    if ((!smoke && argc != 2) || !absolute_path(argv[smoke ? 2 : 1], dll_path)) {
        fprintf(stderr, "usage: rebirth-bridge-worker.exe [--no-panel-smoke] C:\\absolute\\ReBirth.dll\n");
        error = ERROR_INVALID_PARAMETER; goto done;
    }
    input = GetStdHandle(STD_INPUT_HANDLE);
    if (!GetConsoleMode(input, &console_mode)) input = INVALID_HANDLE_VALUE;
    InterlockedExchange(&current_stage, ST_LOCK);
    original = CreateFileW(dll_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (original == INVALID_HANDLE_VALUE) { error = GetLastError(); goto done; }
    if (!GetFileSizeEx(original, &size)) { error = GetLastError(); goto done; }
    if (size.QuadPart != 2472960) { error = ERROR_BAD_EXE_FORMAT; goto done; }
    InterlockedExchange(&current_stage, ST_PREHASH);
    if (!file_sha256(original, digest)) { error = GetLastError(); goto done; }
    if (strcmp(digest, expected_sha256)) { error = ERROR_CRC; goto done; }
    validated = TRUE;

    SetLastError(ERROR_SUCCESS);
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                  (DWORD)sizeof(RBShared), RB_IPC_NAME);
    error = GetLastError();
    if (!mapping) goto done;
    if (error == ERROR_ALREADY_EXISTS) {
        status = "refused_existing_bridge"; result = 2; goto done;
    }
    shared = (RBShared *)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RBShared));
    if (!shared) { error = GetLastError(); goto done; }
    memset(shared, 0, sizeof(*shared));
    shared->version = RB_IPC_VERSION;
    shared->bytes = (uint32_t)sizeof(*shared);
    shared->frames = RB_BLOCK_FRAMES;
    shared->sample_rate = RB_SAMPLE_RATE;
    shared->tempo_milli = RB_DEFAULT_TEMPO_MILLI;
    shared->latency = RB_LATENCY;
    shared->worker_pid = GetCurrentProcessId();
    worker_status(shared, RB_STARTING, ERROR_SUCCESS);
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
        result = host.open_result == 4 ? 2 : 1; goto done;
    }
    host.owned = TRUE;
    InterlockedExchange(&device_owned, 1);
    if (!integrity(&host)) { error = ERROR_INVALID_DATA; goto done; }
    host.drive_enabled = TRUE;
    if (smoke) {
        unsigned int i;
        BOOL allowed = FALSE;
        if (!call_close_ok(&host, &allowed)) { error = ERROR_INVALID_DATA; goto done; }
        if (!allowed) {
            host.drive_enabled = FALSE;
            error = ERROR_BUSY; status = "refused_panel_connected_before_smoke";
            result = 2; goto done;
        }
        for (i = 0; i < SMOKE_BLOCKS; ++i) {
            if (!call_idle(&host) || drive_checked(&host, FALSE, 0, 0) < 0) {
                error = ERROR_INVALID_DATA; goto done;
            }
        }
        status = "passed_no_panel_worker_smoke"; result = 0; goto done;
    }
    worker_status(shared, RB_NO_SONG, ERROR_SUCCESS);
    rb_store(&shared->magic, RB_IPC_MAGIC); /* Publish only the initialized backend. */
    fprintf(stderr,
        "ReBirth-brug geopend. Laat dit venster open.\n"
        "1. Open de originele ReBirth-app en laad je song. Zet Loop UIT.\n"
        "2. Gebruik in Ableton een ondersteunde samplefrequentie en 20-500 BPM.\n"
        "   De VST3 converteert Live-rate naar de 44,1-kHz engine. Zet Loop UIT.\n"
        "3. Laad de ReBirth Bridge VST3 en start met Live Play vanaf het begin.\n"
        "   De Play-knop in ReBirth bestuurt deze proef niet.\n"
        "   Een song sla je apart op in ReBirth, niet in de VST3-projectstatus.\n"
        "Afsluiten: stop Live, sluit ReBirth via File > Quit, druk hier op Escape of Ctrl+C.\n");
    status = "running";
    for (;;) {
        RBRequest request;
        int popped, count = 0;
        uint32_t current_lease = rb_load(&shared->client_token);
        BOOL close_allowed = FALSE;
        rb_store(&shared->heartbeat, GetTickCount());
        if (atomic_read(&cancellation)) { status = "cancelled_normally"; result = 0; break; }
        if (input != INVALID_HANDLE_VALUE) {
            if (!poll_console(input, &enter, &escape)) { error = GetLastError(); break; }
            if (escape) { status = "cancelled_normally"; result = 0; break; }
        }
        if (!call_idle(&host) || !call_close_ok(&host, &close_allowed)) {
            error = ERROR_INVALID_DATA; break;
        }
        if (!close_allowed) saw_panel = TRUE;
        else if (saw_panel) { status = "panel_quit_normally"; result = 0; break; }
        rb_run_lease(&run, current_lease);
        if (expired(last_job, 2000)) rb_run_stale(&run);
        while (count++ < RB_RING_CAPACITY && (popped = rb_pop_request(shared, &request)) != 0) {
            RBReply reply = {0};
            enum RBRunAction action;
            int served;
            if (popped < 0) { error = ERROR_INVALID_DATA; goto loop_failed; }
            current_lease = rb_load(&shared->client_token);
            if (!current_lease || request.client_token != current_lease) {
                InterlockedIncrement(&shared->rejected); continue;
            }
            /* Ownership may change while a batch is being consumed. Transport
             * state belongs to a lease, never merely to an epoch number. */
            rb_run_lease(&run, request.client_token);
            if (!worker_request_metadata_valid(&request)) {
                error = ERROR_INVALID_DATA; goto loop_failed;
            }
            if (!call_idle(&host)) { error = ERROR_INVALID_DATA; goto loop_failed; }
            /* A stopped probe establishes song/loop readiness before PLAY.
             * Between valid playing jobs only Idle runs, never an implicit STOP. */
            if (!run.playing || request.epoch != run.epoch ||
                rb_run_decide(&run, &request, RB_READY) != RB_RUN_PLAY) {
                served = drive_checked_at_position(&host, FALSE, request.tempo_milli,
                                                   request.position_ticks, -1);
                if (served < 0) { error = ERROR_INVALID_DATA; goto loop_failed; }
            }
            reply.client_token = request.client_token;
            reply.epoch = request.epoch;
            reply.stream_sample = request.stream_sample;
            reply.state = worker_ready_state(&host);
            action = rb_run_decide(&run, &request, reply.state);
            if (action == RB_RUN_BLOCKED) {
                /* A stale callback or not-ready start is an unsupported
                 * transport run, not memory corruption. Stop safely, discard
                 * its audio and require a fresh stop/rewind/start from Live. */
                if (reply.state == RB_READY) reply.state = RB_REARM_REQUIRED;
                InterlockedIncrement(&shared->rejected);
            } else if (action == RB_RUN_PLAY) {
                served = drive_checked_at_position(&host, TRUE, request.tempo_milli,
                                                   request.position_ticks, CHANNELS);
                if (served < 0) { error = ERROR_INVALID_DATA; goto loop_failed; }
                memcpy(reply.left, host.audio[0].data, sizeof(reply.left));
                memcpy(reply.right, host.audio[1].data, sizeof(reply.right));
                reply.served_channels = CHANNELS;
                reply.state = RB_PLAYING;
            }
            rb_run_commit(&run, &request, action);
            last_job = GetTickCount();
            InterlockedIncrement(&shared->accepted);
            worker_status(shared, reply.state, ERROR_SUCCESS);
            popped = rb_push_reply(shared, &reply);
            if (popped < 0) { error = ERROR_INVALID_DATA; goto loop_failed; }
            if (!popped) InterlockedIncrement(&shared->underruns);
        }
        if (!run.playing) {
            int served = drive_checked(&host, FALSE, 0, -1);
            uint32_t ready;
            if (served < 0) { error = ERROR_INVALID_DATA; goto loop_failed; }
            ready = worker_ready_state(&host);
            if (ready == RB_READY && run.rearm_required) ready = RB_REARM_REQUIRED;
            worker_status(shared, ready, ERROR_SUCCESS);
        }
        Sleep(1);
        continue;
loop_failed:
        host.drive_enabled = FALSE;
        status = "failed_protocol_or_render";
        result = 1;
        break;
    }
done:
    worker_status(shared, result == 0 ? RB_STOPPING : RB_FAILED, error);
    if (host.owned && !host.fatal_integrity) {
        shutdown_ok = shutdown_host(&host, smoke);
        if (!shutdown_ok) { result = 1; status = "failed_incomplete_shutdown"; }
    }
    if (host.fatal_integrity) {
        result = 1; status = "failed_buffer_integrity"; error = ERROR_INVALID_DATA;
    }
    if (module && !host.owned && !host.fatal_integrity) {
        snapshot(&host);
        begin_native(ST_UNLOAD);
        if (!FreeLibrary(module)) { result = 1; error = GetLastError(); }
        else host.unloaded = TRUE;
        end_native();
        if (host.buffers_safe && !integrity(&host)) {
            result = 1; status = "failed_buffer_integrity"; error = ERROR_INVALID_DATA;
        }
    }
    if (validated && original != INVALID_HANDLE_VALUE && !host.fatal_integrity) {
        InterlockedExchange(&current_stage, ST_POSTHASH);
        unchanged = file_sha256(original, digest) && !strcmp(digest, expected_sha256);
        if (!unchanged) { result = 1; error = ERROR_CRC; }
    }
    worker_status(shared, result == 0 ? RB_STOPPED : RB_FAILED, error);
    printf("{\"status\":\"%s\",\"mode\":\"%s\",\"audio_verified\":false,\"exit_code\":%d,\"error\":%lu,\"open_result\":%lu,"
           "\"drive_calls\":%lu,\"idle_calls\":%lu,\"clean_shutdown\":%s,"
           "\"dll_unloaded\":%s,\"original_hash_unchanged\":%s}\n",
           status, smoke ? "no_panel_smoke" : "live_bridge", result,
           (unsigned long)error, (unsigned long)host.open_result,
           host.drive_calls, host.idle_calls, shutdown_ok ? "true" : "false",
           host.unloaded ? "true" : "false", unchanged ? "true" : "false");
    /* Do not unload or free native storage after corruption or unsafe Close.
     * This hard exit affects only this backend, never the panel or the DAW. */
    if (host.fatal_integrity || host.owned) terminate_only_this_host((UINT)(result ? result : 1));
    if (shared) UnmapViewOfFile(shared);
    if (mapping) CloseHandle(mapping);
    if (mutex) CloseHandle(mutex);
    if (original != INVALID_HANDLE_VALUE) CloseHandle(original);
    if (finished) SetEvent(finished);
    if (watchdog) { WaitForSingleObject(watchdog, 1000); CloseHandle(watchdog); }
    if (finished) CloseHandle(finished);
    guarded_free(&host.drive);
    guarded_free(&host.audio[0]);
    guarded_free(&host.audio[1]);
    return result;
}
