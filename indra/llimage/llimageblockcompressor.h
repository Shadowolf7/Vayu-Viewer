/**
 * @file llimageblockcompressor.h
 * @brief High-performance workload-aware CPU block compression for textures
 *
 * Provides BC1, BC4, BC5, and BC7 block compression via bc7enc and rgbcx,
 * with linear-space sRGB mipmap generation and workload-aware alpha analysis.
 */

#pragma once

#include "llimage.h"
#include <vector>

enum class ELLBlockCompressionFormat : U8
{
    Auto = 0,    // Auto-select: BC7 if image has non-trivial alpha (< 255), else BC1 (saving 50% VRAM)
    BC1,         // Opaque albedo / punchthrough alpha: DXT1 (4 bpp, sRGB)
    BC4,         // Single-channel mask / roughness: RGTC1 (4 bpp, linear)
    BC5,         // Two-channel normal map: RGTC2 (8 bpp, linear X/Y)
    BC7,         // Translucent / high-fidelity RGBA / PBR: BPTC (8 bpp, sRGB)
};

struct LLBlockCompressionResult
{
    ELLBlockCompressionFormat mFormat = ELLBlockCompressionFormat::Auto;
    U32 mGLInternalFormat = 0;
    U32 mGLPrimaryFormat = 0;
    U32 mWidth = 0;
    U32 mHeight = 0;
    S32 mMipLevels = 0;
    S32 mComponents = 0;
    std::vector<U8> mBuffer; // Mip chain stored in reverse order (smallest mip at offset 0, largest at end)

    // Offset in mBuffer where the largest mip starts
    size_t getLargestMipOffset() const;

    // Size in bytes of a specific discard level (0 = largest mip)
    size_t getMipBytes(S32 discard_level) const;
};

class LLImageBlockCompressor
{
public:
    static void init();

    // Textures at or below this size skip compression and stay on direct raw upload
    static constexpr U32 kMinEncodeDim = 4;

    // Checks if dimensions and components are eligible for block compression
    static bool isEligible(U32 width, U32 height, S32 components);

    // Compress raw pixel buffer into a mipped block-compressed payload
    static bool encode(const U8* src_data, U32 width, U32 height, S32 components,
                       LLBlockCompressionResult& result,
                       ELLBlockCompressionFormat format = ELLBlockCompressionFormat::Auto);

    // Convenience overload to encode from an LLImageRaw
    static bool encode(const LLImageRaw* raw_image,
                       LLBlockCompressionResult& result,
                       ELLBlockCompressionFormat format = ELLBlockCompressionFormat::Auto);
};
