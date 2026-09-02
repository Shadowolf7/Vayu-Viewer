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
#include "threadpool_fwd.h"

#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

class VayuBCCachePurgeThread;

class VayuBCTextureCache
{
public:
    static VayuBCTextureCache& instance();
    ~VayuBCTextureCache();

    // Not copyable - single process-wide cache.
    VayuBCTextureCache(const VayuBCTextureCache&) = delete;
    VayuBCTextureCache& operator=(const VayuBCTextureCache&) = delete;

    // Default ceiling on how many bytes of not-yet-flushed writes may sit in
    // RAM at once. See mMaxPendingBytes for why this bound exists at all.
    static constexpr S64 kDefaultMaxPendingBytes = 256 * 1024 * 1024;

    // Creates cache_dir and 16 hex subdirectories ('0'-'f') if needed. Safe to
    // call again to change the size budget; does not re-scan if already initialized
    // with the same directory.
    void initCache(const std::filesystem::path& cache_dir, S64 max_size_bytes,
                   S64 max_pending_bytes = kDefaultMaxPendingBytes,
                   bool second_instance = false);

    // Clears the cache by removing all cached files in all subdirectories.
    void clear();

    // Purges the oldest items in the cache so that the combined size of all
    // files is no bigger than mNominalSizeBytes. May be internally threaded.
    void purge();

    // Threaded cache purging. Can be called from the main thread or background writer thread.
    void threadedPurge();

    // Joins the writer pool's thread and purge thread at a controlled point in app shutdown.
    void shutdown();

    // Looks up (id, discard_level). Only counts as a hit if the cached entry's
    // preset is >= min_preset. Updates the file's access time with rate-limiting.
    bool readEntry(const LLUUID& id, S32 discard_level, U8 min_preset,
                   VayuBCCacheEntryHeader& header, std::vector<U8>& buffer);

    // Writes (or overwrites) the entry for (id, discard_level) asynchronously via
    // the writer pool. Triggers threadedPurge() if budget is exceeded after flushing.
    void writeEntry(const LLUUID& id, S32 discard_level,
                    const VayuBCCacheEntryHeader& header,
                    std::shared_ptr<const std::vector<U8>> buffer);

    // Constructs a file path based on the asset UUID and discard level:
    // cache_dir / hex_subdir / <id>_<discard>.bc
    std::string getFilePath(const LLUUID& id, S32 discard_level) const;

    // Rate-limited touch of the file's last access time to maintain LRU order on disk.
    void updateFileAccessTime(const std::string& file_path);

    // Real-time byte tracking (atomic)
    void addBytesWritten(S64 bytes);

    S64 getCurrentSize() const { return (S64)mCurrentSizeBytes.load(); }
    S64 getMaxSize() const { return (S64)mMaxSizeBytes; }
    S64 getNominalSize() const { return (S64)mNominalSizeBytes; }
    size_t getEntryCount() const { return (size_t)mEntryCount.load(); }

    bool isInitialized() const { return mCacheValid; }

    S64 getPendingBytes() const;
    S64 getMaxPendingBytes() const { return mMaxPendingBytes; }

    const std::string getCacheInfo() const;

private:
    VayuBCTextureCache() = default;

    // A write queued for the background writer pool.
    struct PendingWrite
    {
        std::string mKey;
        std::string mPath;
        VayuBCCacheEntryHeader mMeta;
        std::shared_ptr<const std::vector<U8>> mBuffer;
        S64 mFileSize = 0;
    };
    using PendingList = std::list<PendingWrite>;

    std::string entryKey(const LLUUID& id, S32 discard_level) const;

    U64 cacheDirSize();

    void trimPendingBacklog(bool* should_log_drops);

    bool queuePendingWrite(const std::string& key, const std::string& path,
                           const VayuBCCacheEntryHeader& header,
                           std::shared_ptr<const std::vector<U8>>&& buffer,
                           S64 file_size);

    void drainPendingWrites();

    mutable std::mutex mMutex;
    std::string mCacheDir;
    U64 mNominalSizeBytes = 0;
    U64 mMaxSizeBytes = 0;
    std::atomic<U64> mCurrentSizeBytes{0};
    std::atomic<U64> mEntryCount{0};
    std::atomic<bool> mPurging{false};
    bool mCacheValid = false;

    VayuBCCachePurgeThread* mPurgeThread = nullptr;

    // Writes not yet flushed to disk
    PendingList mPendingWrites;
    std::unordered_map<std::string, PendingList::iterator> mPendingIndex;
    std::unordered_set<std::string> mFlushing;

    S64 mPendingBytes = 0;
    S64 mMaxPendingBytes = kDefaultMaxPendingBytes;

    S64 mDroppedWrites = 0;
    S64 mDroppedBytes = 0;
    S64 mDroppedWritesReported = 0;
    F64 mLastDropLogTime = 0.0;

    bool mDraining = false;

    std::unique_ptr<LL::ThreadPool> mWriterPool;
};
