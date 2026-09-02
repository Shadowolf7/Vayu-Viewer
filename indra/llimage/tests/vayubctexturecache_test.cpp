/**
 * @file vayubctexturecache_test.cpp
 * @brief Unit tests for VayuBCTextureCache
 */

#include "linden_common.h"

#include "../test/lltut.h"

#include "../vayubctexturecache.h"
#include "boost/filesystem.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>

namespace tut
{
    struct bc_texture_cache_test
    {
    };

    typedef test_group<bc_texture_cache_test> bc_texture_cache_group;
    typedef bc_texture_cache_group::object bc_texture_cache_object;
    bc_texture_cache_group bc_texture_cache_testgroup("VayuBCTextureCache");

    static std::filesystem::path test_dir(const char* name)
    {
        return std::filesystem::temp_directory_path() / (std::string("vayu_bccache_test_") + name);
    }

    // The cache takes shared ownership of the buffer now (see writeEntry()),
    // so tests hand it a shared_ptr rather than a reference to a local.
    static std::shared_ptr<const std::vector<U8>> make_buffer(std::vector<U8> bytes)
    {
        return std::make_shared<const std::vector<U8>>(std::move(bytes));
    }

    static VayuBCCacheEntryHeader make_header(U8 format, U8 preset)
    {
        VayuBCCacheEntryHeader h;
        h.mFormat = format;
        h.mPreset = preset;
        h.mMipLevels = 3;
        h.mWidth = 16;
        h.mHeight = 16;
        h.mComponents = 4;
        h.mGLInternalFormat = 0x8E8D;
        h.mGLPrimaryFormat = 0x8E8D;
        return h;
    }

    // Test 1: write then read round-trips header and buffer bytes exactly
    template<> template<>
    void bc_texture_cache_object::test<1>()
    {
        auto dir = test_dir("roundtrip");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        LLUUID id;
        id.generate();
        VayuBCCacheEntryHeader header = make_header(3, 2);
        std::vector<U8> buffer = { 1, 2, 3, 4, 5, 6, 7, 8 };

        VayuBCTextureCache::instance().writeEntry(id, 0, header, make_buffer(buffer));

        VayuBCCacheEntryHeader read_header;
        std::vector<U8> read_buffer;
        bool ok = VayuBCTextureCache::instance().readEntry(id, 0, 0, read_header, read_buffer);

        ensure("Write-then-read succeeds", ok);
        ensure_equals("Format round-trips", read_header.mFormat, header.mFormat);
        ensure_equals("Preset round-trips", read_header.mPreset, header.mPreset);
        ensure_equals("Width round-trips", read_header.mWidth, header.mWidth);
        ensure("Buffer bytes round-trip", read_buffer == buffer);

        VayuBCTextureCache::instance().clear();
    }

    // Test 2: unknown (id, discard) is a miss
    template<> template<>
    void bc_texture_cache_object::test<2>()
    {
        auto dir = test_dir("miss");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        LLUUID id;
        id.generate();
        VayuBCCacheEntryHeader header;
        std::vector<U8> buffer;
        bool ok = VayuBCTextureCache::instance().readEntry(id, 0, 0, header, buffer);
        ensure("Unknown entry is a miss", !ok);

        VayuBCTextureCache::instance().clear();
    }

    // Test 3: a cached entry below the requested preset is treated as a miss,
    // but the same entry still satisfies a request at or below its own preset
    template<> template<>
    void bc_texture_cache_object::test<3>()
    {
        auto dir = test_dir("preset_gate");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        LLUUID id;
        id.generate();
        VayuBCCacheEntryHeader header = make_header(3, 1 /* Fast */);
        std::vector<U8> buffer = { 9, 9, 9 };
        VayuBCTextureCache::instance().writeEntry(id, 0, header, make_buffer(buffer));

        VayuBCCacheEntryHeader out_header;
        std::vector<U8> out_buffer;

        bool below = VayuBCTextureCache::instance().readEntry(id, 0, /*min_preset*/ 3, out_header, out_buffer);
        ensure("Cached-below-configured preset is a miss", !below);

        bool at_or_below = VayuBCTextureCache::instance().readEntry(id, 0, /*min_preset*/ 1, out_header, out_buffer);
        ensure("Cached-at-configured preset is a hit", at_or_below);

        VayuBCTextureCache::instance().clear();
    }

