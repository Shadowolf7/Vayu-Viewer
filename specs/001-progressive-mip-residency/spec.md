# Feature Specification: Progressive Mip Residency (Resolution Staging) for Texture Streaming and Block Compression

**Feature Branch**: `001-progressive-mip-residency`

**Created**: 2026-09-16

**Status**: Draft

**Input**: User description: "https://github.com/Shadowolf7/Vayu-Viewer/issues/98"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Instant Smooth Previews During Movement (Priority: P1)

As a virtual world explorer navigating dense regions, flying, or teleporting, I want nearby textures to appear instantly as smooth, low-latency previews without stuttering or UI frame drops, so that I can orient myself immediately and navigate fluidly without waiting for massive full-resolution images to finish processing.

**Why this priority**: Immediate visual responsiveness during navigation is critical for usability. Frame drops, freezes, or missing textures during locomotion severely degrade the user experience and can cause disorientation or navigation collisions.

**Independent Test**: Teleport into a texture-dense region; verify that coarse texture previews display on surrounding objects within milliseconds without causing render thread hitches or frame time spikes.

**Acceptance Scenarios**:

1. **Given** an unvisited region with numerous high-resolution textures, **When** the user teleports in or traverses the environment at high speed, **Then** coarse texture previews display in transient memory within milliseconds without hitching the main render thread.
2. **Given** background decode queues are heavily loaded with incoming scene assets, **When** new coarse texture data arrives, **Then** the viewer processes and presents the coarse preview immediately while deferring the heavy full-resolution pyramid until queue saturation clears.

---

### User Story 2 - Progressive Sharpening with Fixed Maximum Asset Fidelity (Priority: P2)

As a resident inspecting objects, avatar attire, and environmental surfaces, I want textures to progressively sharpen from their initial coarse preview state into crisp, full-resolution fidelity at maximum visual quality (`Slow`), without harsh block compression artifacts, color banding, or blurry mode degradation, and without requiring manual tuning of compression preset knobs.

**Why this priority**: High visual fidelity is central to the virtual world experience. Dynamic quality downgrades previously introduced noticeable compression noise, blockiness, and color distortion that persisted permanently. Textures now default to hybrid mode (Mip 0 `Slow`, sub-mips `Fast`) for optimal streaming throughput and responsiveness, with an optional developer debug setting (`RenderCompressTexturesHybridMips`) available to benchmark against pure `Slow` across all mips, rendering user-facing preferences UI obsolete.

**Independent Test**: Stand still after arriving in a new region; verify that textures progressively sharpen from soft previews to pristine full-resolution surfaces at maximum fidelity, with zero compression artifacts or visible mode switching defects.

**Acceptance Scenarios**:

1. **Given** an object currently rendering a coarse preview texture, **When** background processing headroom becomes available, **Then** the full-resolution texture pyramid is resolved and seamlessly replaces the coarse preview without visual popping or glitching.
2. **Given** the viewer's fixed target texture quality, **When** textures are processed and saved, **Then** full-resolution textures are encoded strictly at maximum quality (`Slow`) and never dynamically downgraded to lower-fidelity or artifact-prone modes due to transient queue backlogs.

---

### User Story 3 - Persistent Cache Reliability and Zero Thrashing (Priority: P3)

As a returning resident revisiting previously loaded locations and assets, I want cached textures to load instantly from disk storage at their highest available fidelity, without re-downloading, redundant multi-pass re-decoding, or cache thrashing.

**Why this priority**: Maximizing cache efficiency minimizes network bandwidth consumption, reduces CPU and disk wear, and accelerates scene loading when revisiting familiar locations.

**Independent Test**: Revisit a previously loaded location after restarting the viewer; verify that cached textures load directly from disk storage at their maximum stored resolution without triggering redundant re-decode ladders or cache misses.

**Acceptance Scenarios**:

1. **Given** a texture previously committed to disk storage at full resolution, **When** the texture is requested during a subsequent scene load, **Then** it is loaded directly from disk cache without quality gating or re-decode passes.
2. **Given** a texture already present on disk at full resolution, **When** an incoming lower-resolution stream or coarse preview is processed in memory, **Then** the lower-resolution data never overwrites or degrades the higher-resolution data on disk.

---

### Edge Cases

