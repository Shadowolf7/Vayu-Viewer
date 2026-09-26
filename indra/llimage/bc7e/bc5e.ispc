// bc5e.ispc - Intel-based Fast SIMD BC5 Normal Map Encoder
// Copyright (c) 2016-2026, Intel Corporation and Vayu-Viewer Contributors.
// Licensed under the MIT License.
//
// Typically compiled with ISPC multi-target:
//   ispc -O3 --pic --opt=fast-math --target=sse2-i32x4,sse4-i32x4,avx1-i32x8,avx2-i32x8

typedef unsigned int8 uint8_t;
typedef unsigned int16 uint16_t;
typedef unsigned int32 uint32_t;
typedef unsigned int64 uint64_t;
typedef int8 int8_t;
typedef int int32_t;
typedef int64 int64_t;

// Compress a single channel (16 pixels) of a 4x4 block into an 8-byte BC4 block (64 bits).
// Uses mathematically optimal integer threshold quantization without floating-point division.
inline uint64_t compress_bc4_channel(const uniform uint32_t * varying pPixels, uniform uint32_t chan_shift)
{
    varying int val[16];
    int min_v = (pPixels[0] >> chan_shift) & 0xFF;
    int max_v = min_v;
    val[0] = min_v;

    for (uniform int i = 1; i < 16; i++)
    {
        int v = (pPixels[i] >> chan_shift) & 0xFF;
        val[i] = v;
        min_v = min(min_v, v);
        max_v = max(max_v, v);
    }

    if (max_v == min_v)
    {
        // All pixels are identical: endpoints = min_v, all indices = 0
        return ((uint64_t)(uint32_t)max_v) | (((uint64_t)(uint32_t)min_v) << 8);
    }

    int delta = max_v - min_v;
    int t0 = delta * 13;
    int t1 = delta * 11;
    int t2 = delta * 9;
    int t3 = delta * 7;
    int t4 = delta * 5;
    int t5 = delta * 3;
    int t6 = delta * 1;
    uint64_t indices = 0;
    for (uniform int i = 0; i < 16; i++)
    {
        int v = (val[i] - min_v) * 14;
        int sum = (v >= t0 ? 1 : 0) +
                  (v >= t1 ? 1 : 0) +
                  (v >= t2 ? 1 : 0) +
                  (v >= t3 ? 1 : 0) +
                  (v >= t4 ? 1 : 0) +
                  (v >= t5 ? 1 : 0) +
                  (v >= t6 ? 1 : 0);

        // Map threshold sum to BC4 index:
        // sum 7 -> index 0 (max_v)
        // sum 6 -> index 2
        // sum 5 -> index 3
        // sum 4 -> index 4
        // sum 3 -> index 5
        // sum 2 -> index 6
        // sum 1 -> index 7
        // sum 0 -> index 1 (min_v)
        uint32_t idx;
        if (sum == 0)
        {
            idx = 1;
        }
        else if (sum == 7)
        {
            idx = 0;
        }
        else
        {
            idx = (uint32_t)(8 - sum);
        }

        indices |= ((uint64_t)idx) << (i * 3);
    }

    // Little-endian layout: byte 0 = max_v, byte 1 = min_v, bytes 2..7 = 48-bit indices
    return ((uint64_t)(uint32_t)max_v) |
           (((uint64_t)(uint32_t)min_v) << 8) |
           (indices << 16);
}

// Compress a batch of 4x4 blocks into BC5 format (16 bytes per block).
// Channel 0 (Red / Tangent X) -> bytes 0..7
// Channel 1 (Green / Tangent Y) -> bytes 8..15
export void bc5e_compress_blocks(
    uniform uint32_t num_blocks,
    uniform uint8_t * uniform pBlocks,
    const uniform uint32_t * uniform pPixelsRGBA)
{
    foreach (block_index = 0 ... num_blocks)
    {
        const uniform uint32_t * varying pBlockSrc = &pPixelsRGBA[block_index * 16];

        // Compress Red channel (shift 0)
        uint64_t r_block = compress_bc4_channel(pBlockSrc, 0);

        // Compress Green channel (shift 8)
        uint64_t g_block = compress_bc4_channel(pBlockSrc, 8);

        // Store into output: each block is 16 bytes (two 64-bit values)
        uniform uint64_t * varying pBlockDst = (uniform uint64_t * varying)&pBlocks[block_index * 16];
        pBlockDst[0] = r_block;
        pBlockDst[1] = g_block;
    }
}