    // Test 4: purge() removes oldest files when exceeding nominal budget
    template<> template<>
    void bc_texture_cache_object::test<4>()
    {
        auto dir = test_dir("eviction");
        std::filesystem::remove_all(dir);

        std::vector<U8> payload(64, 0x42);
        VayuBCCacheEntryHeader header = make_header(1, 2);

        // Budget for 2 entries nominal
        S64 approx_entry_size = (S64)(sizeof(VayuBCCacheEntryHeader) + 16 + payload.size());
        VayuBCTextureCache::instance().initCache(dir, approx_entry_size * 2);

        LLUUID id_a, id_b, id_c;
        id_a.generate();
        id_b.generate();
        id_c.generate();

        VayuBCTextureCache::instance().writeEntry(id_a, 0, header, make_buffer(payload));
        VayuBCTextureCache::instance().writeEntry(id_b, 0, header, make_buffer(payload));
        VayuBCTextureCache::instance().writeEntry(id_c, 0, header, make_buffer(payload));

        // Flush writes to disk
        VayuBCTextureCache::instance().shutdown();

        // Adjust timestamps so id_a is oldest, id_c is newest
        std::string path_a = VayuBCTextureCache::instance().getFilePath(id_a, 0);
        std::string path_b = VayuBCTextureCache::instance().getFilePath(id_b, 0);
        std::string path_c = VayuBCTextureCache::instance().getFilePath(id_c, 0);

        time_t now = time(NULL);
        boost::system::error_code ec;
        boost::filesystem::last_write_time(path_a, now - 7200, ec);
        boost::filesystem::last_write_time(path_b, now - 3600, ec);
        boost::filesystem::last_write_time(path_c, now, ec);

        VayuBCTextureCache::instance().purge();

        ensure("Cache stays within its nominal size budget",
               VayuBCTextureCache::instance().getCurrentSize() <= approx_entry_size * 2);

        VayuBCCacheEntryHeader out_header;
        std::vector<U8> out_buffer;
        bool a_survived = VayuBCTextureCache::instance().readEntry(id_a, 0, 0, out_header, out_buffer);
        bool c_survived = VayuBCTextureCache::instance().readEntry(id_c, 0, 0, out_header, out_buffer);
        ensure("Most recently written entry survives purge", c_survived);
        ensure("Oldest entry was purged to make room", !a_survived);

        VayuBCTextureCache::instance().clear();
    }

    // Test 5: the in-RAM pending-write backlog stays under its ceiling, and
    // trimming it never discards the write that was just queued.
    template<> template<>
    void bc_texture_cache_object::test<5>()
    {
        auto dir = test_dir("pending_bound");
        std::filesystem::remove_all(dir);

        constexpr size_t kCount = 200;
        constexpr size_t kPayloadSize = 1024;

        const S64 disk_budget = (S64)(kCount * (kPayloadSize + 256) * 4);
        const S64 pending_budget = (S64)((kPayloadSize + 256) * 2);

        VayuBCTextureCache::instance().initCache(dir, disk_budget, pending_budget);

        LLUUID newest_id;
        for (size_t i = 0; i < kCount; ++i)
        {
            LLUUID id;
            id.generate();
            newest_id = id;

            VayuBCCacheEntryHeader header = make_header(2, 1);
            std::vector<U8> payload(kPayloadSize, (U8)(i & 0xFF));
            VayuBCTextureCache::instance().writeEntry(id, 0, header, make_buffer(std::move(payload)));

            ensure("Pending backlog stays within its ceiling",
                   VayuBCTextureCache::instance().getPendingBytes()
                       <= VayuBCTextureCache::instance().getMaxPendingBytes());
        }

        VayuBCCacheEntryHeader out_header;
        std::vector<U8> out_buffer;
        ensure("The most recently queued write is never the one dropped",
               VayuBCTextureCache::instance().readEntry(newest_id, 0, 0, out_header, out_buffer));
        ensure_equals("Surviving entry's payload is intact", out_buffer.size(), kPayloadSize);

        VayuBCTextureCache::instance().clear();
    }

