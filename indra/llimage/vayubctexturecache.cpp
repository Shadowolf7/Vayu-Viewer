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
#include "lltimer.h"
#include "llprofiler.h"
#include "threadpool.h"

#include <fmt/format.h>
#include <algorithm>
#include <chrono>
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

VayuBCTextureCache& VayuBCTextureCache::instance()
{
    static VayuBCTextureCache sInstance;
    return sInstance;
}

VayuBCTextureCache::~VayuBCTextureCache() = default;

std::string VayuBCTextureCache::getFilePath(const LLUUID& id, const std::string& ext) const
{
    std::string filename = id.asString() + ext;
    return ((mCacheDir + filename[0]) + LL_DIR_DELIM_STR) + filename;
}

std::string VayuBCTextureCache::getFilePath(const LLUUID& id, S32 discard_level, const std::string& ext) const
{
    std::string filename = id.asString() + "_" + std::to_string(discard_level) + ext;
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

void VayuBCTextureCache::initCache(const std::filesystem::path& cache_dir, S64 max_size_bytes)
{
    std::lock_guard<std::mutex> lock(mMutex);

    mNominalSizeBytes = (U64)max_size_bytes;
    mMaxSizeBytes = 15UL * mNominalSizeBytes / 10UL;

    std::string cache_dir_str = cache_dir.string();
    if (!cache_dir_str.empty() && cache_dir_str.back() != LL_DIR_DELIM_CHR)
    {
        cache_dir_str += LL_DIR_DELIM_CHR;
    }

    // Own single-writer pool for cache-fill writes: the producing decode
    // workers post and return instead of blocking on disk. FIFO order on the
    // one writer preserves per-(id, discard) ordering. auto_shutdown=false:
    // shutdown() drains and closes it at a controlled point (llappviewer.cpp),
    // and tests may re-init after shutdown (which re-creates the pool here).
    if (!mWritePool)
    {
        mWritePool = std::make_unique<LL::ThreadPool>("VayuBCTextureCacheWrite", 1, 1024 * 1024, false);
        mWritePool->start();
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
            const size_t dot = loose_file.rfind('.');
            std::string ext = (dot != std::string::npos) ? loose_file.substr(dot) : std::string();
            if (!ext.empty() && ext.back() == 'n')
            {
                ext.pop_back();
            }
            if (ext == ".bc" || ext == ".bc1" || ext == ".bc3" ||
                ext == ".bc4" || ext == ".bc5" || ext == ".bc7")
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
    // A purge pass may be mid-scan/removal on the writer pool thread. Cancel
    // it and wait (bounded) for the queued/running pass to drain before we
    // remove_all() the very directory it is walking; otherwise the pass would
    // race the wipe and its purgeFinish() would clobber the size accounting we
    // reset below. A pass slice is ~0.1s plus at most one whole subdir scan.
    mPurgeRequested.store(false);
    mPurgeCancelPending.store(true);
    if (mPurging.load() || mPurgePassQueued.load())
    {
        for (U32 i = 0; i < 300 && mPurgePassQueued.load(); ++i)
        {
            ms_sleep(10);
        }
    }
    mPurgeCancelPending.store(false);

    // Rebuild the cache directory tree from a cold start rather than walking
    // and unlinking every entry: remove_all() is a handful of syscalls no
    // matter how many files the cache holds, whereas a per-file loop is one
    // unlink syscall per entry (hundreds of thousands for a full cache).
    std::error_code ec;
    if (!mCacheDir.empty())
    {
        std::filesystem::remove_all(mCacheDir, ec);
    }

    ensureDirectoriesExist();

    mCurrentSizeBytes = 0;
    mEntryCount = 0;
    mPurging.store(false);
    LL_INFOS("Texture") << "VayuBCTextureCache: the entire BC texture cache is cleared." << LL_ENDL;
}

void VayuBCTextureCache::purge()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    if (!purgeBegin())
    {
        return;
    }

    // Synchronous completion (test path).
    while (purgeProgress())
    {
    }
}

void VayuBCTextureCache::purgePassJob()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    // Single time-sliced pass on the writer pool thread. The flag is kept set
    // until this job returns so update() can't pile up passes; it decides when
    // the next one is due (>=2s gap, mirroring purgeTextureFilesTimeSliced).
    if (mPurgeCancelPending.load() || LLApp::isQuitting())
    {
        mPurgePassQueued.store(false);
        return;
    }

    if (!mPurging.load())
    {
        if (!mPurgeRequested.load() || !purgeBegin())
        {
            mPurgePassQueued.store(false);
            return;
        }
    }

    // One bounded slice. purgeProgress() does one whole hex subdir's scan or
    // one file removal per call, so progress is guaranteed even if the slice
    // is already spent, at most one subdir's worth of overshoot.
    constexpr F32 time_slice = 0.1f;
    LLTimer slice_timer;
    slice_timer.reset();
    do
    {
        if (!purgeProgress())
        {
            break;
        }
    } while (slice_timer.getElapsedTimeF32() < time_slice);

    mPurgePassQueued.store(false);
}

