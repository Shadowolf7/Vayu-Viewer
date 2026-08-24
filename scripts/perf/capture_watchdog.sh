#!/usr/bin/env bash
# Memory kill switch for Tracy capture sessions.
#
# tracy-capture's own -m cap bounds one capture process, but it can't see the
# rest of the machine: the viewer's Tracy client queue is separately unbounded
# (Tracy's concurrent queue uses MAX_SUBQUEUE_SIZE = SIZE_MAX and grows by
# allocating rather than blocking), and the desktop needs headroom too. A
# session on this codebase once drove the machine deep into swap and froze the
# whole desktop; this is the guard for that.
#
# Watches system-wide MemAvailable rather than any one process's RSS, because
# that is the quantity that actually collapses -- whichever process is to blame.
#
# Normally started via capture_start.sh rather than directly.

set -euo pipefail
# shellcheck source=capture_lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/capture_lib.sh"

POLL_SECONDS="${VAYU_TRACY_WATCHDOG_POLL:-2}"

echo "capture_watchdog: floor=$((MEM_FLOOR_KB / 1024))MB poll=${POLL_SECONDS}s"

while is_running "$SUPERVISOR_PID"; do
    avail="$(mem_available_kb)"
    if [ "$avail" -lt "$MEM_FLOOR_KB" ]; then
        echo "capture_watchdog: MemAvailable $((avail / 1024))MB below floor $((MEM_FLOOR_KB / 1024))MB -- stopping capture" >&2

        # Tell the supervisor to stop looping, then finalize the in-flight
        # window so the data up to this point is still usable.
        touch "$STOP_FLAG"
        if is_running "$CURRENT_CAPTURE_PID"; then
            kill -INT "$(cat "$CURRENT_CAPTURE_PID")" 2>/dev/null || true
        fi

        logger -t vayu-capture-watchdog "stopped Tracy capture: MemAvailable $((avail / 1024))MB" 2>/dev/null || true
        exit 0
    fi
    sleep "$POLL_SECONDS"
done

echo "capture_watchdog: supervisor gone, exiting"
