#!/usr/bin/env bash
# Starts a rotating, memory-bounded Tracy capture session against a running
# viewer's on-demand Tracy client.
#
# Pairs with the tier-1 frame log (VayuPerfFrameLog -> ~/.vayu/logs/
# vayu_perf_frame.csv): that log says *when* a stutter happened and which frame
# phase it landed in; these captures let analyze.py pull the zone-level *why*
# for that moment.
#
# Rather than one unbounded capture for the whole session, this runs back-to-back
# bounded windows (see capture_supervisor.sh) with a watchdog on system memory
# (see capture_watchdog.sh). Both exist because a single full-session capture
# built a ~6GB in-RAM trace and froze the desktop.
#
# Safe to start before the viewer -- tracy-capture waits for the client, and its
# rotation timer only starts once connected.
#
# Usage:
#   scripts/perf/capture_start.sh
#
# Tunables (env): VAYU_TRACY_ROTATE, VAYU_TRACY_MEM_PCT, VAYU_TRACY_KEEP,
#                 VAYU_TRACY_MEM_FLOOR_KB

set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=capture_lib.sh
source "$HERE/capture_lib.sh"

BIN="$(find_tracy_tool tracy-capture)"
if [ -z "$BIN" ]; then
    echo "capture_start: couldn't find tracy-capture." >&2
    echo "  Build with -DUSE_TRACY_TOOLS=ON, or set TRACY_CAPTURE_BIN, or put it on PATH." >&2
    exit 1
fi

if is_running "$SUPERVISOR_PID"; then
    echo "capture_start: a capture session is already running (supervisor pid $(cat "$SUPERVISOR_PID"))." >&2
    echo "  Run scripts/perf/capture_stop.sh first." >&2
    exit 1
fi

mkdir -p "$CAPTURE_DIR"
rm -f "$STOP_FLAG"

# Refuse to start already in the hole -- capture only makes memory pressure
# worse, and starting here is how you get a frozen desktop instead of a trace.
avail="$(mem_available_kb)"
if [ "$avail" -lt "$MEM_FLOOR_KB" ]; then
    echo "capture_start: only $((avail / 1024))MB available, below the ${MEM_FLOOR_KB}kB floor -- refusing to start." >&2
    echo "  Close something first, or lower VAYU_TRACY_MEM_FLOOR_KB if you know what you're doing." >&2
    exit 1
fi

nohup "$HERE/capture_supervisor.sh" >> "$CAPTURE_DIR/supervisor.log" 2>&1 &
sup_pid=$!
echo "$sup_pid" > "$SUPERVISOR_PID"

sleep 1
if ! kill -0 "$sup_pid" 2>/dev/null; then
    echo "capture_start: supervisor exited immediately." >&2
    tail -10 "$CAPTURE_DIR/supervisor.log" >&2
    rm -f "$SUPERVISOR_PID"
    exit 1
fi

nohup "$HERE/capture_watchdog.sh" >> "$CAPTURE_DIR/watchdog.log" 2>&1 &
echo "$!" > "$WATCHDOG_PID"

echo "capture_start: rotating capture running (supervisor pid $sup_pid)"
echo "  window=${ROTATE_SECONDS}s  ram cap=${MEM_PCT}%  keep=${KEEP}  mem floor=$((MEM_FLOOR_KB / 1024))MB"
echo "  captures: $CAPTURE_DIR"
echo "  stop with: scripts/perf/capture_stop.sh"
