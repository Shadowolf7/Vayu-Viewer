# Verification Report: BC5 Normal Map Encoding (Fix Golf-Ball Dimples)

**Date**: 2026-09-23  
**Feature**: BC5 Normal Map Encoding  
**Feature Directory**: `/home/roger/Projects/Vayu-Viewer/specs/003-bc5-normal-maps`  
**Diff Scope**: `all` (base ref: `origin/develop`, HEAD + uncommitted working tree)  
**Total Tasks**: 30  

> ⚠️ **FRESH SESSION ADVISORY**: For maximum reliability, run `/speckit.verify-tasks`
> in a **separate** agent session from the one that performed `/speckit.implement`.
> The implementing agent's context biases it toward confirming its own work.

---

## Summary Scorecard

| Status | Count |
|:---|:---:|
| ✅ VERIFIED | 30 |
| 🔍 PARTIAL | 0 |
| ⚠️ WEAK | 0 |
| ❌ NOT_FOUND | 0 |
| ⏭️ SKIPPED | 0 |

**Verdict**: 100% Verified (0 phantoms, 0 regressions, 0 compile/diagnostic errors).

---

## Flagged Items (0)

No flagged items. All 30 tasks satisfied both mechanical and semantic verification layers.

---

## Verified Items

| Task ID | Verdict | Summary |
|:---|:---|:---|
| T001 | ✅ VERIFIED | Reverted scaffold audit complete across `lltexturefetch` and `llviewertexture`. |
| T002 | ✅ VERIFIED | Audit of 6 deferred/material normal shader sampling sites complete. |
| T003 | ✅ VERIFIED | `VayuBCTextureCache::getFilePath` accepts format extension (`.bc5`/`.bc`), `kFormatVersion = 4` unchanged. |
| T004 | ✅ VERIFIED | `VayuBCTextureCache::readEntry` validates header `mFormat` against requested expected format. |
| T005 | ✅ VERIFIED | `VayuBCTextureCache::writeEntry` routes `kFormatBC5` entries to `.bc5` files and others to `.bc`. |
| T006 | ✅ VERIFIED | Compressor resolves `Normal` jobs to BC5 linear, sets 2 components, applies 2-channel downsampler. |
| T007 | ✅ VERIFIED | Unit test `test<21>` verifies 3-channel and 4-channel Normal encode to BC5 with 2 components. |
| T008 | ✅ VERIFIED | `llfetchedgltfmaterial.cpp` asserts `EVayuTextureJob::Normal` at glTF normal slot bind time. |
| T009 | ✅ VERIFIED | `llviewertexture.cpp` passes `mTextureJob` into `createRequest`. |
| T010 | ✅ VERIFIED | `LLTextureFetch::createRequest` accepts `EVayuTextureJob job` and stores it into `mTextureJob`. |
| T011 | ✅ VERIFIED | `mTextureJob` propagated into `LLImageRaw::setTextureJob` in `llimageworker.cpp` and `lltexturefetch.cpp`. |
| T012 | ✅ VERIFIED | Resolved format propagated to `VayuBCTextureCache::writeEntry` producing `.bc5` on disk. |
| T013 | ✅ VERIFIED | Dual-probe in `LLTextureFetchWorker::doWork` probes `.bc5` first with format guard. |
| T014 | ✅ VERIFIED | `pbropaqueF.glsl` implements B-channel reconstruction `vNt.z = sqrt(max(0.0, 1.0 - dot(vNt.xy, vNt.xy)))`. |
| T015 | ✅ VERIFIED | `pbralphaF.glsl` implements B-channel reconstruction. |
| T016 | ✅ VERIFIED | `pbropaqueIndexedF.glsl` implements B-channel reconstruction at `sample_normal` decode site. |
| T017 | ✅ VERIFIED | `llface.cpp`, `llvovolume.cpp`, and `lldrawable.cpp` assert `EVayuTextureJob::Normal` at `setNormalMap()`. |
| T018 | ✅ VERIFIED | Encode-time alpha classification in `vayuimageblockcompressor.cpp`: exact-255 -> BC5, min_a < 255 -> BC7/BC3 color fallback. |
| T019 | ✅ VERIFIED | Informative alpha fallback normal maps write cache entry under `.bc`. |
| T020 | ✅ VERIFIED | Dual-probe cache hit lookup probes `.bc` fallback when `.bc5` misses. |
| T021 | ✅ VERIFIED | `materialF.glsl` implements unconditional B-reconstruction while preserving `raw.a` gloss read. |
| T022 | ✅ VERIFIED | `materialIndexedF.glsl` implements unconditional B-reconstruction while preserving `raw.a` gloss read. |
| T023 | ✅ VERIFIED | `bumpF.glsl` implements B-channel reconstruction on legacy bump normal. |
| T024 | ✅ VERIFIED | Unit test `test<24>` verifies `.bc` and `.bc5` cache coexistence for identical UUIDs. |
| T025 | ✅ VERIFIED | Unit test `test<22>` verifies bit-identical compression for Default/Albedo/MR jobs (SC-004). |
| T026 | ✅ VERIFIED | `scripts/perf/analyze_vayu_dump.py` supports BC5 cache and dump entry decoding. |
| T027 | ✅ VERIFIED | Water normal shaders `waterF.glsl` and `underWaterF.glsl` verified clean and untouched. |
| T028 | ✅ VERIFIED | Ripgrep shader audit verified no sun/directional normal decoders modified. |
| T029 | ✅ VERIFIED | Clangd diagnostics on all 12 modified C++ files returned 0 errors. |
| T030 | ✅ VERIFIED | Constitution compliance check passed; active viewer process PID 3142315 documented for pre-build safety. |

---

## Unassessable Items (SKIPPED)

None.

---

## Pre-Build Advisory

> [!WARNING]
> An active viewer process was detected:
> `bin/vayu-bin` (PID 3142315).
> Per the Vayu-Viewer Agent Invariants and Constitution, any future compilation/build commands MUST NOT be executed while `vayu-bin` is running. Ensure the process has completely exited prior to requesting a build.
