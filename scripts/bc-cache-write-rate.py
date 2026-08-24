#!/usr/bin/env python3
"""Measure the BC texture cache's real write rate from on-disk file mtimes.

Why this exists: VayuBCTextureCache queues every encoded mip chain into an
in-memory list (mPendingWrites) that holds the *full serialized bytes* until a
single writer thread flushes it to disk. There is no cap and no backpressure,
so if the decode threads produce faster than that one thread can write, the
backlog is pure RAM growth. This script measures the actual producer rate
during a session so that "can the backlog plausibly reach N GB?" is answered
with numbers instead of a guess.

Usage:
    scripts/bc-cache-write-rate.py [--cache-dir DIR] [--bucket SECONDS]
                                   [--top N] [--since ISO8601] [--until ISO8601]

Reads nothing but mtime+size, so it is safe to run against a live cache.
Note mtime reflects flush time (consumer), not enqueue time (producer) - it is
a *lower* bound on the production rate, which is the conservative direction for
the question being asked.
"""

import argparse
import datetime as dt
import os
import sys
from collections import defaultdict


def parse_args():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--cache-dir",
                   default=os.path.expanduser("~/.vayu/cache/bccache"),
                   help="BC cache directory (default: %(default)s)")
    p.add_argument("--bucket", type=float, default=1.0,
                   help="bucket width in seconds (default: %(default)s)")
    p.add_argument("--top", type=int, default=15,
                   help="how many peak buckets to list (default: %(default)s)")
    p.add_argument("--since", default=None,
                   help="only files modified at/after this ISO8601 local time")
    p.add_argument("--until", default=None,
                   help="only files modified at/before this ISO8601 local time")
    return p.parse_args()


def iso_to_epoch(s):
    if s is None:
        return None
    return dt.datetime.fromisoformat(s).timestamp()


def collect(cache_dir, since, until):
    """Return [(mtime, size), ...] for every regular file in cache_dir."""
    out = []
    with os.scandir(cache_dir) as it:
        for entry in it:
            try:
                if not entry.is_file(follow_symlinks=False):
                    continue
                st = entry.stat(follow_symlinks=False)
            except OSError:
                continue
            if since is not None and st.st_mtime < since:
                continue
            if until is not None and st.st_mtime > until:
                continue
            out.append((st.st_mtime, st.st_size))
    return out


def human(n):
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if abs(n) < 1024.0:
            return f"{n:,.1f} {unit}"
        n /= 1024.0
    return f"{n:,.1f} PB"


def main():
    args = parse_args()
    if not os.path.isdir(args.cache_dir):
        sys.exit(f"not a directory: {args.cache_dir}")

    files = collect(args.cache_dir, iso_to_epoch(args.since), iso_to_epoch(args.until))
    if not files:
        sys.exit("no files matched")

    files.sort()
    total_bytes = sum(sz for _, sz in files)
    span = files[-1][0] - files[0][0]

    print(f"cache dir     : {args.cache_dir}")
    print(f"files         : {len(files):,}")
    print(f"total bytes   : {human(total_bytes)}")
    print(f"mtime span    : {human_span(span)} "
          f"({ts(files[0][0])} -> {ts(files[-1][0])})")
    if span > 0:
        print(f"mean rate     : {human(total_bytes / span)}/s, "
              f"{len(files) / span:,.1f} files/s")
    print()

    # Bucket by mtime to find burst peaks - the steady mean hides them.
    buckets_bytes = defaultdict(int)
    buckets_count = defaultdict(int)
    origin = files[0][0]
    for mtime, size in files:
        b = int((mtime - origin) // args.bucket)
        buckets_bytes[b] += size
        buckets_count[b] += 1

    by_bytes = sorted(buckets_bytes.items(), key=lambda kv: kv[1], reverse=True)

    print(f"peak {args.top} buckets of {args.bucket}s (by bytes written):")
    print(f"  {'when':<21} {'files':>7} {'bytes':>12} {'rate':>12}")
    for b, nbytes in by_bytes[:args.top]:
        when = ts(origin + b * args.bucket)
        print(f"  {when:<21} {buckets_count[b]:>7,} {human(nbytes):>12} "
              f"{human(nbytes / args.bucket):>10}/s")
    print()

    # The number that matters for the unbounded-queue question: if the writer
    # thread stalls for T seconds while production continues at the observed
    # peak rate, the in-RAM backlog is peak_rate * T.
    if by_bytes:
        peak_rate = by_bytes[0][1] / args.bucket
        print("projected in-RAM backlog if the single writer thread stalls,")
        print(f"with production continuing at the observed peak "
              f"({human(peak_rate)}/s):")
        for stall in (1, 5, 15, 30, 60, 120):
            print(f"  {stall:>4}s stall -> {human(peak_rate * stall)}")


def human_span(sec):
    if sec < 60:
        return f"{sec:.1f}s"
    if sec < 3600:
        return f"{sec / 60:.1f}m"
    return f"{sec / 3600:.2f}h"


def ts(epoch):
    return dt.datetime.fromtimestamp(epoch).strftime("%Y-%m-%d %H:%M:%S")


if __name__ == "__main__":
    main()
