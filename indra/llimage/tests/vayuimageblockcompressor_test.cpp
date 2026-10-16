/**
 * @file llimageblockcompressor_test.cpp
 * @brief Unit tests for VayuImageBlockCompressor
 */

#include "linden_common.h"
#include "../vayuimageblockcompressor.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>

// LLImageBase::getData() and isBufferInvalid() are satisfied by
// llimageworker_test.cpp: this binary builds all three test files together, one
// definition each, and nothing here reads an image's pixels.

namespace
{
#if LL_DARWIN
    constexpr EVayuBlockCompressionFormat kExpectedTranslucentFormat = EVayuBlockCompressionFormat::BC3;
#else
    constexpr EVayuBlockCompressionFormat kExpectedTranslucentFormat = EVayuBlockCompressionFormat::BC7;
#endif

#ifndef GL_COMPRESSED_RG_RGTC2
    constexpr U32 GL_COMPRESSED_RG_RGTC2 = 0x8DBD;
#endif

    // Test 1: Eligibility checks
    TEST_CASE("VayuImageBlockCompressor: Eligibility checks", "[VayuImageBlockCompressor]")
    {
        UNSCOPED_INFO("Valid 64x64 RGBA is eligible");
        CHECK(VayuImageBlockCompressor::isEligible(64, 64, 4));
        UNSCOPED_INFO("Valid 16x16 RGB is eligible");
        CHECK(VayuImageBlockCompressor::isEligible(16, 16, 3));
        UNSCOPED_INFO("Valid 32x32 RG is eligible");
        CHECK(VayuImageBlockCompressor::isEligible(32, 32, 2));
        UNSCOPED_INFO("Valid 8x8 Grayscale is eligible");
        CHECK(VayuImageBlockCompressor::isEligible(8, 8, 1));
        UNSCOPED_INFO("Below min dimension (2x2) is not eligible");
        CHECK_FALSE(VayuImageBlockCompressor::isEligible(2, 2, 4));
        UNSCOPED_INFO("Invalid components (5) is not eligible");
        CHECK_FALSE(VayuImageBlockCompressor::isEligible(16, 16, 5));
    }

