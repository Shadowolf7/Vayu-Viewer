/**
 * @file vayubctexturecache.cpp
 * @brief On-disk cache for pre-encoded block-compressed texture mip chains.
 */

#include "linden_common.h"

#include "vayubctexturecache.h"
#include "threadpool.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>

namespace
{
    // Bump kFormatVersion (not kMagic) whenever VayuBCCacheEntryHeader's layout
    // changes, so old on-disk entries are treated as misses instead of being
    // misread as valid data.
    constexpr U32 kMagic = 0x31434256; // "VBC1"
    constexpr U32 kFormatVersion = 1;

    // Once eviction kicks in, cut a bit past the ceiling so a steady trickle
    // of writes right at the limit doesn't retrigger eviction on every write.
    constexpr double kEvictionHeadroomFraction = 0.10;

    struct FileHeader
    {
        U32 mMagic = kMagic;
        U32 mVersion = kFormatVersion;
        VayuBCCacheEntryHeader mMeta;
        U64 mBufferSize = 0;
    };
}

VayuBCTextureCache& VayuBCTextureCache::instance()
{
    static VayuBCTextureCache sInstance;
    return sInstance;
}

// Out-of-line so std::unique_ptr<LL::ThreadPool> only needs the complete
// type here, where threadpool.h is included, not in every includer of the
// header.
VayuBCTextureCache::~VayuBCTextureCache() = default;

std::string VayuBCTextureCache::entryKey(const LLUUID& id, S32 discard_level) const
{
    return id.asString() + "_" + std::to_string(discard_level);
}

std::filesystem::path VayuBCTextureCache::entryPath(const LLUUID& id, S32 discard_level) const
{
    return mCacheDir / (entryKey(id, discard_level) + ".bc");
}

void VayuBCTextureCache::initCache(const std::filesystem::path& cache_dir, S64 max_size_bytes)
{
    std::lock_guard<std::mutex> lock(mMutex);

    mMaxSize = max_size_bytes;

    if (mInitialized && mCacheDir == cache_dir)
    {
        return;
    }

    mCacheDir = cache_dir;
    mIndex.clear();
    mLruList.clear();
    mPendingWrites.clear();
    mPendingIndex.clear();
    mCurrentSize = 0;

    std::error_code ec;
    std::filesystem::create_directories(mCacheDir, ec);
    if (ec)
    {
        LL_WARNS("Texture") << "VayuBCTextureCache: failed to create " << mCacheDir.string()
                             << ": " << ec.message() << LL_ENDL;
        mInitialized = false;
        return;
    }

    // Scan existing entries and order them oldest-to-newest by on-disk mtime,
    // then rebuild the LRU list in that order so it starts from a sane
    // recency ordering (the filesystem clock is never consulted again after
    // this).
    std::vector<std::pair<std::filesystem::file_time_type, IndexEntry>> found;
    for (const auto& dirent : std::filesystem::directory_iterator(mCacheDir, ec))
    {
        if (ec || !dirent.is_regular_file() || dirent.path().extension() != ".bc")
            continue;

        std::error_code stat_ec;
        S64 size = (S64)std::filesystem::file_size(dirent.path(), stat_ec);
        if (stat_ec)
            continue;
        auto mtime = std::filesystem::last_write_time(dirent.path(), stat_ec);
        if (stat_ec)
            continue;

        IndexEntry entry;
        entry.mKey = dirent.path().stem().string();
        entry.mPath = dirent.path();
        entry.mFileSize = size;
        found.emplace_back(mtime, entry);
    }

    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Ascending mtime + push_front each => list ends up newest-at-front,
    // oldest-at-back, matching mLruList's eviction convention.
    for (auto& [mtime, entry] : found)
    {
        mLruList.push_front(entry);
        mIndex[entry.mKey] = mLruList.begin();
        mCurrentSize += entry.mFileSize;
    }

    mInitialized = true;

    if (!mWriterPool)
    {
        mWriterPool = std::make_unique<LL::ThreadPool>("BCCacheWriter", 1);
        mWriterPool->start();
    }
}

void VayuBCTextureCache::purge()
{
    std::lock_guard<std::mutex> lock(mMutex);

    for (const auto& entry : mLruList)
    {
        std::error_code ec;
        std::filesystem::remove(entry.mPath, ec);
    }
    mLruList.clear();
    mIndex.clear();
    mCurrentSize = 0;

    // Drop anything not yet flushed too - purge means the cache is empty
    // now, not "empty except whatever the writer pool hasn't gotten to
    // yet." A write mid-flush (key in mFlushing) isn't tracked here since
    // it's already off these lists; it'll finish writing its file and
    // become an untracked-but-harmless entry the next initCache() scan
    // picks back up, same as any other purge/writer-pool race.
    mPendingWrites.clear();
    mPendingIndex.clear();
}

bool VayuBCTextureCache::readEntry(const LLUUID& id, S32 discard_level, U8 min_preset,
                                 VayuBCCacheEntryHeader& header, std::vector<U8>& buffer)
{
    std::lock_guard<std::mutex> lock(mMutex);

    const std::string key = entryKey(id, discard_level);
    auto it = mIndex.find(key);
    if (it == mIndex.end())
        return false;

    auto pending_it = mPendingIndex.find(key);
    if (pending_it != mPendingIndex.end())
    {
        // Not yet handed to the writer pool - serve straight from the
        // queued bytes rather than touching a disk file that may not exist
        // yet at all.
        const std::vector<U8>& bytes = pending_it->second->mFileBytes;
        if (bytes.size() < sizeof(FileHeader))
            return false;

        FileHeader file_header;
        memcpy(&file_header, bytes.data(), sizeof(file_header));
        if (file_header.mMeta.mPreset < min_preset)
            return false;

        buffer.assign(bytes.begin() + sizeof(file_header), bytes.end());
        header = file_header.mMeta;
        touch(it->second);
        return true;
    }

    if (mFlushing.count(key))
    {
        // Currently being written by the writer pool - the file on disk may
        // be truncated/partial right now. Plain temporary miss: don't touch
        // the index or the file, the flush completes on its own regardless
        // and the caller just re-encodes this one pass.
        return false;
    }

    std::ifstream in(it->second->mPath, std::ios::binary);
    if (!in.good())
        return false;

    FileHeader file_header;
    in.read(reinterpret_cast<char*>(&file_header), sizeof(file_header));
    if (!in.good() || file_header.mMagic != kMagic || file_header.mVersion != kFormatVersion)
    {
        // Stale or corrupt entry - reclaim the space rather than leaving dead weight.
        in.close();
        removeEntry(key);
        return false;
    }

    if (file_header.mMeta.mPreset < min_preset)
    {
        // Valid entry, just not made at the quality the caller wants right
        // now - leave it on disk, this is a miss the caller will overwrite.
        return false;
    }

    buffer.resize((size_t)file_header.mBufferSize);
    in.read(reinterpret_cast<char*>(buffer.data()), (std::streamsize)file_header.mBufferSize);
    if (!in.good() && !in.eof())
        return false;

    header = file_header.mMeta;
    touch(it->second);

    return true;
}

void VayuBCTextureCache::writeEntry(const LLUUID& id, S32 discard_level,
                                  const VayuBCCacheEntryHeader& header, const std::vector<U8>& buffer)
{
    // Serialize before taking the lock - this can be a multi-megabyte copy
    // for a large mip chain, and none of it touches shared state.
    FileHeader file_header;
    file_header.mMeta = header;
    file_header.mBufferSize = buffer.size();

    std::vector<U8> file_bytes(sizeof(file_header) + buffer.size());
    memcpy(file_bytes.data(), &file_header, sizeof(file_header));
    memcpy(file_bytes.data() + sizeof(file_header), buffer.data(), buffer.size());

    std::lock_guard<std::mutex> lock(mMutex);

    if (!mInitialized)
        return;

    const std::string key = entryKey(id, discard_level);
    const std::filesystem::path path = entryPath(id, discard_level);
    const S64 new_size = (S64)file_bytes.size();

    auto prior = mIndex.find(key);
    if (prior != mIndex.end())
    {
        mCurrentSize -= prior->second->mFileSize;
        prior->second->mFileSize = new_size;
        touch(prior->second);
    }
    else
    {
        IndexEntry entry;
        entry.mKey = key;
        entry.mPath = path;
        entry.mFileSize = new_size;
        mLruList.push_front(entry);
        mIndex[key] = mLruList.begin();
    }
    mCurrentSize += new_size;

    // Disk I/O happens off this thread now - see queuePendingWrite()/
    // flushOneEntry() below.
    queuePendingWrite(key, path, std::move(file_bytes));

    evictUntilWithinBudget();
}

void VayuBCTextureCache::queuePendingWrite(const std::string& key, const std::filesystem::path& path,
                                           std::vector<U8>&& file_bytes)
{
    auto it = mPendingIndex.find(key);
    if (it != mPendingIndex.end())
    {
        // Coalesce: overwrite the still-unflushed bytes in place. The flush
        // task already posted for this key reads whatever's current when it
        // actually runs, so there's nothing new to post.
        it->second->mFileBytes = std::move(file_bytes);
        return;
    }

    PendingWrite pw;
    pw.mKey = key;
    pw.mPath = path;
    pw.mFileBytes = std::move(file_bytes);
    mPendingWrites.push_back(std::move(pw));
    mPendingIndex[key] = std::prev(mPendingWrites.end());

    mWriterPool->getQueue().post([this] { flushOneEntry(); });
}

void VayuBCTextureCache::flushOneEntry()
{
    PendingList local;
    std::string key;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mPendingWrites.empty())
            return;

        // Splice - don't copy - the front node into our own local list.
        // Once it's off mPendingWrites/mPendingIndex, no other thread can
        // reach it, so reading mFileBytes below without the lock is safe:
        // there's nothing left to race against.
        local.splice(local.begin(), mPendingWrites, mPendingWrites.begin());
        key = local.front().mKey;
        mPendingIndex.erase(key);
        mFlushing.insert(key);
    }

    std::ofstream out(local.front().mPath, std::ios::binary | std::ios::trunc);
    if (out.good())
    {
        const auto& bytes = local.front().mFileBytes;
        out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
    }

    std::lock_guard<std::mutex> lock(mMutex);
    mFlushing.erase(key);
}

void VayuBCTextureCache::touch(LruList::iterator it)
{
    mLruList.splice(mLruList.begin(), mLruList, it);
}

void VayuBCTextureCache::removeEntry(const std::string& key)
{
    auto it = mIndex.find(key);
    if (it == mIndex.end())
        return;

    // A write currently mid-flush (see flushOneEntry()) still has this path
    // open for writing - don't fight it for the file. On Windows, deleting
    // a file another thread has open for writing fails outright (POSIX
    // tolerates it, unlinking without disturbing the open handle); either
    // way it just becomes an untracked file the next initCache() scan picks
    // back up, same tolerance as any other writer-pool race here.
    if (!mFlushing.count(key))
    {
        std::error_code ec;
        std::filesystem::remove(it->second->mPath, ec);
    }
    mCurrentSize -= it->second->mFileSize;
    mLruList.erase(it->second);
    mIndex.erase(it);

    // Cancel a still-queued write for this key too - no file exists yet
    // for it (or if it does, we just removed it above), so there's nothing
    // to flush. A write already mid-flight (key in mFlushing) isn't
    // reachable here anymore; see flushOneEntry()'s comment.
    auto pending_it = mPendingIndex.find(key);
    if (pending_it != mPendingIndex.end())
    {
        mPendingWrites.erase(pending_it->second);
        mPendingIndex.erase(pending_it);
    }
}

void VayuBCTextureCache::evictUntilWithinBudget()
{
    if (mCurrentSize <= mMaxSize)
        return;

    // Cut a bit past the ceiling (10% headroom) so a steady trickle of
    // writes right at the limit doesn't retrigger eviction on every write.
    const S64 target_size = (S64)(mMaxSize * (1.0 - kEvictionHeadroomFraction));

    while (mCurrentSize > target_size && !mLruList.empty())
    {
        removeEntry(mLruList.back().mKey);
    }
}

S64 VayuBCTextureCache::getCurrentSize() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mCurrentSize;
}

size_t VayuBCTextureCache::getEntryCount() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mIndex.size();
}
