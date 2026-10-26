#!/bin/bash
set -euo pipefail
RB_PROBE_SOURCE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RB_PROBE_CC="${RB_PROBE_CC:-i686-w64-mingw32-gcc}"
RB_PROBE_OUTPUT="${1:-/tmp/rebirth-lifecycle-probe.exe}"
mkdir -p -- "$(dirname -- "$RB_PROBE_OUTPUT")"
"$RB_PROBE_CC" -std=c11 -Wall -Wextra -Werror -O2 -municode -static \
    "$RB_PROBE_SOURCE/lifecycle-probe.c" -ladvapi32 -o "$RB_PROBE_OUTPUT"
printf '%s\n' "$RB_PROBE_OUTPUT"