    // Test 2: Auto selection for opaque RGBA -> BC1
    TEST_CASE("VayuImageBlockCompressor: Auto selection for opaque RGBA to BC1", "[VayuImageBlockCompressor]")
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
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto,
                                                   EVayuTextureJob::Albedo);
        UNSCOPED_INFO("Encoding opaque RGBA succeeded");
        CHECK(ok);
        UNSCOPED_INFO("Opaque RGBA resolves to BC1");
        CHECK(result.mFormat == EVayuBlockCompressionFormat::BC1);
        UNSCOPED_INFO("Mip levels are calculated down to 1x1");
        CHECK(result.mMipLevels == 5); // 16, 8, 4, 2, 1
        UNSCOPED_INFO("Buffer size matches total mips");
        REQUIRE(result.mBuffer.size() > 0);
        UNSCOPED_INFO("Largest mip offset is valid");
        REQUIRE(result.getLargestMipOffset() < result.mBuffer.size());
    }

    // Test 3: Auto selection for fully transparent RGBA (all 0s) -> BC7 (or BC3 on macOS)
    TEST_CASE("VayuImageBlockCompressor: Auto selection for fully transparent RGBA (all 0s) to BC7 (or BC3 on macOS)",
              "[VayuImageBlockCompressor]")
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
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto,
                                                   EVayuTextureJob::Albedo);
        UNSCOPED_INFO("Encoding fully transparent RGBA succeeded");
        CHECK(ok);
        UNSCOPED_INFO("Fully transparent RGBA resolves to translucent format");
        CHECK(result.mFormat == kExpectedTranslucentFormat);
    }

    // Test 4: Auto selection for genuine cutout/translucent RGBA -> BC7 (or BC3 on macOS)
    TEST_CASE("VayuImageBlockCompressor: Auto selection for genuine cutout/translucent RGBA to BC7 (or BC3 on macOS)",
              "[VayuImageBlockCompressor]")
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
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto,
                                                   EVayuTextureJob::Albedo);
        UNSCOPED_INFO("Encoding translucent RGBA succeeded");
        CHECK(ok);
        UNSCOPED_INFO("Translucent RGBA resolves to translucent format");
        CHECK(result.mFormat == kExpectedTranslucentFormat);
    }

    // Test 5: Normal map compression (2-channel -> BC5, requires a PBRNormal claim)
    TEST_CASE("VayuImageBlockCompressor: Normal map compression (2-channel to BC5, requires a PBRNormal claim)",
              "[VayuImageBlockCompressor]")
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
        UNSCOPED_INFO("Encoding 2-channel normal succeeded");
        CHECK(ok);
        UNSCOPED_INFO("2-channel normal resolves to BC5");
        CHECK(result.mFormat == EVayuBlockCompressionFormat::BC5);
    }

    // Test 6: 1-channel raw is refused under color jobs (SL decodes >=3 channels;
    // no mono content exists on the wire). BC4 is only reachable via an explicit format.
    TEST_CASE("VayuImageBlockCompressor: 1-channel raw is refused under color jobs (SL decodes >=3 channels; no mono content exists on the "
              "wire). BC4 is only reachable via an explicit format.",
              "[VayuImageBlockCompressor]")
    {
        const U32 width = 16, height = 16;
        std::vector<U8> gray(width * height);
        for (size_t i = 0; i < width * height; ++i)
        {
            gray[i] = (U8)(i & 0xFF);
        }

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(gray.data(), width, height, 1, result, EVayuBlockCompressionFormat::Auto,
                                                   EVayuTextureJob::Albedo);
        UNSCOPED_INFO("Unclaimed 1-channel raw is refused");
        CHECK_FALSE(ok);
    }

    // Test 7: Binary 1-bit alpha cutout (0 and 255) -> must resolve to BC7 (or BC3 on macOS), not BC1
    TEST_CASE("VayuImageBlockCompressor: Binary 1-bit alpha cutout (0 and 255) to must resolve to BC7 (or BC3 on macOS), not BC1",
              "[VayuImageBlockCompressor]")
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
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto,
                                                   EVayuTextureJob::Albedo);
        UNSCOPED_INFO("Encoding binary cutout RGBA succeeded");
        CHECK(ok);
        UNSCOPED_INFO("Binary cutout RGBA resolves to translucent format");
        CHECK(result.mFormat == kExpectedTranslucentFormat);
    }

    // Test 8: Standardized Slow preset model produces valid compression results
    TEST_CASE("VayuImageBlockCompressor: Standardized Slow preset model produces valid compression results", "[VayuImageBlockCompressor]")
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

        UNSCOPED_INFO("getPreset returns Slow");
        CHECK(VayuImageBlockCompressor::getPreset() == EVayuBlockCompressionPreset::Slow);
        UNSCOPED_INFO("Default hybrid mips is true");
        CHECK(VayuImageBlockCompressor::getHybridMips());

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto,
                                                   EVayuTextureJob::Albedo);
        UNSCOPED_INFO("Encoding succeeds");
        CHECK(ok);
        UNSCOPED_INFO("Result preset is Slow");
        CHECK(result.mPreset == EVayuBlockCompressionPreset::Slow);
        UNSCOPED_INFO("Format resolves to translucent format");
        CHECK(result.mFormat == kExpectedTranslucentFormat);
        UNSCOPED_INFO("Result produces mipchain buffer");
        REQUIRE(result.mBuffer.size() > 0);
        UNSCOPED_INFO("Result has multiple mip levels");
        CHECK(result.mMipLevels > 1);

        // Test toggle to pure slow mode
        VayuImageBlockCompressor::setHybridMips(false);
        UNSCOPED_INFO("setHybridMips(false) enables pure slow mode");
        CHECK_FALSE(VayuImageBlockCompressor::getHybridMips());
        VayuBlockCompressionResult slow_res;
        bool slow_ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, slow_res, EVayuBlockCompressionFormat::Auto,
                                                        EVayuTextureJob::Albedo);
        UNSCOPED_INFO("Pure slow mode encoding succeeds");
        CHECK(slow_ok);
        UNSCOPED_INFO("Pure slow mode produces valid buffer");
        REQUIRE(slow_res.mBuffer.size() > 0);
        VayuImageBlockCompressor::setHybridMips(true);
        UNSCOPED_INFO("Reset hybrid mips to true");
        CHECK(VayuImageBlockCompressor::getHybridMips());
    }

    // Test 9: Progressive mip staging encoding produces valid sub-mips and reverse buffer order
    TEST_CASE("VayuImageBlockCompressor: Progressive mip staging encoding produces valid sub-mips and reverse buffer order",
              "[VayuImageBlockCompressor]")
    {
        const U32 width = 32, height = 32;
        std::vector<U8> rgb(width * height * 3, 128);

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgb.data(), width, height, 3, result, EVayuBlockCompressionFormat::BC1,
                                                   EVayuTextureJob::Albedo);
        UNSCOPED_INFO("RGB BC1 encoding succeeds");
        CHECK(ok);
        UNSCOPED_INFO("Result preset is Slow");
        CHECK(result.mPreset == EVayuBlockCompressionPreset::Slow);
        UNSCOPED_INFO("BC1 produces correct mip count");
        CHECK(result.mMipLevels == 6); // 32, 16, 8, 4, 2, 1
        UNSCOPED_INFO("Largest mip offset matches reverse layout");
        CHECK(result.getLargestMipOffset() > 0);
        UNSCOPED_INFO("Discard 0 bytes matches 32x32 BC1 size");
        CHECK(result.getMipBytes(0) == (8 * 8 * 8)); // 8x8 blocks * 8 bytes = 512
    }

    // Test 10: Non-power-of-two (NPOT) dimensions exercise both interior fast path and boundary clamping
    TEST_CASE("VayuImageBlockCompressor: Non-power-of-two (NPOT) dimensions exercise both interior fast path and boundary clamping",
              "[VayuImageBlockCompressor]")
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
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto,
                                                       EVayuTextureJob::Albedo);
            UNSCOPED_INFO("NPOT 4-channel opaque encodes successfully");
            CHECK(ok);
            UNSCOPED_INFO("NPOT 4-channel opaque resolves to BC1");
            CHECK(result.mFormat == EVayuBlockCompressionFormat::BC1);
            UNSCOPED_INFO("NPOT buffer size is non-zero");
            REQUIRE(result.mBuffer.size() > 0);
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
            bool ok = VayuImageBlockCompressor::encode(rgb.data(), width, height, 3, result, EVayuBlockCompressionFormat::Auto,
                                                       EVayuTextureJob::Albedo);
            UNSCOPED_INFO("NPOT 3-channel encodes successfully");
            CHECK(ok);
            UNSCOPED_INFO("NPOT 3-channel resolves to BC1");
            CHECK(result.mFormat == EVayuBlockCompressionFormat::BC1);
            UNSCOPED_INFO("NPOT buffer size is non-zero");
            REQUIRE(result.mBuffer.size() > 0);
        }
    }

    // Test 11: Vectorized alpha scan edge cases (tail cleanup and early detection)
    TEST_CASE("VayuImageBlockCompressor: Vectorized alpha scan edge cases (tail cleanup and early detection)", "[VayuImageBlockCompressor]")
    {
        const U32 width = 37, height = 53;
        const size_t total_px = width * height;

        // Case A: Cutout pixel placed at the very last pixel (tests tail cleanup)
        {
            std::vector<U8> rgba(total_px * 4, 255);
            rgba[(total_px - 1) * 4 + 3] = 128; // Translucent pixel at the tail

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto,
                                                       EVayuTextureJob::Albedo);
            UNSCOPED_INFO("Tail cutout encodes successfully");
            CHECK(ok);
            UNSCOPED_INFO("Tail cutout resolves to translucent format");
            CHECK(result.mFormat == kExpectedTranslucentFormat);
        }

        // Case B: Cutout pixel placed early (index 2) (tests early SIMD exit)
        {
            std::vector<U8> rgba(total_px * 4, 255);
            rgba[2 * 4 + 3] = 0; // Transparent cutout at index 2

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto,
                                                       EVayuTextureJob::Albedo);
            UNSCOPED_INFO("Early cutout encodes successfully");
            CHECK(ok);
            UNSCOPED_INFO("Early cutout resolves to translucent format");
            CHECK(result.mFormat == kExpectedTranslucentFormat);
        }

        // Case C: Fully opaque non-multiple-of-8 image resolves to BC1
        {
            std::vector<U8> rgba(total_px * 4, 255);

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto,
                                                       EVayuTextureJob::Albedo);
            UNSCOPED_INFO("Fully opaque NPOT encodes successfully");
            CHECK(ok);
            UNSCOPED_INFO("Fully opaque NPOT resolves to BC1");
            CHECK(result.mFormat == EVayuBlockCompressionFormat::BC1);
        }
    }

    // Test 12: Downsampling across multiple mip levels for sRGB and Linear formats
    TEST_CASE("VayuImageBlockCompressor: Downsampling across multiple mip levels for sRGB and Linear formats", "[VayuImageBlockCompressor]")
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
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::BC1,
                                                       EVayuTextureJob::Albedo);
            UNSCOPED_INFO("8x8 RGBA downsampling succeeds");
            CHECK(ok);
            UNSCOPED_INFO("8x8 produces 4 mip levels (8, 4, 2, 1)");
            CHECK(result.mMipLevels == 4);
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
            bool ok = VayuImageBlockCompressor::encode(rg.data(), width, height, 2, result, EVayuBlockCompressionFormat::BC5,
                                                       EVayuTextureJob::Albedo);
            UNSCOPED_INFO("4x4 RG downsampling succeeds");
            CHECK(ok);
            UNSCOPED_INFO("4x4 produces 3 mip levels (4, 2, 1)");
            CHECK(result.mMipLevels == 3);
        }

        // 1-channel Linear (Mask) mip pyramid
        {
            const U32 width = 4, height = 4;
            std::vector<U8> gray(width * height, 128);

            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(gray.data(), width, height, 1, result, EVayuBlockCompressionFormat::BC4,
                                                       EVayuTextureJob::Albedo);
            UNSCOPED_INFO("4x4 Grayscale downsampling succeeds");
            CHECK(ok);
            UNSCOPED_INFO("4x4 produces 3 mip levels (4, 2, 1)");
            CHECK(result.mMipLevels == 3);
        }
    }

    // Test 13: 512x512 full mip-chain compression across BC1, BC4, BC5, and BC7
    TEST_CASE("VayuImageBlockCompressor: 512x512 full mip-chain compression across BC1, BC4, BC5, and BC7", "[VayuImageBlockCompressor]")
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
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::BC7,
                                                       EVayuTextureJob::Albedo);
            UNSCOPED_INFO("512x512 BC7 encodes successfully");
            CHECK(ok);
            UNSCOPED_INFO("512x512 has 10 mip levels");
            CHECK(result.mMipLevels == 10);
            UNSCOPED_INFO("Buffer size matches level count");
            REQUIRE(result.mBuffer.size() > 0);
            UNSCOPED_INFO("Largest mip offset valid");
            REQUIRE(result.getLargestMipOffset() < result.mBuffer.size());
        }

        // Test BC1 on 512x512
        {
            VayuBlockCompressionResult result;
            bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::BC1,
                                                       EVayuTextureJob::Albedo);
            UNSCOPED_INFO("512x512 BC1 encodes successfully");
            CHECK(ok);
            UNSCOPED_INFO("512x512 has 10 mip levels");
            CHECK(result.mMipLevels == 10);
            UNSCOPED_INFO("Buffer size is non-empty");
            REQUIRE(result.mBuffer.size() > 0);
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
            bool ok = VayuImageBlockCompressor::encode(rg.data(), width, height, 2, result, EVayuBlockCompressionFormat::BC5,
                                                       EVayuTextureJob::Albedo);
            UNSCOPED_INFO("512x512 BC5 encodes successfully");
            CHECK(ok);
            UNSCOPED_INFO("512x512 has 10 mip levels");
            CHECK(result.mMipLevels == 10);
        }
    }

    // Test 14: Microbenchmark timing across 1024x1024 textures
    TEST_CASE("VayuImageBlockCompressor: Microbenchmark timing across 1024x1024 textures", "[VayuImageBlockCompressor]")
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
            VayuImageBlockCompressor::encode(rgba_opaque.data(), width, height, 4, res, EVayuBlockCompressionFormat::Auto,
                                             EVayuTextureJob::Albedo);
        });

        // 2. BC5 (Normal map 1024x1024 full mipchain)
        benchmark("1024x1024 Normal 2ch -> BC5", [&]() {
            VayuBlockCompressionResult res;
            VayuImageBlockCompressor::encode(rg_normal.data(), width, height, 2, res, EVayuBlockCompressionFormat::Auto,
                                             EVayuTextureJob::Albedo);
        });

        // 3. BC7 (1024x1024 full mipchain)
        {
            benchmark("1024x1024 BC7 [Standard Slow Mip 0, Fast sub-mips]", [&]() {
                VayuBlockCompressionResult res;
                VayuImageBlockCompressor::encode(rgba_trans.data(), width, height, 4, res, EVayuBlockCompressionFormat::BC7,
                                                 EVayuTextureJob::Albedo);
            });
        }
    }

    // Test 15: Explicit BC3 compression for translucent textures
    TEST_CASE("VayuImageBlockCompressor: Explicit BC3 compression for translucent textures", "[VayuImageBlockCompressor]")
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
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::BC3,
                                                   EVayuTextureJob::Albedo);
        UNSCOPED_INFO("Encoding explicit BC3 succeeded");
        CHECK(ok);
        UNSCOPED_INFO("Explicit BC3 format is preserved");
        CHECK(result.mFormat == EVayuBlockCompressionFormat::BC3);
        UNSCOPED_INFO("BC3 produces correct mip levels");
        CHECK(result.mMipLevels == 5);
        UNSCOPED_INFO("BC3 buffer size is non-zero");
        REQUIRE(result.mBuffer.size() > 0);
    }

    // Test 16: Demote BC7 to BC3 on macOS, preserve BC7 on other platforms
    TEST_CASE("VayuImageBlockCompressor: Demote BC7 to BC3 on macOS, preserve BC7 on other platforms", "[VayuImageBlockCompressor]")
    {
        const U32 width = 16, height = 16;
        std::vector<U8> rgba(width * height * 4, 255);

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::BC7,
                                                   EVayuTextureJob::Albedo);
        UNSCOPED_INFO("Encoding BC7 succeeded");
        CHECK(ok);
