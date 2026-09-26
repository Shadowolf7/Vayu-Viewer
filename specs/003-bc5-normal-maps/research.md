# Research: BC5 Normal Map Encoding

**Phase 0 output — resolves the unknowns flagged in plan.md Technical Context.**

## R1: What format should normal maps use, and does it need quality staging?

**Decision**: BC5 (GL_COMPRESSED_RG_RGTC2), full quality on every mip, no staging.

**Rationale**:
- BC5 is the canonical normal-map format (D3D10 BC5 nee ATI 3DC): two independent 8-bit endpoint-palette channels, R by default = tangent X, G = tangent Y. Z is reconstructed in the shader. Used by Unity/Unreal/Frostbite/CryEngine; DXT5nm (BC3) is its historical predecessor.
- The encoder in-tree is **closed-form optimal**: `rgbcx::encode_bc5(out, block_rgba, 0, 1, 4)` (vayuimageblockcompressor.cpp:1048) → two `encode_bc4` calls (rgbcx.cpp:2913), and `encode_bc4` (rgbcx.cpp:2608) does min/max endpoints + 7 thresholds + per-texel comparisons with the comment "this function is optimal for all possible inputs". **There is no quality knob** — Fast == Slow. So the hybrid-mips tradeoff does not exist for BC5; every mip level gets full quality at Fast-comparable cost.
- `_hq` brute-force search variants (`encode_bc4_hq` rgbcx.cpp:2730) exist in the library but are NOT wired into the compressor and are unnecessary.
- BC1+sRGB is why dimples appear today: 565 endpoints are coarse, and applying the sRGB transfer to linear normal data biases every value. Both problems vanish with BC5 linear.

**Alternatives considered**:
- **BC7 linear** (current wrong 3/4ch→BC7 mapping at :586-594): BC7 is the most expensive encode, wastes a channel on reconstructable B, and gives no quality benefit for 2-channel data. Rejected.
- **BC3 (DXT5nm)**: viable legacy alternative but BC5 is the more storage- and decode-efficient target and equally universal. Rejected only on preference — both would work.

## R2: How does the normal role get from material bind to the compressor?

**Decision**: Two bind sites assert the role onto the texture fetch path; the job rides through `createRequest`, the fetch worker, and `decodeImage` into the compressor. This is the reverted scaffold `c9026b830b` re-implemented with two corrections (below).

**Grounding**:
- glTF material: `llfetchedgltfmaterial.cpp:181` `fetch_texture(id)` → called for normal at `:204` (`mNormalTexture = fetch_texture(new_id)`). The role is known here, pre-fetch.
- Legacy material: `llface.cpp:317` `setNormalMap(tex)` → called from `llvovolume.cpp:1833` and `lldrawable.cpp:376/399`. Role known here, pre-fetch.
- Fetch worker already owns `mTextureJob` (`lltexturefetch.cpp:594`, defaulted `Default` at `:922`) and already pipes it into `decodeImage` (`llimageworker.cpp:238/255` sets `mDecodedImageRaw->setTextureJob(mTextureJob)`; `:291` passes it to `VayuImageBlockCompressor::encode`). The gap is: nothing upstream ever sets it.
- `createRequest` (lltexturefetch.cpp:2649) must gain the job parameter and forward it to the worker (scaffold `c9026b830b` did exactly this); the caller in `llviewertexture.cpp:2173` supplies it from the viewer-texture's asserted role.

**Corrections vs scaffold (why it was reverted)**:
1. Scaffold keyed "stale BC1" detection on `cache_header.mFormat` alone and only guarded MetallicRoughness/Normal/SingleChannelMask. Under the new role-keyed cache (R4), format mismatch is resolved by keying, and the header guard is a read-time cross-check, not the primary defense.
2. Scaffold added job propagation only at `createRequest`; it did not cover the same-UUID-two-roles case. The role-keyed file extension (R4) closes that.

## R3: Do we need to reduce 3/4-channel normal data before BC5?

**Decision**: Yes — reduce to 2 channels (R=ch0, G=ch1) at encode time; current mapping 3/4ch→BC7 must become →BC5.

**Grounding**: `vayuimageblockcompressor.cpp:575-596` `case EVayuTextureJob::Normal`: 1ch→BC4 (fine), 2ch→BC5 (fine), **3/4ch→BC7 (wrong)**. The `encode_bc5` call at `:1048` reads R,G from the 4-byte block buffer (`block_rgba`), so forcing BC5 for the 3/4ch case is a format-resolution change (ResolveEnum) plus keeping `is_srgb=false` (already the case at `:722-725`). The block-fill loop (single-channel and multi-channel cases, :1000-1031) already routes `components` into `block_rgba` RG regardless of source channel count.

## R4: How do cache entries for two roles of the same UUID coexist?

**Decision**: File-extension encodes the format/role in the cache key: color stays `id.bc`, normal becomes `id.bc5`. Read verifies header `mFormat` against the requesting job's expected format and treats a mismatch as a miss.

