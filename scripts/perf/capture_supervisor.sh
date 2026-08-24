#!/usr/bin/env bash
# Runs back-to-back bounded tracy-capture windows, so profiling can be left on
# for a whole play session without an unbounded in-RAM trace.
#
# Each window is a separate .tracy file recorded with a wall-clock time range in
# manifest.tsv; analyze.py picks whichever window covers the moment you care
# about. Old captures are pruned so disk stays bounded too.
#
# Normally started via capture_start.sh rather than directly.

set -euo pipefail
# shellcheck source=capture_lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/capture_lib.sh"

BIN="$(find_tracy_tool tracy-capture)"
if [ -z "$BIN" ]; then
    echo "capture_supervisor: tracy-capture not found." >&2
    exit 1
fi

SIGNAL_SAFE="$(signal_safe_prefix)"

mkdir -p "$CAPTURE_DIR"
touch "$MANIFEST"

cleanup() {
    # Finalize whatever window is in flight. SIGINT (not TERM/KILL) is what
    # tracy-capture's handler turns into a clean disconnect-and-save; anything
    # harsher leaves a truncated file that tracy-csvexport can't read.
    if is_running "$CURRENT_CAPTURE_PID"; then
        kill -INT "$(cat "$CURRENT_CAPTURE_PID")" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

prune() {
    local -a files
    mapfile -t files < <(ls -1t "$CAPTURE_DIR"/*.tracy 2>/dev/null || true)
    local i f
    # Sweep any zero-length leftovers regardless of age -- they are unreadable
    # and would otherwise occupy a retention slot that a usable capture needs.
    for f in "${files[@]}"; do
        [ -s "$f" ] || rm -f "$f" "${f%.tracy}.log"
    done
    mapfile -t files < <(ls -1t "$CAPTURE_DIR"/*.tracy 2>/dev/null || true)
    for ((i = KEEP; i < ${#files[@]}; i++)); do
        rm -f "${files[$i]}" "${files[$i]%.tracy}.log"
    done
    # Drop manifest rows whose capture is gone or empty, so analyze.py never
    # points at something unusable.
    if [ -s "$MANIFEST" ]; then
        local tmp
        tmp="$(mktemp)"
        while IFS=$'\t' read -r start end path; do
            [ -s "$path" ] && printf '%s\t%s\t%s\n' "$start" "$end" "$path"
        done < "$MANIFEST" > "$tmp"
        mv "$tmp" "$MANIFEST"
    fi
}

echo "capture_supervisor: rotate=${ROTATE_SECONDS}s mem_cap=${MEM_PCT}% keep=${KEEP}"

while [ ! -f "$STOP_FLAG" ]; do
    ts="$(date +%s)"
    out="$CAPTURE_DIR/${ts}.tracy"
    log="$CAPTURE_DIR/${ts}.log"
    start_ms="$((ts * 1000))"

    # shellcheck disable=SC2086 # prefix is intentionally word-split (may be empty)
    $SIGNAL_SAFE "$BIN" -o "$out" -s "$ROTATE_SECONDS" -m "$MEM_PCT" > "$log" 2>&1 &
    cap_pid=$!
    echo "$cap_pid" > "$CURRENT_CAPTURE_PID"

    wait "$cap_pid" || true
    rm -f "$CURRENT_CAPTURE_PID"

    end_ms="$(($(date +%s) * 1000))"

    # Test for a NON-EMPTY file, not merely an existing one. tracy-capture
    # creates the output at the start of its save and fills it afterwards, so a
    # crash mid-write (observed: a SIGSEGV inside Tracy 0.13.1's own
    # Worker::UpdateSampleStatisticsPostponed) leaves a 0-byte file behind. A
    # plain -f test would happily record that in the manifest and analyze.py
    # would later pick it as the window covering a stutter and find nothing.
    if [ -s "$out" ]; then
        printf '%s\t%s\t%s\n' "$start_ms" "$end_ms" "$out" >> "$MANIFEST"
        prune
    else
        # Either it never connected (viewer not up yet) or it died before
        # writing anything. Keep going -- one lost window shouldn't end the
        # session -- but say so, because silently dropping windows would look
        # identical to "nothing interesting happened" at analysis time.
        if [ -f "$out" ]; then
            echo "capture_supervisor: capture died before writing $(basename "$out") -- window lost" >&2
            tail -3 "$log" >&2 2>/dev/null || true
        fi
        rm -f "$out" "$log"
        sleep 2
    fi
done

rm -f "$STOP_FLAG"
echo "capture_supervisor: stopped"
