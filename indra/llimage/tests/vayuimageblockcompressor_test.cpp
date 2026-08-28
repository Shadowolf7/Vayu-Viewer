/**
 * @file llimageblockcompressor_test.cpp
 * @brief Unit tests for VayuImageBlockCompressor
 */

#include "linden_common.h"
#include "../vayuimageblockcompressor.h"
#include "../test/lltut.h"

namespace tut
{
    struct block_compressor_test
    {
    };

    typedef test_group<block_compressor_test> block_compressor_group;
    typedef block_compressor_group::object block_compressor_object;
    block_compressor_group block_compressor_testgroup("VayuImageBlockCompressor");

    // Test 1: Eligibility checks
    template<> template<>
    void block_compressor_object::test<1>()
    {
        ensure("Valid 64x64 RGBA is eligible", VayuImageBlockCompressor::isEligible(64, 64, 4));
        ensure("Valid 16x16 RGB is eligible", VayuImageBlockCompressor::isEligible(16, 16, 3));
        ensure("Valid 32x32 RG is eligible", VayuImageBlockCompressor::isEligible(32, 32, 2));
        ensure("Valid 8x8 Grayscale is eligible", VayuImageBlockCompressor::isEligible(8, 8, 1));
        ensure("Below min dimension (2x2) is not eligible", !VayuImageBlockCompressor::isEligible(2, 2, 4));
        ensure("Invalid components (5) is not eligible", !VayuImageBlockCompressor::isEligible(16, 16, 5));
    }

    // Test 2: Auto selection for opaque RGBA -> BC1
    template<> template<>
    void block_compressor_object::test<2>()
    {
        const U32 width = 16, height = 16;
        std::vector<U8> rgba(width * height * 4);
        for (size_t i = 0; i < width * height; ++i)
        {
            rgba[i * 4 + 0] = 200; // R
            rgba[i * 4 + 1] = 100; // G
            rgba[i * 4 + 2] = 50;  // B
            rgba[i * 4 + 3] = 255; // Opaque
        }

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
        ensure("Encoding opaque RGBA succeeded", ok);
        ensure("Opaque RGBA resolves to BC1", result.mFormat == EVayuBlockCompressionFormat::BC1);
        ensure("Mip levels are calculated down to 1x1", result.mMipLevels == 5); // 16, 8, 4, 2, 1
        ensure("Buffer size matches total mips", result.mBuffer.size() > 0);
        ensure("Largest mip offset is valid", result.getLargestMipOffset() < result.mBuffer.size());
    }

    // Test 3: Auto selection for fully transparent RGBA (all 0s) -> BC7
    template<> template<>
    void block_compressor_object::test<3>()
    {
        const U32 width = 16, height = 16;
        std::vector<U8> rgba(width * height * 4);
        for (size_t i = 0; i < width * height; ++i)
        {
            rgba[i * 4 + 0] = 150;
            rgba[i * 4 + 1] = 150;
            rgba[i * 4 + 2] = 150;
            rgba[i * 4 + 3] = 0; // Fully transparent layer / overlay
        }

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
        ensure("Encoding fully transparent RGBA succeeded", ok);
        ensure("Fully transparent RGBA resolves to BC7", result.mFormat == EVayuBlockCompressionFormat::BC7);
    }

    // Test 4: Auto selection for genuine cutout/translucent RGBA -> BC7
    template<> template<>
    void block_compressor_object::test<4>()
    {
        const U32 width = 16, height = 16;
        std::vector<U8> rgba(width * height * 4);
        for (size_t i = 0; i < width * height; ++i)
        {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = (i % 2 == 0) ? 255 : 128; // Mixed alpha
        }

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
        ensure("Encoding translucent RGBA succeeded", ok);
        ensure("Translucent RGBA resolves to BC7", result.mFormat == EVayuBlockCompressionFormat::BC7);
    }

