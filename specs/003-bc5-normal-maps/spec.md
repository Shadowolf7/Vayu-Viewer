# Feature Specification: BC5 Normal Map Encoding (Fix Golf-Ball Dimples)

**Feature Branch**: `003-bc5-normal-maps`

**Created**: 2026-09-22

**Status**: Draft

**Input**: User description: "Fix golf-ball dimples on PBR surfaces — PBR normal maps are being compressed like diffuse color textures (lossy BC1 + sRGB gamma), producing block-aligned dimple artifacts on high-repeat faces. Route normal maps to the specialized two-channel BC5 path so they encode at full quality on every mip level, and teach the renderer's normal-sampling shaders to consume that representation."

## Clarifications

### Session 2026-09-22
- Q: Should normal-role disk cache files use the .bc5 file extension or a role suffix like _normal.bc? (FR-007) → A: Use `<uuid>.bc5` as the dedicated file extension for BC5 normal textures; each role stores its full reverse-ordered mip chain in a single file starting with the coarsest mip at offset 0.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - PBR materials render smooth surfaces (Priority: P1)

A builder places a vehicle with a tiled PBR normal map in-world. The painted panels display clean, uniform shading with smooth specular highlights, with no grid of small dark dots on any surface regardless of how densely the texture tiles.

**Why this priority**: This is the reported defect. High-repeat faces sample deep sub-mip levels of the compressed normal map; today those levels are encoded lossily and with an sRGB gamma transform, so the reconstructed normals are biased and the surface reads as dimpled. Fixing this is the whole point of the feature.

**Independent Test**: On a known-problematic object (the vehicle used to reproduce the bug), check all painted body panels at multiple camera distances. Every panel must be smooth — no repeating dot pattern — even when the normal map tiles at high density.

**Acceptance Scenarios**:

1. **Given** a PBR material whose normal map tiles at high density across a large surface, **When** the surface is viewed at close and mid range, **Then** no block-aligned dimple/dot grid appears on the shaded surface.
2. **Given** a PBR material with strong normal detail (rivets, panel seams), **When** the surface is rendered, **Then** that detail is visible and correct rather than washed out or inverted.
3. **Given** a normal map with steep/edge-case normals, **When** rendered, **Then** surfaces facing away from the camera still shade plausibly (no mirrored/black artifacts).

---

### User Story 2 - Legacy (non-glTF) materials are fixed too (Priority: P1)

A builder's older meshes use the classic material system (not glTF) but still carry normal maps. Those surfaces behave exactly like the glTF case: smooth, correct shading, no dimples.

**Why this priority**: The reproduction vehicle may use either material path. If only the glTF path is fixed, legacy materials still exhibit the bug, so the fix must cover both routes to the material slot.

**Independent Test**: Open an object that uses a legacy material with a normal map and confirm smooth shading identical to the glTF case.

**Acceptance Scenarios**:

1. **Given** a legacy (non-glTF) material with a normal map, **When** the surface is rendered with dense tiling, **Then** no dimple grid appears.

---

### User Story 3 - No regressions on non-normal textures (Priority: P2)

Diffuse/albedo textures, metallic-roughness maps, and other non-normal textures continue to compress and render exactly as before. Roughness must not be mistaken for a normal map, and standard albedo textures must not gain reconstruction artifacts.

**Why this priority**: The normal-detection step must not over-match. Erroneously treating a roughness or albedo map as a normal map would visibly corrupt materials — a worse regression than the bug being fixed.

**Independent Test**: View a scene with mixed materials (glTF and legacy) including glossy metal, matte cloth, and painted surfaces. All must render correctly with no new artifacts.

**Acceptance Scenarios**:

1. **Given** a material with a metallic-roughness map alongside its normal map, **When** rendered, **Then** the roughness/metallic response is unchanged and the surface is dimple-free only where the actual normal map applies.
2. **Given** a diffuse-only material (no normal map), **When** rendered, **Then** its appearance is unchanged byte-for-byte.

---

### Edge Cases

