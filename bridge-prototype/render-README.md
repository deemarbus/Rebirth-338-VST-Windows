# Experimental ReBirth engine render host

This separate x86 command-line experiment implements the reviewed direct-host
protocol for the original ReBirth 2.0.1 engine. It is not a VST plug-in. The
existing metadata and lifecycle probes remain separate and unchanged.

**The guarded no-song gate passed under Wine/QEMU.** Nine actual Drive calls
(eight initial calls and one stopped shutdown call) completed with intact
inputs/canaries, 1,152 untouched sample sentinels, normal Close/FreeLibrary and
unchanged original file hashes. That result explicitly has `audio_verified:false`.
A known-demo capture still requires the user's working original Windows
application and media. No loaded-song music render or native Windows host trial
is claimed from the no-song gate.

For the Windows trial, use [Start-Audio-Test.ps1](Start-Audio-Test.ps1) and the
[Dutch instructions](TESTEN-WINDOWS.md). The starter downloads the helper from
this repository, checks its pinned SHA-256, runs the no-song check on Windows,
then starts the interactive capture. [The binary manifest](bin/windows-x86/manifest.json)
identifies the exact source and compiler; runtime notices accompany the binary.

The sole accepted engine is the original 2,472,960-byte DLL with SHA-256:

```text
4c3a6441a2cd50f797d99b1c5b6cf0aaf198c7ad8ca2648196b530c2e3e58b12
```

The helper holds a read-only handle allowing only read sharing, checks size
and CryptoAPI SHA-256 before loading, and checks the hash again before releasing
the handle. It does not change the EXE, DLL, original media, registry or license
checks. The working original application starts normally; no launcher is used.

## Build without executing

From the checkout on the analysis machine:

```bash
bash bridge-prototype/build-render-probe.sh
```

The default compiler is the verified local
`/workspace/rebirth-analysis/tools/mingw-local/root/usr/bin/i686-w64-mingw32-gcc-posix`.
The build uses C11, `-Wall -Wextra -Werror`, x86 Windows Unicode entry, static
runtime and CryptoAPI. Its default output is outside the checkout:

```text
/workspace/rebirth-analysis/windows/bridge-prototype/rebirth-render-probe.exe
```

An optional first build-script argument supplies another output filename.
`RB_RENDER_CC` selects an alternate compatible x86 MinGW compiler. Building does
not run the executable. Do not invoke the Drive modes until their code review
and execution authorization are complete.

## First gate: no panel, no song

Close ReBirth and other ReWire mixers normally first. On Windows 10, use an
ordinary PowerShell console (not PowerShell ISE), replacing paths as appropriate:

```powershell
& 'C:\RebirthProbe\rebirth-render-probe.exe' --no-panel-smoke 'C:\OriginalReBirth\ReBirth.dll'
$LASTEXITCODE
```

The host creates the real named `ReWi` mutex using
`CreateMutexA(NULL,FALSE,"ReWi")`. It rejects an existing mutex, including a
creation race indicated by `ERROR_ALREADY_EXISTS`. Only OpenDevice result 1
gives the helper ownership; result 4 refuses an existing engine state. Before
the first Drive, the byte-returning IsCloseOK must confirm that no panel is
connected. The helper does not patch or imitate an application's mixer check:
it opens and drives the actual engine while holding the same coordination
mutex used by the original middleware.

Eight initial stopped 64-frame calls must return all 64 served flags zero,
output-event count zero and both sample arrays entirely unchanged from their
fresh sentinels. Shutdown also services guarded stopped Drive and Idle until
IsCloseOK permits Close. `passed_no_song_smoke` additionally requires close,
unload and the post-hash check. It explicitly reports `audio_verified:false`:
an untouched no-song buffer establishes no music or silence rendering.

## Interactive known-demo capture

After the no-panel gate passes and the panel-connected trial is authorized,
start the host **before** opening the original application:

