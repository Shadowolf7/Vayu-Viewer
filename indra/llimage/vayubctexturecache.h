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
#include "lltimer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "threadpool_fwd.h"

// Mirrors the fields callers need to reconstruct a GL upload without
// depending on llimage's VayuBlockCompressionResult type directly.
struct VayuBCCacheEntryHeader
{
    U8  mFormat = 0;           // EVayuBlockCompressionFormat, as encoded by the caller
    U8  mPreset = 0;           // EVayuBlockCompressionPreset the buffer was encoded at
    U8  mIsMask = 0;           // 1 if alpha mask, 0 if alpha blend / no mask
    U8  mRole = 0;             // kRoleColor (0) or kRoleNormal (1); normal is always
                               // linear. Role is part of the filename below, so the same
                               // format never collides across roles.
    U8  mDiscardLevel = 0;     // Discard level at which this entry was encoded (0 = full resolution)
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
    ~VayuBCTextureCache();

    // Not copyable - single process-wide cache.
    VayuBCTextureCache(const VayuBCTextureCache&) = delete;
    VayuBCTextureCache& operator=(const VayuBCTextureCache&) = delete;

    static constexpr U32 kMagic = 0x31434256; // "VBC1"
    // Format/configuration version of the BC cache. This is ALSO the dual-cache
    // invalidation knob: bumping it makes initCache() (llappviewer.cpp) clear the
    // entire JPEG2000 + BC texture cache on the next startup, so any on-disk
    // format or encode-config change wipes both dumb caches at once.
    static constexpr U32 kFormatVersion = 11;

    static constexpr U8 kFormatBC1 = 1;
    static constexpr U8 kFormatBC3 = 2;
    static constexpr U8 kFormatBC4 = 3;
    static constexpr U8 kFormatBC5 = 4;
    static constexpr U8 kFormatBC7 = 5;

    // Role code stored in VayuBCCacheEntryHeader::mRole. Selects the filename
    // discriminator too: normal entries keep a linear-only, format-specific
    // namespace (<uuid>.bcNn), color entries the plain <uuid>.bcN one, so the
    // same format encoded for different roles lives in different files and can
    // no longer last-writer-win over each other.
    static constexpr U8 kRoleColor = 0;
    static constexpr U8 kRoleNormal = 1;

    // On-disk extension for a given (kFormat*, kRole*): ".bc1"..".bc7" for
    // color, ".bc3n"/".bc5n"/".bc7n" for normal roles. Encoding both the format
    // and the role in the name keeps one role's entry from ever being served to
    // another, independent of the header's mFormat/mRole fields.
    static std::string formatExtension(U8 format, U8 role = kRoleColor);

    // Creates cache_dir and 16 hex subdirectories ('0'-'f') if needed. Safe to
    // call again to change the size budget; does not re-scan if already initialized
    // with the same directory.
    void initCache(const std::filesystem::path& cache_dir, S64 max_size_bytes);

    // Ensures cache_dir and all 16 hex subdirectories ('0'-'f') exist on disk.
    // Recreates missing directories if they were deleted externally.
    bool ensureDirectoriesExist();

    // Clears the cache by removing all cached files in all subdirectories.
    void clear();

    // Purges the oldest items in the cache so that the combined size of all
    // files is no bigger than mNominalSizeBytes. Runs the purge to completion
    // synchronously on the calling thread (test path).
    void purge();

    // Requests a background purge. The work then runs time-sliced on the
    // cache's own single writer thread, one bounded pass at a time (see
    // update()). Main thread only; never blocks.
    void requestPurge();

    // Main-thread tick, called each frame from LLAppViewer::updateTextureThreads().
    // Cheap poll: does no disk I/O here. Schedules a single time-sliced purge
    // pass on the write pool when a purge is active, no pass is currently
    // queued/running, and the >=2s gap since the previous pass has elapsed
    // (mirroring LLTextureCache::purgeTextureFilesTimeSliced()). Returns
    // nonzero while purge work remains; the actual purge runs on the pool
    // thread, never on this frame thread.
    S32 update(F32 max_time_ms);

    // Shuts down the cache and drains any outstanding work cleanly.
    void shutdown();

    // Looks up (id, discard_level). Slices sub-buffer if discard_level > entry.mDiscardLevel.
    // The on-disk extension is derived from (expected_format, expected_role) when
    // expected_format is non-zero; otherwise every known color-role extension is probed
    // in turn (first hit wins). Format- and role-specific names mean a color .bc1 entry
    // can never satisfy a normal slot and vice versa. Updates the file's access time
    // with rate-limiting.
    bool readEntry(const LLUUID& id, S32 discard_level,
                   VayuBCCacheEntryHeader& header, std::vector<U8>& buffer,
                   const std::string& ext = ".bc",
                   U8 expected_format = 0,
                   U8 expected_role = kRoleColor);

