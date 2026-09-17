# Quickstart Validation Guide: Progressive Mip Residency

**Feature**: Progressive Mip Residency (Resolution Staging) for Texture Streaming and Block Compression
**Spec**: [spec.md](spec.md)
**Plan**: [plan.md](plan.md)

## 1. Overview
This guide provides runnable scenarios to validate that Progressive Mip Residency (Resolution Staging):
1. Standardizes texture compression to `Slow` by default across all mips, with an optional developer debug setting (`RenderCompressTexturesHybridMips`) to benchmark hybrid sub-mip `Fast` encoding.
2. Completely eliminates user-facing preset knobs, dynamic preset downgrades, and Mode-6 block artifacts.
3. Correctly slices coarse sub-buffers from `<uuid>.bc` cache files.
4. Protects on-disk cache files from lower-resolution overwrites while permitting finer-resolution upgrades.
5. Smoothly stages texture decoding during high-concurrency queue saturation.

---

## 2. Unit Test Validation Scenarios

### Scenario A: Hybrid Mode & Debug Setting Verification
- **Purpose**: Verify `VayuImageBlockCompressor` maintains `Slow` preset fidelity on Mip 0 and `Fast` on sub-mips by default, supports `setHybridMips(false)` toggle for pure `Slow`, and eliminates backlog degradation.
- **Target Test**: `indra/llimage/tests/vayuimageblockcompressor_test.cpp`
- **Expected Outcome**: Test verifies that hybrid mode is default (`getHybridMips() == true`), toggling `setHybridMips(false)` encodes all mips at `Slow`, and backlog does not trigger dynamic downgrades.

### Scenario B: Unified Cache Slicing & Overwrite Invariant
- **Purpose**: Verify `VayuBCTextureCache` serves coarse slices from full pyramids and rejects lower-resolution overwrites.
- **Target Test**: `indra/llimage/tests/vayubctexturecache_test.cpp`
- **Expected Outcome**:
  1. Full-resolution entry (`discard = 0`) is written to `<uuid>.bc`.
  2. Querying `readEntry(id, 2, ...)` succeeds, returning sub-buffer bytes matching Discard 2 dimensions.
  3. Subsequent `writeEntry(id, 2, ...)` is ignored; on-disk file remains full Discard 0.
  4. Querying `readEntry(id, 0, ...)` succeeds, returning full pyramid.

### Scenario C: Image Worker Pipeline & Upgradable Cache Writes
- **Purpose**: Verify `LLImageDecodeThread` processes coarse mips in RAM and writes upgradable entries to disk cache.
- **Target Test**: `indra/llimage/tests/llimageworker_test.cpp`
- **Expected Outcome**: Coarse decode attaches compressed blocks to `LLImageRaw` and initiates non-blocking cache writes with valid discard levels, which are upgraded when Discard 0 completes.

---

## 3. End-to-End Simulation & Runtime Verification

### Scenario D: High-Concurrency Ingestion & Teleportation
- **Prerequisites**: Build viewer with `safe-build.sh` (when user authorizes).
- **Procedure**:
  1. Teleport into a complex region with >500 textures.
  2. Observe immediate coarse mip rendering in RAM without frame stutter.
  3. Verify via debug logs (`LL_DEBUGS(LOG_TXT)`) that Discard 0 decodes defer during queue saturation and complete at full fidelity once queues normalize.
  4. Inspect `~/.vayu/cache/bccache/` to verify all saved entries are full-resolution (`<uuid>.bc`) encoded at `Slow`.

### Scenario E: UI & Settings Cleanliness
- **Procedure**: Open Preferences > Graphics > Advanced Hardware.
- **Expected Outcome**: Verify that the obsolete `RenderCompressTexturesPreset` combo box and label are removed, with texture compression governed automatically by the engine.
