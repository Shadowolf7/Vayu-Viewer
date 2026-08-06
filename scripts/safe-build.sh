#!/usr/bin/env bash
# Runs a build command inside a systemd user-scope cgroup with a MemoryHigh
# limit, so a heavily parallel compile gets pushed into swap before it can
# starve the rest of the system. Large C++ builds (this codebase especially:
# RelWithDebInfo compiles with debug info + -O3, a big precompiled header)
# can use more RAM per parallel job than a modest-RAM/many-core machine has
# to spare, and unbounded ninja/make parallelism can make the whole desktop
# freeze rather than just slow the build down. See doc/BUILD.md's
# troubleshooting section for the incident that prompted this.
#
# Usage:
#   ./scripts/safe-build.sh <command> [args...]
#
# Examples (run from the repository root):
#   ./scripts/safe-build.sh cmake --build --preset ninja-os-relwithdebinfo
#   ./scripts/safe-build.sh cmake --build build-Linux-ninja-os --config RelWithDebInfo --parallel 4
#   ./scripts/safe-build.sh ninja -C build-Linux-ninja-os -j4
#
# The wrapper only adds a MemoryHigh limit to the command it runs. If you
# want to keep memory pressure lower, pass -j/--parallel explicitly as well.
# On systems without systemd-run or cgroup v2, the script falls back to
# running the command unwrapped.

set -euo pipefail

if ! command -v systemd-run >/dev/null 2>&1 || \
   [ "$(stat -fc %T /sys/fs/cgroup 2>/dev/null)" != "cgroup2fs" ]; then
    echo "safe-build: systemd-run or cgroup v2 not available here, running unwrapped" >&2
    exec "$@"
fi

total_kb=$(awk '/^MemTotal:/ {print $2}' /proc/meminfo)
total_bytes=$((total_kb * 1024))
# Leave ~30% of total RAM as headroom for the rest of the system.
limit_bytes=$((total_bytes * 70 / 100))

echo "safe-build: capping this build's cgroup to MemoryHigh=${limit_bytes} bytes (~70% of total RAM)" >&2
exec systemd-run --user --scope -p "MemoryHigh=${limit_bytes}" -- "$@"
