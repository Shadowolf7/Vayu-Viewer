#!/usr/bin/env python3
"""\
@file   analyze.py
@brief  Correlate the tier-1 frame-phase log (vayu_perf_frame.csv, written
        when VayuPerfFrameLog is enabled) with the rotating Tracy captures
        produced by scripts/perf/capture_start.sh, to find what was happening
        during a recent stutter.

Two tiers:
  1. vayu_perf_frame.csv -- always on, cheap, wall-clock timestamped. Says
     *when* a stutter happened and which frame phase it landed in.
  2. Rotating .tracy captures -- bounded windows listed in manifest.tsv. Say
     *why*, at zone level.

This picks the capture window covering the worst frame and reports the zones
that dominate it.

Usage:
    scripts/perf/analyze.py [--minutes N] [--threshold-us US] [--top N]
                            [--tier1 PATH] [--capture PATH]
                            [--capture-start-ms MS] [--pad-ms MS]
                            [--csvexport PATH]
"""

import argparse
import csv
import heapq
import os
import subprocess
import sys
import time
from pathlib import Path
from shutil import which

VAYU_LOGS = Path.home() / ".vayu" / "logs"
TIER1_DEFAULT = VAYU_LOGS / "vayu_perf_frame.csv"
CAPTURE_DIR_DEFAULT = Path(
    os.environ.get("VAYU_TRACY_DIR", VAYU_LOGS / "tracy-captures"))

TIER1_INT_FIELDS = (
    "frame", "doframe_us", "render_frame_us", "render_idle_us",
    "render_sleep_us", "render_display_us", "render_huds_us",
    "render_ui_us", "render_swap_us",
)

# Phase column -> what a spike in it actually means. Mirrors the RecordSceneTime
# scopes in llviewerdisplay.cpp / llappviewer.cpp.
#
# Ordered most-specific first, because these scopes NEST: RENDER_SWAP sits
# inside RENDER_DISPLAY (llviewerdisplay.cpp:1590 within :438), as do the HUD
# and UI scopes. Taking a plain max() would therefore always name the outermost
# scope and report "the render path was slow" for what is really a GPU/vsync
# stall. Attributing to the innermost phase that explains the frame is the
# whole difference between a useful answer and a tautology.
PHASE_ORDER = (
    ("render_swap_us", "GPU buffer swap -- GPU-side or vsync/compositor stall, not viewer CPU code"),
    ("render_huds_us", "HUD drawing"),
    ("render_ui_us", "UI drawing"),
    ("render_sleep_us", "viewer slept on purpose (frame limiter / target FPS) -- not a stall"),
    ("render_display_us", "main render path (batches, shaders, scene complexity)"),
    ("render_idle_us", "input/coroutine/idle-loop work, not rendering"),
)


def find_csvexport(explicit):
    if explicit:
        return explicit
    for var in ("TRACY_CSVEXPORT_BIN", "TRACY_CSVEXPORT"):
        if os.environ.get(var):
            return os.environ[var]
    found = which("tracy-csvexport")
    if found:
        return found
    repo_root = Path(__file__).resolve().parents[2]
    for p in repo_root.glob("build-*/vcpkg_installed/*/tools/tracy/tracy-csvexport"):
        return str(p)
    return None


def load_tier1(path, start_ms, end_ms):
    rows = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            ts = int(row["timestamp_ms"])
            if start_ms <= ts <= end_ms:
                for k in TIER1_INT_FIELDS:
                    row[k] = int(row[k])
                row["timestamp_ms"] = ts
                rows.append(row)
    return rows


def load_manifest(capture_dir):
    """Rotating captures, as (start_ms, end_ms, path) with the file still present."""
    manifest = Path(capture_dir) / "manifest.tsv"
    if not manifest.exists():
        return []
    entries = []
    with open(manifest) as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 3:
                continue
            start, end, path = parts
            if Path(path).exists():
                entries.append((int(start), int(end), Path(path)))
    entries.sort()
    return entries


def pick_capture(entries, ts_ms):
    """The window containing ts_ms, else the nearest one (rotation gaps happen)."""
    for start, end, path in entries:
        if start <= ts_ms <= end:
            return start, end, path, True
    if not entries:
        return None
    start, end, path = min(
        entries, key=lambda e: min(abs(e[0] - ts_ms), abs(e[1] - ts_ms)))
    return start, end, path, False


def mem_available_bytes():
    with open("/proc/meminfo") as f:
        for line in f:
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) * 1024
    return None


# .tracy files are zstd-compressed; tracy-csvexport rehydrates the whole trace
# into RAM before it will answer anything. A measured capture here saved at a
# 12% ratio, so the in-RAM cost is roughly 8x the file on disk.
TRACE_EXPANSION = 8