#if LL_DARWIN
        UNSCOPED_INFO("BC7 demoted to BC3 on macOS");
        REQUIRE(result.mFormat == EVayuBlockCompressionFormat::BC3);
#else
        UNSCOPED_INFO("BC7 preserved on non-macOS");
        CHECK(result.mFormat == EVayuBlockCompressionFormat::BC7);
#endif
    }

    // Test 17: Opaque 4-channel texture signs mIsMask == true and chooses BC1
    TEST_CASE("VayuImageBlockCompressor: Opaque 4-channel texture signs mIsMask == true and chooses BC1", "[VayuImageBlockCompressor]")
    {
        const U32 width = 16, height = 16;
        std::vector<U8> rgba(width * height * 4, 255);

        VayuBlockCompressionResult result;
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto,
                                                   EVayuTextureJob::Albedo);
        UNSCOPED_INFO("Encoding opaque RGBA succeeded");
        CHECK(ok);
        UNSCOPED_INFO("Opaque RGBA resolves to BC1");
        CHECK(result.mFormat == EVayuBlockCompressionFormat::BC1);
        UNSCOPED_INFO("Opaque RGBA signs as mask");
        CHECK(result.mIsMask);
    }

    // Test 18: Cutout 1-bit alpha signs mIsMask == true and chooses translucent format
    TEST_CASE("VayuImageBlockCompressor: Cutout 1-bit alpha signs mIsMask == true and chooses translucent format",
              "[VayuImageBlockCompressor]")
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
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto,
                                                   EVayuTextureJob::Albedo);
        UNSCOPED_INFO("Encoding cutout RGBA succeeded");
        CHECK(ok);
        UNSCOPED_INFO("Cutout RGBA resolves to translucent format");
        CHECK(result.mFormat == kExpectedTranslucentFormat);
        UNSCOPED_INFO("Cutout RGBA signs as mask");
        CHECK(result.mIsMask);
    }

    // Test 19: Smooth translucent gradient signs mIsMask == false and chooses translucent format
    TEST_CASE("VayuImageBlockCompressor: Smooth translucent gradient signs mIsMask == false and chooses translucent format",
              "[VayuImageBlockCompressor]")
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
        bool ok = VayuImageBlockCompressor::encode(rgba.data(), width, height, 4, result, EVayuBlockCompressionFormat::Auto,
                                                   EVayuTextureJob::Albedo);
        UNSCOPED_INFO("Encoding gradient RGBA succeeded");
        CHECK(ok);
        UNSCOPED_INFO("Gradient RGBA resolves to translucent format");
        CHECK(result.mFormat == kExpectedTranslucentFormat);
        UNSCOPED_INFO("Gradient RGBA does NOT sign as mask");
        CHECK_FALSE(result.mIsMask);
    }

    // Test 20: Probe dump of a synthetic periodic normal map (BC7 + BC5) for
    // the offline analyzer in scripts/perf/analyze_vayu_dump.py. Writes the
    // unconverted pyramid + compressed buffer via VAYU_DUMP_DIR. The captured
    // output is analyzed off-line; the assertions here only cover resolve.
    TEST_CASE("VayuImageBlockCompressor: Probe dump of a synthetic periodic normal map (BC7 + BC5) for the offline analyzer in "
              "scripts/perf/analyze_vayu_dump.py. Writes the unconverted pyramid + compressed buffer via VAYU_DUMP_DIR. The captured "
              "output is analyzed off-line; the assertions here only cover resolve.",
              "[VayuImageBlockCompressor]")
    {
        const char* out = std::getenv("VAYU_DUMP_PROBE_DIR");
        std::filesystem::path d = (out && out[0]) ? std::filesystem::path(out)
                                                  : std::filesystem::path("/tmp/vayu_dump_probe");
        std::filesystem::remove_all(d);
        std::filesystem::create_directories(d);
        // Restored by the guard below, however the case ends: the harness's state
        // guard reads the environment a case leaves behind, and a REQUIRE that
        // fails before a trailing call would skip it.
        const char*       previous      = std::getenv("VAYU_DUMP_DIR");
        const std::string previous_dump = previous ? previous : std::string();
        struct DumpDirGuard
        {
            std::string mPrevious;
            ~DumpDirGuard()
            {
#ifdef _WIN32
                _putenv_s("VAYU_DUMP_DIR", mPrevious.empty() ? "" : mPrevious.c_str());
#else
                if (mPrevious.empty())
                {
                    unsetenv("VAYU_DUMP_DIR");
                }
                else
                {
                    setenv("VAYU_DUMP_DIR", mPrevious.c_str(), 1);
                }
#endif
            }
        } dump_dir_guard{ previous_dump };
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
        UNSCOPED_INFO("RGBA normal probe encodes");
        REQUIRE(VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r1, EVayuBlockCompressionFormat::Auto, EVayuTextureJob::PBRNormal));
        UNSCOPED_INFO("RGBA normal resolves to BC5");
        CHECK(r1.mFormat == EVayuBlockCompressionFormat::BC5);

        std::vector<U8> rg((size_t)W * H * 2);
        for (size_t i = 0; i < (size_t)W * H; i++)
        {
            rg[i * 2 + 0] = rgba[i * 4 + 0];
            rg[i * 2 + 1] = rgba[i * 4 + 1];
        }
        VayuBlockCompressionResult r2;
        UNSCOPED_INFO("RG normal probe encodes");
        REQUIRE(VayuImageBlockCompressor::encode(rg.data(), W, H, 2, r2, EVayuBlockCompressionFormat::Auto, EVayuTextureJob::PBRNormal));
        UNSCOPED_INFO("RG normal resolves to BC5");
        CHECK(r2.mFormat == EVayuBlockCompressionFormat::BC5);

        VayuBlockCompressionResult r3;
        UNSCOPED_INFO("4ch opaque albedo encodes");
        REQUIRE(VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r3, EVayuBlockCompressionFormat::Auto, EVayuTextureJob::Albedo));
        UNSCOPED_INFO("4ch opaque under Albedo resolves to BC1");
        CHECK(r3.mFormat == EVayuBlockCompressionFormat::BC1);

        VayuBlockCompressionResult r4;
        UNSCOPED_INFO("3ch albedo encodes");
        REQUIRE(VayuImageBlockCompressor::encode(rgba.data(), W, H, 3, r4, EVayuBlockCompressionFormat::Auto, EVayuTextureJob::Albedo));
        UNSCOPED_INFO("3ch under Albedo resolves to BC1");
        CHECK(r4.mFormat == EVayuBlockCompressionFormat::BC1);
    }

    // Test 21: Verify that 3-channel and 4-channel inputs passed to
    // VayuImageBlockCompressor::encode with EVayuTextureJob::PBRNormal resolve to
    // EVayuBlockCompressionFormat::BC5 with is_srgb = false and 2 components.
    TEST_CASE("VayuImageBlockCompressor: Verify that 3-channel and 4-channel inputs passed to VayuImageBlockCompressor::encode with "
              "EVayuTextureJob::PBRNormal resolve to EVayuBlockCompressionFormat::BC5 with is_srgb = false and 2 components.",
              "[VayuImageBlockCompressor]")
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
        UNSCOPED_INFO("4-channel PBRNormal encodes successfully");
        REQUIRE(
            VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r_4ch, EVayuBlockCompressionFormat::Auto, EVayuTextureJob::PBRNormal));
        UNSCOPED_INFO("4-channel PBRNormal resolves to BC5");
        CHECK(r_4ch.mFormat == EVayuBlockCompressionFormat::BC5);
        UNSCOPED_INFO("4-channel PBRNormal has 2 components");
        CHECK(r_4ch.mComponents == 2);
        UNSCOPED_INFO("4-channel PBRNormal GL internal format is GL_COMPRESSED_RG_RGTC2");
        CHECK(r_4ch.mGLInternalFormat == GL_COMPRESSED_RG_RGTC2);

        // 3-channel test
        std::vector<U8> rgb((size_t)W * H * 3, 128);
        for (size_t i = 0; i < (size_t)W * H; i++)
        {
            rgb[i * 3 + 0] = 128; // X
            rgb[i * 3 + 1] = 128; // Y
            rgb[i * 3 + 2] = 255; // Z
        }

        VayuBlockCompressionResult r_3ch;
        UNSCOPED_INFO("3-channel PBRNormal encodes successfully");
        REQUIRE(
            VayuImageBlockCompressor::encode(rgb.data(), W, H, 3, r_3ch, EVayuBlockCompressionFormat::Auto, EVayuTextureJob::PBRNormal));
        UNSCOPED_INFO("3-channel PBRNormal resolves to BC5");
        CHECK(r_3ch.mFormat == EVayuBlockCompressionFormat::BC5);
        UNSCOPED_INFO("3-channel PBRNormal has 2 components");
        CHECK(r_3ch.mComponents == 2);
        UNSCOPED_INFO("3-channel PBRNormal GL internal format is GL_COMPRESSED_RG_RGTC2");
        CHECK(r_3ch.mGLInternalFormat == GL_COMPRESSED_RG_RGTC2);
    }

    // Test 22: Verify that EVayuTextureJob::Albedo and Emissive
    // produce byte-identical compressed output (SC-004), and that
    // EVayuTextureJob::LegacyMaterialNormal preserves all 4 channels (RGB + Alpha glossiness) via BC7/BC3.
    TEST_CASE("VayuImageBlockCompressor: Verify that EVayuTextureJob::Albedo and Emissive produce byte-identical compressed output "
              "(SC-004), and that EVayuTextureJob::LegacyMaterialNormal preserves all 4 channels (RGB + Alpha glossiness) via BC7/BC3.",
              "[VayuImageBlockCompressor]")
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
        UNSCOPED_INFO("Albedo job encodes");
        REQUIRE(
            VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r_albedo, EVayuBlockCompressionFormat::Auto, EVayuTextureJob::Albedo));

        VayuBlockCompressionResult r_emissive;
        UNSCOPED_INFO("Emissive job encodes");
        REQUIRE(VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r_emissive, EVayuBlockCompressionFormat::Auto,
                                                 EVayuTextureJob::Emissive));

        UNSCOPED_INFO("Emissive is byte-identical to Albedo");
        CHECK(r_emissive.mBuffer == r_albedo.mBuffer);
        UNSCOPED_INFO("Emissive format matches Albedo");
        CHECK(r_emissive.mFormat == r_albedo.mFormat);

        // Generic color maps (legacy diffuse, terrain, water): same sRGB
        // encoding as Albedo, byte-identical.
        VayuBlockCompressionResult r_rgba;
        UNSCOPED_INFO("RGBA job encodes");
        REQUIRE(VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r_rgba, EVayuBlockCompressionFormat::Auto, EVayuTextureJob::RGBA));
        UNSCOPED_INFO("RGBA is byte-identical to Albedo");
        CHECK(r_rgba.mBuffer == r_albedo.mBuffer);
        UNSCOPED_INFO("RGBA format matches Albedo");
        CHECK(r_rgba.mFormat == r_albedo.mFormat);

        // MetallicRoughness: 4-channel linear
        VayuBlockCompressionResult r_mr;
        UNSCOPED_INFO("MetallicRoughness job encodes");
        REQUIRE(VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r_mr, EVayuBlockCompressionFormat::Auto,
                                                 EVayuTextureJob::MetallicRoughness));
        UNSCOPED_INFO("MetallicRoughness has 4 components");
        CHECK(r_mr.mComponents == 4);

        // Legacy Blinn-Phong material normal: 4-channel with gloss variation in alpha
        std::vector<U8> normal_with_gloss = rgba;
        normal_with_gloss[3] = 120; // texel 0 has gloss < 255
        normal_with_gloss[7] = 200; // texel 1 has gloss < 255

        VayuBlockCompressionResult r_norm_gloss;
        UNSCOPED_INFO("LegacyMaterialNormal encodes");
        REQUIRE(VayuImageBlockCompressor::encode(normal_with_gloss.data(), W, H, 4, r_norm_gloss, EVayuBlockCompressionFormat::Auto,
                                                 EVayuTextureJob::LegacyMaterialNormal));
