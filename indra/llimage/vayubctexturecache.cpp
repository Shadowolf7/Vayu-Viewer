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
    constexpr U32 kMagic = VayuBCTextureCache::kMagic;
    constexpr U32 kFormatVersion = VayuBCTextureCache::kFormatVersion;

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

std::string VayuBCTextureCache::getFilePath(const LLUUID& id) const
{
    std::string filename = id.asString() + ".bc";
    return ((mCacheDir + filename[0]) + LL_DIR_DELIM_STR) + filename;
}

std::string VayuBCTextureCache::getFilePath(const LLUUID& id, S32 discard_level) const
{
    std::string filename = id.asString() + "_" + std::to_string(discard_level) + ".bc";
    return ((mCacheDir + filename[0]) + LL_DIR_DELIM_STR) + filename;
}

size_t VayuBCTextureCache::calcSubBufferBytes(U8 format, U32 width, U32 height, S32 num_mips, S32 diff)
{
    if (diff <= 0)
    {
        diff = 0;
    }
    if (diff >= num_mips || num_mips <= 0)
    {
        return 0;
    }

    // Format values mirror EVayuBlockCompressionFormat:
    // BC1 = 1, BC3 = 2, BC4 = 3, BC5 = 4, BC7 = 5
    const U32 block_bytes = (format == 1 /* BC1 */ || format == 3 /* BC4 */) ? 8 : 16;

    size_t total_bytes = 0;
    for (S32 m = diff; m < num_mips; ++m)
    {
        U32 mw = std::max(1u, width >> m);
        U32 mh = std::max(1u, height >> m);
        U32 bw = (mw + 3) / 4;
        U32 bh = (mh + 3) / 4;
        if (bw < 1) bw = 1;
        if (bh < 1) bh = 1;
        total_bytes += static_cast<size_t>(bw) * bh * block_bytes;
    }
    return total_bytes;
}


bool VayuBCTextureCache::ensureDirectoriesExist()
{
    if (mCacheDir.empty())
    {
        return false;
    }

    bool ok = (LLFile::mkdir(mCacheDir) == 0);
    if (ok)
    {
        for (U32 i = 0; i < 16; ++i)
        {
            ok &= (LLFile::mkdir(mCacheDir + sDigits[i]) == 0);
        }
    }

    mCacheValid = ok;
    if (!ok)
    {
        LL_WARNS("Texture") << "VayuBCTextureCache: failed to ensure cache directory: " << mCacheDir << LL_ENDL;
    }
    return ok;
}

void VayuBCTextureCache::initCache(const std::filesystem::path& cache_dir, S64 max_size_bytes,
                                   bool second_instance)
{
    std::lock_guard<std::mutex> lock(mMutex);

    mNominalSizeBytes = (U64)max_size_bytes;
    mMaxSizeBytes = 15UL * mNominalSizeBytes / 10UL;
    if (second_instance)
    {
        mMaxSizeBytes += (50UL + 5UL * U64(ll_frand(20.f))) * 1048576UL;
    }

    std::string cache_dir_str = cache_dir.string();
    if (!cache_dir_str.empty() && cache_dir_str.back() != LL_DIR_DELIM_CHR)
    {
        cache_dir_str += LL_DIR_DELIM_CHR;
    }

    if (mCacheValid && mCacheDir == cache_dir_str && LLFile::isdir(mCacheDir))
    {
        bool subdirs_exist = true;
        for (U32 i = 0; i < 16; ++i)
        {
            if (!LLFile::isdir(mCacheDir + sDigits[i]))
            {
                subdirs_exist = false;
                break;
            }
        }
        if (subdirs_exist)
        {
            return;
        }
    }

    mCacheDir = cache_dir_str;
    mCurrentSizeBytes = 0;
    mEntryCount = 0;

    if (!ensureDirectoriesExist())
    {
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
        return;
    }
#endif

    mCurrentSizeBytes = cacheDirSize();
    LL_INFOS("Texture") << "VayuBCTextureCache: nominal size: " << mNominalSizeBytes
                        << " bytes. Max size: " << mMaxSizeBytes
                        << " bytes. Current size: " << mCurrentSizeBytes.load()
                        << " bytes (" << mEntryCount.load() << " entries). Cache dir: "
                        << mCacheDir << LL_ENDL;
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
    ensureDirectoriesExist();

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
    LL_INFOS("Texture") << "VayuBCTextureCache: the entire BC texture cache is cleared." << LL_ENDL;
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

    size_t count = file_info.size();
    LL_INFOS("Texture") << "VayuBCTextureCache: " << count
                        << " files found in cache. Checking total size and purging old files..."
                        << LL_ENDL;

    U64 files_size_total = 0;
    U64 removed_bytes = 0;
    U32 purged_files = 0;
    for (size_t i = 0; i < count; ++i)
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

    // If not called by the main thread, or a threaded purging is in progress,
    // bail out now. Mirroring CoolVL LLDiskCache::addBytesWritten.
    if (!is_main_thread() || mPurging)
    {
        return;
    }

    if (mCurrentSizeBytes.load() > mMaxSizeBytes)
    {
        threadedPurge();
    }
}

