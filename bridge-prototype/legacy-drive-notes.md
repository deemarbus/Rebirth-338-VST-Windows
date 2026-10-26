# ReWire 1 combined drive-buffer review

Read-only review, 26 October 2026. The user has now confirmed that the original Windows application starts normally and plays a demo with audible sound. No device-open, idle or audio-drive export was called during this review. The original application and DLLs remain unchanged.

## Conclusion

The **complete public ReWire 1 ABI is not yet established**: the event payload regions and several reserved/unused fields have no confirmed definition. The later `ReWire.h` already saved in `rewire-header-reference` contains legacy version constants, but **no legacy combined drive-structure definition**. Its separate 256-channel input/output structures must not be substituted.

For this exact engine DLL, however, the **complete set of direct drive-buffer accesses** can be bounded. The render path consumes one combined little-endian x86 buffer, accesses no byte beyond `0x11c7`, and uses 32-bit planar output pointers. A carefully bounded, guarded test can therefore be designed around explicit offsets without claiming to have recovered a portable SDK header. That test is a later acceptance gate, after the metadata-only probe and reviewed device lifecycle.

## Call chain and validation limits

`ReWire.dll_:TopHatDriveAudioImp` at preferred VA `0x10003840` takes a device handle/index and a drive-buffer pointer, resolves the open device, and forwards the pointer to its `TopHatDriveAudioProc` callback. Its instructions `0x1000384a`–`0x10003858` show no structure-size, frame-count, flag or pointer validation. **Do not rely on the middleware to check these bounds.**

`ReBirth.dll:TopHatDriveAudioProc` is x86 cdecl with one pointer argument and a plain `ret` at `0x1001b8ed`. It:

1. Reads `+0x95c` and sets a DLL-global mode byte inversely from its zero/nonzero value.
2. Clears all 64 served-channel bytes at `+0x1188..+0x11c7` and a 32-bit output-count field at `+0x1184`.
3. Checks the engine's internal synchronization/IPC object, then calls pre-render state update `0x1001c0f4`, time/IPC service `0x1001cc40` with `+0x944`, and post-render routine `0x1001c204`.
4. Post-render calls song renderer `0x10013240` only if an active song exists.

The pre-render/post-render functions pass no drive pointer onward except from post-render to `0x10013240`. The song renderer uses only the requested flags, output pointers and frame count from this buffer; its DSP subroutine receives an internal array of audio pointers rather than the drive buffer. This bounds the relevant pointer chain for this build.

## Observed fields, with uncertainty preserved

Offsets are bytes from the sole buffer pointer, under the original 32-bit ABI. R/W refers to the device's use of this buffer.

| Offset | Width/extent | R/W | Established behavior |
| --- | --- | --- | --- |
| `0x000..0x803` | 0x804 bytes | Not accessed on reviewed path | Opaque prefix. Its arrangement is consistent with a count and fixed-size events, but neither a public event definition nor capacity is established. Zero for a future no-event test; do not fabricate event records. |
| `0x804` | 64 byte flags | R | Requested channels. Renderer indexes only 0..17; any nonzero byte means requested. Use 0 or 1 explicitly. |
| `0x844` | 64 x 4-byte pointers | R | Planar channel pointers. Renderer indexes only 0..17, and advances pointers internally by four bytes per sample. These are **x86 pointers**, not host-sized 64-bit pointers. |
| `0x944` | 32-bit | R | Frames to render. Passed to engine time/IPC service and consumed as a signed loop quantity by song renderer. No reliable public maximum or rejecting validation is visible on this ReWire path. |
| `0x948` | 32-bit | No access found | Unknown/unused in this build's reviewed path. Do not assign sample-rate semantics merely from its position. |
| `0x94c` | signed 32-bit | R | Batch-start musical position. Passed to one of two position synchronization helpers. Evidence supports **192 ticks per quarter note**, not the later API's 15360. |
| `0x950` | 32-bit | R | Tempo copied into song transport parameters and engine feedback state. Evidence supports **BPM x 1000**. |
| `0x954` | 32-bit, low byte used for action | R | Compared with previous play state; changes call start/stop helper. Zero stops, nonzero low byte starts. Restrict a test to 0/1. This is not evidence for the later 0/1/2 play-mode enum. |
| `0x958` | 32-bit | R | Chooses position synchronization helper: zero -> `0x1001c2f4`; nonzero -> `0x1001c3c4`. The latter includes coarse 0x300-tick positioning and seek handling. Public field name/precise chase policy still unconfirmed. |
| `0x95c` | 32-bit | R | Inverted into global mode byte. Nonzero makes the device accept loop inputs; zero leaves loop ownership/updates with the panel side. Public enum/name unconfirmed. |
| `0x960` | signed 32-bit | R | Loop start copied into song/engine state when `+0x95c` is nonzero. Same musical-position units. |
| `0x964` | signed 32-bit | R | Loop end, same condition/units. |
| `0x968` | byte | R | Loop enabled flag, same condition. Restrict to 0/1. |
| `0x969` | byte | No access found | Unknown/reserved. |
| `0x96a`, `0x96b` | byte each | R | Copied into feedback bytes `+0x982`, `+0x983`. Signature-related semantics are plausible from the later API, but no denominator convention is established. They do not drive this engine's sequencer in the reviewed call chain. |
| `0x96c` | byte | W | Panel/engine play-state request copied out, then the internal request is cleared. Internal values 1/2 are tested against the current play state. Do not treat this as the host's input play-mode field. |
| `0x96d` | byte | W | Panel/engine position-request flag copied out and cleared internally. |
| `0x96e..0x96f` | 2 bytes | No access found | Unknown/padding. |
| `0x970` | signed 32-bit | W | Requested musical position copied from engine feedback state. |
| `0x974` | 32-bit | W | Tempo feedback. |
| `0x978` | signed 32-bit | W | Loop-start feedback. |
| `0x97c` | signed 32-bit | W | Loop-end feedback. |
| `0x980` | byte | W | Loop-enabled feedback. |
| `0x981` | byte | No access found | Unknown/padding. |
| `0x982`, `0x983` | byte each | W | Copy of `+0x96a`, `+0x96b`. |
| `0x984..0x1183` | 0x800 bytes | No access found | Opaque output/event region; no payloads emitted on this reviewed device path. Do not infer an SDK event definition. |
| `0x1184` | 32-bit | W | Cleared to zero at entry. No later nonzero write seen in this drive path. |
| `0x1188..0x11c7` | 64 byte flags | W | All cleared at entry; indices 0..17 set from whether a channel is requested when the renderer runs. |

