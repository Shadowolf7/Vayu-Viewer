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
    Auto = 0,    // Auto-select: BC7 if image has non-trivial alpha (< 255), else BC1 (saving 50% VRAM)
    BC1,         // Opaque albedo / punchthrough alpha: DXT1 (4 bpp, sRGB)
    BC4,         // Single-channel mask / roughness: RGTC1 (4 bpp, linear)
    BC5,         // Two-channel normal map: RGTC2 (8 bpp, linear X/Y)
    BC7,         // Translucent / high-fidelity RGBA / PBR: BPTC (8 bpp, sRGB)
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
    EVayuBlockCompressionPreset mPreset = EVayuBlockCompressionPreset::Basic; // preset actually used to produce mBuffer
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

class VayuImageBlockCompressor
{
public:
    static void init();

    // Current encode preset, read by worker threads on every encode() call.
    static void setPreset(EVayuBlockCompressionPreset preset);
    static EVayuBlockCompressionPreset getPreset();

    // Reported by LLImageDecodeThread once per frame (its queue backlog / pending
    // request count). encode() uses this to downgrade below the configured
    // preset when the decode queue is falling behind, trading fidelity for
    // throughput; it never raises effort above the user's configured preset.
    static void setQueueBacklog(size_t pending);

    // Resolves the preset actually used for the next encode() call: the
    // configured preset, clamped down by the current queue backlog.
    static EVayuBlockCompressionPreset getEffectivePreset();

    // Textures at or below this size skip compression and stay on direct raw upload
    static constexpr U32 kMinEncodeDim = 4;

    // Checks if dimensions and components are eligible for block compression
    static bool isEligible(U32 width, U32 height, S32 components);

    // Compress raw pixel buffer into a mipped block-compressed payload
    static bool encode(const U8* src_data, U32 width, U32 height, S32 components,
                       VayuBlockCompressionResult& result,
                       EVayuBlockCompressionFormat format = EVayuBlockCompressionFormat::Auto);

    // Convenience overload to encode from an LLImageRaw
    static bool encode(const LLImageRaw* raw_image,
                       VayuBlockCompressionResult& result,
                       EVayuBlockCompressionFormat format = EVayuBlockCompressionFormat::Auto);
};
