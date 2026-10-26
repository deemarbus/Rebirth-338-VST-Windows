# First stereo-capture design review

Read-only review, 26 October 2026. No lifecycle, render or panel process was started by this review. This reviews a small isolated x86 CLI host, not a VST3 implementation.

## Decision

The proposed guarded 64-frame stereo capture is feasible to prototype, with two changes required before a panel-connected trial:

1. **Establish real mixer presence**, not just direct OpenDevice. The original panel checks the standard named `ReWi` mutex before accepting an already-online engine. A direct OpenDevice call alone leaves the panel rejecting that engine.
2. **Service both Idle and stopped Drive during panel setup**, once the separate lifecycle and guarded no-song-drive gates have passed. Idle alone services a different command queue; the audio-drive service performs startup acknowledgements and active-song selection. A strict “load the demo completely before the first Drive” wait is not established and can stall the handshake/loading sequence.

Do not address either issue by patching the panel or disabling checks.

## Mixer presence: exact original protocol

`Rebirth.exe:0x40faea` asks helper `0x4763a1` whether the mutex named **`ReWi`** exists. That helper calls OpenMutexA with access `0x1f0001` and `bInheritHandle=FALSE`. Separately, `0x428753` checks the `ReBirth Panel->DLL` file-mapping endpoint.

If the engine endpoint exists but the mixer mutex does not, `0x40fb43`–`0x40fb7f` displays:

`The ReBirth Engine is online but not registred![Quit]`

and throws the ordinary initialization-exit exception. This is a mixer-coordination condition, not evidence of a registry-key or media-license check.

The bundled middleware implements this coordination in `TopHatOpenImp`:

- `0x10002ef7`–`0x10002f0c` calls `0x100013a0`, which probes OpenMutexA for `ReWi`; an existing mixer returns error 6.
- `0x10002f45`–`0x10002f53` constructs an object that calls **CreateMutexA(NULL, FALSE, "ReWi")** at `0x1000140d` and retains the handle.
- `TopHatCloseImp` closes devices first, then closes that retained mutex handle at `0x10003537`–`0x10003551`.

A direct, single-device prototype can reproduce this small **legitimate host-coordination protocol** without registry writes: create the mutex with initial owner FALSE, immediately reject `ERROR_ALREADY_EXISTS` so an intervening competing mixer is handled, retain the handle while its own device and connected panel are active, and close the handle only after normal device/panel shutdown. No ReleaseMutex is needed, because the middleware does not acquire ownership. A failed/denied CreateMutex is a failed host-open, not permission to proceed.

Using the actual middleware instead is another route, but its device enumeration reads installed ReWire registry configuration. This review establishes neither a need to write registry keys nor a reason to substitute another engine.

## Why Idle-only setup is insufficiently established

Idle's `0x1001cb00` services context FIFO `+0x410` and its distributor at `+0x418`. In contrast, the time/IPC service reached by Drive, `0x1001cc40`, services `+0x414` and `+0x41c`, then dispatches commands through `0x1001cdf0`.

Two concrete examples on that latter path:

- `0x1001ce70`–`0x1001ced1` handles an initialization state flag and sends a reply whose command is `0x40`.
- `0x1001cf13`–`0x1001cf1f` calls `0x1001d7f4`, which selects a song through `0x1001bda4`. Idle's creation handler `0x1001d840` allocates a song and replies with an ID; creation alone is not an active render selection.

Therefore, after validating the separate no-song Drive gate, use a bounded setup loop with **play=0, position=0, valid nonzero tempo, both uncertain selector fields initially zero, and guarded stereo buffers**, continuing to call Idle serially. An Enter/key wait must be nonblocking with respect to this loop. Do not keep a native call running concurrently on a thread while another thread changes its buffers or closes the device.

The first Drive can process a queued song selection after its pre-render update already skipped the null-song state; allow bounded priming and require the served flags/readiness evidence before capture. No-song Drive may leave audio buffers untouched: initialize sample memory with a recognizable sentinel and require actual writes when channels are reported served. Do not mistake zero-filled caller memory for rendered silence.

## Input fields and reset policy

Keep the `rewire-drive-abi-review.md` bounds: a zero-initialized 0x11c8-byte combined buffer, separate guarded x86 float32 buffers, 64 frames, requested flags 0/1 for channels 0/1 only, no events, every other requested flag/pointer zero.

`+0x958=0` uses the ordinary position synchronizer `0x1001c2f4`. Nonzero selects `0x1001c3c4`, which quantizes/coarsely resets sequencer position and then applies the requested position. The observed code supports a seek/chase interpretation, but not a public SDK enum name. **Do not set it to 1 on every capture block**. For the first trial, keep zero and require the panel to be rewound/stopped at the beginning before capture, or validate an explicit one-block seek transition separately. Continuous and seek transport tests should remain distinct.

