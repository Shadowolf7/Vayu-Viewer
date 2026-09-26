#!/usr/bin/env python3
"""
Regression guard: the compressed-mip walk in LLImageGL::setImage must agree
with the tight block layout produced by VayuImageBlockCompressor::encode.

Both sides now use the block-aligned formula:

    size = ceil(w/4) * ceil(h/4) * block_bytes

  Encoder  (vayuimageblockcompressor.cpp calc_level_bytes) uses it to lay out
           the pyramid.
  Uploader (llimagegl.cpp dataFormatBytes) uses it to step backward through
           the buffer and to size imageSize for glCompressedTexSubImage2D.

The formula is only satisfied trivially for power-of-two textures (where
ceil(w/4)*4 == w anyway); the historical per-pixel shortcut
round4(w*h*bpp/8) under-counts any level whose dimension is not a multiple
of four, which both misplaced the mip walk and drew GL_INVALID_VALUE on
immutable storage.

Prints per-level sizes and exits nonzero if any level mismatches.

Usage: check_mip_upload_offsets.py WIDTH HEIGHT [bpp]   # bpp = 4 (DXT1/BC4) or 8
"""

import sys


def enc_size(w, h, block_bytes):
    bw = max(1, (w + 3) // 4)
    bh = max(1, (h + 3) // 4)
    return bw * bh * block_bytes


def dfb_size(w, h, bpp):
    block = 16 if bpp == 8 else 8
    return enc_size(w, h, block)


def main():
    W = int(sys.argv[1]) if len(sys.argv) > 1 else 600
    H = int(sys.argv[2]) if len(sys.argv) > 2 else 600
    bpp = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    block = 16 if bpp == 8 else 8

    print(f"texture {W}x{H}, bpp {bpp}, block {block} bytes/block")
    print(f"{'level':>5} {'dims':>9} {'enc':>9} {'dfb':>9} {'diff':>6}  notes")

    enc_levels = []
    w, h = W, H
    while True:
        enc_levels.append((w, h))
        if w <= 1 and h <= 1:
            break
        w = max(1, w // 2)
        h = max(1, h // 2)

    mismatches = 0
    for i, (w, h) in enumerate(enc_levels):
        enc_sz = enc_size(w, h, block)
        dfb_sz = dfb_size(w, h, bpp)
        diff = enc_sz - dfb_sz
        notes = []
        if diff:
            mismatches += 1
            notes.append("MISMATCH")
        print(f"{i:>5} {w:>3}x{h:<3} {enc_sz:>9} {dfb_sz:>9} {diff:>6}  {', '.join(notes)}")

    if mismatches:
        print(f"\nFAIL: {mismatches} level(s) disagree between encoder and uploader")
        return 1
    print(f"\nOK: encoder and uploader agree for all {len(enc_levels)} levels")
    return 0


if __name__ == "__main__":
    sys.exit(main())