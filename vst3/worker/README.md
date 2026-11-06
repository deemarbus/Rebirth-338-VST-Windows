# Persistent x86 backend

This is a prototype for one exact original ReBirth engine, stereo at 44,100 Hz,
following Live tempo from 20 to 500 BPM. The VST3 resamples this engine output
to supported Live rates from 44,100 to 192,000 Hz. It reuses the guarded diagnostic implementation by including its
source with the diagnostic entry point renamed. The original EXE/DLL, songs
and ISO are never supplied, installed, modified or registered by this worker.

Run the worker before opening the original panel:

```powershell
.\rebirth-bridge-worker.exe 'C:\ReBirth RB-338\ReBirth.dll'
```

Keep its console open. Load the song in the original ReBirth panel and turn
panel Loop off. Use Live at a supported sample rate and 20-500 BPM, with Live Loop off, and
start its transport from the beginning. The original Play button does not
control Live. Save songs separately in ReBirth; project state does not embed
the original song.

The worker retains the actual `ReWi` host-coordination mutex and its own
session-local IPC mapping. Existing ownership is refused. It serializes every
native engine call, checks guarded memory and samples, and retains the native
call watchdog. Input requests are bounded and sequence-tagged. A not-ready or
stale play run returns silence and `RB_REARM_REQUIRED`; an explicit stopped
request clears that latch. A new run must start at engine frame zero.

The plug-in must never kill the worker. To end the session, stop Live and close
ReBirth normally through File > Quit. The worker then closes the engine,
unloads the DLL, rechecks its hash and releases its objects. Escape or Ctrl+C
requests the same bounded shutdown. A shutdown timeout or memory corruption
terminates only this backend and never forcibly terminates the panel or DAW.

## Build and focused checks

Use `build-worker.sh` with the retained MinGW x86 compiler, or native MSVC:

```powershell
cmake -S vst3/worker -B build-worker -G 'Visual Studio 17 2022' -A Win32
cmake --build build-worker --config Release
ctest --test-dir build-worker -C Release --output-on-failure
```

`worker-run-test.c` executes the actual pure state machine with no engine or
Windows runtime. It checks no-song/Loop-on rejection, the rearm latch, fresh
stop/start, sequential frames, staleness and client handoff.

`--no-panel-smoke <absolute DLL>` is an explicitly separate native diagnostic.
It requires no panel, opens the exact hash-gated engine, performs eight guarded
stopped service iterations, and closes normally (including one final stopped
Drive). It reports `audio_verified:false`; it does not prove music or VST3
integration. Do not run it with an existing ReBirth or ReWire session.
