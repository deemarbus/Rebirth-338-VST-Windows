#!/bin/bash
set -euo pipefail
RB_WORKER_SOURCE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RB_WORKER_CC="${RB_WORKER_CC:-/workspace/rebirth-analysis/tools/mingw-local/root/usr/bin/i686-w64-mingw32-gcc-posix}"
RB_WORKER_OUTPUT="${1:-/workspace/rebirth-analysis/windows/vst3/rebirth-bridge-worker.exe}"
mkdir -p -- "$(dirname -- "$RB_WORKER_OUTPUT")"
"$RB_WORKER_CC" -std=c11 -Wall -Wextra -Werror -O2 -municode -static \
    "$RB_WORKER_SOURCE/bridge-worker.c" -ladvapi32 -lm -o "$RB_WORKER_OUTPUT"
printf '%s\n' "$RB_WORKER_OUTPUT"
