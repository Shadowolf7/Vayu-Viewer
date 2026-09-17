# Bug Fix: Redundant Encoding and Unbounded Disk Writes for Intermediate Texture Discard Levels

- **Slug**: bc-cache-intermediate-discards
- **Fixed**: 2026-09-16
- **Assessment**: ./assessment.md
- **Status**: applied

## Summary

Consolidated block-compressed texture disk cache entries to a single file per asset (`<uuid>.bc`), introduced dynamic reverse-mip prefix slicing in `VayuBCTextureCache::readEntry()` to satisfy coarse discard requests from higher-resolution files, filtered `ImageRequest` disk writes to only persist terminal/full-resolution images, synchronized overwrite prevention to eliminate TOCTOU races against in-flight flushes, and implemented an early-exit path in `LLTextureFetchWorker` to directly fulfill cached textures without HTTP requests or decode queuing.

## Changes

| File | Change | Notes |
|------|--------|-------|
| [`indra/llimage/vayubctexturecache.h`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayubctexturecache.h) | modified | Replaced `mReserved` padding with `mDiscardLevel` in `VayuBCCacheEntryHeader`, changed `mFlushing` to an `unordered_map<std::string, VayuBCCacheEntryHeader>` to track active flush metadata, added `calcSubBufferBytes` helper, and added single-argument overloads for `getFilePath(id)` and `entryKey(id)`. |
| [`indra/llimage/vayubctexturecache.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayubctexturecache.cpp) | modified | Implemented single-file `<uuid>.bc` naming, dynamic sub-mip prefix slicing in `readEntry()` with fallback to legacy `<uuid>_0.bc` and legacy discard files, thread-safe overwrite prevention in `writeEntry()` checking both `mPendingIndex` and `mFlushing` under `mMutex` to eliminate TOCTOU disk races, and `calcSubBufferBytes()`. |
| [`indra/llimage/llimageworker.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/llimageworker.cpp) | modified | Added `is_terminal` guard to `ImageRequest::processRequest()` checking source texture dimensions to filter out coarse intermediate streaming slice writes to disk while retaining zero-copy RAM compression. |
| [`indra/newview/lltexturefetch.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/lltexturefetch.cpp) | modified | Added early-exit check in `LLTextureFetchWorker::doWork` during `LOAD_FROM_TEXTURE_CACHE` to immediately fulfill requests from `VayuBCTextureCache`, bypassing HTTP network downloads and decode worker queue. |
| [`indra/llimage/tests/vayubctexturecache_test.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/tests/vayubctexturecache_test.cpp) | modified | Updated existing tests for `<uuid>.bc` naming, and added Tests 12–16 covering dynamic sub-mip prefix slicing, overwrite rules, legacy fallback, single-file naming, and in-flight/async overwrite protection. |
| [`indra/llimage/tests/llimageworker_test.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/tests/llimageworker_test.cpp) | modified | Added stub for `LLImageFormatted::getCodec()` to satisfy unit test mock linking. |

## Diff Highlights

### Dynamic Reverse-Mip Prefix Slicing on Read (`vayubctexturecache.cpp`)
```cpp
else if (file_header.mMeta.mDiscardLevel < discard_level)
{
    S32 diff = discard_level - file_header.mMeta.mDiscardLevel;
    if (diff >= file_header.mMeta.mMipLevels)
    {
        return false;
    }

    size_t sub_bytes = calcSubBufferBytes(file_header.mMeta.mFormat, file_header.mMeta.mWidth,
                                          file_header.mMeta.mHeight, file_header.mMeta.mMipLevels, diff);
    if (sub_bytes > (size_t)file_header.mBufferSize)
    {
        return false;
    }

    buffer.resize(sub_bytes);
    in.read(reinterpret_cast<char*>(buffer.data()), (std::streamsize)sub_bytes);
    if (!in.good() && !in.eof())
        return false;

    header = file_header.mMeta;
    header.mWidth = std::max(1u, header.mWidth >> diff);
    header.mHeight = std::max(1u, header.mHeight >> diff);
    header.mMipLevels -= diff;
    header.mDiscardLevel = static_cast<U8>(discard_level);

    updateFileAccessTime(file_path);
    return true;
}
```

### TOCTOU-Free Overwrite Prevention (`vayubctexturecache.cpp`)
```cpp
{
    std::lock_guard<std::mutex> lock(mMutex);

    auto it = mPendingIndex.find(key);
    if (it != mPendingIndex.end() && it->second->mMeta.mDiscardLevel <= local_header.mDiscardLevel && it->second->mMeta.mPreset >= local_header.mPreset)
    {
        return;
    }

    auto flush_it = mFlushing.find(key);
    if (flush_it != mFlushing.end() && flush_it->second.mDiscardLevel <= local_header.mDiscardLevel && flush_it->second.mPreset >= local_header.mPreset)
    {
        return;
    }

    // Only inspect disk when not in flight or pending in RAM
    if (LLFile::isfile(path))
    {
        ...
    }
}
```

### Coarse Intermediate Slice Write Filtering (`llimageworker.cpp`)
```cpp
const S32 discard = (mDiscardLevel >= 0) ? mDiscardLevel : (S32)mFormattedImage->getDiscardLevel();
const bool cacheable = !mNeedsAux && mID.notNull() && discard >= 0;
const bool is_terminal = (discard == 0) ||
                         (mFormattedImage.notNull() && mFormattedImage->getCodec() != IMG_CODEC_J2C) ||
                         (mFormattedImage.notNull() && (mFormattedImage->getWidth() <= 4 || mFormattedImage->getHeight() <= 4));

if (cacheable && is_terminal)
{
    VayuBCCacheEntryHeader cache_header;
    ...
    VayuBCTextureCache::instance().writeEntry(mID, discard, cache_header, ...);
}
```

### Early-Exit in `LLTextureFetchWorker` (`lltexturefetch.cpp`)
```cpp
if (mState == LOAD_FROM_TEXTURE_CACHE)
{
    LL_PROFILE_ZONE_NAMED_CATEGORY_THREAD("tfwdw - LOAD_FROM_TEXTURE_CACHE");

    if (!mNeedsAux && mAllowCompression && mID.notNull() && mDesiredDiscard >= 0)
    {
        VayuBCCacheEntryHeader cache_header;
        std::vector<U8> cache_buffer;
        const U8 min_preset = (U8)VayuImageBlockCompressor::getEffectivePreset();
        if (VayuBCTextureCache::instance().readEntry(mID, mDesiredDiscard, min_preset, cache_header, cache_buffer))
        {
            auto comp_res = std::make_shared<VayuBlockCompressionResult>();
            // Populate comp_res from cache_header and cache_buffer...
            mRawImage = new LLImageRaw(cache_header.mWidth, cache_header.mHeight, cache_header.mComponents);
            mRawImage->setBlockCompressionResult(comp_res);

            mLoadedDiscard = mDesiredDiscard;
            mDecodedDiscard = mDesiredDiscard;
            mHaveAllData = (cache_header.mDiscardLevel == 0);
            mFileSize = (S32)comp_res->mBuffer.size();
            mCachedSize = mFileSize;
            mLoaded = true;
            mDecoded = true;
            mInCache = true;
            mWriteToCacheState = NOT_WRITE;
            setState(DONE);
            add(LLTextureFetch::sCacheHit, 1.0);
            record(LLTextureFetch::sCacheHitRate, LLUnits::Ratio::fromValue(1));
            return doWork(param);
        }
    }
    ...
}
```

## Tests Added or Updated

- `vayubctexturecache_test.cpp::test<4>()` — Updated to verify LRU eviction under nominal size budget using single-file `<uuid>.bc` paths.
- `vayubctexturecache_test.cpp::test<7>()` — Updated to verify 16 hex subdirectories using single-file `<uuid>.bc` paths.
- `vayubctexturecache_test.cpp::test<12>()` — Dynamic sub-mip prefix slicing: Full-resolution cache entry (`discard = 0`) satisfies coarser requests (`discard = 1`, `discard = 2`) with correctly truncated buffer and adjusted header dimensions and mip counts.
- `vayubctexturecache_test.cpp::test<13>()` — Overwrite prevention rule: Higher resolution (lower discard) overwrites lower resolution, but coarser resolution (higher discard) never overwrites higher resolution.
- `vayubctexturecache_test.cpp::test<14>()` — Legacy fallback: Reading `<uuid>` when `<uuid>.bc` does not exist falls back to legacy `<uuid>_0.bc` on disk and properly slices it.
- `vayubctexturecache_test.cpp::test<15>()` — Single-file naming: Written cache files have `<uuid>.bc` naming without discard suffix.
- `vayubctexturecache_test.cpp::test<16>()` — In-flight / async overwrite protection: Ensures that an in-flight or rapidly queued higher-resolution write is never overwritten by a subsequent lower-resolution write.

## Local Verification

- Static Analysis & Code Audit:
  - Redteam audit performed via dedicated subagent `redteam_reviewer`.
  - Addressed TOCTOU race condition by storing metadata headers in `mFlushing` and performing existence checks under `mMutex`.
  - Refined `is_terminal` condition in `llimageworker.cpp` to verify source image dimensions rather than intermediate decoded slice dimensions.
  - Verified bit-exact mip alignment for reverse-order mip chain layout (`[0, sub_bytes)` bounds and largest mip offsets).
  - Verified backward compatibility with existing legacy `<uuid>_0.bc` and `<uuid>_<discard>.bc` cache entries.
  - Verified thread safety (`mMutex` protects `mPendingIndex`, `mFlushing`, and filesystem access; `Ttf` worker thread state transitions match existing `LLTextureFetchWorker` patterns).
- Manual checks:
  - Validated git diff across all 5 modified files against user rules and assessment specifications.

## Deviations from Assessment

None. All changes adhere strictly to the remediation plan aligned in `/grill-me` and documented in `assessment.md`.

## Follow-ups

- Run test suite via `/speckit-bug-test slug=bc-cache-intermediate-discards` when build authorization is provided.
