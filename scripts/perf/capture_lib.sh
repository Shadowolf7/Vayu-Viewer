#!/usr/bin/env bash
# Shared configuration and helpers for the Tracy capture scripts
# (capture_start.sh, capture_stop.sh, capture_supervisor.sh, capture_watchdog.sh).
# Not meant to be run directly -- source it.

CAPTURE_DIR="${VAYU_TRACY_DIR:-${HOME}/.vayu/logs/tracy-captures}"
MANIFEST="$CAPTURE_DIR/manifest.tsv"
STOP_FLAG="$CAPTURE_DIR/.stop"
SUPERVISOR_PID="$CAPTURE_DIR/supervisor.pid"
WATCHDOG_PID="$CAPTURE_DIR/watchdog.pid"
CURRENT_CAPTURE_PID="$CAPTURE_DIR/current_capture.pid"

# Rotation window, in seconds of *connected* profiling. tracy-capture's -s timer
# starts after the handshake completes (capture.cpp sets t0 past the connect
# loop), so the supervisor can be started before the viewer without burning a
# window waiting.
#
# Sized from measurement, not guesswork. An early full-session capture grew at
# ~110MB/s of in-RAM trace and tripped the memory watchdog after 11 seconds --
# a 90s window was never reachable. Disabling the THREAD and SHADER zone
# categories (see indra/llcommon/llprofilercategories.h) removed 63.5% of that
# volume, leaving roughly 40MB/s, so a 30s window lands near 1.2GB: under
# MEM_PCT below, and leaving headroom above MEM_FLOOR_KB.
#
# If you re-enable those categories, drop this correspondingly or the watchdog
# will simply cut every window short.
ROTATE_SECONDS="${VAYU_TRACY_ROTATE:-30}"

# Percentage of physical RAM tracy-capture may use before it terminates the
# connection. This is real enforcement, not just a readout: TracyWorker bails to
# `close` past the limit and capture.cpp still writes the .tracy file, so hitting
# it yields a short-but-valid capture rather than an OOM.
MEM_PCT="${VAYU_TRACY_MEM_PCT:-20}"

# How many finished captures to retain. 6 x 90s ~= 9 minutes of history, which
# covers /vayutrace's default 5-minute lookback with margin.
KEEP="${VAYU_TRACY_KEEP:-6}"

# Watchdog trips if MemAvailable falls below this (kB). System-wide availability
# is the right signal -- it is what actually collapsed the desktop, and it
# accounts for the viewer, tracy-capture and everything else at once.
MEM_FLOOR_KB="${VAYU_TRACY_MEM_FLOOR_KB:-1572864}" # 1.5 GiB

# Command prefix that restores SIGINT to its default disposition for a child.
#
# The stop path depends on SIGINT reaching tracy-capture -- that is the only
# signal it turns into a clean disconnect-and-save; anything harsher truncates
# the .tracy. But this supervisor is itself started with `&`, and POSIX requires
# a child of a non-interactive shell started that way to inherit SIGINT as
# SIG_IGN. A signal ignored on entry cannot be trapped or reset from the shell,
# and the disposition propagates on down, so the capture would never see it and
# the supervisor would block in `wait` forever.
#
# `env --default-signal=INT` (coreutils 8.31+) resets it at exec time. Verified
# necessary: without it a background child never receives the signal at all.
signal_safe_prefix() {
    if env --default-signal=INT true >/dev/null 2>&1; then
        echo "env --default-signal=INT"
    else
        # Older coreutils. The real tracy-capture installs its handler with
        # sigaction() unconditionally, which overrides an inherited SIG_IGN, so
        # this still works there -- just not with a shell-script stand-in.
        echo ""
    fi
}

find_tracy_tool() {
    # $1: tool basename, e.g. tracy-capture
    local tool="$1" var found repo_root
    var="TRACY_$(echo "${tool#tracy-}" | tr '[:lower:]-' '[:upper:]_')_BIN"
    if [ -n "${!var:-}" ]; then
        echo "${!var}"
        return
    fi
    if command -v "$tool" >/dev/null 2>&1; then
        command -v "$tool"
        return
    fi
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
    found=$(find "$repo_root"/build-*/vcpkg_installed/*/tools/tracy/"$tool" -type f 2>/dev/null | head -1 || true)
    echo "$found"
}

mem_available_kb() {
    awk '/^MemAvailable:/ {print $2}' /proc/meminfo
}

is_running() {
    # $1: path to a pid file
    local f="$1" pid
    [ -f "$f" ] || return 1
    pid="$(cat "$f" 2>/dev/null)" || return 1
    [ -n "$pid" ] || return 1
    kill -0 "$pid" 2>/dev/null
}
