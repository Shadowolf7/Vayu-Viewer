#!/usr/bin/env python3
"""
Analyze VAYU_DUMP_DIR captures from VayuImageBlockCompressor::encode.

For each captured texture this:
  1. Reconstructs the mip pyramid exactly as the GL upload walk reads it
     (block-aligned offsets, the same as dataFormatBytes now computes).
  2. Decodes every compressed level back to RGBA (BC1/BC4/BC5; BC7 is skipped
     with a placeholder) and measures decode error against the raw mip.
  3. Re-derives each raw mip[i+1] from raw mip[i] using a faithful model of
     the C++ downsampler (linear sRGB for srgb 3/4ch, plain 2x2 box for the
     rest) and reports any divergence -- this is what catches a broken mip
     *content* (e.g. the packed-RG ScalePlane_16 path) independent of encode.

Writes a PPM contact sheet per level: raw | decoded.

Usage: analyze_vayu_dump.py <dump_dir> [--png]
"""

import glob
import os
import sys

import numpy as np

FMT = {1: "BC1", 2: "BC3", 3: "BC4", 4: "BC5", 5: "BC7"}

SRGB_TO_LINEAR = np.array([(i / 255.0) ** 2.2 if False else 0 for i in range(256)], dtype=np.float32)


def build_srgb_to_linear():
    t = np.empty(256, dtype=np.float32)
    for i in range(256):
        v = i / 255.0
        t[i] = v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return t


def build_linear_to_srgb():
    t = np.empty(256, dtype=np.float32)
    for i in range(256):
        v = i / 255.0
        t[i] = 12.92 * v if v <= 0.0031308 else 1.055 * (v ** (1.0 / 2.4)) - 0.055
    return t


def build_srgb_to_srgb_u8():
    f = build_linear_to_srgb()
    return (f * 255.0 + 0.5).astype(np.uint8)