S32 VayuBCTextureCache::update(F32 max_time_ms)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    (void)max_time_ms; // per-pass budget is fixed (0.1s) inside purgePassJob()

    if (!mCacheValid)
    {
        return 0;
    }
    if (!mPurging.load() && !mPurgeRequested.load())
    {
        return 0;
    }

    // Cheap poll: schedule a purge pass on the writer pool when one isn't
    // already queued/running and the >=2s gap since the previous pass has
    // elapsed. No disk I/O happens here.
    static constexpr F64 kMinBetweenPasses = 2.0;
    if (mWritePool &&
        !mPurgePassQueued.load() &&
        mPurgeLastPassTimer.getElapsedTimeF32() >= (F32)kMinBetweenPasses)
    {
        if (!mPurgePassQueued.exchange(true))
        {
            mPurgeLastPassTimer.reset();
            if (!mWritePool->getQueue().post([this]() { purgePassJob(); }))
            {
                mPurgePassQueued.store(false);
            }
        }
    }

    // Purge work is pending if a purge is requested, mid-scan, or a pass is
    // queued/running - nonzero even during the >=2s gap between passes so the
    // frame scheduler keeps counting us as busy until the purge actually ends.
    return (mPurgeRequested.load() || mPurging.load() || mPurgePassQueued.load()) ? 1 : 0;
}

void VayuBCTextureCache::requestPurge()
{
    if (!mCacheValid)
    {
        return;
    }
    mPurgeRequested.store(true);
}

bool VayuBCTextureCache::purgeBegin()
{
    if (mPurging.load())
    {
        return false;
    }

    if (mPurgeCancelPending.load())
    {
        return false;
    }

    if (!LLFile::isdir(mCacheDir))
    {
        LL_INFOS("Texture") << "VayuBCTextureCache: no cache directory: nothing to purge." << LL_ENDL;
        mPurgeRequested.store(false);
        return false;
    }

    mPurging.store(true);
    mPurgeTimer.reset();
    mPurgeScanSubdir = 0;
    mPurgeFilesScannedAll = false;
    mPurgeFiles.clear();
    mPurgeRemoveIndex = 0;
    mPurgeAccumulatedSize = 0;
    mPurgeRemovedBytes = 0;
    mPurgeRemovedCount = 0;
    return true;
}

bool VayuBCTextureCache::purgeProgress()
{
    if (!mPurgeFilesScannedAll)
    {
        // Phase A: scan one whole hex subdir per call. LLDirIterator is
        // recreated each call, so never stop halfway through a subdir or the
        // rescan would duplicate entries.
        while (mPurgeScanSubdir < 16)
        {
            if (LLApp::isQuitting() || mPurgeCancelPending.load())
            {
                purgeFinish();
                return false;
            }

            const U32 i = mPurgeScanSubdir++;
            const std::string subdir = mCacheDir + sDigits[i];
            if (!LLFile::isdir(subdir))
            {
                continue;
            }
            LLDirIterator iter(subdir, NULL, DI_ISFILE | DI_SIZE | DI_TIMESTAMP);
            std::string filename;
            while (iter.next(filename))
            {
                if (iter.isFile())
                {
                    mPurgeFiles.emplace_back(iter.getTimeStamp(),
                                             std::make_pair(iter.getSize(),
                                                            iter.getPath() + filename));
                }
            }
            break;
        }

        if (mPurgeScanSubdir >= 16)
        {
            std::sort(mPurgeFiles.begin(), mPurgeFiles.end(),
                      [](const purge_file_info_t& x, const purge_file_info_t& y)
                      {
                          return x.first > y.first;
                      });
            mPurgeFilesScannedAll = true;
            LL_INFOS("Texture") << "VayuBCTextureCache: " << mPurgeFiles.size()
                                << " files found in cache. Checking total size and purging old files..."
                                << LL_ENDL;
        }
        return true;
    }

    if (LLApp::isQuitting() || mPurgeCancelPending.load())
    {
        purgeFinish();
        return false;
    }

    // Phase B: remove one file per call. The list is sorted newest-first, so
    // the file that tips the running total over the nominal budget, and every
    // older file after it, gets removed - mirroring the original synchronous
    // purge's LRU behavior.
    if (mPurgeRemoveIndex >= mPurgeFiles.size())
    {
        purgeFinish();
        return false;
    }

    const purge_file_info_t& entry = mPurgeFiles[mPurgeRemoveIndex++];
    mPurgeAccumulatedSize += entry.second.first;
    if (mPurgeAccumulatedSize > mNominalSizeBytes)
    {
        try
        {
            // Skip files touched since they were scanned.
            if (boost::filesystem::last_write_time(entry.second.second) <= entry.first)
            {
                boost::filesystem::remove(entry.second.second);
                ++mPurgeRemovedCount;
                mPurgeRemovedBytes += entry.second.first;
            }
        }
        catch (const boost::filesystem::filesystem_error& e)
        {
            LL_WARNS("Texture") << "VayuBCTextureCache: failure to remove \"" << entry.second.second
                                << "\". Reason: " << e.what() << LL_ENDL;
        }
    }

    return true;
}

