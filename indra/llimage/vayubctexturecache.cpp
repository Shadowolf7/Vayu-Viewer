/**
 * @file vayubctexturecache.cpp
 * @brief On-disk cache for pre-encoded block-compressed texture mip chains.
 */

#include "linden_common.h"

#include "vayubctexturecache.h"
#include "boost/filesystem.hpp"
#include "llapp.h"
#include "lldiriterator.h"
#include "llfile.h"
#include "llrand.h"
#include "llthread.h"
#include "lltimer.h"
#include "llprofiler.h"
#include "threadpool.h"

#include <fmt/format.h>
#include <algorithm>
#include <cstring>
#include <fstream>

constexpr char LL_DIR_DELIM_CHR = std::filesystem::path::preferred_separator;
constexpr const char* LL_DIR_DELIM_STR = (LL_DIR_DELIM_CHR == '\\') ? "\\" : "/";
static const std::string sDigits = "0123456789abcdef";

constexpr time_t TIME_THRESHOLD = 1800;
constexpr time_t TIME_THRESHOLD_PURGE = 60;

namespace
{
    constexpr U32 kMagic = 0x31434256; // "VBC1"
    constexpr U32 kFormatVersion = 1;
    constexpr F64 kDropLogIntervalSeconds = 10.0;

    struct FileHeader
    {
        U32 mMagic = kMagic;
        U32 mVersion = kFormatVersion;
        VayuBCCacheEntryHeader mMeta;
        U64 mBufferSize = 0;
    };
}

class VayuBCCachePurgeThread final : public LLThread
{
public:
    inline VayuBCCachePurgeThread()
    :   LLThread("BC cache purging thread")
    {
        start();
    }

    void run() override
    {
        VayuBCTextureCache::instance().purge();
    }
};

VayuBCTextureCache& VayuBCTextureCache::instance()
{
    static VayuBCTextureCache sInstance;
    return sInstance;
}

VayuBCTextureCache::~VayuBCTextureCache() = default;

std::string VayuBCTextureCache::entryKey(const LLUUID& id, S32 discard_level) const
{
    return id.asString() + "_" + std::to_string(discard_level);
}

std::string VayuBCTextureCache::getFilePath(const LLUUID& id, S32 discard_level) const
{
    std::string filename = entryKey(id, discard_level) + ".bc";
    return ((mCacheDir + filename[0]) + LL_DIR_DELIM_STR) + filename;
}

