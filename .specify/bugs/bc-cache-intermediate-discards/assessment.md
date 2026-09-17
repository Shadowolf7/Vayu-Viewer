# Bug Assessment: Redundant Encoding and Unbounded Disk Writes for Intermediate Texture Discard Levels

- **Slug**: bc-cache-intermediate-discards
- **Created**: 2026-09-16
- **Source**: https://github.com/Shadowolf7/Vayu-Viewer/issues/97
- **Verdict**: valid
- **Severity**: high

## URL Trust Policy

- **Supplied URL**: `https://github.com/Shadowolf7/Vayu-Viewer/issues/97`
- **Parsed Host**: `github.com`
- **Policy Branch**: `allowlisted`

## Report (verbatim or summarized)

From [Issue #97](https://github.com/Shadowolf7/Vayu-Viewer/issues/97):
> **Title**: [Bug]: Redundant encoding and unbounded disk writes for intermediate texture discard levels
>
> ### Summary
> When streaming textures over the network, `LLTextureFetch` requests textures progressively in multiple slices (e.g. discard 5, 4, 3, 2, 1, 0). `ImageRequest::processRequest()` runs full OpenJPEG software decode, block compression, and disk writes for every intermediate discard level as packets arrive. During rapid region crossings, this generated **7,693 `.bc` files written in a single minute** (~128 writes/sec), ballooning `~/.vayu/cache/bccache/` by 88,000+ files in 10 minutes and pegging all 6 image worker threads at high CPU.
>
> Furthermore, if a full-resolution entry (`_0.bc`) is already cached on disk, initial low-resolution network requests (e.g. `_5.bc`) miss the cache because cache keys require exact discard level matches, triggering redundant network fetching and decoding.
>
> ### Steps to Reproduce
> 1. Fly or teleport across multiple regions in a short period of time.
> 2. Inspect `~/.vayu/cache/bccache/`: a single UUID has separate files for `_5.bc`, `_4.bc`, `_3.bc`, `_2.bc`, `_1.bc`, and `_0.bc`.
> 3. Check `top`: `ThreadPool:Imag` threads are saturated compressing coarse discard levels that are superseded within hundreds of milliseconds.
>
> ### Root Cause
> In `indra/llimage/vayubctexturecache.cpp:97`:
> ```cpp
> std::string getFilePath(const LLUUID& id, S32 discard_level) const;
> ```
> 1. Every discard level is treated as an independent cacheable entry (`<uuid>_<discard>.bc`).
> 2. `llimageworker.cpp` writes every intermediate decode to disk even when higher-resolution packets are already in flight.
> 3. `VayuBlockCompressionResult` contains a full mipmap chain for the decoded image, meaning higher-resolution cache files already contain the data needed for coarser discard levels.
>
> ### Proposed Fix
> - Avoid writing `.bc` cache entries for coarse intermediate discard levels (e.g. discard >= 3 or 2) when actively streaming higher-resolution textures.
> - Allow lower-resolution read requests to be satisfied from the mip levels of an existing higher-resolution `.bc` cache entry (e.g. use `_0.bc` to satisfy discard 2 or 3).

## Symptom

1. **Unbounded Disk Writes**: When progressively streaming textures over the network, the viewer writes separate `.bc` files for every intermediate discard level (e.g., `<uuid>_5.bc`, `<uuid>_4.bc`, `<uuid>_3.bc`, `<uuid>_2.bc`, `<uuid>_1.bc`, and `<uuid>_0.bc`). During rapid flight or region crossings, write rates exceed 125+ files/sec (7,690+ files/min), accumulating over 88,000 files in 10 minutes and causing extreme SSD write amplification and cache thrashing.
2. **CPU Starvation**: Image worker threads (`ThreadPool:Imag`) stay pegged at ~100% CPU performing redundant block compression encodings on coarse discard levels that are replaced within hundreds of milliseconds by higher-resolution slices.
3. **False Cache Misses on Progressive Loads**: If a texture has already been fetched and fully cached on disk as `<uuid>_0.bc`, subsequent visits or scene loads query `readEntry(uuid, discard=5, ...)` which only searches for `<uuid>_5.bc`. Because `<uuid>_5.bc` does not exist (or was purged), `VayuBCTextureCache` reports a miss:
   - If the raw J2C is present in the legacy `LLTextureCache`, network re-download is avoided, but image worker threads are still forced to perform redundant OpenJPEG CPU software decompressions, redundant block-compression encodings, and write ephemeral `<uuid>_5.bc` files to disk.
   - If the raw J2C is not in `LLTextureCache` (e.g. evicted or on a clean legacy cache while `bccache` is populated), `LLTextureFetch` has no awareness of `VayuBCTextureCache` prior to download and issues redundant HTTP network requests to fetch J2C packets, despite full-resolution compressed texture data already being present locally.

## Reproduction

1. Clear the disk cache or launch with a clean profile.
2. Fly or teleport rapidly across multiple texture-dense regions.
3. Monitor the filesystem in `~/.vayu/cache/bccache/`: observe that a single UUID produces multiple files:
   - `<uuid>_5.bc`
   - `<uuid>_4.bc`
   - `<uuid>_3.bc`
   - `<uuid>_2.bc`
   - `<uuid>_1.bc`
   - `<uuid>_0.bc`
4. Observe `top` or system monitor: all 6–8 image decode worker threads (`ThreadPool:Imag`) stay saturated compressing transient discard levels.
5. In a second session where `<uuid>_0.bc` exists on disk, trigger a new fetch for that texture starting at coarse discard (e.g. discard 5): observe in logs that `readEntry(id, 5)` returns false and triggers OpenJPEG decode and network fetch.

## Suspected Code Paths

- [`indra/llimage/llimageworker.cpp:211-244`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/llimageworker.cpp#L211-L244): `ImageRequest::processRequest()` — Checks `VayuBCTextureCache::instance().readEntry(mID, discard, ...)` using only the exact requested `discard`. Does not check whether higher-resolution entries (e.g. `discard = 0`) already exist.
- [`indra/llimage/llimageworker.cpp:285-318`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/llimageworker.cpp#L285-L318): `ImageRequest::processRequest()` — Unconditionally invokes `VayuBCTextureCache::instance().writeEntry(mID, discard, ...)` for every encoded discard level, persisting coarse intermediate levels (`discard >= 2`) to disk even when higher-resolution slices are actively arriving.
- [`indra/llimage/vayubctexturecache.cpp:69-78`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayubctexturecache.cpp#L69-L78): `entryKey()` and `getFilePath()` — Generates `<uuid>_<discard>.bc`, treating each discard level as an isolated file entity.
- [`indra/llimage/vayubctexturecache.cpp:466-530`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayubctexturecache.cpp#L466-L530): `VayuBCTextureCache::readEntry()` — Only queries exact key matches in `mPendingIndex` and on disk, with no hierarchical fallback to extract coarser mips from higher-resolution entries.
- [`indra/llimage/vayuimageblockcompressor.h:45-52`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayuimageblockcompressor.h#L45-L52) & [`indra/llimage/vayuimageblockcompressor.cpp:298-315, 705-716`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayuimageblockcompressor.cpp#L705-L716): Mip chain storage order — Mips are stored in reverse order (1x1 smallest mip at byte offset 0, largest mip at the end). Subsumed discard levels can be cleanly sliced as a prefix buffer without re-encoding.

## Root Cause Hypothesis

1. **Independent File Keys**: `VayuBCTextureCache` treats every discard level as an independent, unrelated cache entry (`<id>_<discard>.bc`). It does not account for the hierarchical property of mipmapped texture pyramids where discard level $K$ is a strict subset of discard level $J$ (where $J < K$).
2. **Unfiltered Worker Writes**: In `ImageRequest::processRequest()`, every completed encode immediately dispatches `writeEntry()` to disk. Progressive network streams emit decodes for discard levels 5, 4, 3, 2, 1, 0 in rapid succession, causing up to 6 separate disk writes per texture.
3. **Exact-Match Cache Lookup**: `readEntry()` only checks for the exact requested discard level. When `LLTextureFetch` requests an initial coarse slice (e.g. discard 5) for a texture whose full resolution (`_0.bc`) is already on disk, the exact match fails:
   - If J2C is present in the legacy cache, image threads redundantly decompress and block-compress the texture anyway.
   - If J2C is not in the legacy cache, `LLTextureFetch` issues network HTTP requests to fetch J2C packets despite the texture already existing fully compressed on disk.

**Confidence**: High (100% verified against codebase implementations in `llimageworker.cpp`, `vayubctexturecache.cpp`, and `vayuimageblockcompressor.cpp`).

## Proposed Remediation

### Architecture & Design (Aligned via `/grill-me`)

1. **Single-File-Per-Asset Storage (`<uuid>.bc`)**:
   - Transition from `<uuid>_<discard>.bc` to a consolidated single file per UUID: `<cacheDir>/<hex>/<uuid>.bc`.
   - The file stores the highest resolution decoded so far, with `header.mDiscardLevel` recording the stored level (typically 0).
   - **Overwrite Rule**: When `writeEntry(id, discard, ...)` is called, it only persists if `<uuid>.bc` does not exist, or if the incoming `discard` is strictly lower (higher resolution) than the cached entry's discard level.
   - **Transparent Backward Compatibility**: `readEntry` checks for `<uuid>.bc` first; if not found, it checks `<uuid>_0.bc` as a legacy fallback. Legacy intermediate files (`<uuid>_[1-5].bc`) naturally age out via standard LRU cache purges.

2. **Dynamic Sub-Mip Slicing on Read in `VayuBCTextureCache::readEntry`**:
   - When looking up `(id, requested_discard)`:
     - Read `<uuid>.bc` (or legacy `<uuid>_0.bc`).
     - If `cached_discard <= requested_discard`:
       - If `cached_discard == requested_discard`: return the buffer and header directly.
       - If `cached_discard < requested_discard`:
         - Compute `diff = requested_discard - cached_discard`.
         - If `diff < header.mMipLevels`:
           - Calculate target dimensions: `target_w = max(1u, header.mWidth >> diff)`, `target_h = max(1u, header.mHeight >> diff)`.
           - Target mip count: `target_mips = header.mMipLevels - diff`.
           - Because `VayuBlockCompressionResult` stores mips in reverse order (smallest 1x1 mip at byte offset 0, largest mip at the end), the mips for `requested_discard` occupy the exact prefix buffer `[0, sub_bytes)` up to the end of mip `diff`.
           - Truncate / slice `buffer` to this prefix size, adjust `header.mWidth = target_w`, `header.mHeight = target_h`, `header.mMipLevels = target_mips`, and `header.mDiscardLevel = requested_discard`.
           - Return `true` (cache hit!).
     - If `cached_discard > requested_discard`: return `false` (cached data is too coarse; higher resolution needed).

3. **Coarse Intermediate Write Filtering in `llimageworker.cpp`**:
   - In `ImageRequest::processRequest()`:
     - Only dispatch `VayuBCTextureCache::instance().writeEntry()` if `discard == 0` (full resolution) OR if the image cannot decode higher (terminal/native resolution, e.g. small textures whose maximum achievable resolution is at `discard > 0`).
     - Intermediate streaming slices (`discard >= 1` when `discard == 0` is still pending) are still block-compressed in RAM and attached to `mDecodedImageRaw` for zero-copy OpenGL upload on the current frame, but are **never written to disk**.
     - **Impact**: Eliminates 80–90%+ of disk I/O, entirely halting the 7,600+ writes/min storm during region crossings and teleports.

4. **Early-Exit in `LLTextureFetchWorker` (Bypassing Network & Decode Queue)**:
   - In `LLTextureFetchWorker::doWork` (running on the background texture fetch thread `Ttf`), check `VayuBCTextureCache` during `LOAD_FROM_TEXTURE_CACHE`:
     - Query `VayuBCTextureCache::instance().readEntry(mID, mDesiredDiscard, min_preset, header, buffer)`.
     - On a cache hit:
       - Construct `mRawImage` directly, wrapping the sliced `VayuBlockCompressionResult`.
       - Transition state to `CACHE_POST` / completion without issuing network HTTP requests and without queuing to `LLImageDecodeThread`.
     - **Impact**: Completely avoids network round-trips and J2C HTTP downloads for previously cached textures, even if the legacy `LLTextureCache` was cleared or evicted.

**Files likely to change**:
- [`indra/llimage/vayubctexturecache.h`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayubctexturecache.h)
- [`indra/llimage/vayubctexturecache.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayubctexturecache.cpp)
- [`indra/llimage/llimageworker.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/llimageworker.cpp)
- [`indra/newview/lltexturefetch.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/lltexturefetch.cpp)
- [`indra/llimage/tests/vayubctexturecache_test.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/tests/vayubctexturecache_test.cpp)

**Tests to add or update**:
- In `vayubctexturecache_test.cpp`:
  - Verify `<uuid>.bc` single-file naming and ensure higher-resolution writes overwrite lower-resolution entries while lower-resolution writes do not overwrite higher-resolution entries.
  - Test dynamic sub-mip prefix slicing from full resolution (`discard = 0`) down to discards 1, 2, 3, 4, verifying dimensions, mip count, and buffer byte bounds.
  - Test legacy fallback to `<uuid>_0.bc` when `<uuid>.bc` is not yet present.
  - Verify intermediate write filtering skips coarse non-terminal slices.

## Risks & Considerations

- **Buffer Slicing Accuracy**: The sub-buffer byte count calculation must strictly match `calc_level_bytes` and reverse mip ordering so `LLImageGL::setImage(..., data_hasmips=true)` correctly uploads every mip down to 1x1.
- **Thread Safety**: `LLTextureFetchWorker::doWork` runs on thread `Ttf`, while `ImageRequest` runs on `ThreadPool:Imag`. `VayuBCTextureCache::readEntry` is thread-safe and protected by `mMutex` for `mPendingIndex` and filesystem checks.
- **Small Texture Terminal Discard**: Textures whose dimensions are small (e.g. 16x16) may have max resolution at `discard = 0`, but if a texture has limited decode layers, checking `mFormattedImage->getDiscardLevel() == 0` or whether it is at its maximum available LOD ensures it is persisted.

## Open Questions

- None. All architectural branches resolved and aligned via `/grill-me`.
