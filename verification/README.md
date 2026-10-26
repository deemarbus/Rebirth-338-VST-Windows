The meaningful local checks are described in the repository README.

Recorded validation on 2026-10-26 in the Linux cloud environment:

- C# compilation and simulated process-API lifecycle tests passed (25 checks on
  Linux PowerShell 7.6.7; the non-Windows platform-refusal check is Linux-specific).
- The actual x86 checksum scan/sum, initialization gate and command-ID arithmetic
  passed the three-case regression check. The retired disk patch fails the
  original checksum, while original disk bytes with the temporary memory change
  retain the expected value `0x123a2e7b` and unchanged command IDs.
- The original EXE and DLL SHA-256 values remained unchanged.
- A separate local Wine 10.0 + QEMU i386 environment was prepared from
  signature-verified Debian packages. A fresh Wine prefix ran `cmd.exe`, and
  the untouched ReBirth EXE reached its normal request for the ReBirth 2.0 CD.
  No CD image was available in that environment; the media check was preserved.
- A native x86 developer probe created a suspended child in Wine, but its first
  `ReadProcessMemory` call failed with Win32 error 5. It terminated only its own
  new child. This is an unresolved limitation of that emulated test path and
  does not validate the launcher's native process-memory calls on Windows.

These results do not establish native Windows API execution, a working ReBirth
window, media compatibility, audible output, preference persistence or VST3
support. The startup candidate must be tested on Windows before any readiness
claim. The original source files are not included in this repository.
