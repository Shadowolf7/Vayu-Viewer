#!/usr/bin/env python3
"""Scan a VAYU_DUMP_DIR session for role mis-tags and normal-map grid artifacts.

For every texture dumped, classify the *content* of rawmip0 (the uncompressed
encoder input) independently of the job it was tagged with:

  normal-like : R,G near 128 (tangent XY) and B high (Z)
  color-like  : everything else

Cross-tabulating content-class against the recorded job reveals mis-tags:
a normal-like texture tagged Albedo (job0) would render as iridescent
"oil slick"; a color-like texture tagged as a normal (job1/job2) renders as
dark/smeared patches. Also scores each normal-tagged source for a periodic
(dimple) grid via autocorrelation peaks at small lags.

Usage: scan_vayu_dump_roles.py <session-dir>
"""
import sys, os, glob
import numpy as np

NORMAL_R_TOL = 30     # |meanR-128| within this
NORMAL_G_TOL = 30
NORMAL_B_MIN = 180    # meanB above this
LAGS = (2, 3, 4, 5, 6, 7, 8, 12, 16)

def stats_and_grid(path, w, h, c):
    try:
        raw = open(path, "rb").read()
    except OSError:
        return None
    if len(raw) < w * h * c or w < 3 or h < 3:
        return None
    arr = np.frombuffer(raw, dtype=np.uint8, count=w * h * c).reshape(h, w, c).astype(np.float32)
    meanR = float(arr[..., 0].mean())
    meanG = float(arr[..., 1].mean()) if c >= 2 else 0.0
    meanB = float(arr[..., 2].mean()) if c >= 3 else 0.0
    meanA = float(arr[..., 3].mean()) if c >= 4 else None
    # grid score: strongest positive ACF (lag>=2) on R, normalised by variance
    colR = arr[..., 0]
    m = meanR
    var = float(((colR - m) ** 2).mean())
    grid = 0.0
    if var > 0:
        for o in LAGS:
            if w - o < 2:
                continue
            a = colR[:, :-o] - m
            b = colR[:, o:] - m
            grid = max(grid, float((a * b).mean()) / var)
    return dict(meanR=meanR, meanG=meanG, meanB=meanB, meanA=meanA, grid=grid, n=w * h)

def main():
    d = sys.argv[1]
    rows = []
    for meta in sorted(glob.glob(os.path.join(d, "*.meta"))):
        parts = open(meta).read().split("\n")[0].split()
        if len(parts) < 7:
            continue
        w, h, c, job, fmt, srgb, mips = map(int, parts[:7])
        base = meta[:-5]
        st = stats_and_grid(base + "_rawmip0.bin", w, h, c)
        if not st:
            continue
        normal_like = (abs(st['meanR'] - 128) < NORMAL_R_TOL and
                       abs(st['meanG'] - 128) < NORMAL_G_TOL and
                       st['meanB'] > NORMAL_B_MIN)
        rows.append((os.path.basename(base), w, h, c, job, fmt, srgb, normal_like, st))

    def cls(r):
        return "NORMAL-like" if r[7] else "color-like"

    print(f"scanned {len(rows)} textures")
    print("\n=== content-class x job cross-tab ===")
    tab = {}
    for r in rows:
        k = (r[4], r[5], r[3], cls(r))
        tab[k] = tab.get(k, 0) + 1
    for k in sorted(tab):
        job, fmt, c, cl = k
        print(f"  job{job} fmt{fmt} c{c} {cl:11s}: {tab[k]}")

    print("\n=== MIS-TAGS: normal-like content tagged as Albedo (job0) ===")
    mt = [r for r in rows if r[4] == 0 and r[7]]
    for r in mt[:40]:
        print(f"  {r[0]}  {r[1]}x{r[2]} c{r[3]} job{r[4]} fmt{r[5]}  "
              f"RGB=({r[8]['meanR']:.0f},{r[8]['meanG']:.0f},{r[8]['meanB']:.0f}) grid={r[8]['grid']:+.2f}")
    print(f"  total: {len(mt)}")

    print("\n=== MIS-TAGS: color-like content tagged as a Normal (job1/job2) ===")
    mt2 = [r for r in rows if r[4] in (1, 2) and not r[7]]
    for r in mt2[:40]:
        print(f"  {r[0]}  {r[1]}x{r[2]} c{r[3]} job{r[4]} fmt{r[5]}  "
              f"RGB=({r[8]['meanR']:.0f},{r[8]['meanG']:.0f},{r[8]['meanB']:.0f}) grid={r[8]['grid']:+.2f}")
    print(f"  total: {len(mt2)}")

    print("\n=== normal-tagged sources with a strong periodic grid (grid>=0.25) ===")
    gn = [r for r in rows if r[4] in (1, 2) and r[8]['grid'] >= 0.25]
    gn.sort(key=lambda r: -r[8]['grid'])
    for r in gn[:40]:
        print(f"  {r[0]}  {r[1]}x{r[2]} c{r[3]} job{r[4]} fmt{r[5]}  "
              f"RGB=({r[8]['meanR']:.0f},{r[8]['meanG']:.0f},{r[8]['meanB']:.0f}) grid={r[8]['grid']:+.2f}")
    print(f"  total: {len(gn)}")

if __name__ == "__main__":
    main()