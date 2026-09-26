/**
 * @file llimageblockcompressor_test.cpp
 * @brief Unit tests for VayuImageBlockCompressor
 */

#include "linden_common.h"
#include "../vayuimageblockcompressor.h"
#include "../test/lltut.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

// Stubs for unit test harness
const U8* LLImageBase::getData() const { return NULL; }
U8* LLImageBase::getData() { return NULL; }
bool LLImageBase::isBufferInvalid() const { return false; }

namespace tut
{
    struct block_compressor_test
    {
    };

    typedef test_group<block_compressor_test> block_compressor_group;
    typedef block_compressor_group::object block_compressor_object;
    block_compressor_group block_compressor_testgroup("VayuImageBlockCompressor");

#if LL_DARWIN
    constexpr EVayuBlockCompressionFormat kExpectedTranslucentFormat = EVayuBlockCompressionFormat::BC3;
#else
    constexpr EVayuBlockCompressionFormat kExpectedTranslucentFormat = EVayuBlockCompressionFormat::BC7;
#endif

#ifndef GL_COMPRESSED_RG_RGTC2
    constexpr U32 GL_COMPRESSED_RG_RGTC2 = 0x8DBD;
#endif

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

    // Test 3: Auto selection for fully transparent RGBA (all 0s) -> BC7 (or BC3 on macOS)
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
        ensure("Fully transparent RGBA resolves to translucent format", result.mFormat == kExpectedTranslucentFormat);
    }

    // Test 4: Auto selection for genuine cutout/translucent RGBA -> BC7 (or BC3 on macOS)
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
        ensure("Translucent RGBA resolves to translucent format", result.mFormat == kExpectedTranslucentFormat);
    }

    // Test 5: Normal map compression (2-channel -> BC5, requires a PBRNormal claim)
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
        bool ok = VayuImageBlockCompressor::encode(rg.data(), width, height, 2, result,
                                                   EVayuBlockCompressionFormat::Auto,
                                                   EVayuTextureJob::PBRNormal);
        ensure("Encoding 2-channel normal succeeded", ok);
        ensure("2-channel normal resolves to BC5", result.mFormat == EVayuBlockCompressionFormat::BC5);
    }

    // Test 6: 1-channel raw is refused under color jobs (SL decodes >=3 channels;
    // no mono content exists on the wire). BC4 is only reachable via an explicit format.
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
        ensure("Unclaimed 1-channel raw is refused", !ok);
    }

    // Test 7: Binary 1-bit alpha cutout (0 and 255) -> must resolve to BC7 (or BC3 on macOS), not BC1
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
        ensure("Binary cutout RGBA resolves to translucent format", result.mFormat == kExpectedTranslucentFormat);
    }

    // Test 8: Standardized Slow preset model produces valid compression results
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
            rgba[i * 4 + 3] = (i % 3 == 0) ? 128 : 255; // partial alpha -> BC7 (or BC3 on macOS)
        }

        ensure("getPreset returns Slow", VayuImageBlockCompressor::getPreset() == EVayuBlockCompressionPreset::Slow);
        ensure("Default hybrid mips is true", VayuImageBlockCompressor::getHybridMips());

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
        ensure("Encoding succeeds", ok);
        ensure("Result preset is Slow", result.mPreset == EVayuBlockCompressionPreset::Slow);
        ensure("Format resolves to translucent format", result.mFormat == kExpectedTranslucentFormat);
        ensure("Result produces mipchain buffer", result.mBuffer.size() > 0);
        ensure("Result has multiple mip levels", result.mMipLevels > 1);

        // Test toggle to pure slow mode
        VayuImageBlockCompressor::setHybridMips(false);
        ensure("setHybridMips(false) enables pure slow mode", !VayuImageBlockCompressor::getHybridMips());
        VayuBlockCompressionResult slow_res;
        bool slow_ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, slow_res, EVayuBlockCompressionFormat::Auto);
        ensure("Pure slow mode encoding succeeds", slow_ok);
        ensure("Pure slow mode produces valid buffer", slow_res.mBuffer.size() > 0);
        VayuImageBlockCompressor::setHybridMips(true);
        ensure("Reset hybrid mips to true", VayuImageBlockCompressor::getHybridMips());
    }

    // Test 9: Progressive mip staging encoding produces valid sub-mips and reverse buffer order
    template<> template<>
    void block_compressor_object::test<9>()
    {
        const U32 width = 32, height = 32;
        std::vector<U8> rgb(width * height * 3, 128);

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgb.data(), width, height, 3, result, EVayuBlockCompressionFormat::BC1);
        ensure("RGB BC1 encoding succeeds", ok);
        ensure("Result preset is Slow", result.mPreset == EVayuBlockCompressionPreset::Slow);
        ensure("BC1 produces correct mip count", result.mMipLevels == 6); // 32, 16, 8, 4, 2, 1
        ensure("Largest mip offset matches reverse layout", result.getLargestMipOffset() > 0);
        ensure("Discard 0 bytes matches 32x32 BC1 size", result.getMipBytes(0) == (8 * 8 * 8)); // 8x8 blocks * 8 bytes = 512
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
            ensure("Tail cutout resolves to translucent format", result.mFormat == kExpectedTranslucentFormat);
        }

        // Case B: Cutout pixel placed early (index 2) (tests early SIMD exit)
        {
            std::vector<U8> rgba(total_px * 4, 255);
            rgba[2 * 4 + 3] = 0; // Transparent cutout at index 2

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
            ensure("Early cutout encodes successfully", ok);
            ensure("Early cutout resolves to translucent format", result.mFormat == kExpectedTranslucentFormat);
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

    // Test 13: 512x512 full mip-chain compression across BC1, BC4, BC5, and BC7
    template<> template<>
    void block_compressor_object::test<13>()
    {
        const U32 width = 512, height = 512;
        std::vector<U8> rgba(width * height * 4);
        for (size_t i = 0; i < width * height; ++i)
        {
            rgba[i * 4 + 0] = (U8)(i & 0xFF);
            rgba[i * 4 + 1] = (U8)((i >> 2) & 0xFF);
            rgba[i * 4 + 2] = (U8)((i >> 4) & 0xFF);
            rgba[i * 4 + 3] = (i % 7 == 0) ? 128 : 255;
        }

        // Test BC7 on 512x512
        {
            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::BC7);
            ensure("512x512 BC7 encodes successfully", ok);
            ensure("512x512 has 10 mip levels", result.mMipLevels == 10);
            ensure("Buffer size matches level count", result.mBuffer.size() > 0);
            ensure("Largest mip offset valid", result.getLargestMipOffset() < result.mBuffer.size());
        }

        // Test BC1 on 512x512
        {
            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::BC1);
            ensure("512x512 BC1 encodes successfully", ok);
            ensure("512x512 has 10 mip levels", result.mMipLevels == 10);
            ensure("Buffer size is non-empty", result.mBuffer.size() > 0);
        }

        // Test BC5 on 512x512 (2-channel)
        {
            std::vector<U8> rg(width * height * 2);
            for (size_t i = 0; i < width * height; ++i)
            {
                rg[i * 2 + 0] = (U8)(i & 0xFF);
                rg[i * 2 + 1] = (U8)((i * 3) & 0xFF);
            }
            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rg.data(), width, height, 2, result, EVayuBlockCompressionFormat::BC5);
            ensure("512x512 BC5 encodes successfully", ok);
            ensure("512x512 has 10 mip levels", result.mMipLevels == 10);
        }
    }

    // Test 14: Microbenchmark timing across 1024x1024 textures
    template<> template<>
    void block_compressor_object::test<14>()
    {
        const U32 width = 1024, height = 1024;
        std::vector<U8> rgba_trans(width * height * 4);
        std::vector<U8> rgba_opaque(width * height * 4);
        std::vector<U8> rg_normal(width * height * 2);

        for (size_t i = 0; i < width * height; ++i)
        {
            U8 r = (U8)(i & 0xFF);
            U8 g = (U8)((i * 3) & 0xFF);
            U8 b = (U8)((i * 7) & 0xFF);

            rgba_opaque[i * 4 + 0] = r;
            rgba_opaque[i * 4 + 1] = g;
            rgba_opaque[i * 4 + 2] = b;
            rgba_opaque[i * 4 + 3] = 255;

            rgba_trans[i * 4 + 0] = r;
            rgba_trans[i * 4 + 1] = g;
            rgba_trans[i * 4 + 2] = b;
            rgba_trans[i * 4 + 3] = (i % 5 == 0) ? 128 : 255;

            rg_normal[i * 2 + 0] = r;
            rg_normal[i * 2 + 1] = g;
        }

        const auto benchmark = [](const char* name, auto&& fn) {
            auto t0 = std::chrono::high_resolution_clock::now();
            fn();
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            double mpix = (1024.0 * 1024.0 / 1e6) / (ms / 1000.0);
            printf("[Benchmark] %-30s : %6.2f ms (%6.2f MPix/s)\n", name, ms, mpix);
        };

        // 1. BC1 (Opaque 1024x1024 full mipchain)
        benchmark("1024x1024 Opaque -> BC1", [&]() {
            VayuBlockCompressionResult res;
            VayuImageBlockCompressor::encode(rgba_opaque.data(), width, height, 4, res, EVayuBlockCompressionFormat::Auto);
        });

        // 2. BC5 (Normal map 1024x1024 full mipchain)
        benchmark("1024x1024 Normal 2ch -> BC5", [&]() {
            VayuBlockCompressionResult res;
            VayuImageBlockCompressor::encode(rg_normal.data(), width, height, 2, res, EVayuBlockCompressionFormat::Auto);
        });

        // 3. BC7 (1024x1024 full mipchain)
        {
            benchmark("1024x1024 BC7 [Standard Slow Mip 0, Fast sub-mips]", [&]() {
                VayuBlockCompressionResult res;
                VayuImageBlockCompressor::encode(rgba_trans.data(), width, height, 4, res, EVayuBlockCompressionFormat::BC7);
            });
        }
    }

    // Test 15: Explicit BC3 compression for translucent textures
    template<> template<>
    void block_compressor_object::test<15>()
    {
        const U32 width = 16, height = 16;
        std::vector<U8> rgba(width * height * 4);
        for (size_t i = 0; i < width * height; ++i)
        {
            rgba[i * 4 + 0] = 120;
            rgba[i * 4 + 1] = 60;
            rgba[i * 4 + 2] = 30;
            rgba[i * 4 + 3] = (i % 2 == 0) ? 255 : 100;
        }

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::BC3);
        ensure("Encoding explicit BC3 succeeded", ok);
        ensure("Explicit BC3 format is preserved", result.mFormat == EVayuBlockCompressionFormat::BC3);
        ensure("BC3 produces correct mip levels", result.mMipLevels == 5);
        ensure("BC3 buffer size is non-zero", result.mBuffer.size() > 0);
    }

    // Test 16: Demote BC7 to BC3 on macOS, preserve BC7 on other platforms
    template<> template<>
    void block_compressor_object::test<16>()
    {
        const U32 width = 16, height = 16;
        std::vector<U8> rgba(width * height * 4, 255);

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::BC7);
        ensure("Encoding BC7 succeeded", ok);
