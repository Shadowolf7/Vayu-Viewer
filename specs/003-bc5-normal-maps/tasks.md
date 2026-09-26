# Implementation Tasks: BC5 Normal Map Encoding (Fix Golf-Ball Dimples)

**Feature**: BC5 Normal Map Encoding  
**Branch**: `003-bc5-normal-maps` | **Date**: 2026-09-22  
**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Baseline audit of existing texture job enums, reverted scaffold commit `c9026b830b`, normal-map bind sites, and shader touchpoints.

- [X] T001 Audit reverted scaffold `c9026b830b` against current `indra/newview/lltexturefetch.cpp`, `indra/newview/lltexturefetch.h`, and `indra/newview/llviewertexture.cpp` to verify where `createRequest` and `mTextureJob` plumbing previously attached
- [X] T002 [P] Audit normal-map shader sampling sites across `indra/newview/app_settings/shaders/class1/deferred/pbropaqueF.glsl`, `indra/newview/app_settings/shaders/class2/deferred/pbralphaF.glsl`, `indra/newview/app_settings/shaders/class1/deferred/pbropaqueIndexedF.glsl`, `indra/newview/app_settings/shaders/class3/deferred/materialF.glsl`, `indra/newview/app_settings/shaders/class1/deferred/materialIndexedF.glsl`, and `indra/newview/app_settings/shaders/class1/deferred/bumpF.glsl`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core cache key qualification, dual-probe reading, and compressor BC5/Normal-job format resolution that all user stories depend on.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [X] T003 Update `VayuBCTextureCache::getFilePath` overloads in `indra/llimage/vayubctexturecache.h` and `indra/llimage/vayubctexturecache.cpp` to accept an optional file extension / role suffix (`.bc5` for BC5 normal role, default `.bc` for color) without bumping `kFormatVersion = 4` (FR-007, cache-role-keying contract)
- [X] T004 Update `VayuBCTextureCache::readEntry` in `indra/llimage/vayubctexturecache.h` and `indra/llimage/vayubctexturecache.cpp` to accept an expected format / role parameter and validate header `mFormat` against the requested role (FR-007)
- [X] T005 Update `VayuBCTextureCache::writeEntry` in `indra/llimage/vayubctexturecache.h` and `indra/llimage/vayubctexturecache.cpp` to write to the format-qualified path (`.bc5` for `mFormat == BC5`, `.bc` otherwise)
- [X] T006 Update `VayuImageBlockCompressor::encode` in `indra/llimage/vayuimageblockcompressor.cpp` for `EVayuTextureJob::Normal`: route 3/4-channel components to `EVayuBlockCompressionFormat::BC5` with `is_srgb = false` (replacing the previous BC7/BC3 mapping at lines 575-596), set `result.mComponents = 2`, and apply 2-channel independent downsampling (`downsample_half_2ch` rather than 16-bit packed scaling) when building mip pyramids
- [X] T007 [P] Add unit test in `indra/llimage/tests/vayuimageblockcompressor_test.cpp` verifying that 3-channel and 4-channel inputs passed to `VayuImageBlockCompressor::encode` with `EVayuTextureJob::Normal` resolve to `EVayuBlockCompressionFormat::BC5` with `is_srgb = false` and 2 components

**Checkpoint**: Foundation ready — compressor resolves BC5 for normal jobs, and cache supports role-qualified `.bc5` file paths.

---

## Phase 3: User Story 1 - PBR materials render smooth surfaces (Priority: P1) 🎯 MVP

**Goal**: PBR materials binding normal maps in glTF slots (`mNormalTexture`) assert the `Normal` role at bind time, propagate it through the fetch request and worker to the compressor, write `.bc5` cache entries, and sample with B-reconstruction in PBR shaders to eliminate golf-ball dimples on high-repeat faces.

