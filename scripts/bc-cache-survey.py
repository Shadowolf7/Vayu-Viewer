#!/usr/bin/env python3
"""Survey Vayu BC texture cache entries by parsing their on-disk headers.

Why this exists: the BC cache encodes every texture into a compressed block
format; the header (VayuBCCacheEntryHeader) records which format, preset,
component count, dimensions, mip count, and GL upload formats each entry was
actually written with. This survey answers, with numbers instead of guesses:

  * What block formats dominate the cache (BC1/BC3/BC4/BC5/BC7)?
  * What component counts do the encoder inputs actually have (1/2/3/4)?
  * What presets (Slow/Fast) and dimension/mip distributions exist?
  * Which entries preserved alpha (BC3/4-component) vs dropped it (BC1)?
  * How big are the mip0 chains (for sizing option-B raw RGBA VRAM)?

It does NOT answer: whether a BC1-compressed normal map *originally* had
meaningful alpha -- BC1 discards alpha at encode time, so source alpha variance
is not recoverable from the compressed cache. That requires raw dumps
(VAYU_DUMP_DIR captures).

Usage:
    scripts/bc-cache-survey.py [--cache-dir DIR] [--top-uuids N]
                               [--hex-only] [--uuid-hex PREFIX]

Reads only the 44-byte header of each .bc file (never the compressed payload),
so it is safe to run against a live cache.
"""

import argparse
import os
import struct
import sys
from collections import Counter, defaultdict

FILE_HEADER_SIZE = 48  # U32 magic + U32 version + 28-byte meta + 4 pad + U64 size

FMT_NAMES = {1: "BC1", 2: "BC3", 3: "BC4", 4: "BC5", 5: "BC7"}
PRESET_NAMES = {0: "Default", 1: "Slow", 2: "Fast"}  # verify against enum
CHANNEL_NAMES = {1: "1ch", 2: "2ch", 3: "3ch", 4: "4ch/A"}
ROLE_HINT_WRITERS = 0  # replaced by header meta if written; see meta dict


def parse_header(data):
    if len(data) < FILE_HEADER_SIZE:
        return None
    magic, version = struct.unpack_from("<II", data, 0)
    if magic != 0x31434256:
        return None
    # VayuBCCacheEntryHeader, 28 bytes: 4xU8 then 6x U32/S32
    # format, preset, isMask, discard | mips, w, h, components, glInt, glPrim
    meta = struct.unpack_from("<BBBBIiiIII", data, 8)
    format_, preset, is_mask, discard = meta[0:4]
    mips, w, h, comp, gint, gprim = meta[4:10]
    # C++ FileHeader: U32 magic(0-3), U32 version(4-7), meta 28 bytes(8-35),
    # 4 bytes implicit padding to 8-align U64, mBufferSize at 40..47 => 48 total
    buf_size, = struct.unpack_from("<Q", data, 40)
    return {
        "format": format_,
        "preset": preset,
        "is_mask": is_mask,
        "discard": discard,
        "mips": mips,
        "w": w,
        "h": h,
        "components": comp,
        "gl_internal": gint,
        "gl_primary": gprim,
        "buf_size": buf_size,
    }


def hex_dir_entries(cache_dir, hex_only, uuid_hex):
    hits = 0
    for name in sorted(os.listdir(cache_dir)):
        if len(name) != 1 or not name.islower():
            continue
        if not name[0].isdigit() and not ("a" <= name[0] <= "f"):
            continue
        hits += 1
    if hex_only:
        subdirs = [n for n in sorted(os.listdir(cache_dir))
                   if len(n) == 1 and (n.isdigit() or ("a" <= n <= "f"))]
    else:
        subdirs = sorted(os.listdir(cache_dir))
    return subdirs


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--cache-dir", default=os.path.expanduser("~/.vayu/cache/bccache"))
    p.add_argument("--top-uuids", type=int, default=10,
                   help="list N largest mip0 chains (by buffer bytes)")
    p.add_argument("--hex-only", action="store_true",
                   help="only descend the 16 hex partition dirs")
    p.add_argument("--uuid-hex", default=None,
                   help="restrict to entries whose first hex char == this (e.g. '7')")
    args = p.parse_args()

    subdirs = hex_dir_entries(args.cache_dir, args.hex_only, args.uuid_hex)

    fmt_c = Counter()
    preset_c = Counter()
    comp_c = Counter()
    mask_c = Counter()
    dim_c = Counter()
    mip_c = Counter()
    n_files = 0
    n_bad = 0
    total_buf = 0
    largest = []  # (bytes, uuid, meta)

    for sub in subdirs:
        if args.uuid_hex and sub != args.uuid_hex:
            continue
        subpath = os.path.join(args.cache_dir, sub)
        if not os.path.isdir(subpath):
            continue
        for fn in os.listdir(subpath):
            if not fn.endswith(".bc"):
                continue
            n_files += 1
            fp = os.path.join(subpath, fn)
            try:
                with open(fp, "rb") as f:
                    hdr = f.read(FILE_HEADER_SIZE)
            except OSError:
                n_bad += 1
                continue
            m = parse_header(hdr)
            if m is None:
                n_bad += 1
                continue
            fmt_c[m["format"]] += 1
            preset_c[m["preset"]] += 1
            comp_c[m["components"]] += 1
            mask_c[m["is_mask"]] += 1
            dim_c[(m["w"], m["h"])] += 1
            mip_c[m["mips"]] += 1
            total_buf += m["buf_size"]
            uuid = fn.split(".")[0].split("_")[0]
            largest.append((m["buf_size"], uuid, m))

    print(f"scanned {n_files} .bc files, {n_bad} unreadable/corrupt, "
          f"{total_buf/1e6:.1f} MB compressed payload")
    print(f"cache dir: {args.cache_dir}")

    print("\nformats:")
    for k in sorted(fmt_c):
        name = FMT_NAMES.get(k, f"?{k}")
        print(f"  {name:6s} {fmt_c[k]:6d}  ({100*fmt_c[k]/max(1,n_files):5.1f}%)")

    print("\npresets:")
    for k in sorted(preset_c):
        name = PRESET_NAMES.get(k, f"?{k}")
        print(f"  {name:8s} {preset_c[k]:6d}")

    print("\ncomponents:")
    for k in sorted(comp_c):
        name = CHANNEL_NAMES.get(k, f"?{k}")
        print(f"  {name:7s} {comp_c[k]:6d}  ({100*comp_c[k]/max(1,n_files):5.1f}%)")

    print(f"\nalpha-mode masks: {mask_c[1]} alpha-mask, {mask_c[0]} blend/none")

    print("\nby mip count:")
    for k in sorted(mip_c):
        print(f"  {k:4d} mips  {mip_c[k]:6d}")

    print("\nmost common dimensions (w,h):")
    for (w, h), c in dim_c.most_common(8):
        print(f"  {w}x{h}  {c:6d}")

    print(f"\nlargest {args.top_uuids} mip0 chains (compressed bytes):")
    for size, uuid, m in sorted(largest, reverse=True)[:args.top_uuids]:
        print(f"  {size/1e6:8.2f} MB  {uuid}  "
              f"fmt={FMT_NAMES.get(m['format'],'?'+str(m['format']))} "
              f"preset={PRESET_NAMES.get(m['preset'],'?'+str(m['preset']))} "
              f"{m['w']}x{m['h']} {m['components']}ch {m['mips']}mips")


if __name__ == "__main__":
    sys.exit(main())