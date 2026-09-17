# Bug Assessment: Dynamic Compression Preset Downgrade Causes BC Cache Thrashing and Redundant Re-encodes

- **Slug**: bc-cache-thrashing
- **Created**: 2026-09-16
- **Source**: https://github.com/Shadowolf7/Vayu-Viewer/issues/96
- **Verdict**: valid (superseded by architecture change)
- **Status**: closed (superseded by [Issue #98](https://github.com/Shadowolf7/Vayu-Viewer/issues/98))
- **Severity**: high

## URL Trust Policy

- **Supplied URL**: `https://github.com/Shadowolf7/Vayu-Viewer/issues/96`
- **Parsed Host**: `github.com`
- **Policy Branch**: `allowlisted`

## Report (verbatim or summarized)

From [Issue #96](https://github.com/Shadowolf7/Vayu-Viewer/issues/96):
> **Title**: [Bug]: Dynamic compression preset downgrade causes BC cache thrashing and redundant re-encodes
>
> ### Summary
> During high image decode workloads (such as login or teleporting), `VayuImageBlockCompressor` dynamically downgrades its compression preset from `Basic` to `Fast` or `Ultrafast` to prevent worker thread queues from backing up. However, `VayuBCTextureCache::readEntry()` rejects any cached file whose preset is lower than the current effective preset. Once the backlog clears, previously cached entries become immediate cache misses, forcing expensive OpenJPEG decompressions and BC re-encodings of textures already on disk.
>
> ### Steps to Reproduce
> 1. Visit several texture-dense regions to build up queue backlog (`g_queue_backlog > kModerateBacklogThreshold`).
> 2. Observe textures being encoded at `Fast` (1) or `Ultrafast` (0) and saved into `~/.vayu/cache/bccache/`.
> 3. Allow the queue to drain so `getEffectivePreset()` returns `Basic` (2).
> 4. Re-examine or reload those textures: `readEntry()` checks `file_header.mMeta.mPreset < min_preset` (`1 < 2`) and returns `false`.
> 5. Worker threads are forced to run full CPU OpenJPEG software decompressions and BC re-encodings for assets already cached on disk.
>
> ### Root Cause
> In `indra/llimage/vayubctexturecache.cpp:516`:
> ```cpp
> if (file_header.mMeta.mPreset < min_preset)
> {
>     return false;
> }
> ```
> And `indra/llimage/llimageworker.cpp:217`:
> ```cpp
> const U8 min_preset = (U8)VayuImageBlockCompressor::getEffectivePreset();
> if (VayuBCTextureCache::instance().readEntry(mID, discard, min_preset, cache_header, cache_buffer))
> ```
> Strictly enforcing `preset >= min_preset` causes a positive feedback loop: when the queue catches up, all files cached during backlog are invalidated, causing a flood of cache misses that immediately re-spikes the queue backlog.
>
> ### Proposed Fix
> - Relax `readEntry` to accept already-compressed textures regardless of preset, or decouple cache hit qualification from the dynamic encode-backlog preset.
> - An existing compressed texture on disk should almost always be favored over running expensive OpenJPEG software decompression on worker threads.

## Symptom

1. **Multi-Pass Decode/Encode Thrashing**:
   When streaming textures in busy regions or following a teleport, image worker thread backlog exceeds `kModerateBacklogThreshold` (12) and `kHeavyBacklogThreshold` (32). `VayuImageBlockCompressor` downgrades its encoding preset from the configured target (e.g. `Slow` = 3 or `Basic` = 2) down to `Fast` (1) or `Ultrafast` (0).
   
   Because [`VayuBCTextureCache::readEntry()`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayubctexturecache.cpp#L601) enforces `file_header.mMeta.mPreset < min_preset` where `min_preset` is derived from instantaneous queue depth (`getEffectivePreset()`), a texture can be subjected to **up to three separate decode/encode cycles**:
   - **Pass 1 (Heavy Backlog > 32)**: Encoded at `Ultrafast` (0) and written to disk.
   - **Pass 2 (Moderate Backlog 12–32)**: As queue drains, `getEffectivePreset()` steps up to `Fast` (1). Cache check rejects the `Ultrafast` entry (`0 < 1`). Full OpenJPEG CPU decompression runs again, re-saturating the queue into moderate backlog and encoding at `Fast` (1).
   - **Pass 3 (Idle/Normal Backlog < 12)**: Once the queue clears, `getEffectivePreset()` returns target (`Basic`/`Slow`). Cache check rejects the `Fast` entry (`1 < 2`). OpenJPEG CPU decompression runs a third time to finally encode at target quality.

   Each pass incurs a complete OpenJPEG software decompression (~20–100 ms of CPU core time) plus re-encoding and disk writes, keeping `ThreadPool:Imag` worker threads pinned at 100% CPU and causing noticeable frame stuttering.

2. **Full-Resolution Cache Clobbering in `writeEntry()`**:
   In [`VayuBCTextureCache::writeEntry()`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayubctexturecache.cpp#L707-L711), the overwrite protection condition couples discard level (spatial resolution) with compression preset using logical `AND`:
   ```cpp
   if (existing_fh.mMeta.mDiscardLevel <= local_header.mDiscardLevel &&
       existing_fh.mMeta.mPreset >= local_header.mPreset)
   {
       return;
   }
   ```
   If an existing cache entry contains full-resolution texture data (`mDiscardLevel == 0`) compressed under `Fast` (`mPreset == 1`), and a later coarse mip request (e.g. `mDiscardLevel == 3`) completes under an idle queue at `Basic` (`mPreset == 2`), this check evaluates to `false` (because `1 >= 2` is false).
   
   As a consequence, the **coarse low-resolution mip overwrites the full-resolution file** on disk. Subsequent requests requiring full resolution suffer permanent cache misses, destroying the benefits of single-file-per-asset caching.

## Reproduction

1. Set compression preset to `Slow` or `Basic` in Preferences.
2. Teleport to a texture-dense region to push `ThreadPool:Imag` backlog past 32 items.
3. Observe initial textures encoded at `Ultrafast` (0) written to `~/.vayu/cache/bccache/`.
4. Allow backlog to subside partially into moderate range (12–32 items).
5. Observe worker threads re-decoding `Ultrafast` textures via OpenJPEG to encode them at `Fast` (1).
6. Allow backlog to drain completely (< 12 items).
7. Observe worker threads re-decoding `Fast` textures via OpenJPEG a third time to encode them at `Basic` (2) or `Slow` (3).
8. Verify via file inspection that a coarse discard decode at preset 2 can overwrite an existing discard 0 file on disk if the discard 0 file was written at preset 1.

## Suspected Code Paths

- [`indra/llimage/vayuimageblockcompressor.cpp:337-347`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayuimageblockcompressor.cpp#L337-L347):
  [`VayuImageBlockCompressor::getEffectivePreset()`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayuimageblockcompressor.cpp#L337) introduces multiple intermediate fallback tiers (`Ultrafast` and `Fast`) based on backlog thresholds (12 and 32 items).
- [`indra/llimage/llimageworker.cpp:217-219`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/llimageworker.cpp#L217-L219):
  Passes `min_preset = (U8)VayuImageBlockCompressor::getEffectivePreset()` into `readEntry()`, coupling instantaneous queue load directly to cache hit validity.
- [`indra/newview/lltexturefetch.cpp:1155-1157`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/lltexturefetch.cpp#L1155-L1157):
  Queries `readEntry()` using `getEffectivePreset()`, repeating the same premature rejection during texture fetching.
- [`indra/llimage/vayubctexturecache.cpp:524-525, 601-604`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayubctexturecache.cpp#L601-L604):
  `readEntry()` rejects pending and on-disk entries when `mPreset < min_preset`.
- [`indra/llimage/vayubctexturecache.cpp:680-684, 690-694, 707-711, 727-731`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayubctexturecache.cpp#L707-L711):
  `writeEntry()` overwrite guard logic incorrectly conflates resolution preservation with preset comparison, permitting lower-resolution writes to overwrite higher-resolution cache files.

## Root Cause Analysis

Three distinct flaws combine to create severe cache thrashing and data degradation:

1. **Resolution Inversion in Overwrite Guard (Critical Bug)**:
   Spatial resolution (discard level: 0 = highest, 5 = lowest) and compression preset are orthogonal dimensions. A lower-resolution image (`incoming.mDiscardLevel > existing.mDiscardLevel`) must **never** overwrite a higher-resolution image, even if the incoming image was encoded at a higher quality preset. The existing logic permitted coarse mips to destroy full-resolution cached textures.

2. **Multi-Tier Fallback Ladder**:
   Stepping between multiple intermediate presets (`Slow` -> `Fast` -> `Ultrafast`) creates multiple opportunities for re-decode thrashing. When an asset is encoded at `Ultrafast`, the system attempts to upgrade it first to `Fast`, and then to `Slow`/`Basic`. Because worker queues are constantly fluctuating during gameplay, intermediate presets multiply the number of decode/encode passes rather than stabilizing the pipeline.

3. **Treating Quality Upgrades as Synchronous Cache Misses**:
   When `readEntry()` rejects a cached file due to `mPreset < min_preset`, it treats quality difference as a hard cache miss on the critical rendering path. Instead of presenting the existing texture to the renderer and avoiding hitches, the viewer halts rendering and immediately fires a high-priority OpenJPEG software decode. When many textures do this simultaneously, the resulting backlog re-triggers dynamic downgrades, trapping the engine in a continuous re-encode cycle.

### Quality Preset Context

Quality presets serve a concrete visual purpose and cannot be dismissed as indistinguishable:
- **`Ultrafast`** restricts BC7 to Mode 6 only (1 subset, 4-bit endpoints) with no partition search. On large objects, avatars, smooth gradients, and normal maps, Mode 6 produces visible block boundary stepping, banding, and color quantization.
- **`Fast`** tests only 16 partitions and modes 1 & 6.
- **`Basic` and `Slow`** search all 64 partitions and additional modes (modes 0, 1, 6, 7), preserving fine surface gradients and high-frequency details.

Users configure `Basic` or `Slow` specifically to avoid these compression artifacts. A valid solution must respect the user's quality preference without introducing oscillatory thrashing loops.

- **Confidence**: High (verified in code and backed by performance analysis).

## Proposed Remediation

### 1. Fix Overwrite Protection in `writeEntry()` (Mandatory)
Decouple spatial resolution preservation from preset comparisons:
- **Invariant**: If `existing.mDiscardLevel < local_header.mDiscardLevel`, **never overwrite** (existing file has strictly higher resolution).
- If resolutions are equal (`existing.mDiscardLevel == local_header.mDiscardLevel`): only overwrite if `local_header.mPreset > existing.mPreset` (quality upgrade at identical resolution).

### 2. Streamline / Eliminate Dynamic Presets
To eliminate multi-pass thrashing:
- **Preferred Option: Eliminate Dynamic Preset Downgrade**:
  - Remove dynamic backlog-based preset adjustment. Always encode at the user's configured preset (`getPreset()`).
  - *Rationale*: With ISPC `bc7e`, block compression takes ~10–15 ms per 1024x1024 texture. In contrast, OpenJPEG software decompression takes 50–100 ms. Dynamically dropping from `Basic` to `Ultrafast` saves only ~7 ms out of a ~75 ms pipeline (<10% total frame decode time), but introduces visible visual artifacts, complex queue interactions, and massive CPU waste if re-encodes are ever attempted.
  - Encoding once at the target preset guarantees every on-disk entry is final, eliminating all multi-tier re-encodes, intermediate cache states, and thrashing at the source.
- **Alternative Option: Binary Fallback with No Intermediate Stepping**:
  - If load-shedding is retained, collapse the ladder into a single binary fallback (e.g. target preset vs. single fast fallback), completely removing the multi-tier `Ultrafast` -> `Fast` -> `Target` progression.
  - Decouple cache reads on the render path: `readEntry()` must serve available on-disk textures immediately (`min_preset = 0`) to prevent frame hitches. Any quality upgrade must occur strictly as an asynchronous low-priority background task when queues are completely idle.

### 3. Decouple `readEntry()` from Instantaneous Backlog
- Calls to `readEntry()` in [`llimageworker.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/llimageworker.cpp#L217) and [`lltexturefetch.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/lltexturefetch.cpp#L1155) must pass `min_preset = 0` (or rely on a default `min_preset = 0`).
- Serving existing on-disk compressed textures prevents synchronous decode stalls and queue spikes.

**Files likely to change**:
- [`indra/llimage/vayubctexturecache.h`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayubctexturecache.h)
- [`indra/llimage/vayubctexturecache.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayubctexturecache.cpp)
- [`indra/llimage/vayuimageblockcompressor.h`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayuimageblockcompressor.h)
- [`indra/llimage/vayuimageblockcompressor.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/vayuimageblockcompressor.cpp)
- [`indra/llimage/llimageworker.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/llimageworker.cpp)
- [`indra/newview/lltexturefetch.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/newview/lltexturefetch.cpp)
- [`indra/llimage/tests/vayubctexturecache_test.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/tests/vayubctexturecache_test.cpp)
- [`indra/llimage/tests/vayuimageblockcompressor_test.cpp`](file:///home/roger/Projects/Vayu-Viewer/indra/llimage/tests/vayuimageblockcompressor_test.cpp)

**Tests to add or update**:
- In `vayubctexturecache_test.cpp`:
  - Verify that a coarse discard write (e.g. discard 2) with a higher preset does not overwrite a full-resolution (discard 0) file with a lower preset.
  - Verify that `writeEntry()` with identical discard levels and higher preset successfully upgrades the entry.
  - Verify that `readEntry()` serves cached textures without failing on preset mismatches.
- In `vayuimageblockcompressor_test.cpp`:
  - Update or remove backlog downgrade assertions to match the streamlined preset behavior.

## Risks & Considerations

- **Decode Latency under Extreme Bursts**: If dynamic preset downgrade is removed, textures during massive teleport spikes will encode at `Basic`/`Slow`. However, because OpenJPEG decode dominates total pipeline time, the impact on overall queue drain time is small (~10%), while completely eliminating CPU-destroying re-encode loops.
- **Visual Consistency**: Every texture written to cache will reliably match the user's configured visual fidelity setting.

## Open Questions

- Confirmation on preferred preset model: completely eliminate dynamic preset downgrade (encode once at configured preset) vs. binary fallback with asynchronous idle upgrades.