    // Test 6: a backlog of many uniquely-keyed writes queued back-to-back is
    // fully drained by shutdown().
    template<> template<>
    void bc_texture_cache_object::test<6>()
    {
        auto dir = test_dir("drain_backlog");
        std::filesystem::remove_all(dir);

        constexpr size_t kCount = 1500;
        constexpr size_t kPayloadSize = 32;

        S64 budget = (S64)(kCount * (sizeof(VayuBCCacheEntryHeader) + kPayloadSize + 64) * 2);
        VayuBCTextureCache::instance().initCache(dir, budget);

        std::vector<LLUUID> ids(kCount);
        for (size_t i = 0; i < kCount; ++i)
        {
            ids[i].generate();
            VayuBCCacheEntryHeader header = make_header(2, 1);
            std::vector<U8> buffer(kPayloadSize, 0);
            memcpy(buffer.data(), &i, sizeof(i));

            VayuBCTextureCache::instance().writeEntry(ids[i], 0, header, make_buffer(std::move(buffer)));
        }

        // Force a full, deterministic drain
        VayuBCTextureCache::instance().shutdown();

        size_t verified = 0;
        for (size_t i = 0; i < kCount; ++i)
        {
            VayuBCCacheEntryHeader out_header;
            std::vector<U8> out_buffer;
            bool ok = VayuBCTextureCache::instance().readEntry(ids[i], 0, 0, out_header, out_buffer);
            if (!ok)
                continue;
            size_t stamped = 0;
            memcpy(&stamped, out_buffer.data(), sizeof(stamped));
            if (stamped == i)
                ++verified;
        }
        ensure_equals("Every queued entry is readable and intact after shutdown() drains the backlog",
                      verified, kCount);

        // Cross-check against the raw files across the 16 subdirectories on disk
        size_t bc_file_count = 0;
        std::error_code ec;
        for (const auto& dirent : std::filesystem::recursive_directory_iterator(dir, ec))
        {
            if (!ec && dirent.is_regular_file() && dirent.path().extension() == ".bc")
                ++bc_file_count;
        }
        ensure_equals("Every entry actually reached disk across subdirectories", bc_file_count, kCount);

        ensure_equals("Entry count matches the drained backlog",
                       VayuBCTextureCache::instance().getEntryCount(), kCount);

        VayuBCTextureCache::instance().clear();
    }

    // Test 7: 16 hex subdirectories ('0' - 'f') partition and file path structure
    template<> template<>
    void bc_texture_cache_object::test<7>()
    {
        auto dir = test_dir("subdirs");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        for (char ch : std::string("0123456789abcdef"))
        {
            ensure("Subdirectory exists", std::filesystem::is_directory(dir / std::string(1, ch)));
        }

        LLUUID id;
        id.generate();
        std::string expected_subdir(1, id.asString()[0]);
        std::string filepath = VayuBCTextureCache::instance().getFilePath(id, 0);

        ensure("File path contains correct hex subdir",
               filepath.find((dir / expected_subdir).string()) != std::string::npos);

        VayuBCTextureCache::instance().clear();
    }

    // Test 8: Migration of legacy flat root files into 16 subdirectories
    template<> template<>
    void bc_texture_cache_object::test<8>()
    {
        auto dir = test_dir("migration");
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);

        LLUUID id;
        id.generate();
        std::string legacy_filename = id.asString() + "_0.bc";
        std::filesystem::path legacy_path = dir / legacy_filename;

        // Create a dummy file in root
        {
            std::ofstream out(legacy_path, std::ios::binary);
            out << "dummy";
        }
        ensure("Legacy file exists in root", std::filesystem::exists(legacy_path));

        // Init cache should trigger migration
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        ensure("Legacy file removed from root", !std::filesystem::exists(legacy_path));
        std::string expected_subdir(1, id.asString()[0]);
        std::filesystem::path migrated_path = dir / expected_subdir / legacy_filename;
        ensure("File migrated into hex subdirectory", std::filesystem::exists(migrated_path));

        VayuBCTextureCache::instance().clear();
    }

    // Test 9: Atomic byte accounting (addBytesWritten)
    template<> template<>
    void bc_texture_cache_object::test<9>()
    {
        auto dir = test_dir("accounting");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        S64 initial_size = VayuBCTextureCache::instance().getCurrentSize();
        VayuBCTextureCache::instance().addBytesWritten(5000);
        ensure_equals("addBytesWritten increases size",
                      VayuBCTextureCache::instance().getCurrentSize(), initial_size + 5000);

        VayuBCTextureCache::instance().addBytesWritten(-2000);
        ensure_equals("addBytesWritten with negative decreases size",
                      VayuBCTextureCache::instance().getCurrentSize(), initial_size + 3000);

        VayuBCTextureCache::instance().clear();
        VayuBCTextureCache::instance().shutdown();
    }
}
