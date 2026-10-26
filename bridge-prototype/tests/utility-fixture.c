/* Developer utility checks only. No original DLL or engine export is loaded.
 * Include the actual writer/guard/frame-clock code under a renamed entry. */
#define wmain render_probe_program_entry_not_invoked
#include "../render-probe.c"
#undef wmain

static int utility_failure(const char *step) {
    fprintf(stderr, "utility_fixture_failed=%s win32_error=%lu\n", step,
            (unsigned long)GetLastError());
    return 1;
}

int wmain(int argc, WCHAR **argv) {
    Host host = {0};
    HANDLE file = INVALID_HANDLE_VALUE;
    BOOL abrupt;
    int32_t ticks;
    unsigned int block, channel, sample;
    if (argc != 3 || (wcscmp(argv[1], L"--format") && wcscmp(argv[1], L"--abrupt")))
        return utility_failure("arguments");
    abrupt = !wcscmp(argv[1], L"--abrupt");
    if (!initialize_buffers(&host)) return utility_failure("guard_allocations");
    if (host.drive.data_size != 4552 || host.drive.writable_size < 4552 + 128 ||
        !guarded_valid(&host.drive)) return utility_failure("two_page_guard_layout");
    host.drive.data[DRIVE_SIZE] ^= 1;
    if (guarded_valid(&host.drive)) return utility_failure("canary_missed_overwrite");
    guarded_reset_canaries(&host.drive);
    if (!guarded_valid(&host.drive)) return utility_failure("canary_reset");
    if (!frame_position(64, &ticks) || ticks != 0 ||
        !frame_position(128, &ticks) || ticks != 1 ||
        !frame_position(44100, &ticks) || ticks != 448 ||
        !frame_position(441000, &ticks) || ticks != 4480 ||
        frame_position(UINT64_MAX, &ticks)) return utility_failure("frame_clock");
    for (sample = 0; sample < FRAMES; ++sample) {
        float left = (float)sample / 128.0f - 0.25f;
        float right = -left * 0.5f + 0.0625f;
        memcpy(host.audio[0].data + sample * sizeof(float), &left, sizeof(float));
        memcpy(host.audio[1].data + sample * sizeof(float), &right, sizeof(float));
    }
    file = CreateFileW(argv[2], GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return utility_failure("create_new_output");
    if (!wave_disposition(file, TRUE) || !wave_header(file))
        return utility_failure("pending_output_and_actual_header");
    if (abrupt) {
        /* Check that OS handle cleanup deletes incomplete pending output even
         * when the helper's ordinary C cleanup is skipped entirely. */
        fflush(NULL);
        TerminateProcess(GetCurrentProcess(), 77);
        return 78;
    }
    for (block = 0; block < CAPTURE_BLOCKS; ++block) {
        unsigned int frames = FRAMES;
        uint64_t frame_start = (uint64_t)block * FRAMES;
        if (frame_start + frames > CAPTURE_FRAMES)
            frames = (unsigned int)(CAPTURE_FRAMES - frame_start);
        if (!write_audio(&host, file, frames)) return utility_failure("actual_sample_writer");
    }
    if (host.written_frames != 441000 || !FlushFileBuffers(file) ||
        !wave_disposition(file, FALSE) || !CloseHandle(file))
        return utility_failure("commit_complete_output");
    for (channel = 0; channel < CHANNELS; ++channel) guarded_free(&host.audio[channel]);
    guarded_free(&host.drive);
    printf("{\"utility_only\":true,\"original_DLL_loaded\":false,\"engine_called\":false,"
           "\"frame_clock_checked\":true,\"guard_canary_checked\":true,\"frames\":441000}\n");
    return 0;
}
