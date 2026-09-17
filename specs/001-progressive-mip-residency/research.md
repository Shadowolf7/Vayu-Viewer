# Phase 0 Research & Technical Decisions: Progressive Mip Residency

**Feature**: Progressive Mip Residency (Resolution Staging) for Texture Streaming and Block Compression
**Spec**: [spec.md](spec.md)
**Status**: Completed

## 1. Standardized `Slow` Preset & Elimination of User-Settable Preset Knob

### Decision
Completely eliminate both dynamic queue-backlog-based compression preset downgrades (`kModerateBacklogThreshold`, `kHeavyBacklogThreshold`, and `getEffectivePreset()`) and the user-configurable preset knob (`RenderCompressTexturesPreset`, `setPreset()`, and preferences UI combo box). Standardize full-resolution Mip 0 encoding at `EVayuBlockCompressionPreset::Slow` and encode lower mips in the mip chain at coarser presets (`Basic`/`Fast`).

### Rationale
In Issue #96, runtime queue saturation caused `VayuImageBlockCompressor` to downgrade BC7/BC1 encoding to `Fast` or `Ultrafast` (forcing BC7 Mode 6 with no partition search). When the queue cleared, `readEntry()` rejected these low-preset cache entries, causing immediate cache misses and triggering a positive feedback loop of OpenJPEG re-decodes.
Because lower mips in the pyramid are already encoded at coarser presets and represent $< 5\%$ of total blocks, they encode in microseconds in RAM. Heavy Mip 0 pyramids (representing $\sim 75\%$ of encode workload) are deferred during queue saturation. Consequently, full-resolution textures can always be encoded at maximum quality (`Slow`) without stalling worker threads, rendering any user-facing compression preset knob obsolete. Removing the knob eliminates UI clutter and prevents users from inadvertently degrading visual quality.

### Alternatives Considered
- **Retaining user-configurable preset (`Basic` vs `Slow`)**: Rejected because lower mips already encode at coarser presets and resolution staging prevents queue choking. A user preference knob has little to no utility and invites configuration errors.
- **Multi-tier preset caching (`<uuid>_<preset>.bc`)**: Would allow caching downgraded textures alongside full ones. Rejected because it wastes disk space, increases disk I/O contention, and requires multi-pass re-decodes anyway when target quality is reached.
- **Dynamic rate-limiting without resolution staging**: Pausing network requests when queues fill. Rejected because it leaves objects untextured and stalls world loading during locomotion.

---

## 2. Progressive Resolution Staging (Coarse Previews vs. Heavy Mip 0)

### Decision
Decode and compress coarse mips (dimensions $\le 256 \times 256$, representing $< 5\%$ of total blocks) immediately in RAM for zero-latency previews during locomotion. When decode worker queues are saturated (`pending > kSaturationThreshold`), defer the heavy Discard 0 pyramid ($\ge 1024 \times 1024$, representing $\sim 75\%$ of total workload) until worker queue headroom is restored.

### Rationale
A $1024 \times 1024$ texture has 65,536 $4 \times 4$ blocks, whereas a $256 \times 256$ texture has only 4,096 blocks (16x fewer blocks). Encoding coarse mips takes microseconds and allows immediate scene rendering during teleportation or rapid flight.
In `LLTextureFetchWorker`, if a coarse preview is already active (`mDecodedDiscard >= 0`), deferring Discard 0 when worker queues exceed saturation threshold ensures that incoming coarse previews for new objects in view are processed first, preventing visual stalls and hitching.

### Alternatives Considered
- **Downsampling full image on CPU instead of using J2C discard levels**: OpenJPEG natively decodes at requested discard levels at a fraction of the memory and time. Decoding full image and downsampling in CPU would waste OpenJPEG compute.
- **OS-level thread deprioritization**: `LL::ThreadPool` is a lightweight task queue without OS-level thread scheduling priority inversion controls. Deferral within `LLTextureFetchWorker::doWork` state machine is deterministic, zero-overhead, and cooperatively yields CPU to higher-priority coarse jobs.

---

## 3. Persistent Cache Architecture: Unified Asset Pyramids & Invariant Rules

### Decision
1. Persist only completed full-resolution pyramids (`discard == 0` or non-J2C terminal discards) to disk cache as `<uuid>.bc`. Coarse intermediate slices stay in RAM.
2. Slices for coarse requests (`discard > 0`) are satisfied directly from `<uuid>.bc` via sub-buffer offset calculation (`calcSubBufferBytes`) without re-decoding.
3. Remove `min_preset` parameter from `VayuBCTextureCache::readEntry()`. All valid cached entries hit immediately.
4. Enforce the overwrite invariant in `writeEntry()`: a lower-resolution discard level (`mDiscardLevel > existing.mDiscardLevel`) must never overwrite an equal or higher-resolution entry.

### Rationale
- **Zero Cache Thrashing**: Eliminating preset gating guarantees 100% cache hit rates on previously visited assets.
- **Zero Duplicate Storage**: One `<uuid>.bc` file serves both full-resolution and any requested coarser mip levels via sub-buffer slicing.
- **Disk Protection**: Coarse previews decoded in flight do not pollute disk storage with incomplete assets.

### Alternatives Considered
- **Preserving legacy `<uuid>_<discard>.bc` files**: Kept as a read-only fallback for existing caches, but all new writes use `<uuid>.bc`.

---

## 4. Architectural Summary Table

| Component | Previous Architecture | Resolution Staging Architecture |
| :--- | :--- | :--- |
| **Compression Preset Knob** | User-configurable setting (`RenderCompressTexturesPreset`) with UI combo box | Removed entirely; standardized to `Slow` for Mip 0 |
| **Lower Mip Compression** | Encoded at same preset or downgraded dynamically | Encoded at coarser presets (`Basic`/`Fast`) in RAM |
| **Load Management** | Degrades compression fidelity dynamically under backlog | Defers heavy Mip 0 when queues saturated; prioritizes coarse previews |
| **Coarse Previews** | Wrote coarse files to disk; triggered 3-tier re-decode ladder | Kept in RAM; serves immediate preview; resolved once to full fidelity |
| **Cache Lookup** | Gated on `mPreset >= min_preset` (caused false misses) | Unconditional cache hit on matching UUID and resolution |
| **Cache File Layout** | Fragmented `<uuid>_<discard>.bc` files | Unified `<uuid>.bc` full pyramid with sub-buffer slicing |