bool VayuBCTextureCache::readEntry(const LLUUID& id, S32 discard_level,
                                   VayuBCCacheEntryHeader& header, std::vector<U8>& buffer)
{
    std::string file_path = getFilePath(id);
    if (!LLFile::isfile(file_path))
    {
        file_path = getFilePath(id, 0);
        if (!LLFile::isfile(file_path))
        {
            if (discard_level > 0)
            {
                file_path = getFilePath(id, discard_level);
                if (!LLFile::isfile(file_path))
                {
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
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

    if (file_header.mMeta.mDiscardLevel == discard_level)
    {
        buffer.resize((size_t)file_header.mBufferSize);
        in.read(reinterpret_cast<char*>(buffer.data()), (std::streamsize)file_header.mBufferSize);
        if (static_cast<size_t>(in.gcount()) != file_header.mBufferSize)
        {
            buffer.clear();
            return false;
        }

        header = file_header.mMeta;
        updateFileAccessTime(file_path);
        return true;
    }
    else if (file_header.mMeta.mDiscardLevel < discard_level)
    {
        S32 diff = discard_level - file_header.mMeta.mDiscardLevel;
        if (diff >= file_header.mMeta.mMipLevels)
        {
            return false;
        }

        size_t sub_bytes = calcSubBufferBytes(file_header.mMeta.mFormat, file_header.mMeta.mWidth,
                                              file_header.mMeta.mHeight, file_header.mMeta.mMipLevels, diff);
        if (sub_bytes > (size_t)file_header.mBufferSize)
        {
            return false;
        }

        buffer.resize(sub_bytes);
        in.read(reinterpret_cast<char*>(buffer.data()), (std::streamsize)sub_bytes);
        if (static_cast<size_t>(in.gcount()) != sub_bytes)
        {
            buffer.clear();
            return false;
        }

        header = file_header.mMeta;
        header.mWidth = std::max(1u, header.mWidth >> diff);
        header.mHeight = std::max(1u, header.mHeight >> diff);
        header.mMipLevels -= diff;
        header.mDiscardLevel = static_cast<U8>(discard_level);

        updateFileAccessTime(file_path);
        return true;
    }

    return false;
}

void VayuBCTextureCache::writeEntry(const LLUUID& id, S32 discard_level,
                                    const VayuBCCacheEntryHeader& header,
                                    std::shared_ptr<const std::vector<U8>> buffer)
{
    if (!buffer || buffer->empty())
        return;

    if (!mCacheValid && !ensureDirectoriesExist())
        return;

    VayuBCCacheEntryHeader local_header = header;
    local_header.mDiscardLevel = static_cast<U8>(std::clamp(discard_level, 0, 255));

    const std::string path = getFilePath(id);

    S64 old_file_size = 0;
    llstat st;
    if (LLFile::stat(path, &st) == 0)
    {
        old_file_size = st.st_size;
        std::ifstream in(path, std::ios::binary);
        if (in.good())
        {
            FileHeader existing_fh;
            in.read(reinterpret_cast<char*>(&existing_fh), sizeof(existing_fh));
            if (in.good() && existing_fh.mMagic == kMagic && existing_fh.mVersion == kFormatVersion)
            {
                if (existing_fh.mMeta.mDiscardLevel <= local_header.mDiscardLevel)
                {
                    return;
                }
            }
        }
    }
    else if (discard_level > 0)
    {
        std::string legacy_path = getFilePath(id, 0);
        if (LLFile::stat(legacy_path, &st) == 0)
        {
            std::ifstream in(legacy_path, std::ios::binary);
            if (in.good())
            {
                FileHeader existing_fh;
                in.read(reinterpret_cast<char*>(&existing_fh), sizeof(existing_fh));
                if (in.good() && existing_fh.mMagic == kMagic && existing_fh.mVersion == kFormatVersion)
                {
                    if (existing_fh.mMeta.mDiscardLevel <= local_header.mDiscardLevel)
                    {
                        return;
                    }
                }
            }
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.good())
    {
        ensureDirectoriesExist();
        out.clear();
        out.open(path, std::ios::binary | std::ios::trunc);
    }

    if (out.good())
    {
        FileHeader file_header;
        file_header.mMeta = local_header;
        file_header.mBufferSize = buffer->size();

        out.write(reinterpret_cast<const char*>(&file_header), (std::streamsize)sizeof(file_header));
        out.write(reinterpret_cast<const char*>(buffer->data()), (std::streamsize)buffer->size());
        out.close();

        S64 new_size = static_cast<S64>(sizeof(file_header) + buffer->size());
        S64 delta = new_size - old_file_size;
        addBytesWritten(delta);
        if (old_file_size == 0)
        {
            ++mEntryCount;
        }
    }
    else
    {
        LL_WARNS_ONCE("Texture") << "VayuBCTextureCache: failed to write cache entry to \""
                                 << path << "\"" << LL_ENDL;
    }
}

const std::string VayuBCTextureCache::getCacheInfo() const
{
    F64 cur_mb = static_cast<F64>(mCurrentSizeBytes.load()) / (1024.0 * 1024.0);
    F64 nom_mb = static_cast<F64>(mNominalSizeBytes) / (1024.0 * 1024.0);
    F64 pct = (mNominalSizeBytes > 0) ? (static_cast<F64>(mCurrentSizeBytes.load()) / static_cast<F64>(mNominalSizeBytes) * 100.0) : 0.0;
    return fmt::format("{:.1f} MB / {:.1f} MB ({:.0f}%)", cur_mb, nom_mb, pct);
}
