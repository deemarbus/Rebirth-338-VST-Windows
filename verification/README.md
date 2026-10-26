The meaningful local checks are described in the repository README.

Recorded validation on 2026-10-26 in the Linux cloud environment:

- C# compilation and simulated process-API lifecycle tests passed (25 checks on
  Linux PowerShell 7.6.7; the non-Windows platform-refusal check is Linux-specific).
- The actual x86 checksum scan/sum, initialization gate and command-ID arithmetic
  passed the three-case regression check. The retired disk patch fails the
  original checksum, while original disk bytes with the temporary memory change
  retain the expected value `0x123a2e7b` and unchanged command IDs.
- The original EXE and DLL SHA-256 values remained unchanged.

These results do not establish native Windows API execution, a working ReBirth
window, media compatibility, audible output, preference persistence or VST3
support. The startup candidate must be tested on Windows before any readiness
claim. The original source files are not included in this repository.