**Independent Test**: Load a vehicle or object with a tiled PBR normal map; inspect cache for `<uuid>.bc5` entries; verify in-world or via frame capture that body panels shade smoothly with no repeating dot/dimple grid at any distance (SC-001, SC-002).

- [X] T008 [US1] Assert `EVayuTextureJob::Normal` at glTF normal slot bind time in `indra/newview/llfetchedgltfmaterial.cpp` when setting `mNormalTexture = fetch_texture(new_id, ...)`
- [X] T009 [US1] Pass `EVayuTextureJob::Normal` from `LLViewerTexture` into `LLTextureFetch::createRequest` in `indra/newview/llviewertexture.cpp`
- [X] T010 [US1] Update `LLTextureFetch::createRequest` signature and implementation in `indra/newview/lltexturefetch.h` and `indra/newview/lltexturefetch.cpp` to accept `EVayuTextureJob job` and store it in worker `mTextureJob`
- [X] T011 [US1] Propagate `mTextureJob` from `LLTextureFetchWorker` into `LLImageRaw::setTextureJob` in `indra/llimage/llimageworker.cpp` prior to calling `VayuImageBlockCompressor::encode`
- [X] T012 [US1] Pass resolved format / role from `LLTextureFetchWorker` into `VayuBCTextureCache::writeEntry` in `indra/newview/lltexturefetch.cpp` and `indra/llimage/llimageworker.cpp` so BC5 normal entries are written as `<uuid>.bc5`
- [X] T013 [US1] Implement dual-probe cache hit lookup in `LLTextureFetchWorker::doWork` (`LOAD_FROM_TEXTURE_CACHE`) in `indra/newview/lltexturefetch.cpp`: for `mTextureJob == Normal`, probe `<uuid>.bc5` first; if present, validate `mFormat == BC5` and serve directly without decode (FR-007)
- [X] T014 [P] [US1] Implement shader B-reconstruction `vNt.z = sqrt(max(0.0, 1.0 - dot(vNt.xy, vNt.xy)))` in `indra/newview/app_settings/shaders/class1/deferred/pbropaqueF.glsl` at line 93
- [X] T015 [P] [US1] Implement shader B-reconstruction `vNt.z = sqrt(max(0.0, 1.0 - dot(vNt.xy, vNt.xy)))` in `indra/newview/app_settings/shaders/class2/deferred/pbralphaF.glsl` at line 179
- [X] T016 [P] [US1] Implement shader B-reconstruction in `indra/newview/app_settings/shaders/class1/deferred/pbropaqueIndexedF.glsl` around line 220 at `sample_normal` decode site

**Checkpoint**: User Story 1 (MVP) complete. PBR normal maps encode as BC5 linear, persist under `.bc5`, reconstruct B in PBR shaders, and render dimple-free.

---

## Phase 4: User Story 2 - Legacy (non-glTF) materials are fixed too (Priority: P1)

**Goal**: Legacy material normal maps assert the `Normal` role at `setNormalMap()`, classify alpha at encode time via the existing SIMD sweep (exact-255 → BC5 linear; informative alpha < 255 → color fallback preserving gloss), and reconstruct B in legacy shaders while leaving `glossiness *= vNt.a` untouched.

**Independent Test**: View an object using legacy bump/normal mapping with an exact-255 alpha normal map; verify smooth shading with no dimples. View an object with a gloss-modulating legacy normal map; verify per-texel gloss variation remains intact.