#if LL_DARWIN
        ensure("BC7 demoted to BC3 on macOS", result.mFormat == EVayuBlockCompressionFormat::BC3);
#else
        ensure("BC7 preserved on non-macOS", result.mFormat == EVayuBlockCompressionFormat::BC7);
#endif
    }

    // Test 17: Opaque 4-channel texture signs mIsMask == true and chooses BC1
    template<> template<>
    void block_compressor_object::test<17>()
    {
        const U32 width = 16, height = 16;
        std::vector<U8> rgba(width * height * 4, 255);

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
        ensure("Encoding opaque RGBA succeeded", ok);
        ensure("Opaque RGBA resolves to BC1", result.mFormat == EVayuBlockCompressionFormat::BC1);
        ensure("Opaque RGBA signs as mask", result.mIsMask == true);
    }

    // Test 18: Cutout 1-bit alpha signs mIsMask == true and chooses translucent format
    template<> template<>
    void block_compressor_object::test<18>()
    {
        const U32 width = 16, height = 16;
        std::vector<U8> rgba(width * height * 4);
        // Checkerboard of 2x2 solid (255) and 2x2 transparent (0) - punch-through cutout
        for (U32 y = 0; y < height; ++y)
        {
            for (U32 x = 0; x < width; ++x)
            {
                size_t idx = (y * width + x) * 4;
                rgba[idx + 0] = 200;
                rgba[idx + 1] = 200;
                rgba[idx + 2] = 200;
                rgba[idx + 3] = ((x / 2 + y / 2) % 2 == 0) ? 255 : 0;
            }
        }

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
        ensure("Encoding cutout RGBA succeeded", ok);
        ensure("Cutout RGBA resolves to translucent format", result.mFormat == kExpectedTranslucentFormat);
        ensure("Cutout RGBA signs as mask", result.mIsMask == true);
    }

    // Test 19: Smooth translucent gradient signs mIsMask == false and chooses translucent format
    template<> template<>
    void block_compressor_object::test<19>()
    {
        const U32 width = 16, height = 16;
        std::vector<U8> rgba(width * height * 4);
        // Smooth gradient from 32 to 200 (all mid-range values)
        for (size_t i = 0; i < width * height; ++i)
        {
            rgba[i * 4 + 0] = 100;
            rgba[i * 4 + 1] = 100;
            rgba[i * 4 + 2] = 100;
            rgba[i * 4 + 3] = (U8)(32 + (i * (200 - 32)) / (width * height));
        }

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto);
        ensure("Encoding gradient RGBA succeeded", ok);
        ensure("Gradient RGBA resolves to translucent format", result.mFormat == kExpectedTranslucentFormat);
        ensure("Gradient RGBA does NOT sign as mask", result.mIsMask == false);
    }

    // Test 20: Probe dump of a synthetic periodic normal map (BC7 + BC5) for
    // the offline analyzer in scripts/perf/analyze_vayu_dump.py. Writes the
    // unconverted pyramid + compressed buffer via VAYU_DUMP_DIR. The captured
    // output is analyzed off-line; the assertions here only cover resolve.
    template<> template<>
    void block_compressor_object::test<20>()
    {
        const char* out = std::getenv("VAYU_DUMP_PROBE_DIR");
        std::filesystem::path d = (out && out[0]) ? std::filesystem::path(out)
                                                  : std::filesystem::path("/tmp/vayu_dump_probe");
        std::filesystem::remove_all(d);
        std::filesystem::create_directories(d);
#ifdef _WIN32
        _putenv_s("VAYU_DUMP_DIR", d.string().c_str());
#else
        setenv("VAYU_DUMP_DIR", d.string().c_str(), 1);
#endif

        const U32 W = 1024, H = 1024;
        std::vector<U8> rgba((size_t)W * H * 4);
        for (U32 y = 0; y < H; y++)
        {
            for (U32 x = 0; x < W; x++)
            {
                const float u = (float)x / (float)W;
                const float v = (float)y / (float)H;
                float nx = 0.25f * std::sin(u * 63.0f) * std::cos(v * 47.0f)
                         + 0.10f * std::sin(u * 21.0f) * std::cos(v * 29.0f);
                float ny = 0.25f * std::sin(v * 59.0f) * std::cos(u * 37.0f)
                         + 0.10f * std::sin(v * 17.0f) * std::cos(u * 31.0f);
                float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
                size_t p = ((size_t)y * W + x) * 4;
                rgba[p + 0] = (U8)llroundf((nx * 0.5f + 0.5f) * 255.0f);
                rgba[p + 1] = (U8)llroundf((ny * 0.5f + 0.5f) * 255.0f);
                rgba[p + 2] = (U8)llroundf((nz * 0.5f + 0.5f) * 255.0f);
                rgba[p + 3] = 255;
            }
        }

        VayuBlockCompressionResult r1;
        ensure("RGBA normal probe encodes",
               VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r1,
                                                EVayuBlockCompressionFormat::Auto,
                                                EVayuTextureJob::PBRNormal));
        ensure("RGBA normal resolves to BC5", r1.mFormat == EVayuBlockCompressionFormat::BC5);

        std::vector<U8> rg((size_t)W * H * 2);
        for (size_t i = 0; i < (size_t)W * H; i++)
        {
            rg[i * 2 + 0] = rgba[i * 4 + 0];
            rg[i * 2 + 1] = rgba[i * 4 + 1];
        }
        VayuBlockCompressionResult r2;
        ensure("RG normal probe encodes",
               VayuImageBlockCompressor::encode(rg.data(), W, H, 2, r2,
                                                EVayuBlockCompressionFormat::Auto,
                                                EVayuTextureJob::PBRNormal));
        ensure("RG normal resolves to BC5", r2.mFormat == EVayuBlockCompressionFormat::BC5);

        VayuBlockCompressionResult r3;
        ensure("4ch opaque albedo encodes",
               VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r3,
                                                EVayuBlockCompressionFormat::Auto,
                                                EVayuTextureJob::Albedo));
        ensure("4ch opaque under Albedo resolves to BC1", r3.mFormat == EVayuBlockCompressionFormat::BC1);

        VayuBlockCompressionResult r4;
        ensure("3ch albedo encodes",
               VayuImageBlockCompressor::encode(rgba.data(), W, H, 3, r4,
                                                EVayuBlockCompressionFormat::Auto,
                                                EVayuTextureJob::Albedo));
        ensure("3ch under Albedo resolves to BC1", r4.mFormat == EVayuBlockCompressionFormat::BC1);
    }

    // Test 21: Verify that 3-channel and 4-channel inputs passed to
    // VayuImageBlockCompressor::encode with EVayuTextureJob::PBRNormal resolve to
    // EVayuBlockCompressionFormat::BC5 with is_srgb = false and 2 components.
    template<> template<>
    void block_compressor_object::test<21>()
    {
        const U32 W = 64, H = 64;

        // 4-channel test
        std::vector<U8> rgba((size_t)W * H * 4, 128);
        for (size_t i = 0; i < (size_t)W * H; i++)
        {
            rgba[i * 4 + 0] = 128; // X
            rgba[i * 4 + 1] = 128; // Y
            rgba[i * 4 + 2] = 255; // Z
            rgba[i * 4 + 3] = 255; // Alpha
        }

        VayuBlockCompressionResult r_4ch;
        ensure("4-channel PBRNormal encodes successfully",
               VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r_4ch,
                                                EVayuBlockCompressionFormat::Auto,
                                                EVayuTextureJob::PBRNormal));
        ensure("4-channel PBRNormal resolves to BC5", r_4ch.mFormat == EVayuBlockCompressionFormat::BC5);
        ensure("4-channel PBRNormal has 2 components", r_4ch.mComponents == 2);
        ensure("4-channel PBRNormal GL internal format is GL_COMPRESSED_RG_RGTC2",
               r_4ch.mGLInternalFormat == GL_COMPRESSED_RG_RGTC2);

        // 3-channel test
        std::vector<U8> rgb((size_t)W * H * 3, 128);
        for (size_t i = 0; i < (size_t)W * H; i++)
        {
            rgb[i * 3 + 0] = 128; // X
            rgb[i * 3 + 1] = 128; // Y
            rgb[i * 3 + 2] = 255; // Z
        }

        VayuBlockCompressionResult r_3ch;
        ensure("3-channel PBRNormal encodes successfully",
               VayuImageBlockCompressor::encode(rgb.data(), W, H, 3, r_3ch,
                                                EVayuBlockCompressionFormat::Auto,
                                                EVayuTextureJob::PBRNormal));
        ensure("3-channel PBRNormal resolves to BC5", r_3ch.mFormat == EVayuBlockCompressionFormat::BC5);
        ensure("3-channel PBRNormal has 2 components", r_3ch.mComponents == 2);
        ensure("3-channel PBRNormal GL internal format is GL_COMPRESSED_RG_RGTC2",
               r_3ch.mGLInternalFormat == GL_COMPRESSED_RG_RGTC2);
    }

    // Test 22: Verify that EVayuTextureJob::Albedo and Emissive
    // produce byte-identical compressed output (SC-004), and that
    // EVayuTextureJob::LegacyMaterialNormal preserves all 4 channels (RGB + Alpha glossiness) via BC7/BC3.
    template<> template<>
    void block_compressor_object::test<22>()
    {
        const U32 W = 64, H = 64;

        // Baseline: 4-channel opaque diffuse texture
        std::vector<U8> rgba((size_t)W * H * 4);
        for (size_t i = 0; i < (size_t)W * H; i++)
        {
            rgba[i * 4 + 0] = (U8)(i & 0xFF);
            rgba[i * 4 + 1] = (U8)((i * 3) & 0xFF);
            rgba[i * 4 + 2] = (U8)((i * 7) & 0xFF);
            rgba[i * 4 + 3] = 255;
        }

        VayuBlockCompressionResult r_albedo;
        ensure("Albedo job encodes",
               VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r_albedo,
                                                EVayuBlockCompressionFormat::Auto,
                                                EVayuTextureJob::Albedo));

        VayuBlockCompressionResult r_emissive;
        ensure("Emissive job encodes",
               VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r_emissive,
                                                EVayuBlockCompressionFormat::Auto,
                                                EVayuTextureJob::Emissive));

        ensure("Emissive is byte-identical to Albedo", r_emissive.mBuffer == r_albedo.mBuffer);
        ensure("Emissive format matches Albedo", r_emissive.mFormat == r_albedo.mFormat);

        // Generic color maps (legacy diffuse, terrain, water): same sRGB
        // encoding as Albedo, byte-identical.
        VayuBlockCompressionResult r_rgba;
        ensure("RGBA job encodes",
               VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r_rgba,
                                                EVayuBlockCompressionFormat::Auto,
                                                EVayuTextureJob::RGBA));
        ensure("RGBA is byte-identical to Albedo", r_rgba.mBuffer == r_albedo.mBuffer);
        ensure("RGBA format matches Albedo", r_rgba.mFormat == r_albedo.mFormat);

        // MetallicRoughness: 4-channel linear
        VayuBlockCompressionResult r_mr;
        ensure("MetallicRoughness job encodes",
               VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r_mr,
                                                EVayuBlockCompressionFormat::Auto,
                                                EVayuTextureJob::MetallicRoughness));
        ensure_equals("MetallicRoughness has 4 components", r_mr.mComponents, 4);

        // Legacy Blinn-Phong material normal: 4-channel with gloss variation in alpha
        std::vector<U8> normal_with_gloss = rgba;
        normal_with_gloss[3] = 120; // texel 0 has gloss < 255
        normal_with_gloss[7] = 200; // texel 1 has gloss < 255

        VayuBlockCompressionResult r_norm_gloss;
        ensure("LegacyMaterialNormal encodes",
               VayuImageBlockCompressor::encode(normal_with_gloss.data(), W, H, 4, r_norm_gloss,
                                                EVayuBlockCompressionFormat::Auto,
                                                EVayuTextureJob::LegacyMaterialNormal));
#if LL_DARWIN
        ensure("LegacyMaterialNormal falls back to BC3", r_norm_gloss.mFormat == EVayuBlockCompressionFormat::BC3);
#else
        ensure("LegacyMaterialNormal encodes to BC7", r_norm_gloss.mFormat == EVayuBlockCompressionFormat::BC7);
#endif
        ensure_equals("LegacyMaterialNormal preserves 4 components", r_norm_gloss.mComponents, 4);

        // Legacy Blinn-Phong specular: RGB tint + gloss in alpha, same
        // 4-channel linear encoding as the legacy normal.
        VayuBlockCompressionResult r_spec;
        ensure("LegacySpecular encodes",
               VayuImageBlockCompressor::encode(normal_with_gloss.data(), W, H, 4, r_spec,
                                                EVayuBlockCompressionFormat::Auto,
                                                EVayuTextureJob::LegacySpecular));
#if LL_DARWIN
        ensure("LegacySpecular falls back to BC3", r_spec.mFormat == EVayuBlockCompressionFormat::BC3);
#else
        ensure("LegacySpecular encodes to BC7", r_spec.mFormat == EVayuBlockCompressionFormat::BC7);
#endif
        ensure_equals("LegacySpecular preserves 4 components", r_spec.mComponents, 4);
        ensure("LegacySpecular is byte-identical to LegacyMaterialNormal",
               r_spec.mBuffer == r_norm_gloss.mBuffer);
    }

    // Test 23: An Unknown (unclaimed) texture job must never be block-compressed.
    // encode() refuses it outright; the worker gate and fetch probe gate keep it
    // out upstream. Defense in depth: direct encode() calls also refuse.
    template<> template<>
    void block_compressor_object::test<23>()
    {
        const U32 W = 64, H = 64;
        std::vector<U8> rgba((size_t)W * H * 4);
        for (size_t i = 0; i < (size_t)W * H; i++)
        {
            rgba[i * 4 + 0] = (U8)(i & 0xFF);
            rgba[i * 4 + 1] = (U8)((i * 3) & 0xFF);
            rgba[i * 4 + 2] = (U8)((i * 7) & 0xFF);
            rgba[i * 4 + 3] = 255;
        }

        VayuBlockCompressionResult r_unknown;
        ensure("Unknown job is refused by encode()",
               !VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r_unknown,
                                                  EVayuBlockCompressionFormat::Auto,
                                                  EVayuTextureJob::Unknown));
        ensure("Unknown never resolves a format", r_unknown.mBuffer.empty());
    }
}