def check_analysis_headroom(capture_path, force):
    """Refuse to expand a trace we don't have the RAM for.

    Analysing a capture costs about as much memory as recording it did. Running
    csvexport on an oversized file is a good way to freeze the desktop while
    investigating a freeze, so this is a hard stop rather than a warning.
    """
    est = capture_path.stat().st_size * TRACE_EXPANSION
    avail = mem_available_bytes()
    if avail is None or est < avail * 0.7 or force:
        return
    print(f"analyze: {capture_path.name} is {capture_path.stat().st_size / 1e6:.0f}MB "
          f"on disk, needing roughly {est / 1e9:.1f}GB in RAM to analyse, but only "
          f"{avail / 1e9:.1f}GB is available.", file=sys.stderr)
    print("  Refusing -- expanding it would likely push the machine into swap.\n"
          "  Shorter rotation windows (VAYU_TRACY_ROTATE) keep captures analysable.\n"
          "  Override with --force-big if you really want to try.", file=sys.stderr)
    sys.exit(1)


def scan_zones(csvexport, capture_path, top, lo, hi):
    """Stream zone occurrences, keeping only the top-N by self-time.

    Deliberately streaming with bounded heaps rather than collecting rows into
    a list: a real capture off this codebase held 196 million zones, and
    materialising those would consume tens of GB -- recreating the very
    out-of-memory freeze this tooling exists to diagnose. Memory here stays
    O(top) no matter how large the capture.

    One pass fills two heaps -- inside the window, and across the whole file --
    so the fallback path doesn't require re-reading hundreds of MB.
    """
    # -u gives one row per occurrence with ns_since_start (the aggregate default
    # would bury a single multi-second stall in a mean); -e gives self-time, so
    # a slow parent doesn't mask which child actually cost the time.
    proc = subprocess.Popen([csvexport, "-u", "-e", str(capture_path)],
                            stdout=subprocess.PIPE, text=True)
    windowed, overall, total, malformed = [], [], 0, 0
    try:
        for row in csv.DictReader(proc.stdout):
            try:
                item = (int(row["exec_time_ns"]), row["name"],
                        row["thread"], int(row["ns_since_start"]))
            except (KeyError, ValueError, TypeError):
                # TypeError means DictReader saw a short row and filled the
                # missing fields with None -- zone names in this codebase
                # contain commas and quotes, so a name that doesn't round-trip
                # through CSV quoting shifts the whole row. Skip those rather
                # than abort the analysis over a handful of unparseable names.
                malformed += 1
                continue
            total += 1
            for heap, keep in ((overall, True), (windowed, lo <= item[3] <= hi)):
                if not keep:
                    continue
                if len(heap) < top:
                    heapq.heappush(heap, item)
                elif item[0] > heap[0][0]:
                    heapq.heappushpop(heap, item)
    finally:
        proc.stdout.close()
        rc = proc.wait()
    if rc != 0:
        raise RuntimeError(f"tracy-csvexport exited with status {rc}")
    if malformed:
        print(f"analyze: skipped {malformed:,} unparseable rows", file=sys.stderr)
    return sorted(windowed, reverse=True), sorted(overall, reverse=True), total


