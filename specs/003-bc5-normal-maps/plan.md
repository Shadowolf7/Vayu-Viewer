# Implementation Plan: BC5 Normal Map Encoding

**Branch**: `003-bc5-normal-maps` | **Date**: 2026-09-22 | **Spec**: [spec.md](../spec.md)

**Input**: Feature specification from `/specs/003-bc5-normal-maps/spec.md`

## Summary

PBR surfaces currently render golf-ball dimples because normal maps are denatured like diffuse color textures: routed through the general color path, gamma-transformed (sRGB), and blocky at Fast sub-mips. This feature routes textures bound to a normal slot into a dedicated `EVayuTextureJob::Normal` pipeline that:

1. Reduces 3/4-channel normal data to two channels (R,G = tangent-space X,Y) and encodes them as **BC5 linear** (no sRGB gamma, no quality staging — BC5's encoder is closed-form optimal at any level). Legacy (non-glTF) normals take this path only when alpha is exact-255 (trivial alpha → bit-identical under BC5); informative-alpha legacy normals stay on the color path to preserve per-texel gloss (FR-003a).
2. Carries the normal role from material bind → fetch request → worker → compressor (job propagation, previously scaffolded and reverted as `c9026b830b`).
3. Teaches normal-sampling shaders to reconstruct the missing B channel: `n.xy = tex.rg*2-1; n.z = sqrt(max(0, 1-dot(n.xy,n.xy)))`.
4. Keys cache entries by (texture UUID × role), so color (`id.bc`) and normal (`id.bc5`) entries for the same UUID coexist — no cache wipe, no role fights.

## Technical Context

**Language/Version**: C++17 (viewer), OpenGL shaders (GLSL 130 / GLSL 400 conditioned)

**Primary Dependencies**:
- `vayuimageblockcompressor.cpp/h` — block compressor; `EVayuTextureJob`, `EVayuBlockCompressionFormat` (BC5 = GL_COMPRESSED_RG_RGTC2), rgbcx backend.
- `lltexturefetch.cpp/h` — fetch worker; `mTextureJob` member + `createRequest()` job param (already present in reverted scaffold).
- `vayubctexturecache.cpp/h` (indra/llimage) — disk cache; `getFilePath()`, `readEntry()`, `writeEntry()`; `VayuBCCacheEntryHeader` already records `mFormat`.
- `llfetchedgltfmaterial.cpp` — glTF material slot bindings (`fetch_texture`, `mNormalTexture`).
- `llface.cpp` / `llvovolume.cpp` / `lldrawable.cpp` — legacy `setNormalMap()` path.
- `llimageworker.cpp` / `llviewertexture.cpp` — decode-thread job plumbing + `createRequest` caller.
- rgbcx (bc7e/rgbcx.cpp): `encode_bc5` closed-form; `encode_bc4/5_hq` brute-force variants NOT wired (not needed).

**Storage**: Texture cache at `~/.vayu/cache/bccache/` (UUID-partitioned subdirs, `kFormatVersion = 4`). Normal entries get a distinct file extension (`.bc5`) so both roles coexist; header `mFormat` used as read-time guard.

**Testing**:
- `indra/llimage/tests/vayuimageblockcompressor_test.cpp` — probe test<20> pattern (existing on-disk verification convention).
- `scripts/perf/analyze_vayu_dump.py` — decodes VAYU_DUMP_DIR captures bit-exactly (validates BC5 blocks).
- In-world visual before/after on the reproduction vehicle (requires manual build + quiescent machine).
- `lltexturefetch` cache-hit path regression (color entries byte-identical).

**Target Platform**: Linux (dev machine), macOS/iOS guarded (`LL_DARWIN`), graphics-API-agnostic (format resolves at GL upload).

**Project Type**: Desktop 3D viewer (C++/OpenGL), texture-pipeline subsystem.

**Performance Goals**: No encode-time regression on color path (byte-identical output per SC-004); normal path must be at least as fast as today's per-mip slow encode (BC5 closed-form is faster than BC7/BC1-slow). Zero new per-frame GPU cost (BC5 is one native format). The exact-255 alpha verdict reuses the existing SIMD alpha sweep (Default path already scans 4ch alpha for BC1-vs-BC7) — one encode-time pass, nothing per frame, no per-mip format switching (format resolves once per image, `encode()` top, all mips share it, cf. vayuimageblockcompressor.cpp:534/783/851).

**Constraints**:
- **Constitution**: NEVER build/run without explicit instruction; never launch `vayu`/`vayu-bin`; pre-build guard `pgrep -x vayu-bin || pgrep -x vayu`; kFormatVersion stays 4.
- Hybrid mips (sub-mips = Fast) are DELIBERATE for color — must not roll back; BC5 needs no staging because Fast == Slow encodes.
- No in-tree BC7 decoder — probe/analyzer comparisons must use BC5/BC1 only.
- `is_srgb=false` mandatory for BC5 (normals are linear data).

**Scale/Scope**: One new cache file-naming suffix, one role-assertion path through two binding sites, one encoder-format change, ~6 shader touchpoints. Small diff, no data migration.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Gate | Status |
|------|--------|
| I. Strict Launch/Build Prohibitions — no autonomous builds, GUI launches, or pre-build guard gaps | PASS (build/run only on explicit instruction) |
| II. Minimal Diff & Anti-Scope-Creep — changes confined to files in tasks.md, no adjacent refactors | PASS (each FR maps to named files; shaders enumerated, not swept) |
| III. Task Completeness — every tasks.md checkbox verifiable, converge before completion | PASS (tasks will carry per-file assertions) |
| IV. C++ Engineering — no hidden allocs, zero-copy, no declaration shadowing | PASS (compressor only reuses existing 4-byte block buffers; no new heap in hot loops) |
| V. Clear Communication / ripgrep — use rg, explain intent | PASS |

**Complexity Tracking**: No violations to justify. (One table row reserved if shader reconstruction triggers a shared-util addition.)

## Project Structure

### Documentation (this feature)

```text
specs/003-bc5-normal-maps/
├── plan.md              # This file
├── research.md          # Phase 0 — resolved unknowns
├── data-model.md        # Phase 1 — role/cache-key model
├── quickstart.md        # Phase 1 — validation guide
├── contracts/           # Phase 1 — cache-naming + shader-reconstruction contracts
└── tasks.md             # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

```text
indra/llimage/
├── vayuimageblockcompressor.cpp        # Normal job: 3/4ch→2ch → BC5 (#1)
├── vayubctexturecache.cpp/.h           # getFilePath role/key + mFormat read guard (#4)
→ indra/newview/
├── llfetchedgltfmaterial.cpp           # bind-time role assertion (glTF normal slot)
├── lltexturefetch.cpp/.h               # createRequest(job) + worker.mTextureJob (#2)
├── llviewertexture.cpp                 # createRequest caller passes job
├── llface.cpp / lldrawable.cpp         # legacy setNormalMap role assertion
└── app_settings/shaders/class1+2+3/deferred/pbropaqueF, pbralphaF, pbropaqueIndexedF,
    materialIndexedF, materialF, bumpF.glsl + waterF/underWaterF (B-reconstruction, #3)

indra/llimage/tests/vayuimageblockcompressor_test.cpp  # probe coverage (BC5 mips)
scripts/perf/analyze_vayu_dump.py                        # capture analyzer (BC5 awareness)
```

**Structure Decision**: Existing in-tree structure extended at the same layer boundaries that already own each responsibility (compressor, fetch worker, cache, shaders). No new modules — the feature is a role-assertion spine through existing pipelines.

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |