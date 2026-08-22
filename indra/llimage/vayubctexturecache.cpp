/**
 * @file vayubctexturecache.cpp
 * @brief On-disk cache for pre-encoded block-compressed texture mip chains.
 */

#include "linden_common.h"

#include "vayubctexturecache.h"

#include <algorithm>
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
}

bool VayuBCTextureCache::readEntry(const LLUUID& id, S32 discard_level, U8 min_preset,
                                 VayuBCCacheEntryHeader& header, std::vector<U8>& buffer)
{
    std::lock_guard<std::mutex> lock(mMutex);

    const std::string key = entryKey(id, discard_level);
    auto it = mIndex.find(key);
    if (it == mIndex.end())
        return false;

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
    std::lock_guard<std::mutex> lock(mMutex);

    if (!mInitialized)
        return;

    const std::string key = entryKey(id, discard_level);
    const std::filesystem::path path = entryPath(id, discard_level);

    FileHeader file_header;
    file_header.mMeta = header;
    file_header.mBufferSize = buffer.size();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.good())
        return;

    out.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
    out.write(reinterpret_cast<const char*>(buffer.data()), (std::streamsize)buffer.size());
    out.close();

    const S64 new_size = (S64)(sizeof(file_header) + buffer.size());

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

    evictUntilWithinBudget();
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

    std::error_code ec;
    std::filesystem::remove(it->second->mPath, ec);
    mCurrentSize -= it->second->mFileSize;
    mLruList.erase(it->second);
    mIndex.erase(it);
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