- **User teleports away before full resolution resolves**: If the user leaves a region before deferred full-resolution pyramids are processed, transient coarse previews in RAM are cleanly released. Any coarse slice already decoded and written to disk cache is preserved as an upgradable entry, preventing redundant J2C decompressions during future visits.
- **Coarse mip requested for an asset already fully cached**: When a low-resolution mip is needed for distant geometry but the full-resolution pyramid already exists in cache, the system slices the required sub-resolution directly from the cached asset without re-decoding.
- **Extreme queue saturation under mass avatar arrivals**: When hundreds of avatars or complex meshes enter view simultaneously, coarse mips continue to receive top scheduling priority to maintain visual awareness, while full-resolution pyramids are prioritized strictly by spatial relevance (proximity and camera frustum).
- **Encountering legacy on-disk cache entries**: Previously cached entries generated under legacy versions (format version < 3) or older conventions are automatically invalidated and purged from disk on startup when `VayuBCTextureCacheVersion != kFormatVersion` (where `kFormatVersion == 3`), preventing orphaned disk bloat and obsolete preset artifacts.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST decouple runtime worker queue load management from asset compression fidelity, eliminating dynamic preset downgrades and standardizing full-resolution encoding to `Slow`.
- **FR-002**: The system MUST process coarse texture mip levels (256x256 down to 1x1) with minimal latency in transient memory to provide immediate visual feedback.
- **FR-003**: The system MUST defer heavy full-resolution texture pyramid processing during worker queue saturation until worker capacity falls below saturation thresholds.
- **FR-004**: The system MUST persist texture data to disk cache storage as upgradable entries (coarse partials and full pyramids), allowing finer resolution decodes to upgrade coarser ones while strictly rejecting coarser overwrites of finer data.
- **FR-005**: The system MUST enforce a strict resolution invariant on disk cache storage such that a lower-resolution mip never overwrites an existing higher-resolution entry.
- **FR-006**: The system MUST eliminate quality-preset gating during disk cache reads, ensuring that valid on-disk cached textures hit immediately.
- **FR-007**: The system MUST satisfy coarse mip requests directly by slicing from an existing higher-resolution cached pyramid without performing duplicate decompressions.
- **FR-008**: The system MUST eliminate multi-pass decode ladders (such as sequential decompression of the same texture across multiple quality levels).
- **FR-009**: The system MUST encode lower mips in the mip chain with the `Fast` preset by default to maximize streaming throughput while reserving maximum fidelity (`Slow`) for Mip 0, while supporting an optional debug setting (`RenderCompressTexturesHybridMips`) for developers to benchmark against pure `Slow` encoding.
- **FR-010**: The system MUST remove user-facing compression preset configuration controls from Preferences UI, standardizing on hybrid `Slow` target encoding with pure `Slow` mode gated strictly behind a developer debug setting.
- **FR-011**: The system MUST automatically invalidate and purge legacy on-disk block compression cache entries upon upgrade when the saved cache version does not match `VayuBCTextureCache::kFormatVersion` (where `kFormatVersion == 3`), aligning version tracking variables to prevent stale or orphaned files.

### Key Entities

- **Texture Asset**: A distinct visual asset identified by a unique UUID, consisting of a sequence of mip levels spanning from 1x1 up to full source dimensions.
- **Coarse Mip Slice**: A low-resolution mip subset (256x256 and smaller) representing a small fraction of total block data, decoded and held in transient memory for zero-latency previews.
- **Full-Resolution Texture Pyramid**: The complete set of all mip levels from native resolution down to 1x1, fully compressed at maximum fidelity (`Slow`) for final rendering and persistent disk storage.
- **Persistent Disk Cache Entry**: The on-disk cached asset file keyed by UUID (`<uuid>.bc`), storing the highest resolved mip pyramid (coarse partial or full resolution) with discard upgrade semantics.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Coarse texture previews are decoded and rendered within 50 milliseconds of packet arrival during region teleports and rapid camera movement.
- **SC-002**: Eliminates 100% of dynamic compression preset downgrades and low-quality compression mode artifacts in persistent disk cache files.
- **SC-003**: Eliminates 100% of redundant multi-pass re-decode cycles (previously up to 3 decode passes per texture under load).
- **SC-004**: Repeated texture requests for cached assets achieve a 0ms decode penalty by reading directly from on-disk cache without preset rejections.
- **SC-005**: Viewer frame time stability improves during rapid traversal, eliminating main-thread freezes and worker thread starvation caused by oscillatory cache thrashing.
- **SC-006**: User interface and configuration footprint is simplified by completely removing redundant texture compression preset controls.

## Assumptions

- Textures default to hybrid mode (Mip 0 `Slow`, sub-mips `Fast`) for maximum streaming throughput without perceptual quality loss, with an optional debug toggle (`RenderCompressTexturesHybridMips`) available for comparative testing.
- Available system RAM is sufficient to buffer active transient coarse mip previews during streaming.
- Spatial relevance (frustum culling, distance, and visual size) determines the priority order in which deferred full-resolution pyramids are scheduled for completion.
- Coarse previews persisted to disk cache follow strict discard upgrade rules, preventing degradation of finer cached assets while saving redundant decodes.