```powershell
& 'C:\RebirthProbe\rebirth-render-probe.exe' --capture 'C:\OriginalReBirth\ReBirth.dll' 'C:\RebirthProbe\demo-140bpm.wav'
$LASTEXITCODE
```

Both paths must be explicit drive-letter absolute Windows paths. The WAV must
have a `.wav` extension and a new filename in an existing writable directory.
Device names, alternate streams, UNC paths and ambiguous path components are
rejected. `CREATE_NEW` and exclusive output sharing prevent overwriting an
existing file. The helper never starts the panel or opens a song automatically.

Once the console says the engine is open:

1. Open the working original ReBirth application normally, without a launcher.
2. Load a known demo that you have already heard in the standalone application.
3. Turn **Loop OFF**, stop playback and rewind to the beginning in the panel.
4. Return to the host console and press Enter. Enter is accepted only after
   actual finite stereo output is served and loop feedback is disabled.
5. When capture finishes or aborts, **Quit ReBirth normally** while leaving the
   host console open. The host continues stopped Drive and Idle until IsCloseOK
   permits normal CloseDevice. Then it unloads the DLL and releases `ReWi`.

The setup wait is not a blocking line read: bounded console-event polling and
Windows message pumping run alongside serialized Idle and stopped Drive. Drive
is necessary for the original engine's initialization acknowledgement and
active-song selection; Idle alone is not an established handshake. A distinct
stopped priming block follows readiness before the first playing block at
position zero.

Escape or Ctrl+C cancels capture and enters the normal panel-quit service loop.
These keys do not terminate the original application. Do not close the console
window as a substitute for normal panel Quit.

## Capture format and acceptance

The test fixes host transport to **140 BPM, 192 ticks per quarter, 44,100 Hz**.
There is no recovered sample-rate negotiation; other rates remain unvalidated.
The engine receives exactly 64 frames per call, with ticks calculated from the
accumulated frame clock using checked rational arithmetic. Both uncertain
selector fields remain zero. Consequently the panel owns loop state and must
have Loop disabled; zero host loop inputs do not claim to disable panel loops.

The helper makes 6,891 playing calls: 441,024 native frames. It validates every
returned sample, then trims the final 24 frames to write exactly 441,000 stereo
frames, **10.000 seconds**. The WAV is interleaved IEEE float32 (format tag 3),
with an 18-byte WAVEFORMATEX `fmt` chunk, `fact` frame count and `data` chunk.
There is no normalization, limiter, PCM conversion or automatic playback.

Each capture block must serve exactly channels 0/1, write every requested sample
and produce finite values. A fresh NaN-bit sentinel detects untouched caller
memory. Peak and RMS are measured from the written samples. Each channel must
have peak greater than 0.001 and RMS greater than 0.0001; quieter output is
`inconclusive_quiet_audio`, because normal dither alone is insufficient evidence
of music. Peak above 1.0 is `inconclusive_overload`, since the original renderer
can exceed nominal full scale. It is not normalized automatically.

The new output is marked delete-pending through its own open handle before the
first header or sample write. Existing-handle I/O remains possible, and Windows
also deletes that file when closing handles after a host crash or watchdog
termination. The helper clears delete-pending only after accepted capture,
normal device close, DLL unload, post-hash verification and successful mixer
mutex release. Rejected and incomplete output remains marked for deletion.
To retain a complete finite overloaded capture for inspection, add
`--retain-overload` as the final argument. The JSON then reports
`inconclusive_overload`, `WAV_retained:true` and exit code 3; treat that file as
raw diagnostic float output. A failure to close normally still rejects it.

`passed_stereo_capture` and `audio_verified:true` mean the bounded finite,
nontrivial stereo criteria and normal shutdown passed. Listening and comparing
the known demo are still required to establish musical identity; the helper
always reports `music_identity_verified:false`. A useful Windows trial report
includes the demo filename, emitted JSON, and whether listening matches the
original demo. No VST or Ableton integration is proven by this CLI experiment.