    // Writes (or overwrites) the entry for (id, discard_level). Runs the actual
    // disk I/O on the cache's own single-writer thread pool and returns
    // immediately, so the producing worker thread never blocks on the disk.
    // Writes the per-format extension for header.mFormat (see formatExtension()).
    // FIFO order on the single writer preserves per-(id, discard) write order,
    // keeping the "finer entry wins" decision race-free. 'buffer' is kept alive
    // by the shared_ptr the caller hands in until the write completes.
    // If no write pool exists (not initialized, or already shut down) the write
    // is performed synchronously instead, so the cache stays usable after
    // shutdown() and writes are never silently dropped.
    void writeEntry(const LLUUID& id, S32 discard_level,
                    const VayuBCCacheEntryHeader& header,
                    std::shared_ptr<const std::vector<U8>> buffer);

    // Blocks until every queued/in-flight write has been committed to disk.
    // No-op if no write pool exists. Tests use this as a write barrier.
    void waitForPendingWrites();

    // Constructs a file path based on the asset UUID:
    // cache_dir / hex_subdir / <id><ext>
    std::string getFilePath(const LLUUID& id, const std::string& ext = ".bc") const;

    // Backward-compatible overload for legacy <id>_<discard><ext> paths
    std::string getFilePath(const LLUUID& id, S32 discard_level, const std::string& ext = ".bc") const;

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

    // Single-extension read body behind readEntry()'s format selection.
    bool readEntryExt(const LLUUID& id, S32 discard_level,
                      VayuBCCacheEntryHeader& header, std::vector<U8>& buffer,
                      const std::string& ext);

    U64 cacheDirSize();

    // Shared decision + temp-write + rename + size accounting. Called either
    // synchronously (no write pool) or from the single writer pool thread.
    void writeEntrySync(const LLUUID& id, S32 discard_level,
                        const VayuBCCacheEntryHeader& header,
                        const std::shared_ptr<const std::vector<U8>>& buffer);

    // Time-sliced purge machinery. Runs on the single writer pool thread for
    // the async purge path (see purgePassJob()), or on the calling thread via
    // purge()/the pre-pool fallback (test/startup paths). All state below is
    // owned by whichever thread is currently executing a purge; never
    // concurrent because the pass-queued flag gates re-entry.
    void purgePassJob();      // one ~0.1s time-sliced pass on the writer pool
    bool purgeBegin();        // returns false if there is nothing to purge
    bool purgeProgress();     // one hex subdir's scan or one file removal per call
    void purgeFinish();

    mutable std::mutex mMutex;
    std::string mCacheDir;
    U64 mNominalSizeBytes = 0;
    U64 mMaxSizeBytes = 0;
    std::atomic<U64> mCurrentSizeBytes{0};
    std::atomic<U64> mEntryCount{0};
    std::atomic<bool> mPurging{false};
    bool mCacheValid = false;

    // Time-sliced purge state. While a purge pass job is queued or running on
    // the writer pool thread, these are owned by the pool thread; otherwise
    // (no pool, e.g. shutdown/startup fallback) they're owned by the calling
    // thread of purge()/update(). Guards are atomics.
    typedef std::pair<time_t, std::pair<U64, std::string>> purge_file_info_t; // (mtime, (size, path))
    std::atomic<bool> mPurgeRequested{false};
    std::atomic<bool> mPurgePassQueued{false};   // a pass job is in flight or queued
    std::atomic<bool> mPurgeCancelPending{false};// clear()/shutdown() asked to stop
    U32 mPurgeScanSubdir = 0;
    bool mPurgeFilesScannedAll = false;
    std::vector<purge_file_info_t> mPurgeFiles;
    size_t mPurgeRemoveIndex = 0;
    U64 mPurgeAccumulatedSize = 0;
    U64 mPurgeRemovedBytes = 0;
    U32 mPurgeRemovedCount = 0;
    LLTimer mPurgeTimer;
    LLTimer mPurgeLastPassTimer;  // main-thread gate for the >=2s gap between passes

    std::unique_ptr<LL::ThreadPool> mWritePool;
    std::atomic<U32> mPendingWrites{0};
    std::mutex mWriteCVLock;
    std::condition_variable mWriteCV;
};