- **All-flat normal maps**: a normal map with zero variation (pure 0x7F/0x7F/0xFF) must compress and render as perfectly flat (no gradient artifact).
- **Non-square or odd-sized normal textures**: mip chains must not alias or produce artifacts at partial 4x4 blocks along edges.
- **Downward-facing normals**: the two-channel representation reconstructs the third channel from the first two; texels whose original normal pointed mostly downward will be mirrored to the positive hemisphere (accepted trade-off, see Assumptions) and must render as a smooth, plausible surface — not black or inverted.
- **Mixed material systems**: the same model may bind a glTF material to some faces and legacy to others; both paths must behave consistently.
- **Same UUID in two roles**: one asset bound as a normal on one material and as a diffuse/color on another. On disk, each role keeps its own cache entry (keyed by role/format as `<uuid>.bc` and `<uuid>.bc5` per FR-007), so a texture first cached as diffuse and later reused as a normal compresses fresh for the normal role — no re-encode ping-pong between roles, no cache wipe. If the same texture is bound to both roles at once and actively rendered simultaneously, the single GL texture object can only hold one format at a time (last-role-wins on the GPU, deterministic); this is the rare degenerate case and the rendering stays plausible. Cache size cost is one extra file only for assets that genuinely share roles.
- **Non-normal texture bound to a normal slot** (a "weird texture used as a normal map"): treated as a normal map because the slot says so; the author sees an obviously-broken surface. This is a content-authoring issue, not a detection failure — content-based guessing is explicitly out of scope.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST identify normal maps at the material level, separately for the modern (glTF) material system and the legacy material system, AT the moment the material binds its texture slot (before the fetch/compress request is created for that texture).
- **FR-001a**: The role identified in FR-001 MUST be recorded in the fetch request itself and survive to the point where compression format is decided — a texture may not be left to fall back to the general color path merely because it was bound late or refetched.
- **FR-002**: The system MUST route identified normal maps to the specialized two-channel compression path rather than the general color path, on every mip level.
- **FR-003**: The specialized path MUST store a two-channel representation of the surface direction — one channel per in-plane axis — without the third (out-of-plane) axis and WITHOUT any color-space gamma transform.
- **FR-003a**: A legacy (non-glTF) normal map qualifies for FR-003 only when its alpha channel is exact-255 on every texel (making alpha redundant: `255/255 = 1.0`, byte-identical to BC5 sampling). A legacy normal map with informative alpha (any texel < 255) MUST keep its alpha — it routes through the general color path with alpha preserved, so per-texel gloss modulation in the legacy material shader is unchanged. The alpha verdict is computed ONCE at encode time by a single SIMD sweep (the same scan the existing color path already runs for 4-channel data); 3-channel legacy normals need no scan (no alpha channel → always samples `1.0`). Format is resolved once per image before compression — never switched mid-stream.
- **FR-004**: The system MUST keep compressing all non-normal textures (albedo, metallic-roughness, legacy color maps) exactly as today, including the existing per-mip quality staging for color data.
- **FR-005**: The renderer MUST reconstruct the full surface direction from the two stored channels when sampling a normal map, such that shading, lighting, and specular response are continuous across 4x4 block boundaries.
- **FR-006**: The compression pipeline MUST detect and handle textures whose channels do not match the expected count for a normal map (e.g., 4-channel normal data) by reducing them to the two-channel form losslessly-within-format-limits.
- **FR-007**: The texture cache MUST key each stored entry by (texture identity × compression format/role), so a color-path entry (`<uuid>.bc`) and a normal-path entry (`<uuid>.bc5`) for the same texture coexist as separate cache files instead of overwriting each other. Each role stores its full reverse-ordered mip chain in a single file starting with the coarsest mip at offset 0. On read, the system MUST verify the entry's stored format matches the role of the requesting fetch job and treat a mismatch as a cache miss (do not apply).

### Key Entities

- **Normal map**: a texture whose per-texel values encode a local surface orientation; the input to the specialized path.
- **Material texture slot**: the binding point in a material (modern or legacy) that declares a texture's role; the signal used to identify normal maps.
- **Block-compressed normal texture**: the two-channel encoded output stored in the texture cache.
- **Surface direction (reconstructed)**: the per-texel orientation produced at render time by decoding the stored representation.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On the reproduction vehicle, no dimple/dot grid is visible on any PBR surface at any camera distance — verified visually and by comparing captured frames before/after.
- **SC-002**: Normal-mapped surfaces show correct detail orientation (no inverted or mirrored normal imagery) on both modern and legacy material systems.
- **SC-003**: Compression probe tests for the two-channel path pass on known input patterns, confirming bit-correct block encoding per the format spec (probe test coverage).
- **SC-004**: Textures compressed via the general color path (albedo, roughness) produce byte-identical cache output to today's compressor for identical inputs at the same preset — no collateral change.
- **SC-005**: No new visual artifacts (other than the documented positive-hemisphere mirroring trade-off) appear on normal-mapped surfaces in the test scene.

## Assumptions

- **Tangent-space normal convention**: normal maps are assumed to use the standard tangent-space layout where most texels point outward (positive out-of-plane axis). Reconstructing the third axis as always-positive is the accepted industry trade-off (used by DXT5nm/BC5 everywhere); texels with negative out-of-plane components are rare and will render as their mirrored direction.
- **Alpha policy (decided)**: exact-255 alpha is rendered redundant by BC5 (both sides read `1.0`), so exact-255 legacy normal maps are bit-identical under BC5 and safe to convert. Informative-alpha legacy normal maps cannot be BC5 (alpha loss unacceptable) — they stay on the color path and continue to modulate gloss per-texel via `glossiness *= vNt.a`.
- **BC5 format validity on target hardware**: all target GPUs support decoding the two-channel format natively (universally supported on the same class of hardware that already decodes BC1/BC3/BC4).
- **Performance**: the two-channel path does not need quality staging — its baseline encode is already full-quality and fast, so the existing "high quality on level 0, faster on sub-levels" rule applies only to color data.
- **Detection scope**: only textures explicitly bound to a normal slot are treated as normal maps; no content-based or filename-based guessing. Role is knowable at fetch time because the fetch request is created by the material's slot binding (in-world resolution), not by an unbounded background scan.
- **In-world verification requires a manual build/session**: the build machine must be free (no viewer running) and a screenshotted in-world before/after comparison is part of the test plan.
- **Cache format version and color entries unchanged**: `kFormatVersion` stays 4. Color-path files keep their existing name/extension (`<uuid>.bc`) and byte-identical encoding; normal-path entries use the `.bc5` file extension (`<uuid>.bc5`) so both coexist without a format bump. Existing cached `.bc` files remain valid for color roles; a texture later bound as normal simply misses the normal-path key and is compressed fresh under its own extension. No cache wipe required.