## Buffer, feedback and shutdown boundaries

The combined buffer has exactly `0x11c8` payload bytes, zero-initialized before
inputs are installed. Its guarded allocation spans multiple writable pages
because 4,552 payload bytes exceed a Windows page. Each of the two independent
float32[64] channel payloads has its own guarded allocation. Every accessible
byte outside each payload is a checked canary; outer pages have no access.
Fresh sample sentinels and checked/reset canaries precede every Drive call.

Requested flags and pointers for only channels 0/1 are set. All remaining
flags/pointers, event regions, unused fields, loop inputs and selectors begin
zero. Frames at `+0x944`, position at `+0x94c`, tempo at `+0x950` and play 0/1 at
`+0x954` are the only varying native inputs. A full input snapshot is checked
after native calls, including both original pointer values. Native writes are
permitted only at exact reviewed output bytes: `0x96c/0x96d`, `0x970..0x980`,
`0x982/0x983`, and `0x1184..0x11c7`. Padding `0x96e/0x96f` and `0x981` remains
immutable. Only one-shot request outputs are cleared between calls.

Play requests 1/2 and seek requests are logged. Setup holds stopped position
zero deliberately. Capture aborts on a stop request, any seek request or enabled
loop feedback. Differing tempo feedback is logged; the next block still sends
the documented fixed 140 BPM. Idle, Drive, IsCloseOK, Close and unload are
serialized on the main thread. IsCloseOK's ABI is an unsigned byte in AL.

Setup is limited to 300 seconds, rendering to 60 seconds of wall time and normal
panel close to 180 seconds (5 seconds in no-panel smoke). A separate internal
watchdog allows 10 seconds for each native call. A native exception logs stage,
exception address and module base. A hung call or unsuccessful normal quit is a
failed host run, not a completed capture. The helper never kills the panel or
forces Close/FreeLibrary on an owned open engine. On close timeout it reports
incomplete shutdown and terminates only its own process. Protocol/sample
validation failure disables further Drive calls but permits serialized
Idle/IsCloseOK while waiting for normal Quit. Canary or immutable-input
corruption instead prohibits every further engine call and DLL unload; the
helper reports failure, cleans up its own output handle and terminates only
itself. Hard self-termination uses `TerminateProcess(GetCurrentProcess(),...)`
to avoid a hung engine's loader-lock/DLL-detach deadlock; it never obtains a
panel process handle. The engine coordination handle stays retained until
successful device close; normal DLL unload requires a closed context.

Exit codes: 0 = accepted gate; 1 = failure; 2 = refused existing mixer/engine or
connected panel at the smoke preflight; 3 = inconclusive capture; 124 = native
call watchdog; 125 = native exception. A successful WAV write alone cannot make
a run successful when shutdown, guards, inputs or post-hash verification fails.

The ABI evidence and review remain in
[legacy-drive-notes.md](legacy-drive-notes.md) and
[capture-design.md](capture-design.md).

## Additional checks

The published helper rejected an altered same-size DLL before load/Open/Drive
with `ERROR_CRC` (23). A separate fixture held an existing `ReWi` mutex: the
helper refused it with exit 2 and zero Open/Idle/Drive calls, without terminating
another process. Both tests preserved original hashes.

The utility-only fixture exercises the actual guard allocator, frame-position
conversion, WAV header and interleaved writer without loading ReBirth. FFprobe
recognized its synthetic output as float32 stereo, 44,100 Hz, exactly 10 seconds;
the file length, sample interleave and 24-frame trim were checked. Existing
output was rejected with error 80. Abrupt self-termination with exit 77 removed
the delete-pending incomplete output automatically. These fixture results do
not constitute synthesized music from ReBirth. The overwrite-refusal fixture
also emitted unresolved fontconfig/QEMU auxiliary warnings; the expected
refusal status and unchanged complete file were recorded separately.

Sources for these fixtures are in `tests/`; they are developer tools, not part
of the Windows listening procedure.
