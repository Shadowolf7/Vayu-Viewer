/**
 * @file llbctexturecache_test.cpp
 * @brief Unit tests for VayuBCTextureCache
 */

#include "linden_common.h"

#include "../test/lltut.h"

#include "../vayubctexturecache.h"

#include <filesystem>

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

        VayuBCTextureCache::instance().writeEntry(id, 0, header, buffer);

        VayuBCCacheEntryHeader read_header;
        std::vector<U8> read_buffer;
        bool ok = VayuBCTextureCache::instance().readEntry(id, 0, 0, read_header, read_buffer);

        ensure("Write-then-read succeeds", ok);
        ensure_equals("Format round-trips", read_header.mFormat, header.mFormat);
        ensure_equals("Preset round-trips", read_header.mPreset, header.mPreset);
        ensure_equals("Width round-trips", read_header.mWidth, header.mWidth);
        ensure("Buffer bytes round-trip", read_buffer == buffer);

        VayuBCTextureCache::instance().purge();
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

        VayuBCTextureCache::instance().purge();
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
        VayuBCTextureCache::instance().writeEntry(id, 0, header, buffer);

        VayuBCCacheEntryHeader out_header;
        std::vector<U8> out_buffer;

        bool below = VayuBCTextureCache::instance().readEntry(id, 0, /*min_preset*/ 3, out_header, out_buffer);
        ensure("Cached-below-configured preset is a miss", !below);

        bool at_or_below = VayuBCTextureCache::instance().readEntry(id, 0, /*min_preset*/ 1, out_header, out_buffer);
        ensure("Cached-at-configured preset is a hit", at_or_below);

        VayuBCTextureCache::instance().purge();
    }

    // Test 4: exceeding the size budget evicts the least-recently-touched entry
    template<> template<>
    void bc_texture_cache_object::test<4>()
    {
        auto dir = test_dir("eviction");
        std::filesystem::remove_all(dir);

        std::vector<U8> payload(64, 0x42);
        VayuBCCacheEntryHeader header = make_header(1, 2);

        // Budget for roughly 2 entries; a 3rd write should evict the oldest.
        S64 approx_entry_size = (S64)(sizeof(header) + payload.size() + 16);
        VayuBCTextureCache::instance().initCache(dir, approx_entry_size * 2);

        LLUUID id_a, id_b, id_c;
        id_a.generate();
        id_b.generate();
        id_c.generate();

        VayuBCTextureCache::instance().writeEntry(id_a, 0, header, payload);
        VayuBCTextureCache::instance().writeEntry(id_b, 0, header, payload);
        VayuBCTextureCache::instance().writeEntry(id_c, 0, header, payload);

        ensure("Cache stays within its size budget", VayuBCTextureCache::instance().getCurrentSize() <= approx_entry_size * 2);

        VayuBCCacheEntryHeader out_header;
        std::vector<U8> out_buffer;
        bool a_survived = VayuBCTextureCache::instance().readEntry(id_a, 0, 0, out_header, out_buffer);
        bool c_survived = VayuBCTextureCache::instance().readEntry(id_c, 0, 0, out_header, out_buffer);
        ensure("Most recently written entry survives eviction", c_survived);
        ensure("Oldest entry was evicted to make room", !a_survived);

        VayuBCTextureCache::instance().purge();
    }

    // Test 5: a backlog of many uniquely-keyed writes queued back-to-back is
    // fully drained by shutdown() - regression test for the writer-pool
    // deadlock fixed by replacing one WorkQueue ticket per queued key with a
    // single self-looping drain pass (see queuePendingWrite()/
    // drainPendingWrites() in vayubctexturecache.cpp). This doesn't attempt
    // to reproduce the deadlock itself - that needed the old per-key-post
    // shape plus real multi-threaded timing to saturate the WorkQueue, which
    // isn't practical to force deterministically from a single-threaded
    // test. What it verifies instead is the property the fix depends on:
    // an arbitrarily large backlog still gets completely flushed to disk by
    // the time shutdown()'s join() returns, with none of it silently
    // stranded.
    //
    // NOTE: this test calls shutdown(), which tears down the process-wide
    // singleton's writer pool. Keep it LAST in this file. initCache()
    // self-heals a null mWriterPool (see vayubctexturecache.cpp), so later
    // tests would still work even out of order, but a shutdown()-invoking
    // test is easiest to reason about kept at the end.
    template<> template<>
    void bc_texture_cache_object::test<5>()
    {
        auto dir = test_dir("drain_backlog");
        std::filesystem::remove_all(dir);

        constexpr size_t kCount = 1500; // > the old WorkQueue capacity (1024)
        constexpr size_t kPayloadSize = 32;

        // Generous budget - this test is about the drain loop, not
        // eviction, so keep every entry alive.
        S64 budget = (S64)(kCount * (sizeof(VayuBCCacheEntryHeader) + kPayloadSize + 64) * 2);
        VayuBCTextureCache::instance().initCache(dir, budget);

        std::vector<LLUUID> ids(kCount);
        for (size_t i = 0; i < kCount; ++i)
        {
            ids[i].generate();
            VayuBCCacheEntryHeader header = make_header(2, 1);
            std::vector<U8> buffer(kPayloadSize, 0);
            // Stamp the index into the payload so a misdirected/corrupted
            // write shows up as a content mismatch, not just a miss.
            memcpy(buffer.data(), &i, sizeof(i));

            VayuBCTextureCache::instance().writeEntry(ids[i], 0, header, buffer);
        }

        // Force a full, deterministic drain: shutdown()'s join() doesn't
        // return until drainPendingWrites() has emptied mPendingWrites
        // entirely.
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

        // Cross-check against the raw files on disk too, independent of any
        // in-memory bookkeeping readEntry() relies on.
        size_t bc_file_count = 0;
        std::error_code ec;
        for (const auto& dirent : std::filesystem::directory_iterator(dir, ec))
        {
            if (!ec && dirent.is_regular_file() && dirent.path().extension() == ".bc")
                ++bc_file_count;
        }
        ensure_equals("Every entry actually reached disk", bc_file_count, kCount);

        ensure_equals("In-memory index matches the drained backlog",
                       VayuBCTextureCache::instance().getEntryCount(), kCount);

        VayuBCTextureCache::instance().purge();
    }
}