void VayuBCTextureCache::initCache(const std::filesystem::path& cache_dir, S64 max_size_bytes,
                                   S64 max_pending_bytes, bool second_instance)
{
    std::lock_guard<std::mutex> lock(mMutex);

    mNominalSizeBytes = (U64)max_size_bytes;
    mMaxSizeBytes = 15UL * mNominalSizeBytes / 10UL;
    if (second_instance)
    {
        mMaxSizeBytes += (50UL + 5UL * U64(ll_frand(20.f))) * 1048576UL;
    }
    mMaxPendingBytes = max_pending_bytes;

    std::string cache_dir_str = cache_dir.string();
    if (!cache_dir_str.empty() && cache_dir_str.back() != LL_DIR_DELIM_CHR)
    {
        cache_dir_str += LL_DIR_DELIM_CHR;
    }

    if (mCacheValid && mCacheDir == cache_dir_str)
    {
        return;
    }

    mCacheDir = cache_dir_str;
    mPendingWrites.clear();
    mPendingIndex.clear();
    mPendingBytes = 0;
    mCurrentSizeBytes = 0;
    mEntryCount = 0;

    mCacheValid = (LLFile::mkdir(mCacheDir) == 0);
    if (mCacheValid)
    {
        for (U32 i = 0; i < 16; ++i)
        {
            mCacheValid &= (LLFile::mkdir(mCacheDir + sDigits[i]) == 0);
        }
    }

    if (!mCacheValid)
    {
        LL_WARNS("Texture") << "VayuBCTextureCache: failed to create cache directory: " << mCacheDir << LL_ENDL;
        return;
    }

    // Migration: if any legacy loose .bc files exist directly in mCacheDir root, move them into subdirectories
    if (LLFile::isdir(mCacheDir))
    {
        LLDirIterator iter(mCacheDir, NULL, DI_ISFILE);
        std::string loose_file;
        while (iter.next(loose_file))
        {
            if (loose_file.size() > 3 && loose_file.compare(loose_file.size() - 3, 3, ".bc") == 0)
            {
                char hex = loose_file[0];
                if ((hex >= '0' && hex <= '9') || (hex >= 'a' && hex <= 'f'))
                {
                    std::string src = mCacheDir + loose_file;
                    std::string dst = mCacheDir + hex + LL_DIR_DELIM_STR + loose_file;
                    LLFile::rename(src, dst);
                }
            }
        }
    }

#if LL_WINDOWS
    if (!second_instance)
    {
        LL_INFOS("Texture") << "VayuBCTextureCache: nominal size: " << mNominalSizeBytes
                            << " bytes. Max size: " << mMaxSizeBytes
                            << " bytes. Cache directory: " << mCacheDir << LL_ENDL;
        if (!mWriterPool)
        {
            mWriterPool = std::make_unique<LL::ThreadPool>("BCCacheWriter", 1);
            mWriterPool->start();
        }
        return;
    }
#endif

    mCurrentSizeBytes = cacheDirSize();
    LL_INFOS("Texture") << "VayuBCTextureCache: nominal size: " << mNominalSizeBytes
                        << " bytes. Max size: " << mMaxSizeBytes
                        << " bytes. Current size: " << mCurrentSizeBytes.load()
                        << " bytes (" << mEntryCount.load() << " entries). Cache dir: "
                        << mCacheDir << LL_ENDL;

    if (!mWriterPool)
    {
        mWriterPool = std::make_unique<LL::ThreadPool>("BCCacheWriter", 1);
        mWriterPool->start();
    }
}

U64 VayuBCTextureCache::cacheDirSize()
{
    U64 total_file_size = 0;
    U64 total_entries = 0;
    std::string subdir, filename;
    for (U32 i = 0; i < 16; ++i)
    {
        subdir = mCacheDir + sDigits[i];
        if (LLFile::isdir(subdir))
        {
            LLDirIterator iter(subdir, NULL, DI_SIZE);
            while (iter.next(filename))
            {
                total_file_size += iter.getSize();
                ++total_entries;
            }
        }
    }
    mEntryCount = total_entries;
    return total_file_size;
}

void VayuBCTextureCache::clear()
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mPendingWrites.clear();
        mPendingIndex.clear();
        mFlushing.clear();
        mPendingBytes = 0;
    }

    if (LLFile::isdir(mCacheDir))
    {
        std::string subdir;
        for (U32 i = 0; i < 16; ++i)
        {
            subdir = mCacheDir + sDigits[i];
            if (LLFile::isdir(subdir))
            {
                LLDirIterator::deleteFilesInDir(subdir);
            }
        }
    }
    mCurrentSizeBytes = 0;
    mEntryCount = 0;
}