def report_zones(zones, label):
    print(f"\nanalyze: top {len(zones)} zones by self-time {label}:")
    for exec_ns, name, thread, _ns in zones:
        print(f"  {exec_ns / 1000:>12.1f}us  thread={thread:<8} {name}")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--minutes", type=float, default=5,
                    help="how far back to look (default: 5)")
    ap.add_argument("--threshold-us", type=int, default=33333,
                    help="frame time counted as an outlier (default: ~30fps)")
    ap.add_argument("--top", type=int, default=10)
    ap.add_argument("--tier1", default=str(TIER1_DEFAULT))
    ap.add_argument("--capture", default=None,
                    help="explicit .tracy file (bypasses manifest lookup)")
    ap.add_argument("--capture-start-ms", type=int, default=None,
                    help="required with --capture, to map wall-clock into the capture")
    ap.add_argument("--csvexport", default=None)
    # Generous by default: the manifest records when the capture process was
    # launched, but its clock starts at connect, a little later. A stutter worth
    # chasing lasts far longer than that skew.
    ap.add_argument("--pad-ms", type=int, default=10000,
                    help="window padding around the worst frame (default: 10000ms)")
    ap.add_argument("--force-big", action="store_true",
                    help="analyse a capture even if it may not fit in RAM")
    args = ap.parse_args()

    now_ms = int(time.time() * 1000)
    start_ms = now_ms - int(args.minutes * 60 * 1000)

    tier1_path = Path(args.tier1)
    if not tier1_path.exists():
        print(f"analyze: no tier-1 log at {tier1_path} -- is VayuPerfFrameLog enabled?",
              file=sys.stderr)
        sys.exit(1)

    rows = load_tier1(tier1_path, start_ms, now_ms)
    if not rows:
        print(f"analyze: no frames logged in the last {args.minutes:g} minute(s). "
              "Is the viewer running with VayuPerfFrameLog on?", file=sys.stderr)
        sys.exit(1)

    outliers = sorted((r for r in rows if r["doframe_us"] >= args.threshold_us),
                      key=lambda r: -r["doframe_us"])

    fps = 1_000_000 / args.threshold_us
    print(f"analyze: {len(rows)} frames in the last {args.minutes:g} min, "
          f"{len(outliers)} at/above {args.threshold_us}us (~{fps:.0f}fps)")

    if not outliers:
        print("analyze: no frame-time outliers in this window at the tier-1 level.")
        return

    for r in outliers[:args.top]:
        print(f"  frame {r['frame']:>8}  t={r['timestamp_ms']}  doframe={r['doframe_us']:>8}us  "
              f"idle={r['render_idle_us']:>7} sleep={r['render_sleep_us']:>7} "
              f"display={r['render_display_us']:>8} huds={r['render_huds_us']:>5} "
              f"ui={r['render_ui_us']:>6} swap={r['render_swap_us']:>8}")

    worst = outliers[0]
    worst_ts = worst["timestamp_ms"]

    # Attribute to the innermost phase that explains most of the frame.
    half = worst["doframe_us"] * 0.5
    culprit = next(((k, worst[k], why) for k, why in PHASE_ORDER if worst[k] > half), None)
    if culprit:
        name, val, why = culprit
        print(f"\nanalyze: worst frame dominated by {name} ({val}us) -- {why}")
    else:
        biggest = max(PHASE_ORDER, key=lambda kv: worst[kv[0]])[0]
        print(f"\nanalyze: no single phase accounts for the worst frame "
              f"({worst['doframe_us']}us total, largest phase {biggest}={worst[biggest]}us). "
              "Time is going somewhere outside the RecordSceneTime scopes "
              "(mesh repo, texture decode, asset I/O -- or system-wide memory pressure).")

    # --- tier 2: the matching Tracy capture -------------------------------
    if args.capture:
        capture_path = Path(args.capture)
        capture_start_ms = args.capture_start_ms
        exact = True
        if capture_start_ms is None:
            print("\nanalyze: --capture requires --capture-start-ms.", file=sys.stderr)
            sys.exit(1)
    else:
        picked = pick_capture(load_manifest(CAPTURE_DIR_DEFAULT), worst_ts)
        if picked is None:
            print("\nanalyze: no Tracy captures recorded -- run "
                  "scripts/perf/capture_start.sh next time for zone-level detail.")
            return
        capture_start_ms, _cap_end, capture_path, exact = picked
        if not exact:
            print(f"\nanalyze: no capture window covers that moment "
                  f"(rotation gap); using nearest, {capture_path.name}.")

    csvexport = find_csvexport(args.csvexport)
    if not csvexport:
        print("\nanalyze: found a capture but no tracy-csvexport -- set "
              "TRACY_CSVEXPORT_BIN, put it on PATH, or build with USE_TRACY_TOOLS.",
              file=sys.stderr)
        sys.exit(1)

    check_analysis_headroom(capture_path, args.force_big)

    print(f"analyze: reading {capture_path.name} "
          f"({capture_path.stat().st_size / 1e6:.0f}MB)...")

    lo = (worst_ts - args.pad_ms - capture_start_ms) * 1_000_000
    hi = (worst_ts + args.pad_ms - capture_start_ms) * 1_000_000

    try:
        windowed, overall, total = scan_zones(
            csvexport, capture_path, args.top, lo, hi)
    except RuntimeError as e:
        print(f"analyze: {e}", file=sys.stderr)
        sys.exit(1)

    if not total:
        print("analyze: capture contains no zone data (did the viewer connect?).")
        return

    print(f"analyze: {total:,} zone occurrences in this capture")

    if windowed:
        report_zones(windowed, f"within +/-{args.pad_ms}ms of the worst frame")
    else:
        # Clock alignment is approximate; a multi-second stall still dominates
        # the file by self-time, so fall back to ranking the whole capture.
        print(f"\nanalyze: nothing landed in the +/-{args.pad_ms}ms window "
              "(clock skew) -- ranking the whole capture instead.")
        report_zones(overall, f"across all of {capture_path.name}")


if __name__ == "__main__":
    main()
