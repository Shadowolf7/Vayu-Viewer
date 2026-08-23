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

class VayuBCTextureCache
{
public:
    static VayuBCTextureCache& instance();
    ~VayuBCTextureCache();

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

    // Joins the writer pool's thread at a controlled point in app shutdown.
    // Must be called (from LLAppViewer::cleanup(), after the ImageDecode
    // thread pool that feeds writeEntry() is already torn down) rather than
    // relying on this object's own destructor: as a function-local static,
    // that destructor only runs whenever the C++ runtime's exit()-time
    // static-destructor chain happens to reach it, by which point other
    // global state ThreadPool::close() touches (LLEventPumps, the fiber
    // scheduler behind LL::WorkQueue) may already be gone - confirmed via a
    // live gdb backtrace to be fatal (std::terminate from an exception
    // escaping ~ThreadPoolBase() during exit()). Safe to call more than
    // once; idempotent no-op once mWriterPool is already null.
    void shutdown();

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

    // A write queued for the background writer pool. Fully serialized
    // (FileHeader + buffer, ready to write as-is) so the flush itself is a
    // single sequential write with no further assembly.
    struct PendingWrite
    {
        std::string mKey;
        std::filesystem::path mPath;
        std::vector<U8> mFileBytes;
    };
    using PendingList = std::list<PendingWrite>;

    std::filesystem::path entryPath(const LLUUID& id, S32 discard_level) const;
    std::string entryKey(const LLUUID& id, S32 discard_level) const;
    void touch(LruList::iterator it);
    void evictUntilWithinBudget();
    void removeEntry(const std::string& key);

    // Queues (or coalesces into an existing queued write for the same key)
    // a write for the background pool. Returns true exactly when this call
    // flipped mDraining from false to true - i.e. when the caller is
    // responsible for handing the writer pool its one outstanding wakeup
    // ticket by posting drainPendingWrites(). With a single writer thread
    // there only ever needs to be one ticket in flight no matter how deep
    // the backlog gets; drainPendingWrites() loops internally to work
    // through whatever's queued once it starts. Called with mMutex already
    // held.
    bool queuePendingWrite(const std::string& key, const std::filesystem::path& path,
                           std::vector<U8>&& file_bytes);

    // Runs on mWriterPool's single thread, once per false->true transition
    // of mDraining (see queuePendingWrite()). Loops until mPendingWrites is
    // empty: each iteration pops (splices) one entry off the front, writes
    // it to disk with no lock held, then re-locks briefly to clear it from
    // mFlushing before checking for more. Splicing - not copying - out from
    // under the lock is what makes the unlocked disk write safe: once a
    // node is off mPendingWrites/mPendingIndex, no other thread can reach
    // it, so there's nothing left to race against. The loop's final
    // iteration clears mDraining in the SAME lock acquisition that finds
    // mPendingWrites empty - splitting that into two acquisitions would let
    // a write queued into the gap get stranded with nobody left to flush it.
    void drainPendingWrites();

    mutable std::mutex mMutex;
    std::filesystem::path mCacheDir;
    S64 mMaxSize = 0;
    // Logical size: bytes reserved by queued-or-cached entries, not bytes
    // physically on disk right now. A just-queued write counts toward this
    // (and toward eviction) immediately, before the writer pool has
    // actually flushed it - budget accounting has to work off what the
    // cache will look like once it catches up, not its transient disk state.
    S64 mCurrentSize = 0;
    bool mInitialized = false;

    LruList mLruList;
    std::unordered_map<std::string, LruList::iterator> mIndex;

    // Writes not yet flushed to disk (readEntry() serves these straight out
    // of memory) plus keys mid-flush (spliced out of the above, being
    // written right now - readEntry() treats these as a plain temporary
    // miss instead of reading a possibly-truncated file).
    PendingList mPendingWrites;
    std::unordered_map<std::string, PendingList::iterator> mPendingIndex;
    std::unordered_set<std::string> mFlushing;

    // True while a drainPendingWrites() pass is queued-or-running on the
    // writer pool. The single source of truth for "is there currently
    // exactly one outstanding wakeup ticket (or an active drain loop that
    // will notice new work without needing one)". Set true only by
    // queuePendingWrite()'s false->true transition (which is also the only
    // time it posts); cleared only by drainPendingWrites() itself, in the
    // same critical section where it confirms mPendingWrites is empty.
    // purge() deliberately does not touch this: if it empties
    // mPendingWrites out from under an in-flight drain pass, that pass will
    // simply notice on its next loop iteration and clear the flag itself.
    bool mDraining = false;

    // Declared last so it's destroyed first: ~ThreadPool() joins the writer
    // thread, which must happen before mMutex/mPendingWrites/mIndex/
    // mDraining above are torn down, since drainPendingWrites() touches all
    // of them.
    std::unique_ptr<LL::ThreadPool> mWriterPool;
};
