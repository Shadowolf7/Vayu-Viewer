# Bug Assessment: Job-Aware Block Compression Restricted to BC1 and BC7

- **Slug**: job-aware-texture-compression
- **Created**: 2026-09-16
- **Source**: pasted text
- **Verdict**: valid
- **Severity**: medium

## Report (verbatim or summarized)

> BC1 and BC7 appear to be the only compression formats in play, where job-aware compression should be using all appropriate compressions for their respective job, eg. metallic maps etc.

## Symptom

During in-world texture ingestion with block compression enabled (`RenderCompressTextures = true`), only `EVayuBlockCompressionFormat::BC1` and `EVayuBlockCompressionFormat::BC7` (or `BC3` on macOS) are ever selected. Specialized formats supported by `VayuImageBlockCompressor`—namely `BC4` (single-channel linear masks/roughness) and `BC5` (two-channel linear normal maps)—are never utilized for textures downloaded from the grid. Furthermore, PBR data maps such as metallic-roughness (glTF ORM: Occlusion, Roughness, Metallic) and normal maps are collapsed into `BC1` (if 3-component or opaque 4-component) or `BC7` with non-linear sRGB downsampling, causing channel cross-talk and visual degradation.

## Reproduction

1. Enable texture compression via Preferences > Graphics > Advanced ("Compress Textures in VRAM") or by verifying `RenderCompressTextures = 1` in debug settings.
2. Log into any region containing modern PBR materials (objects utilizing base color, normal maps, and metallic-roughness textures).
3. Inspect the active texture formats either via telemetry, GPU debugger, or by checking `VayuBCCacheEntryHeader::mFormat` in the on-disk cache files (`~/.secondlife/cache/bc_cache/`).
4. **Observed Result**: Every compressed texture entry is stored as either format `1` (`BC1`) or format `5` (`BC7`) (or format `2` on macOS). Zero entries ever resolve to format `3` (`BC4`) or format `4` (`BC5`).
5. **Expected Result**: Job-aware compression should assign formats matching the functional role and color space of each texture:
   - Albedo / Base Color: `BC1` (opaque) or `BC7` (alpha/translucent) in sRGB space.
   - Metallic-Roughness / ORM: Linear `BC7` (`GL_COMPRESSED_RGBA_BPTC_UNORM`) or `BC5`/`BC4` without sRGB gamma distortion or RGB cross-talk.
   - Normal Maps: `BC5` (`GL_COMPRESSED_RG_RGTC2`) or linear `BC7`.
   - Single-channel roughness / height / ambient occlusion masks: `BC4` (`GL_COMPRESSED_RED_RGTC1`).

## Suspected Code Paths

