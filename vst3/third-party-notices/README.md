# VST3 runtime notices

The frontend uses the unchanged official Steinberg VST3 SDK 3.8.1, commit
`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`. Its MIT notice is included verbatim
in `Steinberg-VST3-SDK-MIT.txt`. The SDK's trademark rules still apply; this
project does not claim endorsement by Steinberg or by ReBirth's authors.

The frontend statically links libsamplerate 0.2.2 for host-rate conversion.
Its BSD 2-Clause license is included in `libsamplerate-BSD-2-Clause.txt`.

The native Windows package is built using Visual Studio 2022/MSVC with static
C runtime linkage. It contains this project's frontend and worker, with SDK
code in the frontend. It does not contain the original ReBirth engine,
application, songs, CD/ISO or macOS binaries.

Local MinGW development builds have different compiler/runtime dependencies.
The GCC runtime exception and MinGW-w64 notices are retained in
`bridge-prototype/third-party-notices`. Such builds must carry those notices
if distributed; they are not interchangeable with the MSVC package manifest.
