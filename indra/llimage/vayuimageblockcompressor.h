/**
 * @file vayuimageblockcompressor.h
 * @brief High-performance workload-aware CPU block compression for textures
 *
 * Provides BC1, BC4, BC5, and BC7 block compression via bc7enc and rgbcx,
 * with linear-space sRGB mipmap generation and workload-aware alpha analysis.
 */

#pragma once

#include "llimage.h"
#include <vector>

enum class EVayuBlockCompressionFormat : U8
{
    Auto = 0,    // Auto-select based on job role, components, and alpha analysis
    BC1,         // Opaque albedo / punchthrough alpha: DXT1 (4 bpp, sRGB)
    BC3,         // Translucent RGBA / DXT5 fallback for platforms without BPTC (8 bpp, sRGB)
    BC4,         // Single-channel mask / roughness: RGTC1 (4 bpp, linear)
    BC5,         // Two-channel normal map: RGTC2 (8 bpp, linear X/Y)
    BC7,         // Translucent / high-fidelity RGBA / PBR: BPTC (8 bpp, sRGB or linear UNORM)
};

enum class EVayuTextureJob : U8
{
    Default = 0,            // General / unknown usage (Auto: BC1 if opaque, BC7 if alpha)
    Albedo,                 // Base color / diffuse (sRGB: BC1 if opaque, BC7 if alpha)
    Normal,                 // Tangent-space normal map (linear: BC7 or BC5)
    MetallicRoughness,      // glTF PBR ORM / Metallic-Roughness (linear: BC7, or BC5/BC4)
    Emissive,               // Emissive color map (sRGB: BC1 if opaque, BC7 if alpha)
    SingleChannelMask,      // Scalar mask / roughness / height / AO (linear: BC4)
};

// Trades encode latency for compressed-image quality. Applies to both the
// BC7 encoder (mode search breadth) and the BC1 encoder (search level).
enum class EVayuBlockCompressionPreset : U8
{
    Ultrafast = 0,   // BC7 mode 6 only, no partition search; lowest CPU cost
    Fast,
    Basic,           // Default balance of quality vs. encode latency
    Slow,            // Full partition search + highest uber level
};

struct VayuBlockCompressionResult
{
    EVayuBlockCompressionFormat mFormat = EVayuBlockCompressionFormat::Auto;
    EVayuBlockCompressionPreset mPreset = EVayuBlockCompressionPreset::Slow; // preset used for Mip 0
    U32 mGLInternalFormat = 0;
    U32 mGLPrimaryFormat = 0;
    U32 mWidth = 0;
    U32 mHeight = 0;
    S32 mMipLevels = 0;
    S32 mComponents = 0;
    bool mIsMask = false; // Evaluated alpha suitability for 1-bit cutout masking (PASS_ALPHA_MASK)
    std::vector<U8> mBuffer; // Mip chain stored in reverse order (smallest mip at offset 0, largest at end)

    // Offset in mBuffer where the largest mip starts
    size_t getLargestMipOffset() const;

    // Size in bytes of a specific discard level (0 = largest mip)
    size_t getMipBytes(S32 discard_level) const;
};

class VayuImageBlockCompressor
{
public:
    static void init();

    // Standardized target encode preset for Mip 0.
    static constexpr EVayuBlockCompressionPreset getPreset() { return EVayuBlockCompressionPreset::Slow; }

    // Debug setting toggle:
    // When false (default), all mips are encoded at Slow for maximal visual fidelity.
    // When true (hybrid mode), Mip 0 is encoded at Slow while sub-mips (i >= 1) are encoded at Fast for benchmarking.
    static void setHybridMips(bool enable);
    static bool getHybridMips();

    // Textures at or below this size skip compression and stay on direct raw upload
    static constexpr U32 kMinEncodeDim = 4;

    // Checks if dimensions and components are eligible for block compression
    static bool isEligible(U32 width, U32 height, S32 components);

    // Evaluates alpha channel suitability for 1-bit cutout masking (PASS_ALPHA_MASK)
    // using a 16-bin quantized histogram and 2x2 box-filter downsampling.
    static bool analyzeAlphaMask(const U8* data_in, U32 w, U32 h, S8 alpha_offset, S8 alpha_stride);

    // Compress raw pixel buffer into a mipped block-compressed payload
    static bool encode(const U8* src_data, U32 width, U32 height, S32 components,
                       VayuBlockCompressionResult& result,
                       EVayuBlockCompressionFormat format = EVayuBlockCompressionFormat::Auto,
                       EVayuTextureJob job = EVayuTextureJob::Default);

    // Convenience overload to encode from an LLImageRaw
    static bool encode(const LLImageRaw* raw_image,
                       VayuBlockCompressionResult& result,
                       EVayuBlockCompressionFormat format = EVayuBlockCompressionFormat::Auto,
                       EVayuTextureJob job = EVayuTextureJob::Default);
};
