/**
 * @file llbctexturecache.cpp
 * @brief On-disk cache for pre-encoded block-compressed texture mip chains.
 */

#include "linden_common.h"

#include "llbctexturecache.h"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace
{
    // Bump kFormatVersion (not kMagic) whenever LLBCCacheEntryHeader's layout
    // changes, so old on-disk entries are treated as misses instead of being
    // misread as valid data.
    constexpr U32 kMagic = 0x31434256; // "VBC1"
    constexpr U32 kFormatVersion = 1;

    struct FileHeader
    {
        U32 mMagic = kMagic;
        U32 mVersion = kFormatVersion;
        LLBCCacheEntryHeader mMeta;
        U64 mBufferSize = 0;
    };
}

LLBCTextureCache& LLBCTextureCache::instance()
{
    static LLBCTextureCache sInstance;
    return sInstance;
}

std::string LLBCTextureCache::entryKey(const LLUUID& id, S32 discard_level) const
{
    return id.asString() + "_" + std::to_string(discard_level);
}

std::filesystem::path LLBCTextureCache::entryPath(const LLUUID& id, S32 discard_level) const
{
    return mCacheDir / (entryKey(id, discard_level) + ".bc");
}

void LLBCTextureCache::initCache(const std::filesystem::path& cache_dir, S64 max_size_bytes)
{
    std::lock_guard<std::mutex> lock(mMutex);

    mMaxSize = max_size_bytes;

    if (mInitialized && mCacheDir == cache_dir)
    {
        return;
    }

    mCacheDir = cache_dir;
    mIndex.clear();
    mCurrentSize = 0;

    std::error_code ec;
    std::filesystem::create_directories(mCacheDir, ec);
    if (ec)
    {
        LL_WARNS("Texture") << "LLBCTextureCache: failed to create " << mCacheDir.string()
                             << ": " << ec.message() << LL_ENDL;
        mInitialized = false;
        return;
    }

    // Scan existing entries and order them oldest-to-newest by on-disk mtime so
    // the in-process recency counter (used for all eviction decisions from
    // here on, never the filesystem clock again) starts from a sane ordering.
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
        entry.mPath = dirent.path();
        entry.mFileSize = size;
        found.emplace_back(mtime, entry);
    }

    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (auto& [mtime, entry] : found)
    {
        entry.mRecency = ++mNextRecency;
        mIndex[entry.mPath.stem().string()] = entry;
        mCurrentSize += entry.mFileSize;
    }

    mInitialized = true;
}

void LLBCTextureCache::purge()
{
    std::lock_guard<std::mutex> lock(mMutex);

    for (const auto& [key, entry] : mIndex)
    {
        std::error_code ec;
        std::filesystem::remove(entry.mPath, ec);
    }
    mIndex.clear();
    mCurrentSize = 0;
}

bool LLBCTextureCache::readEntry(const LLUUID& id, S32 discard_level, U8 min_preset,
                                 LLBCCacheEntryHeader& header, std::vector<U8>& buffer)
{
    std::lock_guard<std::mutex> lock(mMutex);

    const std::string key = entryKey(id, discard_level);
    auto it = mIndex.find(key);
    if (it == mIndex.end())
        return false;

    std::ifstream in(it->second.mPath, std::ios::binary);
    if (!in.good())
        return false;

    FileHeader file_header;
    in.read(reinterpret_cast<char*>(&file_header), sizeof(file_header));
    if (!in.good() || file_header.mMagic != kMagic || file_header.mVersion != kFormatVersion)
    {
        // Stale or corrupt entry - reclaim the space rather than leaving dead weight.
        in.close();
        removeEntry(key, it->second);
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
    it->second.mRecency = ++mNextRecency;

    return true;
}

void LLBCTextureCache::writeEntry(const LLUUID& id, S32 discard_level,
                                  const LLBCCacheEntryHeader& header, const std::vector<U8>& buffer)
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

    auto prior = mIndex.find(key);
    if (prior != mIndex.end())
    {
        mCurrentSize -= prior->second.mFileSize;
    }

    IndexEntry entry;
    entry.mPath = path;
    entry.mFileSize = (S64)(sizeof(file_header) + buffer.size());
    entry.mRecency = ++mNextRecency;
    mIndex[key] = entry;
    mCurrentSize += entry.mFileSize;

    evictUntilWithinBudget();
}

void LLBCTextureCache::removeEntry(const std::string& key, const IndexEntry& entry)
{
    std::error_code ec;
    std::filesystem::remove(entry.mPath, ec);
    mCurrentSize -= entry.mFileSize;
    mIndex.erase(key);
}

void LLBCTextureCache::evictUntilWithinBudget()
{
    while (mCurrentSize > mMaxSize && !mIndex.empty())
    {
        // O(n) oldest-entry scan per eviction: fine at expected entry counts
        // (thousands, not millions); revisit if that stops being true.
        std::string oldest_key;
        U64 oldest_recency = UINT64_MAX;

        for (const auto& [key, entry] : mIndex)
        {
            if (entry.mRecency < oldest_recency)
            {
                oldest_recency = entry.mRecency;
                oldest_key = key;
            }
        }

        removeEntry(oldest_key, mIndex[oldest_key]);
    }
}

S64 LLBCTextureCache::getCurrentSize() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mCurrentSize;
}

size_t LLBCTextureCache::getEntryCount() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mIndex.size();
}
