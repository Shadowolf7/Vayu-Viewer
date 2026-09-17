# Contract: VayuImageBlockCompressor Interface

**Component**: `indra/llimage/vayuimageblockcompressor.h` / `vayuimageblockcompressor.cpp`
**Spec**: [spec.md](../spec.md)

## 1. Quality Preset Architecture

```cpp
// Target preset defaults to Slow across all mips.
// User-facing preset setters/getters and backlog tracking functions are eliminated:
// - setPreset() / getPreset() eliminated from public API
// - setQueueBacklog() eliminated
// - getEffectivePreset() eliminated
// Developer debug toggle:
static void setHybridMips(bool hybrid);
static bool getHybridMips();
```

### Guarantees
- Textures default to hybrid mode: Mip 0 is encoded at `EVayuBlockCompressionPreset::Slow` and sub-mips ($i \ge 1$) are encoded at `Fast` for maximum streaming throughput.
- When developer debug setting `RenderCompressTexturesHybridMips` is disabled (`setHybridMips(false)`), all mips ($i \ge 0$) encode at `Slow` for pure maximal visual fidelity benchmarking.
- User-configurable preset setting (`RenderCompressTexturesPreset`) and UI combo box are removed.
- Runtime queue backlog functions (`setQueueBacklog`, `getEffectivePreset`) and dynamic downgrade thresholds (`kModerateBacklogThreshold`, `kHeavyBacklogThreshold`) are completely eliminated.

---

## 2. Encoding & Mip Preset Invariant

```cpp
static bool encode(const LLImageRaw* raw_image,
                   VayuBlockCompressionResult& result,
                   EVayuBlockCompressionFormat format = EVayuBlockCompressionFormat::Auto);
```

### Guarantees
- **Mip 0 (Full Resolution)**: Encoded strictly at `EVayuBlockCompressionPreset::Slow` for maximum visual fidelity and artifact-free textures.
- **Lower Mips ($i \ge 1$ / Coarse Mips)**: Encoded at `Fast` by default, or `Slow` when `getHybridMips()` is false.
- `result.mPreset` is reported as `EVayuBlockCompressionPreset::Slow`.
- Transient coarse previews ($\le 256 \times 256$) encode in microseconds without heap allocation spikes.