- [X] T017 [US2] Assert `EVayuTextureJob::Normal` at legacy normal map bind time in `indra/newview/llface.cpp`, `indra/newview/llvovolume.cpp`, and `indra/newview/lldrawable.cpp` when calling `setNormalMap()`
- [X] T018 [US2] Implement encode-time alpha classification in `VayuImageBlockCompressor::encode` in `indra/llimage/vayuimageblockcompressor.cpp`: for `job == EVayuTextureJob::Normal` with 4 components, reuse the SIMD alpha scan (lines 634-692); if `min_a == 255`, resolve to `BC5` (`is_srgb = false`); if `min_a < 255`, fall back to color path (`BC7`/`BC3`, `is_srgb = true`, alpha preserved) (FR-003a)
- [X] T019 [US2] Update `LLTextureFetchWorker` in `indra/newview/lltexturefetch.cpp` so that when a Normal job resolves to a color format (informative alpha fallback), it writes the cache entry under `<uuid>.bc` rather than `<uuid>.bc5`
- [X] T020 [US2] Extend the dual-probe cache hit lookup in `LLTextureFetchWorker::doWork` in `indra/newview/lltexturefetch.cpp`: for `mTextureJob == Normal`, if `<uuid>.bc5` misses, probe `<uuid>.bc` and accept it if header `mFormat` is an expected color format (informative-alpha fallback hit)
- [X] T021 [P] [US2] Implement unconditional shader B-reconstruction while preserving `raw.a` gloss read (`vec4 raw = texture(bumpMap, uv); vec3 vNt = raw.rgb * 2.0 - 1.0; vNt.z = sqrt(max(0.0, 1.0 - dot(vNt.xy, vNt.xy))); glossiness *= raw.a;`) in `indra/newview/app_settings/shaders/class3/deferred/materialF.glsl` at lines 149-151
- [X] T022 [P] [US2] Implement unconditional shader B-reconstruction while preserving `raw.a` gloss read in `indra/newview/app_settings/shaders/class1/deferred/materialIndexedF.glsl` at lines 228-231
- [X] T023 [P] [US2] Implement shader B-reconstruction in legacy bump shader in `indra/newview/app_settings/shaders/class1/deferred/bumpF.glsl` at line 58

**Checkpoint**: User Story 2 complete. Legacy exact-255 normal maps render dimple-free under BC5; informative-alpha legacy normals preserve per-texel gloss; legacy shaders reconstruct B unconditionally.

---

## Phase 5: User Story 3 - No regressions on non-normal textures & Cache Key Coexistence (Priority: P2)

**Goal**: Diffuse/albedo, metallic-roughness, and non-normal textures remain 100% byte-identical (SC-004). If the same UUID binds as both color and normal, both cache files (`<uuid>.bc` and `<uuid>.bc5`) coexist on disk without mutual destruction. Water shaders remain untouched.

**Independent Test**: Run unit test verifying two-role cache coexistence; compare dump captures of control diffuse textures before/after change; verify water normal rendering is unaffected.

- [X] T024 [P] [US3] Add unit test in `indra/llimage/tests/vayubctexturecache_test.cpp` verifying that a single UUID written as both color (`.bc`) and normal (`.bc5`) coexists on disk, neither overwrites the other, and each reads back with matching format (FR-007)
- [X] T025 [P] [US3] Add unit test in `indra/llimage/tests/vayuimageblockcompressor_test.cpp` verifying that `EVayuTextureJob::Default`, `Albedo`, and `MetallicRoughness` produce byte-identical compressed output to today's baseline (SC-004)
- [X] T026 [US3] Update `scripts/perf/analyze_vayu_dump.py` to support decoding and analyzing BC5 cache entries and dump blocks alongside existing BC1/BC7 analyzers
- [X] T027 [US3] Audit water normal shaders in `indra/newview/app_settings/shaders/class3/environment/waterF.glsl` and `indra/newview/app_settings/shaders/class3/environment/underWaterF.glsl` to verify they remain untouched and correctly excluded from material-slot BC5 changes

**Checkpoint**: All user stories complete. Cache coexistence validated, non-normal textures bit-identical, analyzer updated, water shaders unaffected.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Guard check, non-normal shader exclusions audit, diagnostics, and Constitution compliance.