void VayuBCTextureCache::purge()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    if (!LLFile::isdir(mCacheDir))
    {
        LL_INFOS("Texture") << "VayuBCTextureCache: no cache directory: nothing to purge." << LL_ENDL;
        return;
    }

    mPurging = true;

    typedef std::pair<time_t, std::pair<U64, std::string>> file_info_t;
    std::vector<file_info_t> file_info;

    LLTimer purge_timer;
    purge_timer.reset();

    std::string subdir, filename;
    for (U32 i = 0; i < 16; ++i)
    {
        if (LLApp::isQuitting())
        {
            mPurging = false;
            return;
        }

        subdir = mCacheDir + sDigits[i];
        if (!LLFile::isdir(subdir))
        {
            continue;
        }
        LLDirIterator iter(subdir, NULL, DI_ISFILE | DI_SIZE | DI_TIMESTAMP);
        while (iter.next(filename))
        {
            if (iter.isFile())
            {
                file_info.emplace_back(iter.getTimeStamp(),
                                       std::make_pair(iter.getSize(),
                                                      iter.getPath() + filename));
            }
        }
    }

    std::sort(file_info.begin(), file_info.end(),
              [](const file_info_t& x, const file_info_t& y)
              {
                  return x.first > y.first;
              });

    U32 count = file_info.size();
    LL_INFOS("Texture") << "VayuBCTextureCache: " << count
                        << " files found in cache. Checking total size and purging old files..."
                        << LL_ENDL;

    U64 files_size_total = 0;
    U64 removed_bytes = 0;
    U32 purged_files = 0;
    for (U32 i = 0; i < count; ++i)
    {
        if (LLApp::isQuitting())
        {
            break;
        }

        const file_info_t& entry = file_info[i];
        files_size_total += entry.second.first;
        bool removed = files_size_total > mNominalSizeBytes;
        if (removed)
        {
            try
            {
                if (boost::filesystem::last_write_time(entry.second.second) <= entry.first)
                {
                    boost::filesystem::remove(entry.second.second);
                    ++purged_files;
                    removed_bytes += entry.second.first;
                }
                else
                {
                    removed = false;
                }
            }
            catch (const boost::filesystem::filesystem_error& e)
            {
                removed = false;
                LL_WARNS("Texture") << "VayuBCTextureCache: failure to remove \"" << entry.second.second
                                    << "\". Reason: " << e.what() << LL_ENDL;
            }
        }
    }

    mPurging = false;
    mCurrentSizeBytes = files_size_total - removed_bytes;
    mEntryCount = count - purged_files;

    U32 ms = (U32)(purge_timer.getElapsedTimeF32() * 1000.f);
    if (purged_files)
    {
        LL_INFOS("Texture") << "VayuBCTextureCache: cache purge took " << ms << "ms to execute. "
                            << purged_files << " purged files and " << removed_bytes
                            << " bytes removed. " << mCurrentSizeBytes.load()
                            << " bytes now in cache." << LL_ENDL;
    }
    else
    {
        LL_INFOS("Texture") << "VayuBCTextureCache: cache check took " << ms << "ms. Cache size: "
                            << mCurrentSizeBytes.load() << " bytes." << LL_ENDL;
    }
}

void VayuBCTextureCache::threadedPurge()
{
    if (!mCacheValid)
    {
        return;
    }

    if (mPurgeThread)
    {
        if (mPurgeThread->isStopped())
        {
            delete mPurgeThread;
            mPurgeThread = nullptr;
        }
        else
        {
            return;
        }
    }

    mPurgeThread = new VayuBCCachePurgeThread;
}

void VayuBCTextureCache::shutdown()
{
    mCacheValid = false;

    if (mPurgeThread)
    {
        U32 loops = 0;
        while (loops++ < 100 && !mPurgeThread->isStopped())
        {
            ms_sleep(10);
        }
        delete mPurgeThread;
        mPurgeThread = nullptr;
        mPurging = false;
    }

    mWriterPool.reset();

    std::lock_guard<std::mutex> lock(mMutex);
    mDraining = false;
}

void VayuBCTextureCache::updateFileAccessTime(const std::string& filename)
{
    const time_t cur_time = time(NULL);
    llstat st;
    time_t last_write = 0;
    if (LLFile::stat(filename, &st) == 0)
    {
        last_write = st.st_mtime;
    }

    time_t threshold = mPurging ? TIME_THRESHOLD_PURGE : TIME_THRESHOLD;
    if (cur_time - last_write > threshold)
    {
        boost::system::error_code ec;
#if LL_WINDOWS
        boost::filesystem::last_write_time(ll_convert_string_to_wide(filename), cur_time, ec);
#else
        boost::filesystem::last_write_time(filename, cur_time, ec);
#endif
        if (ec.failed())
        {
            LL_WARNS("Texture") << "VayuBCTextureCache: failure to touch \"" << filename
                                << "\". Reason: " << ec.message() << LL_ENDL;
        }
    }
}