- [`indra/llimage/vayuimageblockcompressor.cpp:463-588`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayuimageblockcompressor.cpp#L463-L588) — `VayuImageBlockCompressor::encode` format resolution in `Auto` mode checks only `components` (1, 2, 3, 4) and an alpha channel scan. Because JPEG 2000 (.j2c) assets from the grid always decode into 3 components (RGB) or 4 components (RGBA), the `components == 1` (`BC4`) and `components == 2` (`BC5`) branches are unreachable in real workloads.
- [`indra/llimage/vayuimageblockcompressor.cpp:590, 609-613, 641-664`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayuimageblockcompressor.cpp#L590-L664) — Mip pyramid generation flags `is_srgb = true` for all BC1, BC3, and BC7 textures, routing them through non-linear sRGB transfer functions (`g_srgb_to_linear` / `linear_to_srgb_u8`) and assigning `GL_COMPRESSED_SRGB_ALPHA_*`. Linear PBR textures (metallic, roughness, normals) are thus corrupted across downsampled mips.
- [`indra/llimage/llimageworker.cpp:284`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/llimageworker.cpp#L284) — Background decode worker unconditionally calls `VayuImageBlockCompressor::encode(mDecodedImageRaw, *comp_res)` with default `format = EVayuBlockCompressionFormat::Auto`, possessing no knowledge of the texture's functional job.
- [`indra/llimage/llimageworker.h:52-55`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/llimageworker.h#L52-L55) & [`indra/llimage/llimageworker.cpp:97-103`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/llimageworker.cpp#L97-L103) — `LLImageDecodeThread::decodeImage` interface only receives `bool allow_compression` without any texture semantic role or compression format parameter.
- [`indra/newview/lltexturefetch.cpp:1891-1896`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/lltexturefetch.cpp#L1891-L1896) — `LLTextureFetchWorker::doWork` passes `mAllowCompression` as a bare boolean into `decodeImage`.
- [`indra/newview/llviewertexture.cpp:2233-2237`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/llviewertexture.cpp#L2233-L2237) — `LLViewerFetchedTexture::updateFetch()` computes `allow_compression` as a boolean and invokes `createRequest(mFTType, ...)`, omitting material slot or channel identity.
- [`indra/newview/llviewerobject.cpp:5420-5426, 5359-5368`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/llviewerobject.cpp#L5420-L5426) — Material textures (`mBaseColorTexture`, `mNormalTexture`, `mMetallicRoughnessTexture`, `mEmissiveTexture`, `mTENormalMaps`, `mTESpecularMaps`) are requested via `LLViewerTextureManager::getFetchedTexture` with default boost levels and no semantic channel tags.
- [`indra/llrender/llimagegl.cpp:1882, 1903`](file:///home/roger/Projects/Vayu-Viewer/indra/llrender/llimagegl.cpp#L1882-L1903) — Main-thread fallback compression in `LLImageGL::createGLTexture` also defaults to `Auto` without job context.

## Root Cause Hypothesis

`VayuImageBlockCompressor` contains valid encoders for BC1, BC3, BC4, BC5, and BC7, but format selection was implemented as an isolated heuristic in `encode()` that depends strictly on `components` (channel count) and an alpha scan (`min_a`). In Second Life, all grid texture assets (albedo, normal maps, metallic-roughness maps) are encoded in JPEG 2000 (`.j2c`) containers that invariably unpack into 3 (RGB) or 4 (RGBA) channels upon decoding by OpenJPEG or Kakadu. Consequently, `components` is never 1 or 2, causing `Auto` to exclusively route 3-channel and opaque 4-channel textures to `BC1`, and translucent 4-channel textures to `BC7` (or `BC3` on macOS). Furthermore, the upstream caller pipeline (`LLViewerObject` -> `LLViewerFetchedTexture` -> `LLTextureFetchWorker` -> `LLImageDecodeThread` -> `LLImageWorker`) only passes a binary `allow_compression` flag, dropping all semantic information regarding whether a texture is an albedo map, normal map, metallic-roughness map, or mask. Because of this architectural disconnection, the compressor cannot be "job-aware" and inappropriately compresses uncorrelated linear data (e.g. Roughness in G and Metalness in B) using BC1's single-line RGB color endpoints and non-linear sRGB downsampling.

Confidence: **High**.

## Proposed Remediation

**Preferred**:
Establish a job-aware pipeline that propagates texture role hints from the scene and material managers down through the fetch and decode stack:

1. **Define Texture Usage / Job Enum**:
   Introduce an enum in `indra/llimage/` (or `llrender/`), such as `EVayuTextureJob`:
   - `Unknown` / `Auto`
   - `Albedo` (Diffuse / Base Color, sRGB)
   - `Normal` (Tangent-space normal vector, linear)
   - `MetallicRoughness` (glTF ORM: Occlusion, Roughness, Metal, linear)
   - `Emissive` (Emissive color, sRGB)
   - `SingleChannelMask` (Roughness, scalar mask, linear)

2. **Propagate Job Hint Through Fetch & Decode**:
   - Tag texture requests in `LLViewerObject::updateTEMaterialTextures` and `LLFetchedGLTFMaterial::replaceLocalTexture` with their corresponding job.
   - Pass the job hint through `LLViewerFetchedTexture`, `LLTextureFetch::createRequest`, `LLTextureFetchWorker`, and `LLImageDecodeThread::decodeImage` down to `LLImageWorker`.

3. **Format Selection & Color-Space Handling in `VayuImageBlockCompressor`**:
   - **`MetallicRoughness`**:
     - Encode using linear `BC7` (`GL_COMPRESSED_RGBA_BPTC_UNORM`). Unlike BC1, BC7 supports multiple partition subsets and independent scalar channels, eliminating cross-talk between the green (roughness) and blue (metal) channels.
     - Generate mips using `downsample_half_linear` rather than `downsample_half_srgb`.
   - **`Normal`**:
     - If shader-side $Z$-reconstruction is active ($Z = \sqrt{\max(0, 1 - X^2 - Y^2)}$), extract R and G into two channels and compress with `BC5` (`GL_COMPRESSED_RG_RGTC2`), cutting VRAM and eliminating 5:6:5 BC1 normal banding.
     - Alternatively, compress with linear `BC7` (`GL_COMPRESSED_RGBA_BPTC_UNORM`) with linear downsampling to preserve 3-channel normal vectors without modifying existing shader pipelines.
   - **`Albedo` / `Emissive`**:
     - Opaque: `BC1` (`GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT`).
     - Translucent: `BC7` (`GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM`, or `BC3` on macOS).
   - **`SingleChannelMask`**:
     - Compress with `BC4` (`GL_COMPRESSED_RED_RGTC1`).

4. **Multi-Role / Shared UUID Conflict Resolution**:
   If a texture UUID is referenced in conflicting roles across multiple materials (e.g. used as both an albedo and a normal map), promote the texture to the higher-fidelity linear format (`BC7`).

**Alternatives**:
- *In-Encoder Pixel Heuristics*: Analyze pixel data directly within `VayuImageBlockCompressor::encode()` without changing caller signatures (e.g., test if $R \approx G \approx B$ across texels to detect grayscale masks for `BC4`, or test if vector length $\sqrt{R^2+G^2+B^2} \approx 1$ with high blue content to detect normal maps for `BC5`).
  *Trade-offs*: Introduces non-trivial CPU latency scanning texels, vulnerable to false positives/negatives, and cannot reliably distinguish a metallic-roughness ORM map from a general color texture.

**Files likely to change**:
- [`indra/llimage/vayuimageblockcompressor.h`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayuimageblockcompressor.h)
- [`indra/llimage/vayuimageblockcompressor.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayuimageblockcompressor.cpp)
- [`indra/llimage/llimageworker.h`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/llimageworker.h)
- [`indra/llimage/llimageworker.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/llimageworker.cpp)
- [`indra/newview/lltexturefetch.h`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/lltexturefetch.h)
- [`indra/newview/lltexturefetch.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/lltexturefetch.cpp)
- [`indra/newview/llviewertexture.h`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/llviewertexture.h)
- [`indra/newview/llviewertexture.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/llviewertexture.cpp)
- [`indra/newview/llviewerobject.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/llviewerobject.cpp)
- [`indra/newview/llfetchedgltfmaterial.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/llfetchedgltfmaterial.cpp)
- [`indra/llrender/llimagegl.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llrender/llimagegl.cpp)

**Tests to add or update**:
- Unit tests in [`indra/llimage/tests/vayuimageblockcompressor_test.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/tests/vayuimageblockcompressor_test.cpp) verifying that passing job hints (e.g. `MetallicRoughness`, `Normal`) resolves to linear `BC7`, `BC5`, or `BC4`, and enforces linear mip downsampling.
- Unit tests in [`indra/llimage/tests/llimageworker_test.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/tests/llimageworker_test.cpp) verifying job hint propagation through `LLImageDecodeThread::decodeImage`.

## Risks & Considerations

- **Shader Sampler Expectations**: Existing shaders (`pbropaqueF.glsl`, `pbralphaF.glsl`, `materialF.glsl`) sample `bumpMap` using `.xyz * 2.0 - 1.0`. If normal maps are compressed to `BC5` (`GL_COMPRESSED_RG_RGTC2`), OpenGL returns `(R, G, 0.0, 1.0)`. Shaders must either reconstruct $Z$ dynamically or normal maps must use linear 3-channel `BC7` (`GL_COMPRESSED_RGBA_BPTC_UNORM`).
- **Disk Cache Compatibility (`VayuBCTextureCache`)**: The cache header stores `mFormat` as a `U8`. The cache reader dynamically respects the stored format (`BC1`, `BC3`, `BC4`, `BC5`, `BC7`), so no schema migration is required; however, existing on-disk cache entries previously encoded as `BC1` may persist until invalidated or cleared.
- **Shared UUID Semantic Collisions**: In Second Life content, authors can assign any UUID to any texture slot. If a texture is used as an albedo in one place and an ORM map in another, binding a `BC5` texture to an albedo slot would render red/green. Promoting contested textures to linear `BC7` preserves all 3/4 channels and avoids rendering breakages.

## Open Questions

- Should normal maps default to two-channel `BC5` (requiring shader Z-reconstruction) or linear `BC7` (retaining full $(X, Y, Z)$ compatibility across all legacy and PBR shaders)?
- Should legacy Bump / Normal / Specular maps (from `LLMaterialParams`) share the same job classification as glTF PBR maps?
