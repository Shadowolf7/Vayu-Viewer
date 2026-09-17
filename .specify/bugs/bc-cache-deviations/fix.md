# Bug Fix: VayuBCTextureCache Architectural Deviations from CoolVL

- **Slug**: bc-cache-deviations
- **Fixed**: 2026-09-16
- **Assessment**: ./assessment.md
- **Status**: applied

## Summary

Aligned `VayuBCTextureCache` and `LLAppViewer::purgeCache()` with CoolVL's non-destructive disk cache architecture by eliminating directory-destroying calls, adding directory self-healing to the cache manager, logging write errors, and inverting the decode pipeline in `ImageRequest::processRequest()` to check the BC cache *before* running OpenJPEG CPU decompression.

## Changes

| File | Change | Notes |
|------|--------|-------|
| `indra/llimage/vayubctexturecache.h` | modified | Declared `ensureDirectoriesExist()` public helper method. |
| `indra/llimage/vayubctexturecache.cpp` | modified | Implemented `ensureDirectoriesExist()`, integrated directory validation into `initCache()`, `clear()`, `writeEntry()`, and self-healing retry + warning logging in `drainPendingWrites()`. |
| `indra/newview/llappviewer.cpp` | modified | Replaced destructive `deleteDirAndContents()` in `purgeCache()` with non-destructive `VayuBCTextureCache::instance().clear()`. |
| `indra/llimage/llimageworker.cpp` | modified | Inverted pipeline in `ImageRequest::processRequest()` to query `VayuBCTextureCache::readEntry()` before `mFormattedImage->decode()`, skipping OpenJPEG software decompression on cache hits. |
| `indra/llimage/tests/vayubctexturecache_test.cpp` | modified | Added Test 10 (self-healing write when subdirectories are missing) and Test 11 (self-healing clear when root directory is deleted). |

## Diff Highlights (optional)

### Non-destructive Purge in `indra/newview/llappviewer.cpp`
```cpp
    const std::string bc_cache_dir = gDirUtilp->getExpandedFilename(LL_PATH_CACHE, "bccache");
    LL_INFOS("AppCache") << "Clearing BC texture cache at " << bc_cache_dir << LL_ENDL;
    VayuBCTextureCache::instance().clear();
    gDirUtilp->deleteFilesInDir(gDirUtilp->getExpandedFilename(LL_PATH_CACHE, ""), "*");
```

### Self-Healing Writes in `indra/llimage/vayubctexturecache.cpp`
```cpp
    std::ofstream out(pending.mPath, std::ios::binary | std::ios::trunc);
    if (!out.good())
    {
        ensureDirectoriesExist();
        out.clear();
        out.open(pending.mPath, std::ios::binary | std::ios::trunc);
    }
```

### Pre-Decode Cache Check in `indra/llimage/llimageworker.cpp`
```cpp
    // Check VayuBCTextureCache *before* running expensive CPU OpenJPEG decode.
    // On a hit, we attach the cached compressed blocks and bypass software decode entirely.
    if (cacheable && VayuBCTextureCache::instance().readEntry(mID, discard, min_preset, cache_header, cache_buffer))
    {
        // Populate comp_res, attach to mDecodedImageRaw, mark done, and skip decode!
        cache_hit = true;
    }
    if (!cache_hit)
    {
        done = mFormattedImage->decode(mDecodedImageRaw, decode_time_slice);
    }
```

## Tests Added or Updated

- `indra/llimage/tests/vayubctexturecache_test.cpp::test<10>` — Simulates external deletion of subdirectories `0`–`f`, executes `writeEntry()`, and verifies that directories are self-healed and the entry is readable.
- `indra/llimage/tests/vayubctexturecache_test.cpp::test<11>` — Simulates external deletion of the entire cache root directory, executes `clear()`, and verifies that the directory tree is fully restored and ready for writes.

## Local Verification

- Commands run:
  - `./scripts/safe-build.sh ninja -C build-Linux-ninja-perf -f build-Release.ninja PROJECT_llimage_TEST_vayubctexturecache:Release` → compiled cleanly (exit code 0).
  - `./build-Linux-ninja-perf/sharedlibs/Release/bin/PROJECT_llimage_TEST_vayubctexturecache` → passed 11/11 tests:
    ```text
    Unit test group_started name=VayuBCTextureCache
    Unit test group_completed name=VayuBCTextureCache
    	Total Tests:	11
    	Passed Tests:	11	YAY!! \o/
    ```
  - `./scripts/safe-build.sh cmake --build build-Linux-ninja-perf --config Release` → full incremental build, binary link (`vayu-bin`), and packaging completed successfully (exit code 0).
- Manual checks: Static audit of directory self-healing logic, verification that `mDecodedImageRaw` and `mFormattedImage` discard levels are correctly initialized on cache hit, and confirmation that `purgeCache()` delegates to `VayuBCTextureCache::clear()`.

## Deviations from Assessment

None. All preferred remediations were implemented exactly as defined in `assessment.md`.

## Follow-ups

- Perform an in-world session with a one-time startup cache clear to verify visual texture streaming and verify worker threads remain near 0% CPU on cache hits.