- [X] T028 [P] Run ripgrep audit across `indra/newview/app_settings/shaders/` to ensure no sun/directional normal decoders were accidentally modified (`deferredUtil.glsl`, `dofCombineF.glsl`, `globalF.glsl`, `postDeferredF.glsl`, `screenSpaceReflUtil.glsl`, `shadowUtil.glsl`, `avatarV.glsl`, `normgenV.glsl`)
- [X] T029 [P] Run clangd diagnostics on all modified C++ files: `indra/llimage/vayuimageblockcompressor.h`, `indra/llimage/vayuimageblockcompressor.cpp`, `indra/llimage/vayubctexturecache.h`, `indra/llimage/vayubctexturecache.cpp`, `indra/llimage/llimageworker.cpp`, `indra/newview/llfetchedgltfmaterial.cpp`, `indra/newview/llviewertexture.cpp`, `indra/newview/lltexturefetch.h`, `indra/newview/lltexturefetch.cpp`, `indra/newview/llface.cpp`, `indra/newview/llvovolume.cpp`, `indra/newview/lldrawable.cpp`
- [X] T030 Review git diff against Vayu-Viewer Constitution principles (pre-build guard `pgrep -x vayu-bin || pgrep -x vayu`, minimal diff, zero-copy block buffers, `kFormatVersion = 4` unchanged)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — can start immediately.
- **Foundational (Phase 2)**: Depends on Phase 1 audit — BLOCKS all user stories.
- **User Story 1 (Phase 3)**: Depends on Phase 2 completion. Delivers PBR BC5 compression and rendering (MVP).
- **User Story 2 (Phase 4)**: Depends on Phase 2 and Phase 3 completion (reuses job propagation pipeline from US1).
- **User Story 3 (Phase 5)**: Depends on Phase 2 completion; can run in parallel with US1/US2 verification.
- **Polish (Phase 6)**: Depends on all user stories being complete.

### User Story Dependencies

```
Phase 1: Setup (T001, T002)
   │
   ▼
Phase 2: Foundational (T003 - T007)
   │
   ├──────────────────────────────┬──────────────────────────────┐
   ▼                              ▼                              ▼
Phase 3: US1 - PBR (P1) 🎯     Phase 5: US3 - Regression (P2)   ...
(T008 - T016)                  (T024 - T027)
   │                              │
   ▼                              │
Phase 4: US2 - Legacy (P1)        │
(T017 - T023)                     │
   │                              │
   └──────────────┬───────────────┘
                  ▼
          Phase 6: Polish (T028 - T030)
```

---

## Parallel Execution Opportunities

### Phase 2 (Foundational)
- `T007` [P] (Unit test for compressor format resolution) can be written in parallel with `T003`-`T006`.

### Phase 3 (User Story 1)
- Shader changes `T014`, `T015`, `T016` [P] can be implemented in parallel across the three PBR shader files while C++ pipeline changes `T008`-`T013` are implemented.

### Phase 4 (User Story 2)
- Legacy shader changes `T021`, `T022`, `T023` [P] can be implemented in parallel across `materialF.glsl`, `materialIndexedF.glsl`, and `bumpF.glsl`.

### Phase 5 (User Story 3)
- Cache unit test `T024` [P] and compressor baseline unit test `T025` [P] can execute in parallel.

### Phase 6 (Polish)
- Shader audit `T028` [P] and C++ diagnostics `T029` [P] can execute in parallel.

---

## Implementation Strategy & MVP Scope

- **MVP Scope**: Complete Phase 1, Phase 2, and Phase 3 (Tasks T001–T016). This delivers the complete PBR normal map pipeline (bind tagging → fetch job → BC5 encoding → `.bc5` cache → PBR shader reconstruction), directly resolving the reported golf-ball dimple defect on PBR surfaces.
- **Incremental Delivery**:
  1. Foundational engine ready (T003–T007).
  2. PBR MVP verified (T008–T016).
  3. Legacy support with exact-255 gating & gloss preservation added (T017–T023).
  4. Cache coexistence & non-normal byte-parity validated (T024–T027).
  5. Final safety audit & Constitution compliance check (T028–T030).
