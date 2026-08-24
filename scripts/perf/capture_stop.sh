#!/usr/bin/env bash
# Stops the rotating Tracy capture session started by capture_start.sh.
#
# The in-flight window is finalized with SIGINT rather than killed: tracy-capture
# turns SIGINT into a clean disconnect-and-save, while anything harsher leaves a
# truncated .tracy that tracy-csvexport can't read.
#
# Usage:
#   scripts/perf/capture_stop.sh

set -euo pipefail
# shellcheck source=capture_lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/capture_lib.sh"

if ! is_running "$SUPERVISOR_PID"; then
    echo "capture_stop: no capture session running." >&2
    rm -f "$SUPERVISOR_PID" "$WATCHDOG_PID" "$CURRENT_CAPTURE_PID"
    exit 1
fi

# Stop the loop first, so the supervisor doesn't start a fresh window while
# we're finalizing the current one.
touch "$STOP_FLAG"

if is_running "$CURRENT_CAPTURE_PID"; then
    kill -INT "$(cat "$CURRENT_CAPTURE_PID")" 2>/dev/null || true
fi

sup_pid="$(cat "$SUPERVISOR_PID")"
for _ in $(seq 1 40); do
    kill -0 "$sup_pid" 2>/dev/null || break
    sleep 0.5
done

if kill -0 "$sup_pid" 2>/dev/null; then
    echo "capture_stop: supervisor $sup_pid still running after 20s, sending TERM" >&2
    kill -TERM "$sup_pid" 2>/dev/null || true
    sleep 1
fi

if is_running "$WATCHDOG_PID"; then
    kill -TERM "$(cat "$WATCHDOG_PID")" 2>/dev/null || true
fi

rm -f "$SUPERVISOR_PID" "$WATCHDOG_PID" "$CURRENT_CAPTURE_PID" "$STOP_FLAG"

echo "capture_stop: stopped. Captures retained:"
if [ -s "$MANIFEST" ]; then
    tail -"$KEEP" "$MANIFEST" | while IFS=$'\t' read -r start end path; do
        printf '  %s  (%ss)  %s\n' \
            "$(date -d "@$((start / 1000))" '+%H:%M:%S')" \
            "$(( (end - start) / 1000 ))" \
            "$(basename "$path")"
    done
else
    echo "  (none -- did the viewer ever connect?)"
fi