def rgb565(c):
    r = (c >> 11) & 0x1F
    g = (c >> 5) & 0x3F
    b = c & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def decode_bc1(data, w, h):
    bw, bh = (w + 3) // 4, (h + 3) // 4
    out = np.zeros((h, w, 4), dtype=np.uint8)
    for by in range(bh):
        for bx in range(bw):
            off = (by * bw + bx) * 8
            b = data[off:off + 8]
            c0, c1 = b[0] | (b[1] << 8), b[2] | (b[3] << 8)
            c0r, c0g, c0b = rgb565(c0)
            c1r, c1g, c1b = rgb565(c1)
            idx = int.from_bytes(b[4:8], "little")
            for py in range(4):
                for px in range(4):
                    i = (idx >> (2 * (py * 4 + px))) & 3
                    if c0 > c1:
                        pal = {
                            0: (c0r, c0g, c0b, 255), 1: (c1r, c1g, c1b, 255),
                            2: ((2 * c0r + c1r) // 3, (2 * c0g + c1g) // 3, (2 * c0b + c1b) // 3, 255),
                            3: ((c0r + 2 * c1r) // 3, (c0g + 2 * c1g) // 3, (c0b + 2 * c1b) // 3, 255),
                        }[i]
                    else:
                        if i == 3:
                            pal = (0, 0, 0, 0)
                        else:
                            pal = {
                                0: (c0r, c0g, c0b, 255), 1: (c1r, c1g, c1b, 255),
                                2: ((c0r + c1r) // 2, (c0g + c1g) // 2, (c0b + c1b) // 2, 255),
                            }[i]
                    yy, xx = by * 4 + py, bx * 4 + px
                    if yy < h and xx < w:
                        out[yy, xx] = pal
    return out[:, :, :3], out[:, :, 3]


_BT = np.array([
    0x0, 0x0, 0x1, 0x1, 0x2, 0x2, 0x3, 0x3,
    0x4, 0x4, 0x5, 0x5, 0x6, 0x6, 0x7, 0x7,
], dtype=np.uint8)


def decode_bc5(data, w, h):
    """BC5: per block, 8 bytes of BC4 for channel 0 then 8 bytes for channel 1."""
    bw, bh = (w + 3) // 4, (h + 3) // 4
    nblk = bw * bh
    r = np.zeros((h, w), dtype=np.float32)
    g = np.zeros((h, w), dtype=np.float32)
    for i in range(nblk):
        blk0 = decode_bc4_block(data[i * 16:i * 16 + 8])
        blk1 = decode_bc4_block(data[i * 16 + 8:i * 16 + 16])
        by, bx = divmod(i, bw)
        for py in range(4):
            for px in range(4):
                yy, xx = by * 4 + py, bx * 4 + px
                if yy < h and xx < w:
                    r[yy, xx] = blk0[py][px]
                    g[yy, xx] = blk1[py][px]
    out = np.zeros((h, w, 4), dtype=np.uint8)
    out[:, :, 0] = r.astype(np.uint8)
    out[:, :, 1] = g.astype(np.uint8)
    out[:, :, 3] = 255
    return out


def decode_bc4_block(data):
    """Decode one 8-byte BC4 block -> 4x4 list."""
    r0, r1 = data[0], data[1]
    bits = int.from_bytes(data[2:8], "little")
    if r0 > r1:
        pal = [r0, r1] + [((7 - i) * r0 + i * r1) // 7 for i in range(1, 7)]
    else:
        pal = [r0, r1] + [((5 - i) * r0 + i * r1) // 5 for i in range(1, 5)] + [0, 255]
    out = [[0] * 4 for _ in range(4)]
    for py in range(4):
        for px in range(4):
            i = (bits >> (3 * (py * 4 + px))) & 7
            out[py][px] = pal[int(i)]
    return out


def decode_bc45_plane(data, w, h):
    """Single RGTC1 (BC4) plane -> HxW uint8."""
    bw, bh = (w + 3) // 4, (h + 3) // 4
    out = np.zeros((h, w), dtype=np.float32)
    for by in range(bh):
        for bx in range(bw):
            blk = decode_bc4_block(data[(by * bw + bx) * 8:(by * bw + bx) * 8 + 8])
            for py in range(4):
                for px in range(4):
                    yy, xx = by * 4 + py, bx * 4 + px
                    if yy < h and xx < w:
                        out[yy, xx] = blk[py][px]
    return out.astype(np.uint8)


def decode(data, fmt, w, h, chunks):
    if fmt == 4:  # BC5 = two BC4 planes
        return decode_bc5(data, w, h)
    if fmt == 3:  # BC4
        r = decode_bc45_plane(data, w, h)
        img = np.zeros((h, w, 4), dtype=np.uint8)
        img[:, :, 0] = r; img[:, :, 1] = r; img[:, :, 2] = r; img[:, :, 3] = 255
        return img
    if fmt == 1:  # BC1
        rgb, a = decode_bc1(data, w, h)
        img = np.zeros((h, w, 4), dtype=np.uint8)
        img[:, :, :3] = rgb
        img[:, :, 3] = a
        return img
    img = np.full((h, w, 4), 128, dtype=np.uint8)  # BC7: no decoder in-tree
    return img


def downsample_model(raw, w, h, nch, srgb):
    dw, dh = max(1, w // 2), max(1, h // 2)
    src = raw.astype(np.float32).reshape(h, w, nch)
    sy0 = np.arange(dh) * 2
    sx0 = np.arange(dw) * 2
    sy1 = np.minimum(np.arange(dh) * 2 + 1, h - 1)
    sx1 = np.minimum(np.arange(dw) * 2 + 1, w - 1)
    if srgb and nch >= 3:
        linear = build_srgb_to_linear()[src.astype(np.int16)]
        lin4 = np.zeros((h, w, 4), dtype=np.float32)
        lin4[:, :, :nch] = linear
        if nch == 3:
            lin4[:, :, 3] = 255.0
        a = lin4[sy0][:, sx0]
        b = lin4[sy0][:, sx1]
        c = lin4[sy1][:, sx0]
        d = lin4[sy1][:, sx1]
        avg = (a + b + c + d) * 0.25
        out = np.empty((dh, dw, 4), dtype=np.uint8)
        for ch_ in range(3):
            out[:, :, ch_] = build_srgb_to_srgb_u8()[(avg[:, :, ch_] * 255.0 + 0.5).astype(np.int64).clip(0, 255)]
        alpha = 255
        if nch == 4:
            alpha = (src[sy0][:, sx0][:, :, 3].astype(np.int32) +
                     src[sy0][:, sx1][:, :, 3].astype(np.int32) +
                     src[sy1][:, sx0][:, :, 3].astype(np.int32) +
                     src[sy1][:, sx1][:, :, 3].astype(np.int32) + 2) >> 2
        out[:, :, 3] = alpha
        return out[:, :, :nch]
    a = src[sy0][:, sx0]
    b = src[sy0][:, sx1]
    c = src[sy1][:, sx0]
    d = src[sy1][:, sx1]
    return ((a + b + c + d) / 4.0 + 0.5).astype(np.uint8)


def err(a, b):
    a = a.astype(np.int16); b = b.astype(np.int16)
    return float(np.abs(a - b).mean()), int(np.abs(a - b).max())


def write_ppm(path, img):
    img = img[:, :, :3]
    h, w, _ = img.shape
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(img.tobytes())


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    use_png = "--png" in sys.argv
    if not args:
        print(f"usage: {sys.argv[0]} <dump_dir> [--png]")
        return 1
    d = args[0]
    metas = sorted(glob.glob(os.path.join(d, "*.meta")))
    if not metas:
        print(f"no captures in {d}")
        return 1

    if use_png:
        try:
            from PIL import Image
        except Exception:
            use_png = False
            print("Pillow unavailable; writing PPM")

    for meta_path in metas:
        base = meta_path[:-5]  # strip .meta
        with open(meta_path) as f:
            lines = [l.split() for l in f.read().splitlines()]
        hdr = lines[0]
        w, h, nch, job, fmt_s, srgb, nmips = [int(x) for x in hdr[:7]]

        fmt = int(fmt_s)
        print(f"\n=== {os.path.basename(base)}  {w}x{h} c{nch} job{job} {FMT.get(fmt, '?')} "
              f"{'sRGB' if srgb else 'linear'} m{nmips}")

        rows = lines[1:]
        comp = open(base + "_compressed.bin", "rb").read()

        mips_raw = []
        for i, r in enumerate(rows):
            lvl, sz, csz, off = (int(r[0]), int(r[1]), int(r[2]), int(r[3]))
            lw, lh = max(1, w >> i), max(1, h >> i)
            raw_img = np.frombuffer(open(f"{base}_rawmip{i}.bin", "rb").read(),
                                    dtype=np.uint8).reshape(lh, lw, nch)
            mips_raw.append(raw_img)
            block = 16 if fmt in (2, 4, 5) else 8
            comp_bytes = comp[off:off + csz]
            dec = decode(comp_bytes, fmt, lw, lh, nch)
            raw_rgb = np.zeros((lh, lw, 4), dtype=np.uint8)
            if nch == 4:
                raw_rgb = raw_img
            elif nch == 3:
                raw_rgb[:, :, :3] = raw_img
                raw_rgb[:, :, 3] = 255
            elif nch == 2:
                raw_rgb[:, :, :2] = raw_img
                raw_rgb[:, :, 3] = 255
            else:
                raw_rgb[:, :, 0] = raw_img[:, :, 0]
                raw_rgb[:, :, 1] = raw_img[:, :, 0]
                raw_rgb[:, :, 2] = raw_img[:, :, 0]
                raw_rgb[:, :, 3] = 255

            if fmt == 5:
                dmae = dmax = float("nan")
            elif fmt == 3:
                dmae, dmax = err(dec[:, :, 0], raw_img[:, :, 0])
            elif fmt == 4:
                dmae, dmax = err(dec[:, :, :2], raw_img[:, :, :2])
            else:
                dmae, dmax = err(dec[:, :, :3], raw_rgb[:, :, :3])
            print(f"  level {i:2d} {lw:4d}x{lh:<4d} decoded-vs-raw MAE={dmae:6.2f} max={dmax if dmax != float('nan') else '-'}")

            combined = np.concatenate([raw_rgb, dec], axis=1)
            if use_png:
                Image.fromarray(combined).save(f"{base}_cmp{i}.png")
            else:
                write_ppm(f"{base}_cmp{i}.ppm", combined)

        if nmips > 1:
            print("  downsample-model check (raw[i+1] vs box(raw[i])):")
            for i in range(nmips - 1):
                lw, lh = max(1, w >> i), max(1, h >> i)
                model = downsample_model(mips_raw[i], lw, lh, nch, bool(srgb))
                actual = mips_raw[i + 1]
                mae, mx = err(model, actual)
                flag = "  <-- DIVERGES" if mae > 1.5 else ""
                print(f"    {i}->{i + 1}: MAE={mae:6.2f} max={mx}{flag}")

    return 0


if __name__ == "__main__":
    sys.exit(main())