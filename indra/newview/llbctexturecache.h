/**
 * @file llbctexturecache.h
 * @brief On-disk cache for pre-encoded block-compressed texture mip chains.
 *
 * Standalone from LLTextureCache (which caches the still-JPEG2000-compressed
 * asset bytes) so that a mistake here can't corrupt the texture cache every
 * load already depends on. Deliberately knows nothing about
 * LLBlockCompressionResult/bc7e - it stores whatever (header, buffer) bytes
 * it's given and hands them back, so it stays unit-testable without linking
 * llimage.
 */

#ifndef LL_LLBCTEXTURECACHE_H
#define LL_LLBCTEXTURECACHE_H

#include "lluuid.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// Mirrors the fields callers need to reconstruct a GL upload without
// depending on llimage's LLBlockCompressionResult type directly.
struct LLBCCacheEntryHeader
{
    U8  mFormat = 0;           // ELLBlockCompressionFormat, as encoded by the caller
    U8  mPreset = 0;           // ELLBlockCompressionPreset the buffer was encoded at
    S32 mMipLevels = 0;
    U32 mWidth = 0;
    U32 mHeight = 0;
    S32 mComponents = 0;
    U32 mGLInternalFormat = 0;
    U32 mGLPrimaryFormat = 0;
};

class LLBCTextureCache
{
public:
    static LLBCTextureCache& instance();

    // Not copyable - single process-wide cache.
    LLBCTextureCache(const LLBCTextureCache&) = delete;
    LLBCTextureCache& operator=(const LLBCTextureCache&) = delete;

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
                   LLBCCacheEntryHeader& header, std::vector<U8>& buffer);

    // Writes (or overwrites) the entry for (id, discard_level). Evicts the
    // least-recently-touched entries afterward if this pushed the cache over
    // its size budget.
    void writeEntry(const LLUUID& id, S32 discard_level,
                    const LLBCCacheEntryHeader& header, const std::vector<U8>& buffer);

    S64 getCurrentSize() const;
    S64 getMaxSize() const { return mMaxSize; }
    size_t getEntryCount() const;

private:
    LLBCTextureCache() = default;

    struct IndexEntry
    {
        std::filesystem::path mPath;
        S64 mFileSize = 0;
        U64 mRecency = 0; // higher = more recently written/read; see mNextRecency
    };

    std::filesystem::path entryPath(const LLUUID& id, S32 discard_level) const;
    std::string entryKey(const LLUUID& id, S32 discard_level) const;
    void evictUntilWithinBudget();
    void removeEntry(const std::string& key, const IndexEntry& entry);

    mutable std::mutex mMutex;
    std::filesystem::path mCacheDir;
    S64 mMaxSize = 0;
    S64 mCurrentSize = 0;
    bool mInitialized = false;
    U64 mNextRecency = 0; // monotonic counter; avoids relying on filesystem mtime resolution

    // Keyed by entryKey(id, discard_level); ordering for eviction comes from
    // each file's on-disk mtime (touched on every read hit and every write),
    // not this map's iteration order.
    std::map<std::string, IndexEntry> mIndex;
};

#endif // LL_LLBCTEXTURECACHE_H
