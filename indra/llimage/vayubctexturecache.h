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

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Mirrors the fields callers need to reconstruct a GL upload without
// depending on llimage's VayuBlockCompressionResult type directly.
struct VayuBCCacheEntryHeader
{
    U8  mFormat = 0;           // EVayuBlockCompressionFormat, as encoded by the caller
    U8  mPreset = 0;           // EVayuBlockCompressionPreset the buffer was encoded at
    U8  mIsMask = 0;           // 1 if alpha mask, 0 if alpha blend / no mask
    U8  mDiscardLevel = 0;     // Discard level at which this entry was encoded (0 = full resolution)
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

    static constexpr U32 kMagic = 0x31434256; // "VBC1"
    static constexpr U32 kFormatVersion = 3;

    // Creates cache_dir and 16 hex subdirectories ('0'-'f') if needed. Safe to
    // call again to change the size budget; does not re-scan if already initialized
    // with the same directory.
    void initCache(const std::filesystem::path& cache_dir, S64 max_size_bytes,
                   bool second_instance = false);

    // Ensures cache_dir and all 16 hex subdirectories ('0'-'f') exist on disk.
    // Recreates missing directories if they were deleted externally.
    bool ensureDirectoriesExist();

    // Clears the cache by removing all cached files in all subdirectories.
    void clear();

    // Purges the oldest items in the cache so that the combined size of all
    // files is no bigger than mNominalSizeBytes. May be internally threaded.
    void purge();

    // Threaded cache purging. Must be called only from the main thread.
    void threadedPurge();

    // Shuts down the cache and joins the purge thread cleanly.
    void shutdown();

    // Looks up (id, discard_level). Slices sub-buffer if discard_level > entry.mDiscardLevel.
    // Updates the file's access time with rate-limiting.
    bool readEntry(const LLUUID& id, S32 discard_level,
                   VayuBCCacheEntryHeader& header, std::vector<U8>& buffer);

    // Writes (or overwrites) the entry for (id, discard_level) directly to disk from
    // whichever worker thread produces the compressed texture, with zero mutex locks
    // held during file I/O.
    void writeEntry(const LLUUID& id, S32 discard_level,
                    const VayuBCCacheEntryHeader& header,
                    std::shared_ptr<const std::vector<U8>> buffer);

    // Constructs a file path based on the asset UUID:
    // cache_dir / hex_subdir / <id>.bc
    std::string getFilePath(const LLUUID& id) const;

    // Backward-compatible overload for legacy <id>_<discard>.bc paths
    std::string getFilePath(const LLUUID& id, S32 discard_level) const;

    // Slices sub-buffer byte count for coarser discard levels
    static size_t calcSubBufferBytes(U8 format, U32 width, U32 height, S32 num_mips, S32 diff);

    // Rate-limited touch of the file's last access time to maintain LRU order on disk.
    void updateFileAccessTime(const std::string& file_path);

    // Real-time byte tracking (atomic). Bails out immediately when called on worker threads.
    void addBytesWritten(S64 bytes);

    S64 getCurrentSize() const { return (S64)mCurrentSizeBytes.load(); }
    S64 getMaxSize() const { return (S64)mMaxSizeBytes; }
    S64 getNominalSize() const { return (S64)mNominalSizeBytes; }
    size_t getEntryCount() const { return (size_t)mEntryCount.load(); }

    bool isInitialized() const { return mCacheValid; }

    const std::string getCacheInfo() const;

private:
    VayuBCTextureCache() = default;

    U64 cacheDirSize();

    mutable std::mutex mMutex;
    std::string mCacheDir;
    U64 mNominalSizeBytes = 0;
    U64 mMaxSizeBytes = 0;
    std::atomic<U64> mCurrentSizeBytes{0};
    std::atomic<U64> mEntryCount{0};
    std::atomic<bool> mPurging{false};
    bool mCacheValid = false;

    VayuBCCachePurgeThread* mPurgeThread = nullptr;
};