void VayuBCTextureCache::addBytesWritten(S64 bytes)
{
    if (bytes >= 0)
    {
        mCurrentSizeBytes += (U64)bytes;
    }
    else
    {
        U64 delta = (U64)(-bytes);
        U64 current = mCurrentSizeBytes.load();
        while (current > 0)
        {
            U64 target = (current > delta) ? (current - delta) : 0;
            if (mCurrentSizeBytes.compare_exchange_weak(current, target))
            {
                break;
            }
        }
    }

    if (mPurging)
    {
        return;
    }

    if (mCurrentSizeBytes.load() > mMaxSizeBytes)
    {
        threadedPurge();
    }
}

bool VayuBCTextureCache::readEntry(const LLUUID& id, S32 discard_level, U8 min_preset,
                                   VayuBCCacheEntryHeader& header, std::vector<U8>& buffer)
{
    const std::string key = entryKey(id, discard_level);

    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto pending_it = mPendingIndex.find(key);
        if (pending_it != mPendingIndex.end())
        {
            const PendingWrite& pending = *pending_it->second;
            if (pending.mMeta.mPreset < min_preset)
                return false;

            buffer = *pending.mBuffer;
            header = pending.mMeta;
            return true;
        }

        if (mFlushing.count(key))
        {
            return false;
        }
    }

    std::string file_path = getFilePath(id, discard_level);
    if (!LLFile::isfile(file_path))
    {
        return false;
    }

    std::ifstream in(file_path, std::ios::binary);
    if (!in.good())
        return false;

    FileHeader file_header;
    in.read(reinterpret_cast<char*>(&file_header), sizeof(file_header));
    if (!in.good() || file_header.mMagic != kMagic || file_header.mVersion != kFormatVersion)
    {
        in.close();
        llstat st;
        if (LLFile::stat(file_path, &st) == 0)
        {
            addBytesWritten(-st.st_size);
            if (mEntryCount > 0) --mEntryCount;
        }
        LLFile::remove(file_path);
        return false;
    }

    if (file_header.mMeta.mPreset < min_preset)
    {
        return false;
    }

    buffer.resize((size_t)file_header.mBufferSize);
    in.read(reinterpret_cast<char*>(buffer.data()), (std::streamsize)file_header.mBufferSize);
    if (!in.good() && !in.eof())
        return false;

    header = file_header.mMeta;
    updateFileAccessTime(file_path);

    return true;
}

void VayuBCTextureCache::writeEntry(const LLUUID& id, S32 discard_level,
                                    const VayuBCCacheEntryHeader& header,
                                    std::shared_ptr<const std::vector<U8>> buffer)
{
    if (!buffer || !mCacheValid)
        return;

    const S64 new_size = (S64)(sizeof(FileHeader) + buffer->size());
    const std::string key = entryKey(id, discard_level);
    const std::string path = getFilePath(id, discard_level);

    bool needs_post = false;
    bool log_drops = false;
    S64 dropped_writes = 0;
    S64 dropped_bytes = 0;
    S64 max_pending = 0;

    {
        std::lock_guard<std::mutex> lock(mMutex);

        needs_post = queuePendingWrite(key, path, header, std::move(buffer), new_size);
        trimPendingBacklog(&log_drops);

        if (log_drops)
        {
            dropped_writes = mDroppedWrites;
            dropped_bytes = mDroppedBytes;
            max_pending = mMaxPendingBytes;
        }
    }

    if (log_drops)
    {
        LL_WARNS("Texture") << "VayuBCTextureCache: pending-write backlog hit its "
            << (max_pending / (1024 * 1024)) << " MB ceiling; dropping oldest queued writes. "
            << dropped_writes << " dropped this session (" << (dropped_bytes / (1024 * 1024))
            << " MB). Each drop costs a re-encode later, not correctness. If this is frequent "
            << "and there's memory to spare, raise VayuBCTextureCacheMaxPendingSize." << LL_ENDL;
    }

    if (needs_post && mWriterPool)
    {
        mWriterPool->getQueue().post([this] { drainPendingWrites(); });
    }
}