**Grounding**:
- `getFilePath` (vayubctexturecache.cpp:51-61) builds `cache_dir/<hex>/<id>.bc` — two overloads, base + `_<discard>` variants. The role/format needs to be part of `id`'s filename (e.g. `id.bc5`), or the write path passes a format-qualified name.
- `VayuBCCacheEntryHeader` (:32-44) already stores `mFormat`; the cache-hit path (lltexturefetch.cpp:1191-1214) reads it and can validate against `EVayuTextureJob`-derived expected format. "If the jobs don't match, don't apply" = mismatch → cache miss → re-encode under the correct key.
- `kFormatVersion` (4) is the dual-cache invalidation knob — must NOT bump; extension-based keying avoids it. See plan "Constraints".

## R5: Which shaders sample normal maps, and what's the exact reconstruction?

**Decision**: Replace direct `.xyz*2.0-1.0` / `.rgb*2-1` normal decoding with a two-channel reconstruct: `vec3 n = texture(...).rgb * 2.0 - 1.0; n.z = sqrt(max(0.0, 1.0 - dot(n.xy, n.xy)));` — applied only where a normal *map* is decoded into tangent space (not for sun/dir/vector0 decode).

**Touchpoints (enumerated, not swept)**:
- `class1/deferred/pbropaqueF.glsl:93` — `texture(bumpMap, normal_texcoord.xy).xyz*2.0-1.0` → reconstruct.
- `class2/deferred/pbralphaF.glsl:179` — same pattern → reconstruct.
- `class1/deferred/pbropaqueIndexedF.glsl:121` `sample_normal()` returns raw `.xyz` (:220 does `*2.0-1.0`) → reconstruct at :220.
- `class1/deferred/materialIndexedF.glsl:141` `sample_bump()` returns `.xyz` w/ gloss in `.a`; :228-231 does `*2-1` and multiplies `glossiness *= vNt.a`. Reconstruct xyz; keep `.a` line untouched — it is a provable no-op for BC5 (2-channel sample → `.a == 1.0`) and for exact-255 sources (`255/255 = 1.0`).
- `class3/deferred/materialF.glsl:149-151` — legacy material path, same gloss-in-alpha pattern → reconstruct; `.a` line untouched (same no-op argument).
- `class1/deferred/bumpF.glsl:58` — `.rgb*2.0-1.0` → reconstruct.
- `class3/environment/waterF.glsl`, `underWaterF.glsl` — water normal maps are fetched via their own path (`lldrawpoolwater.cpp:86-87`) not material slots. Defer role assertion for water normals (out of the material-slot scope); do NOT modify these shaders unless water normals get the Normal role. Flag in tasks as "verify only, no change".
- NOT to touch despite matching `*2.0-1.0` greps: `deferredUtil.glsl`, `dofCombineF`, `globalF`, `postDeferredF`, `screenSpaceReflUtil`, `shadowUtil`, `avatarV` — these decode sun/dir normals or generate normals, not bump/normal-map textures. Confirm each with rg before editing.

**Grounded**: mikktspace reconstruction contract at pbropaqueF.glsl:93-99; B-reconstruction is exactly what DXT5nm/BC5 ecosystems do. The clamp `max(0,...)` (Gemini's suggestion, adopted) prevents NaN/black on over-length XY.

## R6: Are there texture-authoring edge cases we must not break?

**Decision**: Handle under existing conventions; no content sniffing.
- Non-normal assigned to normal slot → treated as normal (authoring issue, spec edge case).
- BC5 drops alpha — but ONLY exact-255 legacy normal maps take the BC5 path (decision: alpha loss unacceptable; trivial-alpha content is lossless). Legacy normals with informative alpha (any texel < 255) stay on the color pipeline with alpha preserved, and the shared reconstruction convention (R5) keeps their `.a`-driven gloss working: shader-side `sqrt` equals stored B for unit-length normals.

## R7: Verification methodology

**Decision**: Dual-track: in-repo probe + disk capture, plus in-world visual.
- Probe: `indra/llimage/tests/vayuimageblockcompressor_test.cpp` probe test<20> — encode known normal-pattern blocks through the Normal job, decode with the analyzer, assert BC5 header + reconstructed-Z plus expected XY. (No in-tree BC7 decoder — BC5 decode path added to analyzer if absent.)
- Capture: `VAYU_DUMP_DIR` + `scripts/perf/analyze_vayu_dump.py` — confirm on-disk entries switch from BC1+sRGB to BC5+linear for normal-slot textures, color entries byte-identical (SC-004).
- Visual: reproduction vehicle, no dimples, correct detail orientation, legacy + glTF, screenshotted before/after.

**Grounding**: analyzer counts on probe 20/20 today (bit-exact); handoff notes the bugged fixture `7b54c3f7-...bc` (BC1+sRGB+Slow mip0, Fast sub-mips). After fix: normal fixture written as BC5 linear.