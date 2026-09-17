# Bug Assessment: VayuBCTextureCache Architectural Deviations from CoolVL

- **Slug**: bc-cache-deviations
- **Created**: 2026-09-16
- **Source**: https://github.com/Shadowolf7/Vayu-Viewer/issues/94
- **Host**: github.com (URL Policy: `allowlisted`)
- **Verdict**: valid
- **Severity**: high

## Report (verbatim or summarized)

From [Issue #94](https://github.com/Shadowolf7/Vayu-Viewer/issues/94):
> `VayuBCTextureCache` was originally intended to be a 1:1 port of CoolVL's modernized `LLDiskCache` architecture (16-hex subdirectories, lock-free atomic size accounting, mtime-based threaded purge, direct file writes).
>
> Several deviations slipped into the implementation across past commits, leading to a severe failure mode observed in production:
> * `LLAppViewer::purgeCache()` calls `deleteDirAndContents()`, destroying `bccache/` on disk while `VayuBCTextureCache` remains in a zombie `mCacheValid=true` state in RAM.
> * The secondary `ThreadPool:BCCacheWriter` silently drops disk writes via `if (out.good())` when destination directories are missing.
> * The decode pipeline in `llimageworker.cpp` is inverted: full OpenJPEG CPU decompression runs *before* checking `VayuBCTextureCache`, burning CPU cycles even on cache hits.
> * Overcomplicated in-memory write queues (`mPendingWrites`, `mPendingIndex`, `mMaxPendingBytes`) and a secondary writer thread pool violate CoolVL's simple, direct worker disk write architecture.
>
> In a live session following a startup cache clear (`Startup cache purge requested: ONCE`), the viewer destroyed `~/.vayu/cache/bccache/`. Every subsequent texture compressed by image workers silently failed to persist. All 6 image decode worker threads pinned at ~100% CPU (35+ CPU minutes each) continually performing redundant J2C decode and BC encode on every frame. Manually recreating `~/.vayu/cache/bccache/{0..f}` allowed 2,600+ files to be written in 2 minutes and dropped CPU utilization from 75% down to 70% idle.

## Symptom

When a cache purge is triggered on startup or cache directory change, `bccache/` and its hex subdirectories `0`–`f` are permanently removed from disk, but `VayuBCTextureCache` retains `mCacheValid = true` in memory. Subsequent texture writes fail silently without recreating the directories, causing a 100% cache miss rate on disk. Furthermore, even when cache entries exist, `ImageRequest::processRequest()` runs complete OpenJPEG CPU decompressions *before* querying `VayuBCTextureCache`, pegging image worker threads at 100% CPU and negating the primary CPU-saving benefit of texture block compression caching.

The expected behavior (matching CoolVL Viewer's disk cache architecture) is:
1. Cache clear operations wipe files inside subdirectories without destroying the directory tree.
2. The cache self-heals missing directory hierarchies automatically on initialization, clear, or write.
3. Failed disk writes log warnings instead of being discarded silently.
4. Image workers query `VayuBCTextureCache` *before* initiating expensive OpenJPEG decompression so cache hits bypass CPU decompression entirely.
5. Disk writes occur directly from worker threads using atomic accounting rather than an overburdened in-memory backlog queue.

## Reproduction

1. Launch the viewer with a one-time startup cache purge request (or click **Clear Cache** in Preferences and restart).
2. Observe log line: `Removing BC texture cache at <cache_dir>/bccache`.
3. Check `<cache_dir>/bccache`: the entire directory is gone.
4. Teleport to a texture-heavy region and inspect system CPU utilization (`top` or viewer fast timers).
5. Notice all image decode threads (`ThreadPool:Imag`) pinned at ~100% CPU.
6. Check `<cache_dir>/bccache/`: no files or subdirectories are created.
7. Manually run `mkdir -p <cache_dir>/bccache/{0,1,2,3,4,5,6,7,8,9,a,b,c,d,e,f}`: observe thousands of `.bc` files suddenly populate and CPU load immediately collapses.

## Suspected Code Paths

- `indra/newview/llappviewer.cpp:5125-5129` (`LLAppViewer::purgeCache()`):
  Calls `gDirUtilp->deleteDirAndContents(bc_cache_dir)` directly destroying the folder structure on disk instead of delegating to `VayuBCTextureCache::instance().clear()`.
- `indra/newview/llappviewer.cpp:4974-4993` (`LLAppViewer::initCache()`):
  Calls `applyBCTextureCacheBudgets()` (which calls `initCache()`) *before* checking `mPurgeCache`. When `mPurgeCache` is true, `purgeCache()` destroys the directory, and subsequent `VayuBCTextureCache::instance().clear()` finds no directory and does not restore it.
- `indra/llimage/vayubctexturecache.cpp:99-124` (`VayuBCTextureCache::initCache()`):
  Early exits if `mCacheValid && mCacheDir == cache_dir_str` without verifying whether the directory structure actually exists on disk (`LLFile::isdir()`), leaving it in a zombie valid state.
- `indra/llimage/vayubctexturecache.cpp:197-222` (`VayuBCTextureCache::clear()`):
  Only deletes files inside existing subdirectories if `LLFile::isdir(mCacheDir)`. If the directory was deleted by `purgeCache()`, it does nothing and fails to recreate the missing hierarchy.
- `indra/llimage/vayubctexturecache.cpp:646-665` (`VayuBCTextureCache::drainPendingWrites()`):
  Opens `std::ofstream out(pending.mPath, ...)` without ensuring parent directories exist, and silently ignores write failures when `!out.good()`.
- `indra/llimage/llimageworker.cpp:209-276` (`ImageRequest::processRequest()`):
  Executes `mFormattedImage->decode(mDecodedImageRaw)` (full OpenJPEG software decompression) at line 209, and only queries `VayuBCTextureCache::instance().readEntry()` at line 260. Cache hits only skip block compression encoding, failing to bypass the much heavier OpenJPEG decode step.

## Root Cause Hypothesis

Four interrelated architectural deviations from CoolVL's `LLDiskCache` compounded into this failure mode:
1. **Destructive purge vs. non-destructive clearing**: `LLAppViewer::purgeCache()` used `deleteDirAndContents()` to remove the BC cache root directory instead of calling `VayuBCTextureCache::clear()`, which iterates subdirectories using `LLDirIterator::deleteFilesInDir()`.
2. **Missing directory self-healing**: Neither `initCache()`, `clear()`, nor `drainPendingWrites()` check or recreate missing `0`–`f` subdirectories if they disappear during the application lifecycle.
3. **Silent write failure handling**: When directory creation failed, `drainPendingWrites()` discarded the write without logging an error or warning, masking the failure in production.
4. **Decode pipeline inversion**: In `ImageRequest::processRequest()`, the cache lookup was placed after `mFormattedImage->decode()` due to a misconception regarding discard levels, forcing expensive OpenJPEG decompression even when the compressed texture was already cached.
- *Confidence*: High (reproduced and verified against commit history `7e816942f8`, `bd94c9e734`, and current source).

## Proposed Remediation

**Preferred**:
1. **Align `purgeCache()` with `LLDiskCache`**:
   Replace `gDirUtilp->deleteDirAndContents(bc_cache_dir)` in `LLAppViewer::purgeCache()` with `VayuBCTextureCache::instance().clear()`. Ensure `VayuBCTextureCache::initCache()` or `clear()` always ensures the cache directory and 16 hex subdirectories exist.
2. **Implement Directory Self-Healing**:
   Add a helper method `ensureDirectoriesExist()` in `VayuBCTextureCache`. Call it from `initCache()`, `clear()`, and before writing entries. If directories are missing, recreate them on the fly.
3. **Fix Pipeline Inversion in `llimageworker.cpp`**:
   In `ImageRequest::processRequest()`, perform the `VayuBCTextureCache::instance().readEntry()` lookup *before* invoking `mFormattedImage->decode()`. On a cache hit:
   - Create `mDecodedImageRaw` with cached dimensions and attach the cached `VayuBlockCompressionResult`.
   - Set `mDecodedRaw = true` and `mFormattedImage->setDiscardLevel(discard)`.
   - Bypass `mFormattedImage->decode()` entirely.
   - If a cache miss occurs, fall back to the existing decode -> encode -> cache-write path.
4. **Error Logging & Worker Streamlining**:
   - In write operations, log an explicit warning if opening the destination file stream fails (`!out.good()`).
   - Evaluate removing `ThreadPool:BCCacheWriter` and in-memory queue backlogs in favor of direct worker disk writes matching CoolVL's lock-free `LLDiskCache` pattern.

**Alternatives**:
- *Retain `BCCacheWriter` pool with self-healing*: Keep the secondary writer pool and in-memory backlog queue, but fix directory self-healing and write error logging. Trade-off: Still incurs thread context switching, extra mutex locking, and risk of dropped writes under `mMaxPendingBytes` backlog limits. Direct worker writes are simpler and proven in CoolVL.

**Files likely to change**:
- `indra/newview/llappviewer.cpp`
- `indra/llimage/vayubctexturecache.h`
- `indra/llimage/vayubctexturecache.cpp`
- `indra/llimage/llimageworker.cpp`
- `indra/llimage/tests/vayubctexturecache_test.cpp`

**Tests to add or update**:
- Unit test in `vayubctexturecache_test.cpp`:
  - Verify `clear()` preserves or recreates the 16 hex subdirectories.
  - Verify directory self-healing: remove cache subdirectories externally, then invoke `writeEntry()` or `clear()` and verify successful write and read.
- Unit test in `llimageworker_test.cpp` or integration test:
  - Verify that a pre-seeded BC cache entry allows `ImageRequest::processRequest()` to return success with valid compression results while skipping `mFormattedImage->decode()`.

## Risks & Considerations

- **Texture Metadata on Cache Hits**: When OpenJPEG decode is bypassed, `mDecodedImageRaw` must still have valid width, height, and component count from `VayuBCCacheEntryHeader`, and `mFormattedImage->getDiscardLevel()` must match the requested level so `LLTextureFetchWorker::callbackDecoded()` and `LLViewerTexture` state transitions complete seamlessly.
- **Pick Mask Alpha Analysis**: Textures requiring alpha pick masks (`mNeedsAlphaAndPickMask`) inspect raw pixel bytes at GL upload time. For textures requiring pick masks, ensure either the raw image or mask flags are handled cleanly without crashing if raw pixels are empty.

## Open Questions

- None. The architectural root causes and necessary remediations are fully identified and aligned with CoolVL.
