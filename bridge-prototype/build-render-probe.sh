#!/bin/bash
set -euo pipefail
RB_RENDER_SOURCE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RB_RENDER_CC="${RB_RENDER_CC:-/workspace/rebirth-analysis/tools/mingw-local/root/usr/bin/i686-w64-mingw32-gcc-posix}"
RB_RENDER_OUTPUT="${1:-/workspace/rebirth-analysis/windows/bridge-prototype/rebirth-render-probe.exe}"
mkdir -p -- "$(dirname -- "$RB_RENDER_OUTPUT")"
"$RB_RENDER_CC" -std=c11 -Wall -Wextra -Werror -O2 -municode -static \
    "$RB_RENDER_SOURCE/render-probe.c" -ladvapi32 -lm -o "$RB_RENDER_OUTPUT"
printf '%s\n' "$RB_RENDER_OUTPUT"
