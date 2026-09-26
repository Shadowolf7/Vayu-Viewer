#!/usr/bin/env bash
# Launch the fresh Release build with texture-encode dumping enabled.
#
# Uses the build-tree launcher so the process gets the correct environment
# (OPENSSL_CONF=/dev/null, AMD_DEBUG, mesa_glthread, ulimit, gamemode/GPU
# selection) and runs build-.../Release/bin/vayu-bin -- no install needed.
#
# Each run gets its own timestamped session dir so dumps never mix.
# After quitting the viewer:
#   python3 scripts/perf/analyze_vayu_dump.py <printed session dir>
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LAUNCHER="$ROOT/build-Linux-ninja-perf/newview/Release/vayu"
if [ ! -x "$LAUNCHER" ]; then
    echo "error: launcher not found or not executable: $LAUNCHER" >&2
    exit 1
fi

BASE="${VAYU_DUMP_BASE:-$HOME/vayudump}"
SESSION="$BASE/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$SESSION"

echo "VAYU_DUMP_DIR=$SESSION"
echo "launcher=$LAUNCHER"
echo
echo "1. Park at the target, swing the camera once to trigger decodes."
echo "2. Quit the viewer."
echo "3. python3 scripts/perf/analyze_vayu_dump.py '$SESSION'"

VAYU_DUMP_DIR="$SESSION" exec "$LAUNCHER" "$@"
