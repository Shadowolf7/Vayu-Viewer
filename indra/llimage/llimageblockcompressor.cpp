/**
 * @file llimageblockcompressor.cpp
 * @brief High-performance workload-aware CPU block compression implementation
 */

#include "linden_common.h"
#include "llimageblockcompressor.h"
#include "bc7e/bc7enc.h"
#include "bc7e/rgbcx.h"
#include "llerror.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <cstring>

// Ensure compressed texture formats are defined without needing GL headers
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT  0x83F1
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
#endif

// RGTC formats (BC4/BC5)
#ifndef GL_COMPRESSED_RED_RGTC1
#define GL_COMPRESSED_RED_RGTC1           0x8DBB
#define GL_COMPRESSED_SIGNED_RED_RGTC1    0x8DBC
#define GL_COMPRESSED_RG_RGTC2            0x8DBD
#define GL_COMPRESSED_SIGNED_RG_RGTC2     0x8DBE
#endif

// BPTC formats (BC6H/BC7)
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM     0x8E8C
#define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM 0x8E8D
#define GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT 0x8E8E
#define GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT 0x8E8F
#endif

namespace
{

static float g_srgb_to_linear[256];
static uint8_t g_linear_to_srgb[4096];
static bool g_tables_ready = false;
static std::once_flag g_init_once;
static std::atomic<U8> g_preset{ (U8)ELLBlockCompressionPreset::Basic };

// BC1 rgbcx encode level per preset (see rgbcx.h: MIN_LEVEL=0, MAX_LEVEL=18)
static uint32_t bc1_level_for_preset(ELLBlockCompressionPreset preset)
{
    switch (preset)
    {
    case ELLBlockCompressionPreset::Ultrafast: return 0;
    case ELLBlockCompressionPreset::Fast:      return 3;
    case ELLBlockCompressionPreset::Slow:      return 10;
    case ELLBlockCompressionPreset::Basic:
    default:                                   return 5;
    }
}

// Applies the preset's mode/partition/uber-level tradeoff to a BC7 params block
// already initialized via bc7enc_compress_block_params_init().
static void apply_bc7_preset(bc7enc_compress_block_params& params, ELLBlockCompressionPreset preset)
{
    switch (preset)
    {
    case ELLBlockCompressionPreset::Ultrafast:
        params.m_mode_mask = (1u << 6); // mode 6 only: single partition, handles RGB + alpha
        params.m_max_partitions = 0;
        params.m_uber_level = 0;
        break;
    case ELLBlockCompressionPreset::Fast:
        params.m_max_partitions = 16;
        params.m_uber_level = 0;
        break;
    case ELLBlockCompressionPreset::Slow:
        params.m_max_partitions = BC7ENC_MAX_PARTITIONS;
        params.m_uber_level = BC7ENC_MAX_UBER_LEVEL;
        break;
    case ELLBlockCompressionPreset::Basic:
    default:
        params.m_max_partitions = BC7ENC_MAX_PARTITIONS;
        params.m_uber_level = 0;
        break;
    }
}

static void init_tables()
{
    if (g_tables_ready)
        return;

    for (int i = 0; i < 256; i++)
    {
        const float c = i / 255.0f;
        g_srgb_to_linear[i] = (c <= 0.04045f) ? (c / 12.92f) : powf((c + 0.055f) / 1.055f, 2.4f);
    }
    for (int i = 0; i < 4096; i++)
    {
        const float l = i / 4095.0f;
        const float s = (l <= 0.0031308f) ? (l * 12.92f) : (1.055f * powf(l, 1.0f / 2.4f) - 0.055f);
        int v = (int)(s * 255.0f + 0.5f);
        g_linear_to_srgb[i] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    g_tables_ready = true;
}

static inline uint8_t linear_to_srgb_u8(float l)
{
    int idx = (int)(l * 4095.0f + 0.5f);
    return g_linear_to_srgb[idx < 0 ? 0 : (idx > 4095 ? 4095 : idx)];
}

static void init_compression_tables()
{
    std::call_once(g_init_once, []() {
        rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
        bc7enc_compress_block_init();
        init_tables();
    });
}

// Box-filter downsampling with linear-space colour averaging (sRGB correct)
static void downsample_half_srgb(const uint8_t* src, uint32_t sw, uint32_t sh, S32 channels, uint8_t* dst)
{
    const uint32_t dw = llmax(1u, sw / 2);
    const uint32_t dh = llmax(1u, sh / 2);

    for (uint32_t y = 0; y < dh; y++)
    {
        uint32_t sy0 = y * 2;
        uint32_t sy1 = (sy0 + 1 < sh) ? (sy0 + 1) : sy0;
        const uint8_t* r0 = src + (size_t)sy0 * sw * channels;
        const uint8_t* r1 = src + (size_t)sy1 * sw * channels;
        uint8_t* out = dst + (size_t)y * dw * channels;

        for (uint32_t x = 0; x < dw; x++)
        {
            uint32_t sx0 = x * 2;
            uint32_t sx1 = (sx0 + 1 < sw) ? (sx0 + 1) : sx0;
            const uint8_t* a = r0 + (size_t)sx0 * channels;
            const uint8_t* b = r0 + (size_t)sx1 * channels;
            const uint8_t* c = r1 + (size_t)sx0 * channels;
            const uint8_t* d = r1 + (size_t)sx1 * channels;

            const int color_channels = llmin(channels, 3);
            for (int ch = 0; ch < color_channels; ch++)
            {
                const float lin = (g_srgb_to_linear[a[ch]] + g_srgb_to_linear[b[ch]] +
                                   g_srgb_to_linear[c[ch]] + g_srgb_to_linear[d[ch]]) * 0.25f;
                out[ch] = linear_to_srgb_u8(lin);
            }
            if (channels == 4)
            {
                // Alpha is linear, average directly
                out[3] = (uint8_t)((a[3] + b[3] + c[3] + d[3] + 2) / 4);
            }
            out += channels;
        }
    }
}

// Box-filter downsampling for linear channels (normal maps, roughness, masks)
static void downsample_half_linear(const uint8_t* src, uint32_t sw, uint32_t sh, S32 channels, uint8_t* dst)
{
    const uint32_t dw = llmax(1u, sw / 2);
    const uint32_t dh = llmax(1u, sh / 2);

    for (uint32_t y = 0; y < dh; y++)
    {
        uint32_t sy0 = y * 2;
        uint32_t sy1 = (sy0 + 1 < sh) ? (sy0 + 1) : sy0;
        const uint8_t* r0 = src + (size_t)sy0 * sw * channels;
        const uint8_t* r1 = src + (size_t)sy1 * sw * channels;
        uint8_t* out = dst + (size_t)y * dw * channels;

        for (uint32_t x = 0; x < dw; x++)
        {
            uint32_t sx0 = x * 2;
            uint32_t sx1 = (sx0 + 1 < sw) ? (sx0 + 1) : sx0;
            const uint8_t* a = r0 + (size_t)sx0 * channels;
            const uint8_t* b = r0 + (size_t)sx1 * channels;
            const uint8_t* c = r1 + (size_t)sx0 * channels;
            const uint8_t* d = r1 + (size_t)sx1 * channels;

            for (int ch = 0; ch < channels; ch++)
            {
                out[ch] = (uint8_t)((a[ch] + b[ch] + c[ch] + d[ch] + 2) / 4);
            }
            out += channels;
        }
    }
}

static inline uint32_t calc_level_bytes(uint32_t width, uint32_t height, uint32_t block_bytes)
{
    uint32_t bw = (width + 3) / 4;
    uint32_t bh = (height + 3) / 4;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;
    return bw * bh * block_bytes;
}

} // namespace

size_t LLBlockCompressionResult::getMipBytes(S32 discard_level) const
{
    if (discard_level < 0 || discard_level >= mMipLevels)
        return 0;

    uint32_t w = llmax(1u, mWidth >> discard_level);
    uint32_t h = llmax(1u, mHeight >> discard_level);
    uint32_t block_bytes = (mFormat == ELLBlockCompressionFormat::BC1 || mFormat == ELLBlockCompressionFormat::BC4) ? 8 : 16;
    return calc_level_bytes(w, h, block_bytes);
}

size_t LLBlockCompressionResult::getLargestMipOffset() const
{
    if (mBuffer.empty() || mMipLevels <= 0)
        return 0;
    size_t level0_bytes = getMipBytes(0);
    return (mBuffer.size() >= level0_bytes) ? (mBuffer.size() - level0_bytes) : 0;
}

void LLImageBlockCompressor::init()
{
    init_compression_tables();
}

void LLImageBlockCompressor::setPreset(ELLBlockCompressionPreset preset)
{
    g_preset.store((U8)preset, std::memory_order_relaxed);
}

ELLBlockCompressionPreset LLImageBlockCompressor::getPreset()
{
    return (ELLBlockCompressionPreset)g_preset.load(std::memory_order_relaxed);
}

bool LLImageBlockCompressor::isEligible(U32 width, U32 height, S32 components)
{
    if (width < kMinEncodeDim || height < kMinEncodeDim)
        return false;
    if (components < 1 || components > 4)
        return false;
    return true;
}

bool LLImageBlockCompressor::encode(const LLImageRaw* raw_image,
                                    LLBlockCompressionResult& result,
                                    ELLBlockCompressionFormat format)
{
    if (!raw_image || raw_image->isBufferInvalid())
        return false;
    return encode(raw_image->getData(), raw_image->getWidth(), raw_image->getHeight(),
                  raw_image->getComponents(), result, format);
}

bool LLImageBlockCompressor::encode(const U8* src_data, U32 width, U32 height, S32 components,
                                    LLBlockCompressionResult& result,
                                    ELLBlockCompressionFormat format)
{
    if (!src_data || !isEligible(width, height, components))
        return false;

    init();

    // 1. Resolve format
    ELLBlockCompressionFormat resolved = format;
    bool force_opaque = false;

    if (resolved == ELLBlockCompressionFormat::Auto)
    {
        if (components == 1)
        {
            resolved = ELLBlockCompressionFormat::BC4;
        }
        else if (components == 2)
        {
            resolved = ELLBlockCompressionFormat::BC5;
        }
        else if (components == 3)
        {
            resolved = ELLBlockCompressionFormat::BC1;
        }
        else if (components == 4)
        {
            // Scan alpha channel across the entire image to distinguish:
            // 1. Fully opaque (all pixels have a == 255) -> BC1 (saves 50% VRAM)
            // 2. Genuine cutout / transparency / transparent layers -> BC7 (preserves exact alpha)
            const size_t total_px = (size_t)width * height;
            uint8_t min_a = 255;
            for (size_t i = 0; i < total_px; i++)
            {
                uint8_t a = src_data[i * 4 + 3];
                if (a < 255)
                {
                    min_a = a;
                    break;
                }
            }

            if (min_a == 255)
            {
                // Fully opaque already -> BC1
                resolved = ELLBlockCompressionFormat::BC1;
            }
            else
            {
                // Transparency present: MUST USE BC7
                resolved = ELLBlockCompressionFormat::BC7;
            }
        }
    }

    const bool is_srgb = (resolved == ELLBlockCompressionFormat::BC1 || resolved == ELLBlockCompressionFormat::BC7);
    const uint32_t block_bytes = (resolved == ELLBlockCompressionFormat::BC1 || resolved == ELLBlockCompressionFormat::BC4) ? 8 : 16;

    // 2. Generate uncompressed mip pyramid
    std::vector<std::vector<uint8_t>> uncompressed_mips;
    {
        // Level 0
        std::vector<uint8_t> level0((size_t)width * height * components);
        memcpy(level0.data(), src_data, level0.size());
        uncompressed_mips.push_back(std::move(level0));

        uint32_t cur_w = width;
        uint32_t cur_h = height;
        while (cur_w > 1 || cur_h > 1)
        {
            uint32_t nw = llmax(1u, cur_w / 2);
            uint32_t nh = llmax(1u, cur_h / 2);
            std::vector<uint8_t> next_mip((size_t)nw * nh * components);

            if (is_srgb)
                downsample_half_srgb(uncompressed_mips.back().data(), cur_w, cur_h, components, next_mip.data());
            else
                downsample_half_linear(uncompressed_mips.back().data(), cur_w, cur_h, components, next_mip.data());

            uncompressed_mips.push_back(std::move(next_mip));
            cur_w = nw;
            cur_h = nh;
        }
    }

    const size_t num_mips = uncompressed_mips.size();

    // 3. Compute total buffer size for all compressed mips
    size_t total_compressed_bytes = 0;
    std::vector<size_t> mip_byte_sizes(num_mips);
    for (size_t i = 0; i < num_mips; i++)
    {
        uint32_t mw = llmax(1u, width >> i);
        uint32_t mh = llmax(1u, height >> i);
        mip_byte_sizes[i] = calc_level_bytes(mw, mh, block_bytes);
        total_compressed_bytes += mip_byte_sizes[i];
    }

    result.mFormat = resolved;
    result.mWidth = width;
    result.mHeight = height;
    result.mMipLevels = (S32)num_mips;
    result.mComponents = components;
    result.mBuffer.resize(total_compressed_bytes);

    switch (resolved)
    {
    case ELLBlockCompressionFormat::BC1:
        result.mGLInternalFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
        result.mGLPrimaryFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
        break;
    case ELLBlockCompressionFormat::BC4:
        result.mGLInternalFormat = GL_COMPRESSED_RED_RGTC1;
        result.mGLPrimaryFormat = GL_COMPRESSED_RED_RGTC1;
        break;
    case ELLBlockCompressionFormat::BC5:
        result.mGLInternalFormat = GL_COMPRESSED_RG_RGTC2;
        result.mGLPrimaryFormat = GL_COMPRESSED_RG_RGTC2;
        break;
    case ELLBlockCompressionFormat::BC7:
    default:
        result.mGLInternalFormat = GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;
        result.mGLPrimaryFormat = GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;
        break;
    }

    const ELLBlockCompressionPreset preset = getPreset();

    bc7enc_compress_block_params bc7_params;
    if (resolved == ELLBlockCompressionFormat::BC7)
    {
        bc7enc_compress_block_params_init(&bc7_params);
        apply_bc7_preset(bc7_params, preset);
    }

    // 4. Encode mips into reverse order (smallest mip at offset 0, largest mip at end)
    // Calculate mip offsets in reverse order:
    // Mip index in uncompressed_mips: 0 is largest (w, h), num_mips - 1 is smallest (1, 1)
    // Offset in mBuffer: smallest mip (num_mips - 1) is at offset 0.
    std::vector<size_t> buffer_offsets(num_mips);
    size_t current_offset = 0;
    for (int i = (int)num_mips - 1; i >= 0; i--)
    {
        buffer_offsets[i] = current_offset;
        current_offset += mip_byte_sizes[i];
    }

    // Encode each mip level
    for (size_t i = 0; i < num_mips; i++)
    {
        const uint8_t* src = uncompressed_mips[i].data();
        uint32_t mw = llmax(1u, width >> i);
        uint32_t mh = llmax(1u, height >> i);
        uint32_t bw = (mw + 3) / 4;
        uint32_t bh = (mh + 3) / 4;
        if (bw < 1) bw = 1;
        if (bh < 1) bh = 1;

        uint8_t* out = result.mBuffer.data() + buffer_offsets[i];

        for (uint32_t by = 0; by < bh; by++)
        {
            for (uint32_t bx = 0; bx < bw; bx++)
            {
                // Extract 4x4 block of RGBA pixels (or single/two-channel) with edge clamping
                uint8_t block_rgba[64];
                memset(block_rgba, 0, sizeof(block_rgba));

                for (uint32_t py = 0; py < 4; py++)
                {
                    uint32_t y = (by * 4 + py < mh) ? (by * 4 + py) : (mh - 1);
                    for (uint32_t px = 0; px < 4; px++)
                    {
                        uint32_t x = (bx * 4 + px < mw) ? (bx * 4 + px) : (mw - 1);
                        const uint8_t* pixel_src = src + ((size_t)y * mw + x) * components;
                        uint8_t* pixel_dst = &block_rgba[(py * 4 + px) * 4];

                        if (components == 4)
                        {
                            memcpy(pixel_dst, pixel_src, 4);
                        }
                        else if (components == 3)
                        {
                            pixel_dst[0] = pixel_src[0];
                            pixel_dst[1] = pixel_src[1];
                            pixel_dst[2] = pixel_src[2];
                            pixel_dst[3] = 255;
                        }
                        else if (components == 2)
                        {
                            pixel_dst[0] = pixel_src[0];
                            pixel_dst[1] = pixel_src[1];
                            pixel_dst[2] = 0;
                            pixel_dst[3] = 255;
                        }
                        else if (components == 1)
                        {
                            pixel_dst[0] = pixel_src[0];
                            pixel_dst[1] = pixel_src[0];
                            pixel_dst[2] = pixel_src[0];
                            pixel_dst[3] = 255;
                        }
                    }
                }

                switch (resolved)
                {
                case ELLBlockCompressionFormat::BC1:
                    rgbcx::encode_bc1(bc1_level_for_preset(preset), out, block_rgba, false, false);
                    out += 8;
                    break;
                case ELLBlockCompressionFormat::BC4:
                    rgbcx::encode_bc4(out, block_rgba, 4);
                    out += 8;
                    break;
                case ELLBlockCompressionFormat::BC5:
                    rgbcx::encode_bc5(out, block_rgba, 0, 1, 4);
                    out += 16;
                    break;
                case ELLBlockCompressionFormat::BC7:
                default:
                    bc7enc_compress_block(out, block_rgba, &bc7_params);
                    out += 16;
                    break;
                }
            }
        }
    }

    return true;
}
