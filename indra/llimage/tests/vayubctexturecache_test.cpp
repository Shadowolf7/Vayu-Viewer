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
        std::string path_a = VayuBCTextureCache::instance().getFilePath(id_a);
        std::string path_b = VayuBCTextureCache::instance().getFilePath(id_b);
        std::string path_c = VayuBCTextureCache::instance().getFilePath(id_c);

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
               VayuBCTextureCache::instance().readEntry(newest_id, 0, out_header, out_buffer));
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
            bool ok = VayuBCTextureCache::instance().readEntry(ids[i], 0, out_header, out_buffer);
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

    // Test 14: Legacy fallback: Reading <uuid> when <uuid>.bc does not exist
    // falls back to legacy <uuid>_0.bc on disk and properly slices it.
    template<> template<>
    void bc_texture_cache_object::test<14>()
    {
        auto dir = test_dir("legacy_fallback");
        std::filesystem::remove_all(dir);
        VayuBCTextureCache::instance().initCache(dir, 1024 * 1024);

        LLUUID id;
        id.generate();

        // Write a legacy file <uuid>_0.bc directly to disk
        std::string expected_subdir(1, id.asString()[0]);
        std::string legacy_path = (dir / expected_subdir / (id.asString() + "_0.bc")).string();

        VayuBCCacheEntryHeader header = make_header(1 /* BC1 */, 2);
        header.mWidth = 16;
        header.mHeight = 16;
        header.mMipLevels = 3;
        header.mDiscardLevel = 0;

        std::vector<U8> payload(168, 0x77);

        // Struct layout must match FileHeader in vayubctexturecache.cpp
        struct TestFileHeader
        {
            U32 mMagic = VayuBCTextureCache::kMagic;
            U32 mVersion = VayuBCTextureCache::kFormatVersion;
            VayuBCCacheEntryHeader mMeta;
            U64 mBufferSize = 168;
        } fh;
        fh.mMeta = header;

        {
            std::ofstream out(legacy_path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
            out.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        }
        ensure("Legacy _0.bc file exists on disk", std::filesystem::exists(legacy_path));

        // Read using standard readEntry(id, 0) - should hit legacy file
        VayuBCCacheEntryHeader out_h;
        std::vector<U8> out_b;
        bool ok = VayuBCTextureCache::instance().readEntry(id, 0, out_h, out_b);
        ensure("Legacy fallback succeeds for discard 0", ok);
        ensure("Buffer matches legacy payload", out_b == payload);

        // Read using readEntry(id, 1) - should slice legacy file
        out_b.clear();
        ok = VayuBCTextureCache::instance().readEntry(id, 1, out_h, out_b);
        ensure("Legacy fallback slices to discard 1", ok);
        ensure_equals("Sliced width", out_h.mWidth, 8u);
        ensure_equals("Sliced height", out_h.mHeight, 8u);
        ensure_equals("Sliced buffer size", out_b.size(), 40u);

        // Verify that an outdated format version file (version < kFormatVersion) is rejected and removed
        LLUUID stale_id;
        stale_id.generate();
        std::string stale_subdir(1, stale_id.asString()[0]);
        std::filesystem::path stale_path = dir / stale_subdir / (stale_id.asString() + ".bc");
        TestFileHeader stale_fh = fh;
        stale_fh.mVersion = VayuBCTextureCache::kFormatVersion - 1;
        {
            std::ofstream out(stale_path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(&stale_fh), sizeof(stale_fh));
            out.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        }
        ensure("Stale version file exists before read", std::filesystem::exists(stale_path));
        bool stale_ok = VayuBCTextureCache::instance().readEntry(stale_id, 0, out_h, out_b);
        ensure("Stale version file rejected", !stale_ok);
        ensure("Stale version file removed from disk", !std::filesystem::exists(stale_path));

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
        std::filesystem::path new_path = dir / expected_subdir / (id.asString() + ".bc");
        std::filesystem::path old_path = dir / expected_subdir / (id.asString() + "_0.bc");

        ensure("File exists as <uuid>.bc", std::filesystem::exists(new_path));
        ensure("File does NOT exist as <uuid>_0.bc", !std::filesystem::exists(old_path));

        VayuBCTextureCache::instance().clear();
    }

    // Test 16: Asynchronous / rapid queue overwrite protection:
    // When a high-resolution entry (discard 0) is queued or flushing, a subsequent
    // lower-resolution entry (discard 2) queued before disk flush completes does not
    // overwrite or replace the high-resolution entry in mPendingIndex or mFlushing.
    template<> template<>
    void bc_texture_cache_object::test<16>()
    {
        auto dir = test_dir("async_overwrite_protection");
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

        // Queue high resolution write
        VayuBCTextureCache::instance().writeEntry(id, 0, header0, make_buffer(payload0));

        // Immediately queue low resolution write without calling shutdown() in between
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
}