The output state fields `+0x96c..+0x983` are not guaranteed to be written when no song is selected. Likewise, the served flags being cleared does not imply the audio memory was zeroed.

## Position and tempo evidence

- Engine transport constructor sets tempo feedback to `0x222e0` = **140000**, and loop start/end to 0 and `0x300` = **768** (`0x1001bbb4`–`0x1001bc06`).
- The pre-render update copies `+0x950` unchanged into the song transport tempo (`song+0x158+4`) at `0x1001c112`–`0x1001c121`.
- Sequencer advance `0x1001ef20` obtains that tempo through `0x1001bcc0`. At `0x1001ef55` it multiplies it by the double stored at `0x10153808`: **0.0000046439909297052154** per 64-sample DSP quantum.
- That constant is exactly `192 * 64 / (60000 * 44100)` at ordinary double precision. Thus 140000 produces the expected tick increment for 140 BPM at **192 PPQ / 44.1 kHz**.
- The alternate positioning helper divides by `0x300` at `0x1001c3f8`–`0x1001c414`, consistent with four quarters at 192 PPQ.

This identifies the convention used by this engine; it does not establish that a host can negotiate another sample rate. There is no sample-rate argument on the recovered zero-argument OpenDevice call, and `+0x948` is not read here. A first test should keep its generated WAV/sample-clock interpretation at 44.1 kHz. Future 48/96 kHz DAW integration requires explicit validation or conversion.

## Audio buffers and dangerous bounds

Renderer `0x10013240` reads frame count at `0x10013258`, flags at `0x10013274`, served flags at `0x1001327f`, and per-channel pointers at `0x100132b8`. The channel loop is bounded to **18** by `0x100132e3`–`0x100132e6`.

DSP is produced in **64-sample quanta**. A full quantum advances channel pointers by `0x100` bytes (`0x100133a2`), and remainder handling uses four-byte copies (`0x10013408`–`0x1001341a`). The four-byte data path and DSP support planar float32, but numeric scaling and comparison with the known audible standalone demo still require runtime evidence.

Safety requirements for a future bounded probe:

- Allocate at least `0x11c8` writable bytes for the combined buffer, surrounded by checked canaries/guard pages. Zero its entire extent initially. Do not use the later structure's size or put a guessed `fStructSize` in its prefix.
- Allocate each requested channel independently with room for the exact frame count, plus checked boundaries. Use native x86 addresses whose conversion to uint32 is checked.
- Set requested flags **only** for Mix-L/Mix-R, indices 0/1, and valid separate buffers at `0x844`/`0x848`; leave the other 62 flags and pointers zero. Validate pointers and sizes in the helper before calling native code.
- Use a short fixed **64-frame** block first, then a bounded number of equally sized blocks. This exercises the ordinary DSP quantum and avoids introducing variable-size carry behavior during initial validation. Do not accept negative, zero or unbounded counts based merely on what the DLL tolerates.
- Check canaries, returned served flags, finite samples and progress after each block. A successful call, unchanged buffers or dither-sized values alone are not proof of music playback.
- Do not call drive concurrently with idle/close, and do not assume multiple independent instances: the engine's global state and IPC names are shared.

## Recommended next acceptance gates

1. Finish the existing isolated **metadata-only x86 probe**: load the verified DLL, resolve exports, read version/name/info into guarded buffers, verify stack discipline and unchanged file hashes, then unload. This does not open the engine or claim audio.
2. Review a separate, bounded device lifecycle harness before running it: OpenDevice -> service idle -> IsCloseOK -> CloseDevice, with no drive call and no changes to the original installation/registry. Confirm coexistence failure is handled by asking the user to close the ordinary application; never kill an existing ReBirth process.
3. Only then prepare the guarded explicit-offset drive probe described above. The original panel must connect to the helper's opened ReWire engine and load a known demo. Capture finite, nontrivial stereo audio at 44.1 kHz, verify normal close/reopen, then measure stop/play/position/tempo behavior separately. Without the panel/selected song, the render path skips audio and can leave output sample memory untouched; that is an expected incomplete setup, not a successful instrument.

The current evidence is sufficient to design that narrow test, not to announce a finished legacy ABI, a working ReWire host, or a VST3 plugin.
