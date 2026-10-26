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

These cloud results alone do not establish native Windows API execution, audible
output or VST3 support. The original source files are not included in this
repository.

Subsequent user-reported Windows 10 validation, 2026-10-26:

- ReBirth starts using the new compatibility launcher.
- A demo song plays with audible sound.
- After a normal File > Quit, the original `C:\ReBirth RB-338\Rebirth.exe` starts
  again with sound without PowerShell or the former first-run Help interruption.
  This confirms normal quit/reopen behavior on that installation; preference
  file paths and contents were not independently inspected.

These are the user's observations on their Windows PC, separate from the Linux
tests. No Ableton integration, ReWire-host audio render, VST3 transport, project
state or multiple-instance behavior has been verified yet.

Initial bridge diagnostic results in the separate Wine/QEMU environment:

- The x86 metadata probe returned exit 0, API/info version 8, all 18 expected
  channels and intact output-buffer canaries. It did not open the engine.
- A same-size altered DLL was rejected before loading with exit 1 and
  `ERROR_CRC` (23). Both metadata runs also emitted an unresolved QEMU signal-11
  warning, including the refusal path before loading the engine DLL.
- The separate lifecycle probe completed Open (result 1), ten Idle calls,
  IsCloseOK, Close, FreeLibrary and an unchanged file hash with exit 0. Neither
  its own watchdog nor the external timeout fired. This run had no QEMU warning.
- All original EXE/DLL hashes remained unchanged. No render function was called.

These diagnostics exercise the exact legacy DLL under Wine; they are not
native Windows 10 bridge tests or evidence of hosted music playback.
