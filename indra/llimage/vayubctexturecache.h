/**
 * @file vayubctexturecache.h
 * @brief On-disk cache for pre-encoded block-compressed texture mip chains.
 *
 * Standalone from LLTextureCache (which caches the still-JPEG2000-compressed
 * asset bytes) so that a mistake here can't corrupt the texture cache every
 * load already depends on. Deliberately knows nothing about
 * VayuBlockCompressionResult/bc7e - it stores whatever (header, buffer) bytes
 * it's given and hands them back, so it stays unit-testable without linking
 * llimage.
 */

#pragma once

#include "lluuid.h"

#include <cstdint>
#include <filesystem>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Mirrors the fields callers need to reconstruct a GL upload without
// depending on llimage's VayuBlockCompressionResult type directly.
struct VayuBCCacheEntryHeader
{
    U8  mFormat = 0;           // EVayuBlockCompressionFormat, as encoded by the caller
    U8  mPreset = 0;           // EVayuBlockCompressionPreset the buffer was encoded at
    S32 mMipLevels = 0;
    U32 mWidth = 0;
    U32 mHeight = 0;
    S32 mComponents = 0;
    U32 mGLInternalFormat = 0;
    U32 mGLPrimaryFormat = 0;
};

class VayuBCTextureCache
{
public:
    static VayuBCTextureCache& instance();

    // Not copyable - single process-wide cache.
    VayuBCTextureCache(const VayuBCTextureCache&) = delete;
    VayuBCTextureCache& operator=(const VayuBCTextureCache&) = delete;

    // Creates cache_dir if needed and scans existing entries. Safe to call
    // again to change the size budget; does not re-scan if already initialized
    // with the same directory.
    void initCache(const std::filesystem::path& cache_dir, S64 max_size_bytes);

    // Deletes every entry and resets the in-memory index. Cache stays usable
    // (re-initialized empty) afterward.
    void purge();

    // Looks up (id, discard_level). Only counts as a hit if the cached entry's
    // preset is >= min_preset - a texture cached during a busy moment at a
    // lower preset than currently configured is treated as a miss so callers
    // re-encode and overwrite it, rather than being stuck at low quality.
    bool readEntry(const LLUUID& id, S32 discard_level, U8 min_preset,
                   VayuBCCacheEntryHeader& header, std::vector<U8>& buffer);

    // Writes (or overwrites) the entry for (id, discard_level). Evicts the
    // least-recently-touched entries afterward if this pushed the cache over
    // its size budget.
    void writeEntry(const LLUUID& id, S32 discard_level,
                    const VayuBCCacheEntryHeader& header, const std::vector<U8>& buffer);

    S64 getCurrentSize() const;
    S64 getMaxSize() const { return mMaxSize; }
    size_t getEntryCount() const;

private:
    VayuBCTextureCache() = default;

    struct IndexEntry
    {
        std::string mKey;
        std::filesystem::path mPath;
        S64 mFileSize = 0;
    };

    // Recency order lives in the list itself: front is most recently
    // touched, back is least. Eviction always pops the back; touching an
    // entry always splices it to the front. Both are O(1) - std::list
    // splice/erase never relocate elements or invalidate other iterators,
    // which is what lets mIndex hold onto iterators into this list safely.
    using LruList = std::list<IndexEntry>;

    std::filesystem::path entryPath(const LLUUID& id, S32 discard_level) const;
    std::string entryKey(const LLUUID& id, S32 discard_level) const;
    void touch(LruList::iterator it);
    void evictUntilWithinBudget();
    void removeEntry(const std::string& key);

    mutable std::mutex mMutex;
    std::filesystem::path mCacheDir;
    S64 mMaxSize = 0;
    S64 mCurrentSize = 0;
    bool mInitialized = false;

    LruList mLruList;
    std::unordered_map<std::string, LruList::iterator> mIndex;
};