#if LL_DARWIN
        UNSCOPED_INFO("LegacyMaterialNormal falls back to BC3");
        REQUIRE(r_norm_gloss.mFormat == EVayuBlockCompressionFormat::BC3);
#else
        UNSCOPED_INFO("LegacyMaterialNormal encodes to BC7");
        CHECK(r_norm_gloss.mFormat == EVayuBlockCompressionFormat::BC7);
#endif
        UNSCOPED_INFO("LegacyMaterialNormal preserves 4 components");
        CHECK(r_norm_gloss.mComponents == 4);

        // Legacy Blinn-Phong specular: RGB is an sRGB tint, alpha only scales env
        // intensity (no gloss in any map's alpha). Stored sRGB so the software mip filter
        // averages in linear, which is why it differs from the linear normal map below.
        // This does NOT govern whether the shader decodes -- ALSampler::SRGBDecode does,
        // and a bound sampler overrides the texture's internal format.
        VayuBlockCompressionResult r_spec;
        UNSCOPED_INFO("LegacySpecular encodes");
        REQUIRE(VayuImageBlockCompressor::encode(normal_with_gloss.data(), W, H, 4, r_spec, EVayuBlockCompressionFormat::Auto,
                                                 EVayuTextureJob::LegacySpecular));
#if LL_DARWIN
        UNSCOPED_INFO("LegacySpecular falls back to BC3");
        REQUIRE(r_spec.mFormat == EVayuBlockCompressionFormat::BC3);
