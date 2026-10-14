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
#include <thread>
#include <utility>
#include <vector>

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

    static VayuBCCacheEntryHeader make_header(U8 format, U8 preset, U8 is_mask = 1)
    {
        VayuBCCacheEntryHeader h;
        h.mFormat = format;
        h.mPreset = preset;
        h.mIsMask = is_mask;
        h.mDiscardLevel = 0;
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
        VayuBCCacheEntryHeader header = make_header(3, 2, 1);
        std::vector<U8> buffer = { 1, 2, 3, 4, 5, 6, 7, 8 };

        VayuBCTextureCache::instance().writeEntry(id, 0, header, make_buffer(buffer));

        // Writes are asynchronous on the cache's writer pool; flush before reading.
        VayuBCTextureCache::instance().waitForPendingWrites();

        VayuBCCacheEntryHeader read_header;
        std::vector<U8> read_buffer;
        bool ok = VayuBCTextureCache::instance().readEntry(id, 0, read_header, read_buffer);

        ensure("Write-then-read succeeds", ok);
        ensure_equals("Format round-trips", read_header.mFormat, header.mFormat);
        ensure_equals("Preset round-trips", read_header.mPreset, header.mPreset);
        ensure_equals("IsMask round-trips", read_header.mIsMask, header.mIsMask);
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
        bool ok = VayuBCTextureCache::instance().readEntry(id, 0, header, buffer);
        ensure("Unknown entry is a miss", !ok);

        VayuBCTextureCache::instance().clear();
    }

    // Test 3: a cached entry hits immediately regardless of encode preset (zero penalty)
    template<> template<>
    void bc_texture_cache_object::test<3>()
    {
        auto dir = test_dir("immediate_hit");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        LLUUID id;
        id.generate();
        VayuBCCacheEntryHeader header = make_header(3, 1 /* Fast */);
        std::vector<U8> buffer = { 9, 9, 9 };
        VayuBCTextureCache::instance().writeEntry(id, 0, header, make_buffer(buffer));

        // Writes are asynchronous on the cache's writer pool; flush before reading.
        VayuBCTextureCache::instance().waitForPendingWrites();

        VayuBCCacheEntryHeader out_header;
        std::vector<U8> out_buffer;

        bool hit = VayuBCTextureCache::instance().readEntry(id, 0, out_header, out_buffer);
        ensure("Cached entry hits immediately without preset penalty", hit);
        ensure_equals("Read buffer matches", out_buffer, buffer);

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
        std::string path_a = VayuBCTextureCache::instance().getFilePath(id_a, ".bc1");
        std::string path_b = VayuBCTextureCache::instance().getFilePath(id_b, ".bc1");
        std::string path_c = VayuBCTextureCache::instance().getFilePath(id_c, ".bc1");

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
        bool a_survived = VayuBCTextureCache::instance().readEntry(id_a, 0, out_header, out_buffer);
        bool c_survived = VayuBCTextureCache::instance().readEntry(id_c, 0, out_header, out_buffer);
        ensure("Most recently written entry survives purge", c_survived);
        ensure("Oldest entry was purged to make room", !a_survived);

        VayuBCTextureCache::instance().clear();
    }

    // Test 5: Concurrent multi-threaded direct worker writes across multiple
    // threads execute lock-free without queues, dropped writes, or data races.
    template<> template<>
    void bc_texture_cache_object::test<5>()
    {
        auto dir = test_dir("concurrent_worker_writes");
        std::filesystem::remove_all(dir);

        constexpr size_t kNumThreads = 8;
        constexpr size_t kWritesPerThread = 25;
        constexpr size_t kTotalWrites = kNumThreads * kWritesPerThread;
        constexpr size_t kPayloadSize = 512;

        const S64 disk_budget = (S64)(kTotalWrites * (kPayloadSize + 256) * 4);
        VayuBCTextureCache::instance().initCache(dir, disk_budget);

        std::vector<std::vector<LLUUID>> thread_ids(kNumThreads);
        std::vector<std::thread> workers;
        workers.reserve(kNumThreads);

        for (size_t t = 0; t < kNumThreads; ++t)
        {
            thread_ids[t].resize(kWritesPerThread);
            for (size_t i = 0; i < kWritesPerThread; ++i)
            {
                thread_ids[t][i].generate();
            }

            workers.emplace_back([t, &thread_ids]() {
                for (size_t i = 0; i < kWritesPerThread; ++i)
                {
                    VayuBCCacheEntryHeader header = make_header(2, 1);
                    std::vector<U8> payload(kPayloadSize);
                    U8 pattern = static_cast<U8>((t * 31 + i) & 0xFF);
                    std::fill(payload.begin(), payload.end(), pattern);

                    VayuBCTextureCache::instance().writeEntry(thread_ids[t][i], 0, header,
                                                              make_buffer(std::move(payload)));
                }
            });
        }

        for (auto& w : workers)
        {
            w.join();
        }

        // Threads only posted their writes to the cache's writer pool; flush
        // before verifying anything on disk.
        VayuBCTextureCache::instance().waitForPendingWrites();

        // Verify all concurrently written entries were persisted intact without drops
        size_t verified = 0;
        for (size_t t = 0; t < kNumThreads; ++t)
        {
            for (size_t i = 0; i < kWritesPerThread; ++i)
            {
                VayuBCCacheEntryHeader out_header;
                std::vector<U8> out_buffer;
                if (VayuBCTextureCache::instance().readEntry(thread_ids[t][i], 0, out_header, out_buffer))
                {
                    U8 expected_pattern = static_cast<U8>((t * 31 + i) & 0xFF);
                    if (out_buffer.size() == kPayloadSize && out_buffer[0] == expected_pattern)
                    {
                        ++verified;
                    }
                }
            }
        }

        ensure_equals("All concurrent worker writes succeeded and persisted intact", verified, kTotalWrites);
        ensure_equals("Atomic entry count tracks all concurrent writes",
                      VayuBCTextureCache::instance().getEntryCount(), kTotalWrites);
        ensure("Total cache size is strictly positive", VayuBCTextureCache::instance().getCurrentSize() > 0);

        VayuBCTextureCache::instance().clear();
    }

    // Test 6: Many uniquely-keyed writes written back-to-back are immediately
    // persisted to disk and shutdown() completes deterministically.
    template<> template<>
    void bc_texture_cache_object::test<6>()
    {
        auto dir = test_dir("immediate_persistence");
        std::filesystem::remove_all(dir);

        constexpr size_t kCount = 1000;
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

        // Direct writes are already persisted; shutdown() completes cleanly without backlog drain delay
        VayuBCTextureCache::instance().shutdown();

        size_t verified = 0;
        for (size_t i = 0; i < kCount; ++i)
        {
            VayuBCCacheEntryHeader out_header;
            std::vector<U8> out_buffer;
            bool ok = VayuBCTextureCache::instance().readEntry(ids[i], 0, out_header, out_buffer);
            if (!ok)
                continue;
            size_t stamped = 0;
            memcpy(&stamped, out_buffer.data(), sizeof(stamped));
            if (stamped == i)
                ++verified;
        }
        ensure_equals("Every direct worker write is readable and intact", verified, kCount);

        // Cross-check against the raw files across the 16 subdirectories on disk
        size_t bc_file_count = 0;
        std::error_code ec;
        for (const auto& dirent : std::filesystem::recursive_directory_iterator(dir, ec))
        {
            if (!ec && dirent.is_regular_file())
            {
                std::string ext = dirent.path().extension().string();
                if (!ext.empty() && ext.back() == 'n')
                {
                    ext.pop_back();
                }
                if (ext == ".bc1" || ext == ".bc3" || ext == ".bc4" ||
                    ext == ".bc5" || ext == ".bc7")
                {
                    ++bc_file_count;
                }
            }
        }
        ensure_equals("Every entry actually reached disk across subdirectories", bc_file_count, kCount);

        ensure_equals("Entry count matches total direct writes",
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
        std::string filepath = VayuBCTextureCache::instance().getFilePath(id);

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

    // Test 10: Directory self-healing on write if subdirectories were deleted externally
    template<> template<>
    void bc_texture_cache_object::test<10>()
    {
        auto dir = test_dir("self_healing_write");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        // Simulate external deletion of all hex subdirectories
        for (char ch : std::string("0123456789abcdef"))
        {
            std::filesystem::remove_all(dir / std::string(1, ch));
            ensure("Subdirectory deleted", !std::filesystem::exists(dir / std::string(1, ch)));
        }

        LLUUID id;
        id.generate();
        VayuBCCacheEntryHeader header = make_header(3, 2, 1);
        std::vector<U8> buffer = { 10, 20, 30, 40 };

        // writeEntry should self-heal the missing directory structure
        VayuBCTextureCache::instance().writeEntry(id, 0, header, make_buffer(buffer));

        // Let the write flush
        VayuBCTextureCache::instance().shutdown();

        // Hex subdirectory must have been recreated
        std::string expected_subdir(1, id.asString()[0]);
        ensure("Subdirectory was self-healed", std::filesystem::is_directory(dir / expected_subdir));

        // Cache entry must be readable
        VayuBCCacheEntryHeader read_header;
        std::vector<U8> read_buffer;
        bool ok = VayuBCTextureCache::instance().readEntry(id, 0, read_header, read_buffer);
        ensure("Read entry succeeds after self-healing write", ok);
        ensure("Buffer matches", read_buffer == buffer);

        VayuBCTextureCache::instance().clear();
    }

    // Test 11: Directory self-healing on clear if root cache dir was deleted externally
    template<> template<>
    void bc_texture_cache_object::test<11>()
    {
        auto dir = test_dir("self_healing_clear");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        // Simulate external purge destroying the entire cache directory
        std::filesystem::remove_all(dir);
        ensure("Cache directory deleted", !std::filesystem::exists(dir));

        // clear() should restore the cache directory and 16 hex subdirectories
        VayuBCTextureCache::instance().clear();

        ensure("Root cache directory restored", std::filesystem::is_directory(dir));
        for (char ch : std::string("0123456789abcdef"))
        {
            ensure("Subdirectory restored", std::filesystem::is_directory(dir / std::string(1, ch)));
        }

        // Writes should succeed in the restored hierarchy
        LLUUID id;
        id.generate();
        VayuBCCacheEntryHeader header = make_header(3, 2, 1);
        std::vector<U8> buffer = { 99, 88, 77 };

        VayuBCTextureCache::instance().writeEntry(id, 0, header, make_buffer(buffer));
        VayuBCTextureCache::instance().shutdown();

        VayuBCCacheEntryHeader read_header;
        std::vector<U8> read_buffer;
        bool ok = VayuBCTextureCache::instance().readEntry(id, 0, read_header, read_buffer);
        ensure("Read entry succeeds in restored cache", ok);
        ensure("Buffer matches", read_buffer == buffer);

        VayuBCTextureCache::instance().clear();
    }

    // Test 12: Dynamic sub-mip prefix slicing: Full-resolution cache entry (discard = 0)
    // satisfies coarser requests (discard = 1, discard = 2) with correctly truncated buffer
    // and adjusted header dimensions and mip counts.
    template<> template<>
    void bc_texture_cache_object::test<12>()
    {
        auto dir = test_dir("sub_mip_slicing");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        LLUUID id;
        id.generate();

        // 16x16 texture, format 1 (BC1, 8 bytes per block), 3 mips:
        // mip 0: 16x16 -> 4x4 blocks = 16 blocks * 8 = 128 bytes
        // mip 1: 8x8   -> 2x2 blocks =  4 blocks * 8 =  32 bytes
        // mip 2: 4x4   -> 1x1 block  =  1 block  * 8 =   8 bytes
        // Reverse layout in mBuffer:
        // [0..8):   mip 2 (4x4, 8 bytes)
        // [8..40):  mip 1 (8x8, 32 bytes)
        // [40..168): mip 0 (16x16, 128 bytes)
        VayuBCCacheEntryHeader header = make_header(1 /* BC1 */, 2);
        header.mWidth = 16;
        header.mHeight = 16;
        header.mMipLevels = 3;
        header.mDiscardLevel = 0;

        std::vector<U8> payload(168);
        std::fill_n(payload.data(), 8, 0xAA);
        std::fill_n(payload.data() + 8, 32, 0xBB);
        std::fill_n(payload.data() + 40, 128, 0xCC);

        VayuBCTextureCache::instance().writeEntry(id, 0, header, make_buffer(payload));
        VayuBCTextureCache::instance().shutdown();

        // Read at discard 0 (full res)
        {
            VayuBCCacheEntryHeader out_h;
            std::vector<U8> out_b;
            bool ok = VayuBCTextureCache::instance().readEntry(id, 0, out_h, out_b);
            ensure("Read at discard 0 succeeds", ok);
            ensure_equals("Discard 0 width", out_h.mWidth, 16u);
            ensure_equals("Discard 0 height", out_h.mHeight, 16u);
            ensure_equals("Discard 0 mips", out_h.mMipLevels, 3);
            ensure_equals("Discard 0 discard level", (int)out_h.mDiscardLevel, 0);
            ensure_equals("Discard 0 buffer size", out_b.size(), 168u);
            ensure("Discard 0 buffer matches payload", out_b == payload);
        }

        // Read at discard 1 (half res: 8x8, 2 mips, prefix size 40)
        {
            VayuBCCacheEntryHeader out_h;
            std::vector<U8> out_b;
            bool ok = VayuBCTextureCache::instance().readEntry(id, 1, out_h, out_b);
            ensure("Read at discard 1 succeeds via slicing", ok);
            ensure_equals("Discard 1 width", out_h.mWidth, 8u);
            ensure_equals("Discard 1 height", out_h.mHeight, 8u);
            ensure_equals("Discard 1 mips", out_h.mMipLevels, 2);
            ensure_equals("Discard 1 discard level", (int)out_h.mDiscardLevel, 1);
            ensure_equals("Discard 1 buffer size", out_b.size(), 40u);
            std::vector<U8> expected(payload.begin(), payload.begin() + 40);
            ensure("Discard 1 buffer prefix matches", out_b == expected);
        }

        // Read at discard 2 (quarter res: 4x4, 1 mip, prefix size 8)
        {
            VayuBCCacheEntryHeader out_h;
            std::vector<U8> out_b;
            bool ok = VayuBCTextureCache::instance().readEntry(id, 2, out_h, out_b);
            ensure("Read at discard 2 succeeds via slicing", ok);
            ensure_equals("Discard 2 width", out_h.mWidth, 4u);
            ensure_equals("Discard 2 height", out_h.mHeight, 4u);
            ensure_equals("Discard 2 mips", out_h.mMipLevels, 1);
            ensure_equals("Discard 2 discard level", (int)out_h.mDiscardLevel, 2);
            ensure_equals("Discard 2 buffer size", out_b.size(), 8u);
            std::vector<U8> expected(payload.begin(), payload.begin() + 8);
            ensure("Discard 2 buffer prefix matches", out_b == expected);
        }

        VayuBCTextureCache::instance().clear();
    }

    // Test 13: Overwrite prevention rule: Higher resolution (lower discard) overwrites
    // lower resolution, but coarser resolution (higher discard) never overwrites higher resolution.
    template<> template<>
    void bc_texture_cache_object::test<13>()
    {
        auto dir = test_dir("overwrite_rule");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        LLUUID id;
        id.generate();

        VayuBCCacheEntryHeader header0 = make_header(1, 2);
        header0.mDiscardLevel = 0;
        std::vector<U8> payload0 = { 0x11, 0x22, 0x33, 0x44 };

        // Write full resolution (discard 0)
        VayuBCTextureCache::instance().writeEntry(id, 0, header0, make_buffer(payload0));
        VayuBCTextureCache::instance().shutdown();

        // Attempt to overwrite with coarser slice (discard 2)
        VayuBCCacheEntryHeader header2 = make_header(1, 2);
        header2.mDiscardLevel = 2;
        std::vector<U8> payload2 = { 0xFF, 0xFF };
        VayuBCTextureCache::instance().writeEntry(id, 2, header2, make_buffer(payload2));
        VayuBCTextureCache::instance().shutdown();

        // Verify that discard 0 was NOT overwritten by discard 2
        VayuBCCacheEntryHeader read_h;
        std::vector<U8> read_b;
        bool ok = VayuBCTextureCache::instance().readEntry(id, 0, read_h, read_b);
        ensure("Entry still readable at discard 0", ok);
        ensure_equals("Discard level is still 0", (int)read_h.mDiscardLevel, 0);
        ensure("Payload was preserved", read_b == payload0);

        // Now test the opposite: start with discard 2, then write discard 0
        LLUUID id2;
        id2.generate();

        VayuBCTextureCache::instance().writeEntry(id2, 2, header2, make_buffer(payload2));
        VayuBCTextureCache::instance().shutdown();

        // Overwrite with higher resolution (discard 0)
        VayuBCTextureCache::instance().writeEntry(id2, 0, header0, make_buffer(payload0));
        VayuBCTextureCache::instance().shutdown();

        read_b.clear();
        ok = VayuBCTextureCache::instance().readEntry(id2, 0, read_h, read_b);
        ensure("Higher-resolution write overwrote lower-resolution entry", ok);
        ensure_equals("Discard level upgraded to 0", (int)read_h.mDiscardLevel, 0);
        ensure("Payload upgraded to full-res", read_b == payload0);

        VayuBCTextureCache::instance().clear();
    }

    // Test 14: A cache entry written by an older format version must be rejected and
    // removed on read, so a layout change can never serve mis-parsed bytes.
    //
    // This used to share a test body with a "<uuid>_<discard>.bc1" legacy read fallback.
    // That fallback never existed in the read path: commit 94c2eb0745 removed the
    // per-discard filename form, the getFilePath overload and the three-name probe, and
    // no file on disk uses the form. The test kept asserting it anyway, so it failed
    // against the current code. Only the stale-version half was live.
    template<> template<>
    void bc_texture_cache_object::test<14>()
    {
        auto dir = test_dir("stale_version");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        // Struct layout must match FileHeader in vayubctexturecache.cpp
        struct TestFileHeader
        {
            U32 mMagic = VayuBCTextureCache::kMagic;
            U32 mVersion = VayuBCTextureCache::kFormatVersion;
            VayuBCCacheEntryHeader mMeta;
            U64                    mBufferSize = 0;
        };

        VayuBCCacheEntryHeader header = make_header(1 /* BC1 */, 2);
        header.mWidth                 = 16;
        header.mHeight                = 16;
        header.mMipLevels             = 3;
        header.mDiscardLevel          = 0;
        const std::vector<U8> payload(168, 0x77);

        const auto write_raw = [&](const LLUUID& id, U32 version, const std::vector<U8>& bytes)
        {
            const std::string           subdir(1, id.asString()[0]);
            const std::filesystem::path p = dir / subdir / (id.asString() + ".bc1");
            TestFileHeader              fh;
            fh.mVersion    = version;
            fh.mMeta       = header;
            fh.mBufferSize = bytes.size();
            std::ofstream out(p, std::ios::binary);
            out.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
            out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
            return p;
        };

        // A current-version entry written the same way must still be readable, so a
        // failure below cannot be blamed on the raw-write harness itself.
        LLUUID good;
        good.generate();
        const auto             good_path = write_raw(good, VayuBCTextureCache::kFormatVersion, payload);
        VayuBCCacheEntryHeader out_h;
        std::vector<U8> out_b;
        ensure("same-version raw entry is readable", VayuBCTextureCache::instance().readEntry(good, 0, out_h, out_b));
        ensure("same-version payload round-trips", out_b == payload);

        // An older version must be refused and the file reaped.
        LLUUID stale;
        stale.generate();
        const auto stale_path = write_raw(stale, VayuBCTextureCache::kFormatVersion - 1, payload);
        ensure("Stale version file exists before read", std::filesystem::exists(stale_path));

        out_b.clear();
        ensure("Stale version file rejected", !VayuBCTextureCache::instance().readEntry(stale, 0, out_h, out_b));
        ensure("Stale version file removed from disk", !std::filesystem::exists(stale_path));

        // So must a file claiming our magic with a future version.
        LLUUID future;
        future.generate();
        const auto future_path = write_raw(future, VayuBCTextureCache::kFormatVersion + 1, payload);
        ensure("Future version file rejected", !VayuBCTextureCache::instance().readEntry(future, 0, out_h, out_b));
        ensure("Future version file removed from disk", !std::filesystem::exists(future_path));

        // And one whose magic is wrong, which must not be served or left behind either.
        LLUUID bogus;
        bogus.generate();
        const std::string subdir(1, bogus.asString()[0]);
        const auto        bogus_path = dir / subdir / (bogus.asString() + ".bc1");
        {
            TestFileHeader fh;
            fh.mMagic      = 0xDEADBEEF;
            fh.mMeta       = header;
            fh.mBufferSize = payload.size();
            std::ofstream out(bogus_path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
            out.write(reinterpret_cast<const char*>(payload.data()), (std::streamsize)payload.size());
        }
        ensure("Bad-magic file rejected", !VayuBCTextureCache::instance().readEntry(bogus, 0, out_h, out_b));
        ensure("Bad-magic file removed from disk", !std::filesystem::exists(bogus_path));

        ensure("Surviving good entry untouched", std::filesystem::exists(good_path));

        VayuBCTextureCache::instance().clear();
    }

    // Test 15: Single-file naming: written files have naming <uuid>.bc without discard suffix
    template<> template<>
    void bc_texture_cache_object::test<15>()
    {
        auto dir = test_dir("single_file_naming");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        LLUUID id;
        id.generate();
        VayuBCCacheEntryHeader header = make_header(1, 2);
        std::vector<U8> buffer = { 1, 2, 3, 4 };

        VayuBCTextureCache::instance().writeEntry(id, 0, header, make_buffer(buffer));
        VayuBCTextureCache::instance().shutdown();

        std::string expected_subdir(1, id.asString()[0]);
        std::filesystem::path new_path = dir / expected_subdir / (id.asString() + ".bc1");
        std::filesystem::path old_path = dir / expected_subdir / (id.asString() + "_0.bc1");

        ensure("File exists as <uuid>.bc1", std::filesystem::exists(new_path));
        ensure("File does NOT exist as <uuid>_0.bc1", !std::filesystem::exists(old_path));

        VayuBCTextureCache::instance().clear();
    }

    // Test 16: Direct filesystem overwrite protection:
    // When a high-resolution entry (discard 0) is written, a subsequent
    // lower-resolution entry (discard 2) written does not
    // overwrite or replace the high-resolution entry on disk.
    template<> template<>
    void bc_texture_cache_object::test<16>()
    {
        auto dir = test_dir("direct_overwrite_protection");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        LLUUID id;
        id.generate();

        VayuBCCacheEntryHeader header0 = make_header(1, 2);
        header0.mDiscardLevel = 0;
        std::vector<U8> payload0 = { 0xAA, 0xBB, 0xCC, 0xDD };

        VayuBCCacheEntryHeader header2 = make_header(1, 2);
        header2.mDiscardLevel = 2;
        std::vector<U8> payload2 = { 0x11, 0x22 };

        // Write high resolution entry
        VayuBCTextureCache::instance().writeEntry(id, 0, header0, make_buffer(payload0));

        // Attempt low resolution write for the same asset
        VayuBCTextureCache::instance().writeEntry(id, 2, header2, make_buffer(payload2));

        // Now drain everything cleanly to disk
        VayuBCTextureCache::instance().shutdown();

        // High resolution entry must have survived and not been overwritten
        VayuBCCacheEntryHeader read_h;
        std::vector<U8> read_b;
        bool ok = VayuBCTextureCache::instance().readEntry(id, 0, read_h, read_b);
        ensure("High resolution entry survived concurrent queue attempt", ok);
        ensure_equals("Discard level is 0", (int)read_h.mDiscardLevel, 0);
        ensure("Payload matches high-res payload", read_b == payload0);

        VayuBCTextureCache::instance().clear();
    }

    // Test 17: Coexistence of form + role for the same UUID.
    // Verifies that a single UUID written as both color (e.g. BC1) and normal (BC5)
    // coexists on disk under <uuid>.bc1 and <uuid>.bc5n, neither overwrites the other,
    // and each reads back with matching format.
    template<> template<>
    void bc_texture_cache_object::test<17>()
    {
        auto dir = test_dir("coexistence");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        LLUUID id;
        id.generate();

        // 1. Color entry (BC1 -> .bc1)
        VayuBCCacheEntryHeader header_color = make_header(VayuBCTextureCache::kFormatBC1, 2);
        header_color.mDiscardLevel = 0;
        std::vector<U8> payload_color = { 0xAA, 0xBB, 0xCC, 0xDD };

        // 2. Normal entry (BC5 -> .bc5n)
        VayuBCCacheEntryHeader header_normal = make_header(VayuBCTextureCache::kFormatBC5, 2);
        header_normal.mDiscardLevel = 0;
        header_normal.mComponents = 2;
        header_normal.mRole = VayuBCTextureCache::kRoleNormal;
        std::vector<U8> payload_normal = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };

        VayuBCTextureCache::instance().writeEntry(id, 0, header_color, make_buffer(payload_color));
        VayuBCTextureCache::instance().writeEntry(id, 0, header_normal, make_buffer(payload_normal));

        VayuBCTextureCache::instance().shutdown();

        std::string expected_subdir(1, id.asString()[0]);
        std::filesystem::path bc_path = dir / expected_subdir / (id.asString() + ".bc1");
        std::filesystem::path bc5_path = dir / expected_subdir / (id.asString() + ".bc5n");

        ensure("File exists as <uuid>.bc1", std::filesystem::exists(bc_path));
        ensure("File exists as <uuid>.bc5n", std::filesystem::exists(bc5_path));

        // Read back color entry
        VayuBCCacheEntryHeader read_color_h;
        std::vector<U8> read_color_b;
        bool ok_color = VayuBCTextureCache::instance().readEntry(id, 0, read_color_h, read_color_b, ".bc", VayuBCTextureCache::kFormatBC1);
        ensure("Color entry read succeeded", ok_color);
        ensure_equals("Color format is BC1", read_color_h.mFormat, VayuBCTextureCache::kFormatBC1);
        ensure("Color payload matches", read_color_b == payload_color);

        // Read back normal entry
        VayuBCCacheEntryHeader read_normal_h;
        std::vector<U8> read_normal_b;
        bool ok_normal = VayuBCTextureCache::instance().readEntry(id, 0, read_normal_h, read_normal_b, ".bc5", VayuBCTextureCache::kFormatBC5, VayuBCTextureCache::kRoleNormal);
        ensure("Normal entry read succeeded", ok_normal);
        ensure_equals("Normal format is BC5", read_normal_h.mFormat, VayuBCTextureCache::kFormatBC5);
        ensure("Normal payload matches", read_normal_b == payload_normal);

        // Same-format dual-role: an albedo that also encodes BC7 must not share a
        // file with a legacy-normal BC7 encode of the same UUID. They coexist as
        // <uuid>.bc7 (color) and <uuid>.bc7n (normal) - the last-writer-wins
        // collision across roles is structurally impossible.
        LLUUID id2;
        id2.generate();

        VayuBCCacheEntryHeader header_color_bc7 = make_header(VayuBCTextureCache::kFormatBC7, 2);
        header_color_bc7.mDiscardLevel = 0;
        header_color_bc7.mRole = VayuBCTextureCache::kRoleColor;
        VayuBCCacheEntryHeader header_legacy = make_header(VayuBCTextureCache::kFormatBC7, 2);
        header_legacy.mDiscardLevel = 0;
        header_legacy.mRole = VayuBCTextureCache::kRoleNormal;

        std::vector<U8> payload_color7 = { 0x01, 0x02, 0x03, 0x04 };
        std::vector<U8> payload_legacy = { 0x05, 0x06, 0x07, 0x08 };

        VayuBCTextureCache::instance().writeEntry(id2, 0, header_color_bc7, make_buffer(payload_color7));
        VayuBCTextureCache::instance().writeEntry(id2, 0, header_legacy, make_buffer(payload_legacy));
        VayuBCTextureCache::instance().shutdown();

        std::string subdir2(1, id2.asString()[0]);
        ensure("Color BC7 file exists as <uuid>.bc7",
               std::filesystem::exists(dir / subdir2 / (id2.asString() + ".bc7")));
        ensure("Normal BC7 file exists as <uuid>.bc7n",
               std::filesystem::exists(dir / subdir2 / (id2.asString() + ".bc7n")));

        VayuBCCacheEntryHeader read_bc7_h;
        std::vector<U8> read_bc7_b;
        bool ok_bc7 = VayuBCTextureCache::instance().readEntry(id2, 0, read_bc7_h, read_bc7_b, ".bc7", VayuBCTextureCache::kFormatBC7, VayuBCTextureCache::kRoleColor);
        ensure("Color BC7 reads back independently", ok_bc7 && read_bc7_b == payload_color7);

        VayuBCCacheEntryHeader read_legacy_h;
        std::vector<U8> read_legacy_b;
        bool ok_legacy = VayuBCTextureCache::instance().readEntry(id2, 0, read_legacy_h, read_legacy_b, ".bc7", VayuBCTextureCache::kFormatBC7, VayuBCTextureCache::kRoleNormal);
        ensure("Normal BC7 reads back independently of color BC7", ok_legacy && read_legacy_b == payload_legacy);

        VayuBCTextureCache::instance().clear();
    }

    // Test 18: readEntryHeader / readEntryRaw must agree with readEntry about what exists.
    // These back the incremental append path's cache probe: the worker stats a header to
    // decide whether it needs to encode at all, then reads the raw entry to merge the
    // coarser tail. If either disagreed with readEntry's own validation, the probe could
    // report a hit for an entry the real reader refuses, or miss one it would serve.
    template<>
    template<>
    void bc_texture_cache_object::test<18>()
    {
        auto dir = test_dir("header_raw_probe");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        LLUUID id;
        id.generate();

        VayuBCCacheEntryHeader header = make_header(1, 2);
        header.mDiscardLevel          = 2;
        header.mMipLevels             = 5;
        const std::vector<U8> payload = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
        VayuBCTextureCache::instance().writeEntry(id, 2, header, make_buffer(payload));
        VayuBCTextureCache::instance().waitForPendingWrites();

        VayuBCCacheEntryHeader h_hdr;
        const bool             ok_hdr =
            VayuBCTextureCache::instance().readEntryHeader(id, VayuBCTextureCache::kFormatBC1, VayuBCTextureCache::kRoleColor, h_hdr);
        ensure("Header probe finds a written entry", ok_hdr);
        ensure_equals("Header probe reports the discard level", (int)h_hdr.mDiscardLevel, 2);
        ensure_equals("Header probe reports the mip count", h_hdr.mMipLevels, 5);
        ensure_equals("Header probe reports the format", (int)h_hdr.mFormat, (int)VayuBCTextureCache::kFormatBC1);

        VayuBCCacheEntryHeader h_raw;
        std::vector<U8>        b_raw;
        const bool             ok_raw =
            VayuBCTextureCache::instance().readEntryRaw(id, VayuBCTextureCache::kFormatBC1, VayuBCTextureCache::kRoleColor, h_raw, b_raw);
        ensure("Raw probe finds the entry", ok_raw);
        ensure("Raw probe returns the exact payload", b_raw == payload);
        ensure_equals("Raw probe reports the discard level", (int)h_raw.mDiscardLevel, 2);

        // A format the entry was NOT written under must miss on both probes. This is the
        // shape of the bug that disabled the append path: probe .bc7 against a file really
        // stored as .bc1, miss, and re-encode the whole pyramid on every refinement.
        VayuBCCacheEntryHeader h_wrong;
        ensure(
            "Header probe misses on the wrong format",
            !VayuBCTextureCache::instance().readEntryHeader(id, VayuBCTextureCache::kFormatBC7, VayuBCTextureCache::kRoleColor, h_wrong));

        std::vector<U8> b_wrong;
        ensure("Raw probe misses on the wrong format",
               !VayuBCTextureCache::instance().readEntryRaw(id, VayuBCTextureCache::kFormatBC7, VayuBCTextureCache::kRoleColor, h_wrong,
                                                            b_wrong));

        // Colour and normal namespaces are separate files.
        ensure(
            "Header probe misses on the wrong role",
            !VayuBCTextureCache::instance().readEntryHeader(id, VayuBCTextureCache::kFormatBC1, VayuBCTextureCache::kRoleNormal, h_wrong));

        // An absent UUID must miss cleanly rather than yield a zeroed header.
        LLUUID absent;
        absent.generate();
        ensure("Header probe misses on an absent entry",
               !VayuBCTextureCache::instance().readEntryHeader(absent, VayuBCTextureCache::kFormatBC1, VayuBCTextureCache::kRoleColor,
                                                               h_wrong));

        VayuBCTextureCache::instance().clear();
        // shutdown(), not just clear(): it closes and resets the write pool. Leaving a live
        // pool on the singleton means static teardown runs ~ThreadPoolBase, whose close()
        // throws boost::fibers::lock_error out of a noexcept destructor -> std::terminate.
        // Tests 15/17 already do this; anything that calls initCache must too.
        VayuBCTextureCache::instance().shutdown();
    }

    // Test 19: appendEntry grows one file into a full pyramid, mip-exactly, across a
    // discard 3 -> 2 -> 1 -> 0 refinement stream. Each append adds exactly its gap to the
    // END of the buffer (largest-mip-last layout) and leaves earlier bytes untouched.
    //
    // The payload uses REAL BC1 block geometry, not arbitrary bytes: readEntry's mip
    // fallback recomputes per-level sizes with calcSubBufferBytes(), so a pyramid whose
    // stored byte counts do not match the format is not readable at coarser levels and the
    // test would be asserting the wrong thing.
    // A 16x16 BC1 base is 4x4 blocks = 128 bytes; each halving is 4x fewer, floor 1 block:
    //   mip 0 (16x16) 128 | mip 1 (8x8) 32 | mip 2 (4x4) 8 | mip 3 (2x2) 8 | mip 4 (1x1) 8
    template<>
    template<>
    void bc_texture_cache_object::test<19>()
    {
        auto dir = test_dir("append_pyramid");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 4 * 1024 * 1024);

        LLUUID id;
        id.generate();

        const S32    base_discard = 3;
        const U32    base         = 16;
        const size_t bc1_block    = 8;

        // Bytes for the mip at `level` of a 16x16 base, in BC1 block terms.
        const auto level_bytes = [&](S32 level) -> size_t
        {
            const U32 w = llmax(1u, base >> level);
            const U32 h = llmax(1u, base >> level);
            return ((w + 3) / 4) * ((h + 3) / 4) * bc1_block;
        };
        // A distinctive fill so a prefix/suffix mix-up would be caught.
        const auto fill = [](size_t n, U8 seed)
        {
            std::vector<U8> v(n);
            for (size_t i = 0; i < n; ++i)
                v[i] = (U8)(seed + (i * 7));
            return v;
        };

        // First sight: the whole coarser chain at discard 3 (mips 3 and 4, smallest first).
        std::vector<U8> expected = fill(level_bytes(4), 0x10);
        {
            const std::vector<U8> mip3 = fill(level_bytes(3), 0x40);
            expected.insert(expected.end(), mip3.begin(), mip3.end());
        }

        VayuBCCacheEntryHeader h3 = make_header(1, 2);
        h3.mDiscardLevel          = (U8)base_discard;
        h3.mMipLevels             = 2; // levels 3 and 4
        h3.mWidth                 = llmax(1u, base >> base_discard);
        h3.mHeight                = h3.mWidth;
        VayuBCTextureCache::instance().writeEntry(id, base_discard, h3, make_buffer(expected));
        VayuBCTextureCache::instance().waitForPendingWrites();

        for (S32 step = base_discard - 1; step >= 0; --step)
        {
            const std::vector<U8> new_mip = fill(level_bytes(step), (U8)(0x70 + step * 8));
            expected.insert(expected.end(), new_mip.begin(), new_mip.end());

            // A refinement adds exactly the one finer mip; the header describes the new level 0.
            VayuBCCacheEntryHeader hn = make_header(1, 2);
            hn.mDiscardLevel          = (U8)step;
            hn.mMipLevels             = 1;
            hn.mWidth                 = llmax(1u, base >> step);
            hn.mHeight                = hn.mWidth;

            ensure("append succeeds at each refinement step",
                   VayuBCTextureCache::instance().appendEntry(id, step, hn, make_buffer(new_mip), 1));
            VayuBCTextureCache::instance().waitForPendingWrites();

            VayuBCCacheEntryHeader got;
            std::vector<U8>        buf;
            ensure(
                "entry readable after append",
                VayuBCTextureCache::instance().readEntryRaw(id, VayuBCTextureCache::kFormatBC1, VayuBCTextureCache::kRoleColor, got, buf));

            ensure("appended buffer matches the expected pyramid", buf == expected);
            ensure_equals("header discard level tracks the finest mip", (int)got.mDiscardLevel, (int)step);
            ensure_equals("header mip count tracks the pyramid height", got.mMipLevels, (base_discard + 2) - step);
            ensure_equals("header width describes the new level 0", (int)got.mWidth, (int)(llmax(1u, base >> step)));
        }

        // The finished file must be a single entry holding the whole pyramid, and it must
        // be readable at every level through the normal reader -- which is the property
        // #97 depends on: coarse requests are served by slicing the finer file.
        size_t want_total = 0;
        for (S32 l = 0; l <= 4; ++l)
            want_total += level_bytes(l);
        ensure_equals("one file holds the entire pyramid", (int)expected.size(), (int)want_total);
        for (S32 d = 0; d <= base_discard; ++d)
        {
            VayuBCCacheEntryHeader rh;
            std::vector<U8>        rb;
            ensure("finished pyramid serves every discard level", VayuBCTextureCache::instance().readEntry(id, d, rh, rb));
            ensure_equals("served level reports its own discard level", (int)rh.mDiscardLevel, (int)d);
            ensure("served level returns a non-empty slice", !rb.empty());
        }

        // And a coarse read must reproduce exactly the bytes that were appended for it,
        // proving the merge spliced at the right offset rather than merely appending.
        VayuBCCacheEntryHeader rh;
        std::vector<U8>        rb;
        ensure("coarse read of the first-sight level hits", VayuBCTextureCache::instance().readEntry(id, base_discard, rh, rb));
        std::vector<U8> want_coarse(expected.begin(), expected.begin() + (int)(level_bytes(3) + level_bytes(4)));
        ensure("coarse slice is byte-exact", rb == want_coarse);

        VayuBCTextureCache::instance().clear();
        // shutdown(), not just clear(): it closes and resets the write pool. Leaving a live
        // pool on the singleton means static teardown runs ~ThreadPoolBase, whose close()
        // throws boost::fibers::lock_error out of a noexcept destructor -> std::terminate.
        // Tests 15/17 already do this; anything that calls initCache must too.
        VayuBCTextureCache::instance().shutdown();
    }

    // Test 20: appendEntry must refuse anything that would corrupt the pyramid -- a
    // non-contiguous gap, a finer-or-equal level, degenerate inputs, and a missing entry.
    // Each refusal must leave the existing entry byte-identical so the caller's fallback
    // full write is always safe.
    template<>
    template<>
    void bc_texture_cache_object::test<20>()
    {
        auto dir = test_dir("append_guards");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 4 * 1024 * 1024);

        LLUUID id;
        id.generate();
        VayuBCCacheEntryHeader h3      = make_header(1, 2);
        h3.mDiscardLevel               = 3;
        h3.mMipLevels                  = 4;
        h3.mWidth                      = 2;
        h3.mHeight                     = 2;
        const std::vector<U8> original = { 0x01, 0x02, 0x03, 0x04 };
        VayuBCTextureCache::instance().writeEntry(id, 3, h3, make_buffer(original));
        VayuBCTextureCache::instance().waitForPendingWrites();

        const auto still_original = [&]()
        {
            VayuBCCacheEntryHeader h;
            std::vector<U8>        b;
            ensure("entry still readable",
                   VayuBCTextureCache::instance().readEntryRaw(id, VayuBCTextureCache::kFormatBC1, VayuBCTextureCache::kRoleColor, h, b));
            return b == original;
        };

        // Non-contiguous: existing starts at discard 3, but this claims to add one mip
        // landing at discard 1, skipping discard 2 entirely.
        VayuBCCacheEntryHeader gap = make_header(1, 2);
        gap.mDiscardLevel          = 1;
        gap.mMipLevels             = 1;
        ensure("non-contiguous append is refused", !VayuBCTextureCache::instance().appendEntry(id, 1, gap, make_buffer({ 0xEE }), 1));
        ensure("refusal left the entry untouched (non-contiguous)", still_original());

        // Finer-or-equal: the existing entry already carries this level or finer.
        VayuBCCacheEntryHeader same = make_header(1, 2);
        same.mDiscardLevel          = 3;
        same.mMipLevels             = 1;
        ensure("equal-level append is refused", !VayuBCTextureCache::instance().appendEntry(id, 3, same, make_buffer({ 0xEE }), 1));
        ensure("refusal left the entry untouched (equal level)", still_original());

        VayuBCCacheEntryHeader coarser = make_header(1, 2);
        coarser.mDiscardLevel          = 4;
        coarser.mMipLevels             = 1;
        ensure("coarser append is refused", !VayuBCTextureCache::instance().appendEntry(id, 4, coarser, make_buffer({ 0xEE }), 1));
        ensure("refusal left the entry untouched (coarser)", still_original());

        VayuBCCacheEntryHeader h2 = make_header(1, 2);
        h2.mDiscardLevel          = 2;
        h2.mMipLevels             = 1;
        ensure("empty mip buffer is refused", !VayuBCTextureCache::instance().appendEntry(id, 2, h2, make_buffer({}), 1));
        ensure("zero mip count is refused", !VayuBCTextureCache::instance().appendEntry(id, 2, h2, make_buffer({ 0xEE }), 0));
        ensure("refusal left the entry untouched (degenerate)", still_original());

        LLUUID absent;
        absent.generate();
        ensure("append to an absent entry is refused",
               !VayuBCTextureCache::instance().appendEntry(absent, 2, h2, make_buffer({ 0xEE }), 1));

        // A legitimate contiguous append must still work, proving the guards are not
        // simply rejecting everything.
        ensure("contiguous append still accepted afterwards",
               VayuBCTextureCache::instance().appendEntry(id, 2, h2, make_buffer({ 0xEE }), 1));
        VayuBCTextureCache::instance().waitForPendingWrites();

        VayuBCCacheEntryHeader h;
        std::vector<U8>        b;
        ensure("entry readable after the good append",
               VayuBCTextureCache::instance().readEntryRaw(id, VayuBCTextureCache::kFormatBC1, VayuBCTextureCache::kRoleColor, h, b));
        ensure_equals("good append extended the buffer", (int)b.size(), (int)original.size() + 1);
        ensure_equals("good append kept the original prefix", (int)b[0], (int)original[0]);
        ensure_equals("good append appended at the tail", (int)b.back(), 0xEE);
        ensure_equals("good append lowered the discard level", (int)h.mDiscardLevel, 2);

        VayuBCTextureCache::instance().clear();
        // shutdown(), not just clear(): it closes and resets the write pool. Leaving a live
        // pool on the singleton means static teardown runs ~ThreadPoolBase, whose close()
        // throws boost::fibers::lock_error out of a noexcept destructor -> std::terminate.
        // Tests 15/17 already do this; anything that calls initCache must too.
        VayuBCTextureCache::instance().shutdown();
    }
}