void VayuBCTextureCache::purgeFinish()
{
    mPurging.store(false);
    mCurrentSizeBytes = mPurgeAccumulatedSize - mPurgeRemovedBytes;
    mEntryCount = mPurgeFiles.size() - mPurgeRemovedCount;

    const U32 ms = (U32)(mPurgeTimer.getElapsedTimeF32() * 1000.f);
    if (mPurgeRemovedCount)
    {
        LL_INFOS("Texture") << "VayuBCTextureCache: cache purge took " << ms << "ms to execute. "
                            << mPurgeRemovedCount << " purged files and " << mPurgeRemovedBytes
                            << " bytes removed. " << mCurrentSizeBytes.load()
                            << " bytes now in cache." << LL_ENDL;
    }
    else
    {
        LL_INFOS("Texture") << "VayuBCTextureCache: cache check took " << ms << "ms. Cache size: "
                            << mCurrentSizeBytes.load() << " bytes." << LL_ENDL;
    }

    mPurgeRequested.store(false);
    mPurgeScanSubdir = 0;
    mPurgeFilesScannedAll = false;
    mPurgeRemoveIndex = 0;
    mPurgeFiles.clear();
}

void VayuBCTextureCache::shutdown()
{
    mCacheValid = false;

    // A purge pass may be queued/running on the writer pool thread. Ask it to
    // stop promptly, then let it unwind so it never touches purge state after
    // we close the pool. Queued-but-not-started passes are dropped by close();
    // a running pass aborts on the cancel flag and its slot is forgotten.
    mPurgeRequested.store(false);
    mPurgeCancelPending.store(true);
    if (mPurging.load() || mPurgePassQueued.load())
    {
        for (U32 i = 0; i < 300 && mPurgePassQueued.load(); ++i)
        {
            ms_sleep(10);
        }
    }
    mPurgeCancelPending.store(false);
    mPurging.store(false);
    mPurgePassQueued.store(false);

    // Drain in-flight cache fills, then join the single writer thread at this
    // controlled point (see the writer-pool comment in llappviewer.cpp). The
    // pool is destroyed so any write posted afterwards takes the synchronous
    // fallback path and still persists.
    if (mWritePool)
    {
        waitForPendingWrites();
        mWritePool->close();
        mWritePool.reset();
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

    // If not called by the main thread, or a purge is in progress, bail out now.
    // Mirroring CoolVL LLDiskCache::addBytesWritten.
    if (!is_main_thread() || mPurging)
    {
        return;
    }

    if (mCurrentSizeBytes.load() > mMaxSizeBytes)
    {
        requestPurge();
    }
}

std::string VayuBCTextureCache::formatExtension(U8 format, U8 role)
{
    const std::string base =
        (format == kFormatBC1) ? ".bc1" :
        (format == kFormatBC3) ? ".bc3" :
        (format == kFormatBC4) ? ".bc4" :
        (format == kFormatBC5) ? ".bc5" :
        (format == kFormatBC7) ? ".bc7" : ".bc";
    return (role == kRoleNormal) ? (base + "n") : base;
}

bool VayuBCTextureCache::readEntry(const LLUUID& id, S32 discard_level,
                                   VayuBCCacheEntryHeader& header, std::vector<U8>& buffer,
                                   const std::string& ext,
                                   U8 expected_format,
                                   U8 expected_role)
{
    // (format, role)-aware lookup: with an expected format we only ever probe the
    // file named for that exact pair, so a color .bc1 entry can never be served to
    // a normal slot and vice versa, no matter what the header fields say.
    std::vector<std::string> candidates;
    if (expected_format != 0)
    {
        candidates.push_back(formatExtension(expected_format, expected_role));
    }
    else if (!ext.empty() && ext != ".bc")
    {
        // Explicit non-generic override (e.g. tests probing a single name).
        candidates.push_back(ext);
    }
    else
    {
        // Format-agnostic color probe: try every per-format color name, first hit
        // wins. Normal entries live under .bcNn and are only reached by normal-role
        // callers that pass an expected format, so this list never mis-serves them.
        // The legacy generic .bc name is deliberately not probed here so stale
        // pre-per-format entries are ignored rather than mis-served.
        candidates.push_back(".bc1");
        candidates.push_back(".bc3");
        candidates.push_back(".bc4");
        candidates.push_back(".bc5");
        candidates.push_back(".bc7");
    }

    for (const std::string& candidate : candidates)
    {
        if (readEntryExt(id, discard_level, header, buffer, candidate))
        {
            return true;
        }
    }
    return false;
}

bool VayuBCTextureCache::readEntryExt(const LLUUID& id, S32 discard_level,
                                      VayuBCCacheEntryHeader& header, std::vector<U8>& buffer,
                                      const std::string& ext)
{
    std::string file_path = getFilePath(id, ext);
    if (!LLFile::isfile(file_path))
    {
        file_path = getFilePath(id, 0, ext);
        if (!LLFile::isfile(file_path))
        {
            if (discard_level > 0)
            {
                file_path = getFilePath(id, discard_level, ext);
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

    // The extension is authoritative: reject a header whose stored format does
    // not match the extension it was read from (defensive; writeEntrySync keys
    // the name off the same header). Left in place rather than removed, like
    // the version check above, since a mismatched name implies a rename race.
    const U8 implied_format =
        (ext == ".bc1") ? kFormatBC1 :
        (ext == ".bc3") ? kFormatBC3 :
        (ext == ".bc4") ? kFormatBC4 :
        (ext == ".bc5") ? kFormatBC5 :
        (ext == ".bc7") ? kFormatBC7 : 0;
    if (implied_format != 0 && file_header.mMeta.mFormat != implied_format)
    {
        return false;
    }

    // The filename also encodes the role (trailing "n" = normal). Reject an
    // entry whose stored role disagrees, mirroring the format check above.
    const bool suffixed = !ext.empty() && ext.back() == 'n';
    if (suffixed != (file_header.mMeta.mRole == kRoleNormal))
    {
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

    // Not initialized, or already shut down (shutdown() drains and removes the
    // pool): fall back to a synchronous write so nothing is ever dropped.
    if (!mWritePool)
    {
        writeEntrySync(id, discard_level, header, buffer);
        return;
    }

    ++mPendingWrites;
    auto job = [this, id, discard_level, header, buffer]()
    {
        writeEntrySync(id, discard_level, header, buffer);
        if (--mPendingWrites == 0)
        {
            {
                std::unique_lock<std::mutex> lock(mWriteCVLock);
            }
            mWriteCV.notify_all();
        }
    };
    if (!mWritePool->getQueue().post(std::move(job)))
    {
        --mPendingWrites;
        writeEntrySync(id, discard_level, header, buffer);
    }
}

void VayuBCTextureCache::waitForPendingWrites()
{
    if (!mWritePool)
    {
        return;
    }
    std::unique_lock<std::mutex> lock(mWriteCVLock);
    mWriteCV.wait(lock, [this]() { return mPendingWrites.load() == 0; });
}

void VayuBCTextureCache::writeEntrySync(const LLUUID& id, S32 discard_level,
                                        const VayuBCCacheEntryHeader& header,
                                        const std::shared_ptr<const std::vector<U8>>& buffer)
{
    VayuBCCacheEntryHeader local_header = header;
    local_header.mDiscardLevel = static_cast<U8>(std::clamp(discard_level, 0, 255));

    const std::string ext = formatExtension(local_header.mFormat, local_header.mRole);
    const std::string path = getFilePath(id, ext);

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
        std::string legacy_path = getFilePath(id, 0, ext);
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

    // Write to a sibling temp file, then rename over the target: the swap is
    // atomic, so lockless readers and crash recovery never observe a torn entry.
    // A stale temp left by a crash is picked up and purged like any other file.
    const std::string tmp_path = path + ".tmp";
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out.good())
    {
        ensureDirectoriesExist();
        out.clear();
        out.open(tmp_path, std::ios::binary | std::ios::trunc);
    }

    if (out.good())
    {
        FileHeader file_header;
        file_header.mMeta = local_header;
        file_header.mBufferSize = buffer->size();

        out.write(reinterpret_cast<const char*>(&file_header), (std::streamsize)sizeof(file_header));
        out.write(reinterpret_cast<const char*>(buffer->data()), (std::streamsize)buffer->size());
        out.close();

        if (LLFile::rename(tmp_path, path) == 0)
        {
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
            LLFile::remove(tmp_path);
            LL_WARNS_ONCE("Texture") << "VayuBCTextureCache: failed to commit cache entry \""
                                     << path << "\"" << LL_ENDL;
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
