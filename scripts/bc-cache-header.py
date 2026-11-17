#!/usr/bin/env python3
"""Print the Vayu BC cache header for one or more texture UUIDs.

The cache is keyed by UUID with the (format, role) in the filename extension, and
its header records format/preset/mask/role/dimensions -- enough to answer "is this
texture compressed, at what size, and in what GL format" without running the viewer.
The on-screen texture console cannot: it shows a clamped virtual size and the
post-discard size, which cannot be combined back into the source size.

Layout mirrors FileHeader in indra/llimage/vayubctexturecache.cpp:

    U32  magic          0x31434256 "VBC1"
    U32  version
    -- VayuBCCacheEntryHeader --
    U8   format         EVayuBlockCompressionFormat (BC1=1 BC3=2 BC4=3 BC5=4 BC7=5)
    U8   preset
    U8   is_mask
    U8   role           0 = colour, 1 = normal
    U8   discard_level  level the entry was encoded at
    S32  mip_levels
    U32  width
    U32  height
    S32  components
    U32  gl_internal_format
    U32  gl_primary_format
    U64  buffer_size

Usage:
    scripts/bc-cache-header.py 02e9cb5e-cd30-e14f-4feb-4cda3a1bf3ea [...]
    scripts/bc-cache-header.py --all | head
"""

import argparse
import os
import struct
import sys

MAGIC = 0x31434256
HEADER = "<II5Bxxx i II i II Q"
HEADER_SIZE = struct.calcsize(HEADER)
FORMATS = {1: "BC1", 2: "BC3", 3: "BC4", 4: "BC5", 5: "BC7"}
ROLES = {0: "color", 1: "normal"}


def cache_root():
    return os.environ.get("VAYU_CACHE_DIR") or os.path.expanduser("~/.cache/vayu")


def entries(root):
    """Yield (uuid, extension, path) for every entry under the cache root."""
    cachedir = os.path.join(root, "texturecache")
    if not os.path.isdir(cachedir):
        cachedir = os.path.join(root, "bccache")
    if not os.path.isdir(cachedir):
        sys.exit("no texturecache or bccache directory under %s" % root)
    for shard in sorted(os.listdir(cachedir)):
        shard_dir = os.path.join(cachedir, shard)
        if not os.path.isdir(shard_dir):
            continue
        for name in sorted(os.listdir(shard_dir)):
            stem, _, ext = name.partition(".")
            if ext in ("bc1", "bc3", "bc4", "bc5", "bc7"):
                yield stem, ext, os.path.join(shard_dir, name)


def read_header(path):
    with open(path, "rb") as handle:
        raw = handle.read(HEADER_SIZE)
    if len(raw) < HEADER_SIZE:
        return None
    fields = struct.unpack(HEADER, raw)
    if fields[0] != MAGIC:
        return None
    keys = (
        "version", "format", "preset", "is_mask", "role",
        "discard_level", "mip_levels", "width", "height", "components",
        "gl_internal_format", "gl_primary_format", "buffer_size",
    )
    return dict(zip(keys, fields[1:]))


def describe(uuid, ext, path, header):
    name = "%s.%s" % (uuid, ext) if ext else uuid
    if header is None:
        return "%-46s  unreadable (short file or bad magic)" % name
    on_disk = os.path.getsize(path)
    return (
        "%-46s  %-4s %-6s discard=%d mips=%-2d %dx%d comp=%d  "
        "memsz=%-9d file=%-9d gl=0x%x/0x%x mask=%d"
        % (
            name,
            FORMATS.get(header["format"], "BC%d" % header["format"]),
            ROLES.get(header["role"], "role%d" % header["role"]),
            header["discard_level"],
            header["mip_levels"],
            header["width"],
            header["height"],
            header["components"],
            header["buffer_size"],
            on_disk,
            header["gl_internal_format"],
            header["gl_primary_format"],
            header["is_mask"],
        )
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("uuids", nargs="*", help="texture UUIDs to look up")
    parser.add_argument("--all", action="store_true", help="every entry in the cache")
    parser.add_argument("--root", default=None, help="cache root (default ~/.cache/vayu)")
    args = parser.parse_args()

    root = args.root or cache_root()
    if not args.uuids and not args.all:
        parser.error("give at least one UUID, or --all")

    wanted = {u.lower() for u in args.uuids}
    found = set()
    try:
        for uuid, ext, path in entries(root):
            if not args.all and uuid.lower() not in wanted:
                continue
            found.add(uuid.lower())
            print(describe(uuid, ext, path, read_header(path)))
    except BrokenPipeError:
        sys.stderr.close()
        return 0

    for missing in sorted(wanted - found):
        print("%-46s  not in cache" % missing, file=sys.stderr)
    return 1 if wanted - found else 0


if __name__ == "__main__":
    sys.exit(main())
