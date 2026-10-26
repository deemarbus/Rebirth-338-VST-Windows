# ReWire 1 metadata diagnostic

This is the first bounded diagnostic for a possible Windows bridge. It is not
a VST3, audio host, renderer or editor. It loads only the exact original
32-bit ReBirth 2.0.1 engine DLL and calls two metadata exports:

- `TopHatGetDeviceNameAndVersionProc`
- `TopHatGetDeviceInfoProc`

The input must be 2,472,960 bytes with SHA-256
`4c3a6441a2cd50f797d99b1c5b6cf0aaf198c7ad8ca2648196b530c2e3e58b12`.
The diagnostic holds a read-only handle denying writes/deletion, verifies the
hash with Windows CryptoAPI before loading, resolves exported names rather than
calling fixed addresses, and rechecks the hash after unloading.

Static inspection of the supplied DLL establishes that these exports copy
constant metadata without opening the engine. The info export clears exactly
`0x8d8` bytes, writes version 8 at `0x8c4`, count 18 at `0x20`, and channel names
in 32-byte slots starting at `0x24`. The diagnostic checks all of these values.
Its output buffers have surrounding canaries inside a writable page, with
no-access pages on both sides of that page. All bytes outside each output range
must remain unchanged before success is reported.

It never calls `EngineOpen`, `TopHatOpenDeviceProc`, idle, MIDI or audio functions.
It does not attach to the original panel, register a ReWire device, install a
Windows DLL, alter registry keys, or skip any media/license check. Ordinary DLL
initialization runs as part of `LoadLibraryExW`.

## Build

Use an x86 MinGW cross compiler. The generated binary defaults to `/tmp`, outside
the repository; an explicit output path is supported:

```bash
RB_PROBE_CC=i686-w64-mingw32-gcc bash bridge-prototype/build-metadata-probe.sh \
    /tmp/rebirth-metadata-probe.exe
```

The source explicitly rejects a non-x86 build. Windows CryptoAPI is supplied by
Windows; there is no downloaded executable payload or bundled ReBirth binary.

## Run

On Windows 10, use an absolute path to the original DLL:

```powershell
.\rebirth-metadata-probe.exe 'C:\ReBirth RB-338\ReBirth.dll'
```

Successful output is one JSON object with `status: passed`, device name,
API/info version 8, all 18 expected channels, unchanged canaries and unchanged
file hash. Exit status is zero. A rejected input or API failure produces
`status: failed`, the failed stage and a Windows error code, with nonzero exit
status. A memory guard violation is also a failed process, not a successful
metadata result.

## Validation recorded on 2026-10-26

The source compiled as an x86 PE with MinGW GCC 14 using `-Wall -Wextra -Werror`.
The actual metadata exports ran under the local Wine 10.0/QEMU environment:
exit status 0, version 8 from both calls, all 18 exact channel names, unchanged
canaries and unchanged original DLL hash. No device was opened and no audio
function was called.

One same-size copied DLL with a flipped byte was rejected with exit status 1,
stage `original_sha256`, error 23 (`ERROR_CRC`), before `LoadLibraryExW`.
Both the original DLL and EXE retained their original hashes.

Both Wine runs additionally emitted a QEMU signal-11 line on stderr. That
runtime warning remains unresolved and is recorded separately from the expected
probe output/status; it also occurred on the input-rejection path before the
original engine DLL was loaded. These runs are not native Windows 10 tests.

Passing this diagnostic establishes only the metadata call ABI. The legacy
combined audio/transport structure still requires complete validation before a
test host may request audio. Later gates are audible stereo rendering with the
original panel, transport, then a separate 64-bit VST3 frontend.

## Separate lifecycle-only diagnostic

`lifecycle-probe.c` is the next bounded gate. It uses the same exact original
DLL size/hash and read-only lock, resolves only the four lifecycle exports, and
calls `TopHatOpenDeviceProc` once. Only result 1 gives it ownership. It then calls
`TopHatIdleProc` ten times, with ten 10-ms waits, queries `TopHatIsCloseOKProc`,
and calls `TopHatCloseDeviceProc` only when that query's byte return is 1.
It unloads the closed engine and rechecks the original hash before reporting
success. It never resolves or calls the audio-drive export.

Open result 4 means another engine is in use: it reports refusal, calls no
Close, and does not affect that engine. If an owned engine refuses closing, it
reports refusal and exits its own process without explicitly unloading that
active engine. It never launches or kills a ReBirth panel, modifies a registry
key, or replaces a Windows file. The original engine may create its ordinary
named IPC objects while open.

Build separately, again writing the generated binary outside the repository:

```bash
RB_PROBE_CC=i686-w64-mingw32-gcc bash bridge-prototype/build-lifecycle-probe.sh \
    /tmp/rebirth-lifecycle-probe.exe
```

Run against the same original DLL with ReBirth closed. Use an external timeout
for developer runs; a timeout or crash is a failure even if some earlier stage
completed. The diagnostic writes progress stages to stderr, emits one final JSON
result, and has an unhandled-exception reporter with code/address/module base.
An internal 15-second watchdog can terminate only this diagnostic's own process
if a call hangs; it never looks up or terminates another ReBirth process. The
external timeout also covers Wine startup before this diagnostic's main runs.
Exit 0 means its bounded lifecycle completed; exit 2 is an explicit refusal;
other exits are failures. No completed audio render is implied by any of these
results.

The genuine lifecycle run on 2026-10-26 completed under Wine/QEMU with exit 0:
Open returned 1, all ten Idle calls returned, the close-permission byte was 1,
Close returned, the DLL unloaded, and the original hash remained unchanged.
Neither timeout fired. Stderr contained only the diagnostic's own stage log in
this run, with no QEMU warning. No Drive, panel, song or audio operation was
performed. This remains separate from native Windows validation.
