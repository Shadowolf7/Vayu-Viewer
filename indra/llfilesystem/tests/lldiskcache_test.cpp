/**
 * @file lldiskcache_test.cpp
 * @brief Unit tests for LLDiskCache
 */

#include "linden_common.h"
#include "lltut.h"
#include "../lldiskcache.h"
#include "llfile.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace tut
{
    struct LLDiskCacheFixture
    {
        std::filesystem::path mTestCacheDir;
        const U64 mNominalSize = 10 * 1024 * 1024; // 10MB

        LLDiskCacheFixture()
        {
            mTestCacheDir = std::filesystem::temp_directory_path() / ("vayu_diskcache_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(mTestCacheDir);
            LLDiskCache::init(mTestCacheDir.string(), mNominalSize, false);
        }

        ~LLDiskCacheFixture()
        {
            LLDiskCache::shutdown();
            std::error_code ec;
            std::filesystem::remove_all(mTestCacheDir, ec);
        }

        void createMockFile(const std::string& path_str, size_t size_bytes, time_t write_time)
        {
            std::filesystem::path path(path_str);
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            std::vector<char> data(size_bytes, 'A');
            out.write(data.data(), data.size());
            out.close();

            std::error_code ec;
            auto ftime = std::chrono::file_clock::from_sys(std::chrono::system_clock::from_time_t(write_time));
            std::filesystem::last_write_time(path, ftime, ec);
        }
    };

    typedef test_group<LLDiskCacheFixture> LLDiskCacheTest_group;
    typedef LLDiskCacheTest_group::object LLDiskCacheTest_object;
    LLDiskCacheTest_group disk_cache_testgroup("LLDiskCache");

    // Test 1: Initialization, subdirectories, and getFilePath
    template<> template<>
    void LLDiskCacheTest_object::test<1>()
    {
        ensure("Disk cache is valid", LLDiskCache::isValid());
        ensure_equals("Nominal budget matches", LLDiskCache::getNominalSizeBytes(), mNominalSize);
        ensure_equals("Max budget is 150% of nominal", LLDiskCache::getMaxSizeBytes(), (mNominalSize * 15) / 10);

        LLUUID test_id;
        test_id.generate();
        std::string filepath = LLDiskCache::getFilePath(test_id);
        ensure("Filepath is not empty", !filepath.empty());
        ensure("Filepath contains cache dir", filepath.find(mTestCacheDir.string()) != std::string::npos);
        ensure("Filepath has .asset extension", filepath.size() > 6 && filepath.substr(filepath.size() - 6) == ".asset");
    }

    // Test 2: Atomic byte accounting (addBytesWritten)
    template<> template<>
    void LLDiskCacheTest_object::test<2>()
    {
        ensure("Disk cache valid", LLDiskCache::isValid());

        U64 initial_size = LLDiskCache::getCurrentSizeBytes();
        LLDiskCache::addBytesWritten(5000);
        ensure_equals("addBytesWritten increases size", LLDiskCache::getCurrentSizeBytes(), initial_size + 5000);

        LLDiskCache::addBytesWritten(-2000);
        ensure_equals("addBytesWritten with negative value decreases size", LLDiskCache::getCurrentSizeBytes(), initial_size + 3000);
    }

    // Test 3: Purge logic removes oldest files when over budget
    template<> template<>
    void LLDiskCacheTest_object::test<3>()
    {
        ensure("Disk cache valid", LLDiskCache::isValid());

        time_t now = time(NULL);
        time_t older_time_1 = now - 7200;
        time_t older_time_2 = now - 3600;
        time_t newest_time = now;

        LLUUID old_id_1, old_id_2, new_id;
        old_id_1.generate();
        old_id_2.generate();
        new_id.generate();

        std::string old_path_1 = LLDiskCache::getFilePath(old_id_1);
        std::string old_path_2 = LLDiskCache::getFilePath(old_id_2);
        std::string new_path = LLDiskCache::getFilePath(new_id);

        // 4 MB each = 12 MB total (exceeds 10 MB nominal size)
        createMockFile(old_path_1, 4 * 1024 * 1024, older_time_1);
        createMockFile(old_path_2, 4 * 1024 * 1024, older_time_2);
        createMockFile(new_path, 4 * 1024 * 1024, newest_time);

        ensure("Old file 1 exists", LLFile::isfile(old_path_1));
        ensure("Old file 2 exists", LLFile::isfile(old_path_2));
        ensure("New file exists", LLFile::isfile(new_path));

        LLDiskCache::purge();

        ensure("Cache size updated within nominal budget", LLDiskCache::getCurrentSizeBytes() <= LLDiskCache::getNominalSizeBytes());
        ensure("Oldest file was purged", !LLFile::isfile(old_path_1));
        ensure("Newest file was preserved", LLFile::isfile(new_path));
    }

    // Test 4: Clear cache
    template<> template<>
    void LLDiskCacheTest_object::test<4>()
    {
        ensure("Disk cache valid", LLDiskCache::isValid());

        LLUUID id;
        id.generate();
        std::string path = LLDiskCache::getFilePath(id);
        createMockFile(path, 1024, time(NULL));
        ensure("File created", LLFile::isfile(path));

        LLDiskCache::clear();
        ensure("File removed after clear", !LLFile::isfile(path));
        ensure_equals("Current size reset to zero", LLDiskCache::getCurrentSizeBytes(), 0);
    }
}