bool VayuBCTextureCache::queuePendingWrite(const std::string& key, const std::string& path,
                                           const VayuBCCacheEntryHeader& header,
                                           std::shared_ptr<const std::vector<U8>>&& buffer,
                                           S64 file_size)
{
    auto it = mPendingIndex.find(key);
    if (it != mPendingIndex.end())
    {
        mPendingBytes -= it->second->mFileSize;
        it->second->mMeta = header;
        it->second->mBuffer = std::move(buffer);
        it->second->mFileSize = file_size;
        mPendingBytes += file_size;
        return false;
    }

    PendingWrite pw;
    pw.mKey = key;
    pw.mPath = path;
    pw.mMeta = header;
    pw.mBuffer = std::move(buffer);
    pw.mFileSize = file_size;
    mPendingWrites.push_back(std::move(pw));
    mPendingIndex[key] = std::prev(mPendingWrites.end());
    mPendingBytes += file_size;

    if (mDraining)
    {
        return false;
    }

    mDraining = true;
    return true;
}

void VayuBCTextureCache::trimPendingBacklog(bool* should_log_drops)
{
    const S64 dropped_before = mDroppedWrites;

    while (mPendingBytes > mMaxPendingBytes && mPendingWrites.size() > 1)
    {
        const std::string key = mPendingWrites.front().mKey;
        const S64 dropped_size = mPendingWrites.front().mFileSize;

        mPendingBytes -= dropped_size;
        mPendingIndex.erase(key);
        mPendingWrites.pop_front();

        ++mDroppedWrites;
        mDroppedBytes += dropped_size;
    }

    if (should_log_drops)
    {
        *should_log_drops = false;
        if (mDroppedWrites > dropped_before)
        {
            const F64 now = LLTimer::getElapsedSeconds().value();
            if (now - mLastDropLogTime >= kDropLogIntervalSeconds)
            {
                mLastDropLogTime = now;
                *should_log_drops = true;
            }
        }
    }
}

void VayuBCTextureCache::drainPendingWrites()
{
    for (;;)
    {
        PendingList local;
        std::string key;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mPendingWrites.empty())
            {
                mDraining = false;
                return;
            }

            local.splice(local.begin(), mPendingWrites, mPendingWrites.begin());
            key = local.front().mKey;
            mPendingIndex.erase(key);
            mPendingBytes -= local.front().mFileSize;
            mFlushing.insert(key);
        }

        const PendingWrite& pending = local.front();
        S64 old_file_size = 0;
        llstat st;
        if (LLFile::stat(pending.mPath, &st) == 0)
        {
            old_file_size = st.st_size;
        }

        std::ofstream out(pending.mPath, std::ios::binary | std::ios::trunc);
        if (out.good())
        {
            FileHeader file_header;
            file_header.mMeta = pending.mMeta;
            file_header.mBufferSize = pending.mBuffer->size();

            out.write(reinterpret_cast<const char*>(&file_header), (std::streamsize)sizeof(file_header));
            out.write(reinterpret_cast<const char*>(pending.mBuffer->data()),
                      (std::streamsize)pending.mBuffer->size());
            out.close();

            S64 delta = pending.mFileSize - old_file_size;
            addBytesWritten(delta);
            if (old_file_size == 0)
            {
                ++mEntryCount;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mMutex);
            mFlushing.erase(key);
        }
    }
}

S64 VayuBCTextureCache::getPendingBytes() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mPendingBytes;
}

const std::string VayuBCTextureCache::getCacheInfo() const
{
    F64 cur_mb = static_cast<F64>(mCurrentSizeBytes.load()) / (1024.0 * 1024.0);
    F64 nom_mb = static_cast<F64>(mNominalSizeBytes) / (1024.0 * 1024.0);
    F64 pct = (mNominalSizeBytes > 0) ? (static_cast<F64>(mCurrentSizeBytes.load()) / static_cast<F64>(mNominalSizeBytes) * 100.0) : 0.0;
    return fmt::format("{:.1f} MB / {:.1f} MB ({:.0f}%)", cur_mb, nom_mb, pct);
}