`+0x95c=0` leaves loop ownership with the panel side. In that mode the host must not claim to have forced loops off using zeroes in `+0x960..0x968`; those inputs are ignored. For a simple linear first capture, require **Loop off in the original panel** and log its returned loop state. Alternatively, implement and test PPQ wrapping from feedback before accepting a loop-enabled capture. Advancing a linear host PPQ forever while the engine loops is not a validated synchronization policy.

For a fixed-tempo test, choose a documented tempo within 20–500 BPM, set `+0x950=BPM*1000`, and derive `+0x94c` from an accumulated frame count at 192 PPQ/44.1 kHz. Use a double/rational accumulator and checked int32 conversion; do not add a rounded integer tick increment every 64 frames. State the selected tempo in the report/WAV metadata so a comparison is meaningful.

## Panel feedback that must be distinguished from inputs

Post-render `0x1001c204` exports and clears one-shot engine requests:

| Field | Meaning established for this DLL | Suggested bounded-prototype policy |
| --- | --- | --- |
| Byte `+0x96c` | 0 = none, 1 = request play, 2 = request stop | Log every request. In setup keep transport stopped intentionally. During a panel-controlled capture honor 1/2 on the next block; alternatively declare fixed host transport and abort cleanly on an unexpected stop instead of silently overruling the user. Reject/log other values. |
| Byte `+0x96d`, int32 `+0x970` | Nonzero flag requests reposition to the supplied musical position | Consume the flag only after validating the position. A host may queue the requested position and a single separately validated seek block; the first fixed linear-capture prototype can instead stop capture and report the request. Never reinterpret these as native pointers. |
| uint32 `+0x974` | Tempo feedback/request state | The next pre-render copies host `+0x950` over the song's tempo. For fixed host tempo, log differing feedback and report that policy; to support the panel knob, range-check and apply the feedback on the next block. |
| `+0x978`, `+0x97c`, byte `+0x980` | Loop start/end/enabled feedback | With `+0x95c=0`, these describe panel-owned loop state. Require disabled for the simplest initial linear test or implement validated wrapping. |

Play-request values are proven by `0x1001c27c`–`0x1001c2c7`: request 1 is discarded when the current host play state is already nonzero; request 2 is discarded when that state is already zero; the request is then copied out and the internal byte cleared. Selection/start code sets request 1 (`0x1001bf17`), and stopping/switching can set request 2 (`0x1001bd7a`). Seek requests are generated for the active song in `0x1001bf70`–`0x1001bf9e`.

Because outputs are not guaranteed written without an active song, clear only the **known request-output fields** before each call so stale values cannot be misread as a new request. Preserve and validate all native input fields and guards.

## Float scaling and pointer mutation

**Nominal float normalization is now independently supported by the original PCM16 conversion**, not just API convention. Both standalone and ReWire rendering use DSP function `0x10013444`. The standalone path collects its float Mix-L/R output, then `0x10013c3c`–`0x10013c62` multiplies by the double at `0x10151f70`, whose decoded value is **32768.0**. It clamps to constants 32767.0 / -32768.0 at `0x10151f78` / `0x10151f7c` before storing PCM16. The ReWire path copies those DSP floats directly to caller buffers, so 1.0 corresponds to nominal full scale.

There is no final ReWire hard limiter shown here. A float peak above 1 can be ordinary overloaded mix output; it is not, by itself, proof of a wrong struct or scale. Do not normalize unknown output automatically. A conservative first accepted WAV can require peak <= 1 and otherwise report an overload/inconclusive capture, retaining clear diagnostic statistics. NaN/Inf, canary damage, missing served channels and unchanged sentinels are separate hard failures.

The input pointers at `+0x844 + channel*4` are **read only** in this implementation. `0x100132b8` copies them into global working pointers at `0x100def08`; only the global working copies advance (`0x100133a2` etc.). Verify the combined buffer's original pointer values remain unchanged after every call and reset sample sentinels/canaries before reuse.

## Bounded capture and shutdown

After setup readiness, use a stopped priming block followed by play=1 at position 0, then a fixed frame-clock progression. Keep rendering and Idle serialized, periodically service Idle during capture, and validate every block. Do not start capture merely because the user pressed Enter while no song has reached the render path.

For 10.000 seconds at 44.1 kHz, native calls can stay 64 frames: render 6891 blocks (441024 frames) and write only the first 441000 validated frames. Use a newly created WAV path and no auto-play. Report actual frames/rate/tempo, L/R peak and RMS, non-finite sample count, served flags, guards and request events. Nontrivial music must be distinguished from dither-sized output.

Finally send a stopped block while the device is still open, continue servicing the helper while the user closes the original panel normally, wait for IsCloseOK, then CloseDevice and close the host mutex handle. Do not force Close while the panel is alive, unload an open engine, terminate the user's panel, or leave the host indefinitely unresponsive waiting for console input. Keep a watchdog for native calls; distinguish the setup timeout, bounded audio duration and panel-close timeout.

The next executable gate remains the guarded **no-panel/no-song Drive** test after lifecycle success. A positive known-demo audio capture requires the user's working original media/panel on Windows and remains unverified here.