#else
        UNSCOPED_INFO("LegacySpecular encodes to BC7");
        CHECK(r_spec.mFormat == EVayuBlockCompressionFormat::BC7);
#endif
        UNSCOPED_INFO("LegacySpecular preserves 4 components");
        CHECK(r_spec.mComponents == 4);
        UNSCOPED_INFO("LegacySpecular differs from the linear LegacyMaterialNormal");
        CHECK(r_spec.mBuffer != r_norm_gloss.mBuffer);
        UNSCOPED_INFO("LegacySpecular stores an sRGB GL internal format, the normal a linear one");
        CHECK(r_spec.mGLInternalFormat != r_norm_gloss.mGLInternalFormat);

        // A data map bound without SRGBDecode must stay linear: the shader reads its bytes
        // verbatim, so an sRGB store would bend the vector. GL_COMPRESSED_RGBA_BPTC_UNORM,
        // spelled numerically because the GL enums are private to the compressor .cpp.
        UNSCOPED_INFO("LegacyMaterialNormal stores the linear BC7 format");
        CHECK(r_norm_gloss.mGLInternalFormat == 0x8E8Cu);
    }

    // Test 23: An Unknown (unclaimed) texture job must never be block-compressed.
    // encode() refuses it outright; the worker gate and fetch probe gate keep it
    // out upstream. Defense in depth: direct encode() calls also refuse.
    TEST_CASE("VayuImageBlockCompressor: An Unknown (unclaimed) texture job must never be block-compressed. encode() refuses it outright; "
              "the worker gate and fetch probe gate keep it out upstream. Defense in depth: direct encode() calls also refuse.",
              "[VayuImageBlockCompressor]")
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
        UNSCOPED_INFO("Unknown job is refused by encode()");
        REQUIRE_FALSE(
            VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, r_unknown, EVayuBlockCompressionFormat::Auto, EVayuTextureJob::Unknown));
        UNSCOPED_INFO("Unknown never resolves a format");
        REQUIRE(r_unknown.mBuffer.empty());

        UNSCOPED_INFO("Unknown resolves no cache key either");
        CHECK(VayuImageBlockCompressor::resolveFormat(rgba.data(), W, H, 4, EVayuTextureJob::Unknown, EVayuBlockCompressionFormat::Auto) ==
              EVayuBlockCompressionFormat::Auto);
    }

    // Test 24: resolveFormat() must AGREE with encode() for every input.
    //
    // The BC cache names its files by format (<uuid>.bc1, .bc7, ...), so the worker
    // probes for an existing entry by predicting the format before encoding anything.
    // When the prediction disagreed with encode(), the probe stat'd a filename that
    // never existed, the cache looked permanently empty, and every refinement re-encoded
    // the whole pyramid -- silently reinstating the exact behaviour the incremental
    // append path exists to remove. The failing case was 4-channel opaque colour:
    // encode() scans alpha and picks BC1, while the prediction hardcoded BC7.
    //
    // This is the regression guard for that: assert equality across every job role and
    // component count, including the opaque/alpha split that actually broke.
    TEST_CASE("VayuImageBlockCompressor: This is the regression guard for that: assert equality across every job role and component count, "
              "including the opaque/alpha split that actually broke.",
              "[VayuImageBlockCompressor]")
    {
        const U32    W = 64, H = 64;
        const size_t px = (size_t)W * H;

        std::vector<U8> rgba_opaque(px * 4);
        std::vector<U8> rgba_cutout(px * 4);
        std::vector<U8> rgb(px * 3);
        std::vector<U8> rg(px * 2);
        for (size_t i = 0; i < px; ++i)
        {
            const U8 r = (U8)(i & 0xFF), g = (U8)((i * 3) & 0xFF), b = (U8)((i * 7) & 0xFF);
            rgba_opaque[i * 4 + 0] = r;
            rgba_opaque[i * 4 + 1] = g;
            rgba_opaque[i * 4 + 2] = b;
            rgba_opaque[i * 4 + 3] = 255; // opaque
            rgba_cutout[i * 4 + 0] = r;
            rgba_cutout[i * 4 + 1] = g;
            rgba_cutout[i * 4 + 2] = b;
            rgba_cutout[i * 4 + 3] = (i % 5 == 0) ? 128 : 255;
            rgb[i * 3 + 0]         = r;
            rgb[i * 3 + 1]         = g;
            rgb[i * 3 + 2]         = b;
            rg[i * 2 + 0]          = r;
            rg[i * 2 + 1]          = g;
        }

        struct Case
        {
            const char*     mName;
            const U8*       mData;
            S32             mComponents;
            EVayuTextureJob mJob;
        };
        const Case cases[] = {
            { "4ch opaque", rgba_opaque.data(), 4, EVayuTextureJob::Albedo },
            { "4ch cutout", rgba_cutout.data(), 4, EVayuTextureJob::Albedo },
            { "3ch colour", rgb.data(), 3, EVayuTextureJob::Albedo },
            { "Emissive", rgba_opaque.data(), 4, EVayuTextureJob::Emissive },
            { "PBRNormal", rg.data(), 2, EVayuTextureJob::PBRNormal },
            { "MetalRough", rgba_opaque.data(), 4, EVayuTextureJob::MetallicRoughness },
            { "LegacyNorm", rgba_opaque.data(), 4, EVayuTextureJob::LegacyMaterialNormal },
            { "LegacySpec", rgba_opaque.data(), 4, EVayuTextureJob::LegacySpecular },
        };

        for (const Case& c : cases)
        {
            VayuBlockCompressionResult encoded;
            const bool                 ok =
                VayuImageBlockCompressor::encode(c.mData, W, H, c.mComponents, encoded, EVayuBlockCompressionFormat::Auto, c.mJob);
            UNSCOPED_INFO(c.mName);
            REQUIRE(ok);
            if (!ok)
                continue;

            const EVayuBlockCompressionFormat predicted =
                VayuImageBlockCompressor::resolveFormat(c.mData, W, H, c.mComponents, c.mJob, EVayuBlockCompressionFormat::Auto);
            // The cache-key prediction must match the format the bytes were actually
            // written in, or the worker probes a filename that does not exist.
            UNSCOPED_INFO((std::string(c.mName) + ": resolveFormat matches encode()").c_str());
            REQUIRE((int)predicted == (int)encoded.mFormat);
        }

        // Pin the specific regression: opaque 4-channel is BC1, NOT BC7. If the alpha
        // scan ever stops influencing the choice, this catches it directly rather than
        // only as a side effect of the loop above.
        UNSCOPED_INFO("opaque 4ch resolves to BC1");
        CHECK(VayuImageBlockCompressor::resolveFormat(rgba_opaque.data(), W, H, 4, EVayuTextureJob::Albedo,
                                                      EVayuBlockCompressionFormat::Auto) == EVayuBlockCompressionFormat::BC1);
        UNSCOPED_INFO("cutout 4ch does not resolve to BC1");
        CHECK(VayuImageBlockCompressor::resolveFormat(rgba_cutout.data(), W, H, 4, EVayuTextureJob::Albedo,
                                                      EVayuBlockCompressionFormat::Auto) != EVayuBlockCompressionFormat::BC1);

        // An explicit format request must pass through untouched (subject to encode()'s
        // own platform demotion, which this comparison inherits by construction).
        VayuBlockCompressionResult explicit_bc7;
        UNSCOPED_INFO("explicit BC7 encodes");
        REQUIRE(VayuImageBlockCompressor::encode(rgba_cutout.data(), W, H, 4, explicit_bc7, EVayuBlockCompressionFormat::BC7,
                                                 EVayuTextureJob::Albedo));
        UNSCOPED_INFO("explicit BC7 resolves to what encode() produced");
        CHECK(VayuImageBlockCompressor::resolveFormat(rgba_cutout.data(), W, H, 4, EVayuTextureJob::Albedo,
                                                      EVayuBlockCompressionFormat::BC7) == explicit_bc7.mFormat);
    }

    // No level of a chain is allowed to be a constant. A pyramid whose tail
    // collapses to one repeated value renders as correct up close and flat at
    // distance, which is what a texture in the cache was found doing: 248
    // distinct byte values at mip 2 and 3 at mip 3, sizes correct throughout. The cliff matters more than the average -- real mip chains
    // converge gradually toward the image's mean, they do not stop dead at one
    // level -- so this asserts a level stays non-constant whenever the one
    // above it is, and that the distinct values never collapse by more than the
    // block filter can explain.
    TEST_CASE("VayuImageBlockCompressor: No mip level of an encoded chain is a constant", "[VayuImageBlockCompressor]")
    {
        const U32       W = 512, H = 512;
        std::vector<U8> rgba((size_t)W * H * 4);
        // Low-frequency base plus per-texel variation: every mip keeps some
        // structure, so a level that loses all of it lost it in the encoder.
        for (U32 y = 0; y < H; y++)
        {
            for (U32 x = 0; x < W; x++)
            {
                const size_t p = ((size_t)y * W + x) * 4;
                rgba[p + 0]    = (U8)(32 + ((x * 8) >> 3));
                rgba[p + 1]    = (U8)(64 + ((y * 8) >> 3));
                rgba[p + 2]    = (U8)((x ^ y) & 0xFF);
                rgba[p + 3]    = 255;
            }
        }

        VayuBlockCompressionResult res;
        REQUIRE(VayuImageBlockCompressor::encode(rgba.data(), W, H, 4, res, EVayuBlockCompressionFormat::Auto, EVayuTextureJob::RGBA));

        // Smallest-first: walk from the end, which is the largest mip.
        const auto level_bytes = [&](S32 i)
        {
            return llmax(1, (int)(W >> i) / 4) * llmax(1, (int)(H >> i) / 4) * (res.mFormat == EVayuBlockCompressionFormat::BC1 ? 8 : 16);
        };
        const auto distinct = [&](S32 i)
        {
            // Smallest-first, so level i (i = 0 largest) ends at the buffer's
            // end and starts level_bytes(0..i) before it.
            size_t before = 0;
            for (S32 k = 0; k < i; k++)
            {
                before += level_bytes(k);
            }
            const size_t total = res.mBuffer.size();
            const size_t start = total - before - level_bytes(i);
            const size_t end   = total - before;
            std::set<U8> values;
            for (size_t k = start; k < end; ++k)
            {
                values.insert(res.mBuffer[k]);
            }
            return values.size();
        };

        INFO("levels encoded: " << res.mMipLevels << " format " << (int)res.mFormat);
        CHECK(res.mMipLevels >= 9);

        S32 prev = 0;
        for (S32 i = 0; i < res.mMipLevels; i++)
        {
            const S32 dim = llmax(1, (int)(W >> i));
            INFO("mip " << i << " " << dim << "x" << dim << " distinct byte values " << distinct(i));
            // A level of 64x64 or more is 256 blocks of real image; a constant
            // there means the encoder lost it, which is the shape of the cache
            // entries found flat below their midpoint. The last few levels are
            // a handful of blocks and converge legitimately.
            if (dim >= 64)
            {
                CHECK(distinct(i) > 8);
            }
            // And no collapse from a level that still had content: a real chain
            // thins gradually, it does not stop dead at one level.
            if (prev > 64)
            {
                CHECK(distinct(i) * 4 > prev);
            }
            prev = distinct(i);
        }
    }

    } // anonymous namespace
