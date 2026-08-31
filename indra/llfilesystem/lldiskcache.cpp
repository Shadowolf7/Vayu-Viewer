/**
 * @file lldiskcache.cpp
 * @brief Implementation of the disk cache.
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 *
 * Copyright (c) 2020, Linden Research, Inc. (c) 2021 Henri Beauchamp.
 *
 * Modifications by Henri Beauchamp:
 *  - Pointless per-asset-type file naming removed.
 *  - Use of LLFile faster operations and of the extended LLDiriterator where
 *    possible.
 *  - Cache structure changed to speed up file opening and reduce the risk of
 *    hitting file-systems limitations (such as the max number of files per
 *    directory).
 *  - Proper cache validation and shutdown.
 *  - Proper catching of throw()s and boost::filesystem errors.
 *  - Track cache files size in real time (lock-less: just via an atomic
 *    variable).
 *  - Proper and threaded auto-purging of the cache when it exceeds 150% of
 *    its nominal size.
 *  - Multiple threads and multiple viewer instances deconfliction.
 *
 * Second Life Viewer Source Code
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; version 2.1 of the License only.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA"
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "boost/filesystem.hpp"

#include "lldiskcache.h"

#include "llapp.h"
#include "llcallbacklist.h"
#include "lldir.h"
#include "lldiriterator.h"
#include "llfile.h"
#include "llrand.h"
#include "llthread.h"
#include "lltimer.h"
#include "llprofiler.h"

#include <fmt/format.h>

using namespace boost::filesystem;

// Threshold in time_t units that is used to decide if the last access time of
// the file is updated or not. Added as a precaution for the concern outlined
// in SL-14582 about frequent writes on SSDs reducing their lifespan. Let's
// start with half an hour in time_t units and see how that unfolds.
constexpr time_t TIME_THRESHOLD = 1800;
// ... reduced to only one minute when we are currently purging the cache. HB
constexpr time_t TIME_THRESHOLD_PURGE = 60;

// Interval of time between consecutive checks for the stopping of the purging
// thread (1 second).
constexpr F32 INTERVAL_BETWEEN_CHECKS = 1.f;

// Static variable members
LLCachePurgeThread* LLDiskCache::sPurgeThread = NULL;
std::string LLDiskCache::sCacheDir;
U64 LLDiskCache::sNominalSizeBytes = 0;
U64 LLDiskCache::sMaxSizeBytes = 0;
std::atomic<U64> LLDiskCache::sCurrentSizeBytes{0};
std::atomic<bool> LLDiskCache::sPurging{false};
bool LLDiskCache::sCacheValid = false;

// Subdirectory names 0...9a...f, concatenated in a string
static std::string sDigits = "0123456789abcdef";

constexpr char LL_DIR_DELIM_CHR = std::filesystem::path::preferred_separator;
constexpr const char* LL_DIR_DELIM_STR = (LL_DIR_DELIM_CHR == '\\') ? "\\" : "/";

///////////////////////////////////////////////////////////////////////////////
// LLCachePurgeThread class
///////////////////////////////////////////////////////////////////////////////

class LLCachePurgeThread final : public LLThread
{
protected:
    LOG_CLASS(LLCachePurgeThread);

public:
    inline LLCachePurgeThread()
    :   LLThread("Disk cache purging thread")
    {
        start();
    }

    void run(void) override
    {
        LLDiskCache::purge();
    }
};

///////////////////////////////////////////////////////////////////////////////
// LLDiskCache class
///////////////////////////////////////////////////////////////////////////////

//static
void LLDiskCache::init(U64 nominal_size_bytes, bool second_instance)
{
    std::string cache_dir = gDirUtilp ? gDirUtilp->getExpandedFilename(LL_PATH_CACHE, "assets") : "assets";
    init(cache_dir, nominal_size_bytes, second_instance);
}

//static
void LLDiskCache::init(const std::string& cache_dir, U64 nominal_size_bytes, bool second_instance)
{
    LL_INFOS("DiskCache") << "Initializing cache..." << LL_ENDL;

    sNominalSizeBytes = nominal_size_bytes;
    sMaxSizeBytes = 15UL * sNominalSizeBytes / 10UL;
    if (second_instance)
    {
        // Add 50 to 150 Mb (in random steps of 5Mb) to the maximum size for
        // the second and further instances, so that the various instances do
        // not attempt to purge the cache at the same time (even though, since
        // they only account each for their own cache file writes, they will
        // not see the same apparent cache size at the same time)... HB
        sMaxSizeBytes += (50UL + 5UL * U64(ll_frand(20.f))) * 1048576UL;
    }

    // We enforce the storage of our files in an "assets" sub-directory, which
    // saves us from worrying about deleting files that do not belong to our
    // cache (no need to test for a file prefix or extension, meaning faster
    // operations when purging, clearing, or calculating the cache size). HB
    sCacheDir = cache_dir;

    sCacheValid = (LLFile::mkdir(sCacheDir) == 0);
    if (sCacheValid)
    {
        if (sCacheDir.back() != LL_DIR_DELIM_CHR)
        {
            sCacheDir += LL_DIR_DELIM_CHR;
        }
        // We use sub-directories to lower the number of file entries per
        // directory (which can easily count in hundred of thousands when
        // using a large cache in a single directory). This avoids hitting
        // any file-system limitation, and helps speeding up the opening of
        // cache files. HB
        for (U32 i = 0; i < 16; ++i)
        {
            sCacheValid &= (LLFile::mkdir(sCacheDir + sDigits[i]) == 0);
        }
    }
    if (sCacheValid)
    {
#if LL_WINDOWS
        if (!second_instance)
        {
            // Do not call cacheDirSize() on startup from the main thread under
            // Windows when the cache directory has not already been scanned
            // (i.e. after boot, from the first viewer instance): it causes
            // minutes-long delays for large caches on hard disks (obviously a
            // problem with "SuperFetch", but even after disabling it, scanning
            // the cache can take a couple dozens seconds, when the same cache
            // takes at most a few seconds to get scanned under Linux) !
            // LLDiskCache::threadedPurge() will instead set sCurrentSizeBytes
            // for us, and in a non-blocking thread... HB
            LL_INFOS("DiskCache") << "Nominal cache size: " << sNominalSizeBytes
                                  << " bytes. Maximal cache size: " << sMaxSizeBytes
                                  << " bytes. Cache directory: " << sCacheDir << LL_ENDL;
            return;
        }
#endif
        sCurrentSizeBytes = cacheDirSize();
        LL_INFOS("DiskCache") << "Nominal cache size: " << sNominalSizeBytes
                              << " bytes. Maximal cache size: " << sMaxSizeBytes
                              << " bytes. Current cache size: " << sCurrentSizeBytes.load()
                              << " bytes. Cache directory: " << sCacheDir << LL_ENDL;
    }
    else
    {
        LL_WARNS("DiskCache") << "Cache path is invalid: " << sCacheDir << LL_ENDL;
    }
}

//static
void LLDiskCache::shutdown()
{
    // Stop changing the cache now !
    sCacheValid = false;

    if (sPurgeThread)
    {
        U32 loops = 0;
        while (loops++ < 100 && !sPurgeThread->isStopped())
        {
            ms_sleep(10);   // Give it some more time...
        }
        if (sPurgeThread->isStopped())
        {
            LL_INFOS("DiskCache") << "Cache purging thread stopped." << LL_ENDL;
        }
        else
        {
            LL_WARNS("DiskCache") << "Timeout waiting for the cache purging thread to stop. Force-removing it."
                                  << LL_ENDL;
        }
        delete sPurgeThread;
        sPurgeThread = NULL;
        sPurging = false;
    }
}

//static
U64 LLDiskCache::cacheDirSize()
{
    U64 total_file_size = 0;
    std::string subdir, filename;
    for (U32 i = 0; i < 16; ++i)
    {
        subdir = sCacheDir + sDigits[i];
        if (LLFile::isdir(subdir))
        {
            LLDirIterator iter(subdir, NULL, DI_SIZE);
            while (iter.next(filename))
            {
                total_file_size += iter.getSize();
            }
        }
    }
    return total_file_size;
}

//static
void LLDiskCache::clear()
{
    if (LLFile::isdir(sCacheDir))
    {
        std::string subdir;
        for (U32 i = 0; i < 16; ++i)
        {
            subdir = sCacheDir + sDigits[i];
            if (LLFile::isdir(subdir))
            {
                LLDirIterator::deleteFilesInDir(subdir);
            }
        }
    }
    else
    {
        LL_INFOS("DiskCache") << "No cache directory: nothing to clear." << LL_ENDL;
    }
    sCurrentSizeBytes = 0;
}

//static
void LLDiskCache::purge()
{
    LL_PROFILE_ZONE_SCOPED;

    if (!LLFile::isdir(sCacheDir))
    {
        LL_INFOS("DiskCache") << "No cache directory: nothing to purge." << LL_ENDL;
        return;
    }

    sPurging = true;

    typedef std::pair<time_t, std::pair<U64, std::string> > file_info_t;
    std::vector<file_info_t> file_info;

    LLTimer purge_timer;
    purge_timer.reset();

    std::string subdir, filename;
    for (U32 i = 0; i < 16; ++i)
    {
        if (LLApp::isQuitting())
        {
            sPurging = false;
            return;
        }

        subdir = sCacheDir + sDigits[i];
        if (!LLFile::isdir(subdir))
        {
            LL_WARNS("DiskCache") << "Missing cache sub-directory: " << subdir << LL_ENDL;
            continue;
        }
        LLDirIterator iter(subdir, NULL, DI_ISFILE | DI_SIZE | DI_TIMESTAMP);
        while (iter.next(filename))
        {
            if (iter.isFile())
            {
                file_info.emplace_back(iter.getTimeStamp(),
                                       std::make_pair(iter.getSize(),
                                                      iter.getPath() +
                                                      filename));
            }
        }
    }

    std::sort(file_info.begin(), file_info.end(),
              [](const file_info_t& x, const file_info_t& y)
              {
                    return x.first > y.first;
              });

    U32 count = file_info.size();

    LL_INFOS("DiskCache") << count
                          << " files found in cache. Checking the total size and possibly purging old files..."
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
        bool removed = files_size_total > sNominalSizeBytes;
        if (removed)
        {
            try
            {
                // Verify that the file did not get touched by another thread
                // or viewer instance since we last checked its time stamp !
                if (last_write_time(entry.second.second) <= entry.first)
                {
                    remove(entry.second.second);
                    ++purged_files;
                    removed_bytes += entry.second.first;
                }
                else
                {
                    LL_DEBUGS("DiskCache") << "Skipped updated file: "
                                           << entry.second.second << LL_ENDL;
                    removed = false;
                }
            }
            catch (const filesystem_error& e)
            {
                removed = false;
                LL_WARNS("DiskCache") << "Failure to remove \"" << entry.second.second
                                      << "\". Reason: " << e.what() << LL_ENDL;
            }
        }
        LL_DEBUGS("DiskCache") << (removed ? " Removed " : "Kept ")
                               << entry.second.second << LL_ENDL;
    }

    sPurging = false;

    sCurrentSizeBytes = files_size_total - removed_bytes;

    U32 ms = (U32)(purge_timer.getElapsedTimeF32() * 1000.f);
    if (purged_files)
    {
        LL_INFOS("DiskCache") << "Cache purge took " << ms << "ms to execute. "
                              << purged_files << " purged files and " << removed_bytes
                              << " bytes removed. " << sCurrentSizeBytes.load()
                              << " bytes now in cache." << LL_ENDL;
    }
    else
    {
        LL_INFOS("DiskCache") << "Cache check took " << ms << "ms to execute. Cache size: "
                              << sCurrentSizeBytes.load() << " bytes." << LL_ENDL;
    }
    LL_DEBUGS("DiskCache") << "Current cache size: " << cacheDirSize()
                           << " bytes." << LL_ENDL;
}

// Must be called from the main thread only !
//static
void LLDiskCache::threadedPurge()
{
    if (!sCacheValid)
    {
        return;
    }
    if (sPurgeThread)   // Called via doAfterInterval()
    {
        if (sPurgeThread->isStopped())
        {
            LL_DEBUGS("DiskCache") << "Purge thread stopped, deleting it."
                                   << LL_ENDL;
            delete sPurgeThread;
            sPurgeThread = NULL;
        }
        else
        {
            LL_DEBUGS("DiskCache") << "Purge thread still running..."
                                   << LL_ENDL;
            // Check again later to see if the thread has stopped
            doAfterInterval(threadedPurge, INTERVAL_BETWEEN_CHECKS);
        }
    }
    else                // Called by addBytesWritten() or from llappviewer.cpp
    {
        // Start a new thread.
        LL_DEBUGS("DiskCache") << "Starting a new purge thread..." << LL_ENDL;
        sPurgeThread = new LLCachePurgeThread;
        // Check again later to see if the thread has stopped
        doAfterInterval(threadedPurge, INTERVAL_BETWEEN_CHECKS);
    }
}

//static
std::string LLDiskCache::getFilePath(const LLUUID& id, const char* extra_info)
{
    std::string filename = id.asString();
    if (extra_info && *extra_info)
    {
        filename += '_';
        filename += extra_info;
    }
    filename += ".asset";
    return ((sCacheDir + filename[0]) + LL_DIR_DELIM_STR) + filename;
}

//static
void LLDiskCache::addBytesWritten(S32 bytes)
{
    if (bytes >= 0)
    {
        sCurrentSizeBytes += (U64)bytes;
    }
    else
    {
        U64 delta = (U64)(-bytes);
        U64 current = sCurrentSizeBytes.load();
        while (current > 0)
        {
            U64 target = (current > delta) ? (current - delta) : 0;
            if (sCurrentSizeBytes.compare_exchange_weak(current, target))
            {
                break;
            }
        }
    }

    // If not called by the main thread, or a threaded purging is in progress,
    // bail out now.
    if (!is_main_thread() || sPurgeThread)
    {
        return;
    }

    LL_DEBUGS("DiskCache") << "Cache size: " << sCurrentSizeBytes.load() << " bytes."
                           << LL_ENDL;

    // Start purging the cache if needed.
    if (sCurrentSizeBytes.load() > sMaxSizeBytes)
    {
        threadedPurge();
    }
}

//static
void LLDiskCache::updateFileAccessTime(const std::string& filename)
{
    // Current time
    const time_t cur_time = time(NULL);

    // Last write time
    llstat st;
    time_t last_write = 0;
    if (LLFile::stat(filename, &st) == 0)
    {
        last_write = st.st_mtime;
    }

    // We only write the new value if 'threshold' has elapsed since the last
    // write.
    time_t threshold = sPurging ? TIME_THRESHOLD_PURGE : TIME_THRESHOLD;
    if (cur_time - last_write > threshold)
    {
        boost::system::error_code ec;
#if LL_WINDOWS
        last_write_time(ll_convert_string_to_wide(filename), cur_time, ec);
#else
        last_write_time(filename, cur_time, ec);
#endif
        if (ec.failed())
        {
            LL_WARNS("DiskCache") << "Failure to touch \"" << filename
                                  << "\". Reason: " << ec.message() << LL_ENDL;
        }
    }
}

//static
const std::string LLDiskCache::getCacheInfo()
{
    F64 cur_mb = static_cast<F64>(sCurrentSizeBytes.load()) / (1024.0 * 1024.0);
    F64 nom_mb = static_cast<F64>(sNominalSizeBytes) / (1024.0 * 1024.0);
    F64 pct = (sNominalSizeBytes > 0) ? (static_cast<F64>(sCurrentSizeBytes.load()) / static_cast<F64>(sNominalSizeBytes) * 100.0) : 0.0;
    return fmt::format("{:.1f} MB / {:.1f} MB ({:.0f}%)", cur_mb, nom_mb, pct);
}
