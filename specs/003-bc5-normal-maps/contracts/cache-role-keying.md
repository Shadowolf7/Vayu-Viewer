# Contract: Cache File Naming by Role

**Status**: Design contract | **Owner**: `VayuBCTextureCache`

## Purpose

Both roles of a texture UUID must coexist on disk without a format-version bump. The key is `(UUID × format/role)` encoded via file extension (`.bc` vs `.bc5`).

## Filename Grammar

```
<cache_dir>/<subdir>/<uuid><ext>
```

- `cache_dir` = texture cache root (e.g. `~/.vayu/cache/bccache/`)
- `subdir` = first hex char of `<uuid>` (existing partition scheme)
- `ext` = `.bc` for color entries; `.bc5` for BC5 normal-role entries
- **Note on Discard Suffix**: Mip levels are stored in reverse order (coarsest $1 \times 1$ mip at offset 0, full resolution at the end). A single file holds the entire mip chain, and coarser discard requests are satisfied by reading the front slice directly. The legacy `_<discard>` suffix in `getFilePath(id, discard_level)` is strictly a backward-compatible read fallback for older cache files.

## Rules

1. Existing color entries keep their exact current path (`<uuid>.bc`) — **byte-identical to today** (SC-004).
2. Normal-role entries use the `.bc5` file extension (`<uuid>.bc5`) so they never collide with color entries for the same UUID.
3. `getFilePath()` gains a role/format extension parameter (`ext = ".bc"`); normal-role calls pass `".bc5"`.
4. `kFormatVersion` stays `4`.
5. **Legacy informative-alpha fallback**: a legacy normal whose encode-time alpha sweep finds any texel < 255 is NOT BC5 — it is written as a *color* entry under the plain `<uuid>.bc` name (it genuinely is a color-format entry, BC7/BC3 with alpha). The `.bc5` extension is reserved for genuine BC5 output, so the filename never lies about the header format.
6. **Normal-role read probes two keys**: try `<uuid>.bc5` first; on miss, try `<uuid>.bc`. A `<uuid>.bc` hit is accepted only if its header `mFormat` is a color format (it encodes a previous informative-alpha verdict for this UUID). Deterministic: the alpha property is stable per source texture, so the first encode settles which key the UUID lives under.

## Verification

- After encode, `bccache/<subdir>/<uuid>.bc5` exists for a bound normal; `<uuid>.bc` still exists for the same UUID if it was previously compressed as color.
- Tests in `vayubctexturecache_test.cpp` cover the two-role coexistence (no overwrite, both readable).