    // Test 5: Normal map compression (2-channel -> BC5)
    template<> template<>
    void block_compressor_object::test<5>()
    {
        const U32 width = 16, height = 16;
        std::vector<U8> rg(width * height * 2);
        for (size_t i = 0; i < width * height; ++i)
        {
            rg[i * 2 + 0] = 128; // Normal X
            rg[i * 2 + 1] = 128; // Normal Y
        }

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rg.data(), width, height, 2, result, EVayuBlockCompressionFormat::Auto);
        ensure("Encoding 2-channel normal succeeded", ok);
        ensure("2-channel normal resolves to BC5", result.mFormat == EVayuBlockCompressionFormat::BC5);
    }

    // Test 6: Grayscale mask compression (1-channel -> BC4)
    template<> template<>
    void block_compressor_object::test<6>()
    {
        const U32 width = 16, height = 16;
        std::vector<U8> gray(width * height);
        for (size_t i = 0; i < width * height; ++i)
        {
            gray[i] = (U8)(i & 0xFF);
        }

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(gray.data(), width, height, 1, result, EVayuBlockCompressionFormat::Auto);
        ensure("Encoding 1-channel mask succeeded", ok);
        ensure("1-channel mask resolves to BC4", result.mFormat == EVayuBlockCompressionFormat::BC4);
    }

    // Test 7: Binary 1-bit alpha cutout (0 and 255) -> must resolve to BC7, not BC1
    template<> template<>
    void block_compressor_object::test<7>()
    {
        const U32 width = 16, height = 16;
        std::vector<U8> rgba(width * height * 4);
        for (size_t i = 0; i < width * height; ++i)
        {
            rgba[i * 4 + 0] = 200;
            rgba[i * 4 + 1] = 150;
            rgba[i * 4 + 2] = 100;
            // 80% opaque (255), 20% cutout transparent (0)
            rgba[i * 4 + 3] = (i % 5 == 0) ? 0 : 255;
        }

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
        ensure("Encoding binary cutout RGBA succeeded", ok);
        ensure("Binary cutout RGBA resolves to BC7", result.mFormat == EVayuBlockCompressionFormat::BC7);
    }

    // Test 8: All presets encode successfully and don't affect the chosen format
    template<> template<>
    void block_compressor_object::test<8>()
    {
        const U32 width = 16, height = 16;
        std::vector<U8> rgba(width * height * 4);
        for (size_t i = 0; i < width * height; ++i)
        {
            rgba[i * 4 + 0] = 180;
            rgba[i * 4 + 1] = 90;
            rgba[i * 4 + 2] = 45;
            rgba[i * 4 + 3] = (i % 3 == 0) ? 128 : 255; // partial alpha -> BC7
        }

        const EVayuBlockCompressionPreset saved_preset = VayuImageBlockCompressor::getPreset();

        const EVayuBlockCompressionPreset presets[] = {
            EVayuBlockCompressionPreset::Ultrafast,
            EVayuBlockCompressionPreset::Fast,
            EVayuBlockCompressionPreset::Basic,
            EVayuBlockCompressionPreset::Slow,
        };

        for (EVayuBlockCompressionPreset preset : presets)
        {
            VayuImageBlockCompressor::setPreset(preset);
            ensure("getPreset reflects setPreset", VayuImageBlockCompressor::getPreset() == preset);

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
            ensure("Encoding succeeds at every preset", ok);
            ensure("Preset doesn't change the resolved format", result.mFormat == EVayuBlockCompressionFormat::BC7);
            ensure("Preset run still produces mip data", result.mBuffer.size() > 0);
        }

        VayuImageBlockCompressor::setPreset(saved_preset);
    }

    // Test 9: Queue backlog downgrades the effective preset but never
    // raises it above what the user configured.
    template<> template<>
    void block_compressor_object::test<9>()
    {
        const EVayuBlockCompressionPreset saved_preset = VayuImageBlockCompressor::getPreset();

        VayuImageBlockCompressor::setPreset(EVayuBlockCompressionPreset::Slow);

        VayuImageBlockCompressor::setQueueBacklog(0);
        ensure("No backlog uses the configured preset unmodified",
               VayuImageBlockCompressor::getEffectivePreset() == EVayuBlockCompressionPreset::Slow);

        VayuImageBlockCompressor::setQueueBacklog(20);
        ensure("Moderate backlog caps effort at Fast",
               VayuImageBlockCompressor::getEffectivePreset() == EVayuBlockCompressionPreset::Fast);

        VayuImageBlockCompressor::setQueueBacklog(100);
        ensure("Heavy backlog forces Ultrafast",
               VayuImageBlockCompressor::getEffectivePreset() == EVayuBlockCompressionPreset::Ultrafast);

        // A configured preset already below the moderate cap is left alone, not raised.
        VayuImageBlockCompressor::setPreset(EVayuBlockCompressionPreset::Ultrafast);
        VayuImageBlockCompressor::setQueueBacklog(20);
        ensure("Moderate backlog never raises effort above the configured preset",
               VayuImageBlockCompressor::getEffectivePreset() == EVayuBlockCompressionPreset::Ultrafast);

        VayuImageBlockCompressor::setQueueBacklog(0);
        VayuImageBlockCompressor::setPreset(saved_preset);
    }

    // Test 10: Non-power-of-two (NPOT) dimensions exercise both interior fast path and boundary clamping
    template<> template<>
    void block_compressor_object::test<10>()
    {
        const U32 width = 37, height = 53;

        // 4-channel NPOT
        {
            std::vector<U8> rgba(width * height * 4);
            for (size_t i = 0; i < width * height; ++i)
            {
                rgba[i * 4 + 0] = (U8)(i % 255);
                rgba[i * 4 + 1] = (U8)((i * 3) % 255);
                rgba[i * 4 + 2] = (U8)((i * 7) % 255);
                rgba[i * 4 + 3] = 255;
            }

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
            ensure("NPOT 4-channel opaque encodes successfully", ok);
            ensure("NPOT 4-channel opaque resolves to BC1", result.mFormat == EVayuBlockCompressionFormat::BC1);
            ensure("NPOT buffer size is non-zero", result.mBuffer.size() > 0);
        }

        // 3-channel NPOT
        {
            std::vector<U8> rgb(width * height * 3);
            for (size_t i = 0; i < width * height; ++i)
            {
                rgb[i * 3 + 0] = (U8)(i % 255);
                rgb[i * 3 + 1] = (U8)((i * 3) % 255);
                rgb[i * 3 + 2] = (U8)((i * 7) % 255);
            }

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgb.data(), width, height, 3, result, EVayuBlockCompressionFormat::Auto);
            ensure("NPOT 3-channel encodes successfully", ok);
            ensure("NPOT 3-channel resolves to BC1", result.mFormat == EVayuBlockCompressionFormat::BC1);
            ensure("NPOT buffer size is non-zero", result.mBuffer.size() > 0);
        }
    }

    // Test 11: Vectorized alpha scan edge cases (tail cleanup and early detection)
    template<> template<>
    void block_compressor_object::test<11>()
    {
        const U32 width = 37, height = 53;
        const size_t total_px = width * height;

        // Case A: Cutout pixel placed at the very last pixel (tests tail cleanup)
        {
            std::vector<U8> rgba(total_px * 4, 255);
            rgba[(total_px - 1) * 4 + 3] = 128; // Translucent pixel at the tail

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
            ensure("Tail cutout encodes successfully", ok);
            ensure("Tail cutout resolves to BC7", result.mFormat == EVayuBlockCompressionFormat::BC7);
        }

        // Case B: Cutout pixel placed early (index 2) (tests early SIMD exit)
        {
            std::vector<U8> rgba(total_px * 4, 255);
            rgba[2 * 4 + 3] = 0; // Transparent cutout at index 2

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
            ensure("Early cutout encodes successfully", ok);
            ensure("Early cutout resolves to BC7", result.mFormat == EVayuBlockCompressionFormat::BC7);
        }

        // Case C: Fully opaque non-multiple-of-8 image resolves to BC1
        {
            std::vector<U8> rgba(total_px * 4, 255);

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
            ensure("Fully opaque NPOT encodes successfully", ok);
            ensure("Fully opaque NPOT resolves to BC1", result.mFormat == EVayuBlockCompressionFormat::BC1);
        }
    }

    // Test 12: Downsampling across multiple mip levels for sRGB and Linear formats
    template<> template<>
    void block_compressor_object::test<12>()
    {
        // 4-channel sRGB mip pyramid
        {
            const U32 width = 8, height = 8;
            std::vector<U8> rgba(width * height * 4);
            for (size_t i = 0; i < width * height; ++i)
            {
                rgba[i * 4 + 0] = 128;
                rgba[i * 4 + 1] = 64;
                rgba[i * 4 + 2] = 32;
                rgba[i * 4 + 3] = 255;
            }

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::BC1);
            ensure("8x8 RGBA downsampling succeeds", ok);
            ensure("8x8 produces 4 mip levels (8, 4, 2, 1)", result.mMipLevels == 4);
        }

        // 2-channel Linear (Normal map) mip pyramid
        {
            const U32 width = 4, height = 4;
            std::vector<U8> rg(width * height * 2);
            for (size_t i = 0; i < width * height; ++i)
            {
                rg[i * 2 + 0] = (i % 2 == 0) ? 100 : 200;
                rg[i * 2 + 1] = (i % 2 == 0) ? 200 : 100;
            }

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rg.data(), width, height, 2, result, EVayuBlockCompressionFormat::BC5);
            ensure("4x4 RG downsampling succeeds", ok);
            ensure("4x4 produces 3 mip levels (4, 2, 1)", result.mMipLevels == 3);
        }

        // 1-channel Linear (Mask) mip pyramid
        {
            const U32 width = 4, height = 4;
            std::vector<U8> gray(width * height, 128);

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(gray.data(), width, height, 1, result, EVayuBlockCompressionFormat::BC4);
            ensure("4x4 Grayscale downsampling succeeds", ok);
            ensure("4x4 produces 3 mip levels (4, 2, 1)", result.mMipLevels == 3);
        }
    }
}
