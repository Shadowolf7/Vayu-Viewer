# Implementation Plan: Progressive Mip Residency (Resolution Staging)

**Branch**: `001-progressive-mip-residency` | **Date**: 2026-09-16 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/001-progressive-mip-residency/spec.md`

## Summary

Implement an industry-standard Progressive Mip Residency (Resolution Staging) architecture for texture streaming and block compression. This plan decouples worker queue load management from asset compression fidelity, eliminating dynamic preset downgrades, Mode-6 compression artifacts, and multi-tier re-decode thrashing loops (#96). We eliminate the user-facing compression preset preference knob entirely: textures default to hybrid quality encoding (Mip 0 `Slow`, sub-mips `Fast`) for maximum streaming responsiveness, with an optional developer debug setting (`RenderCompressTexturesHybridMips`) to benchmark against pure `Slow` across all mips. Load is managed strictly through spatial resolution staging: coarse mips ($\le 256 \times 256$) decode in RAM in microseconds for immediate navigation feedback and write to disk cache (`<uuid>.bc`) as upgradable entries, while heavy Discard 0 pyramids are deferred during queue saturation and seamlessly upgrade on-disk cache entries once completed.

---

## Technical Context

**Language/Version**: C++17 / C++20  
**Primary Dependencies**: OpenJPEG, bc7enc, rgbcx, libyuv, `LL::ThreadPool` ("ImageDecode"), `LLWorkerThread`  
**Storage**: Persistent on-disk texture cache (`~/.vayu/cache/bccache/` structured as `hex_subdir/<uuid>.bc`)  
**Testing**: TUT C++ Unit Test Framework (`indra/llimage/tests/`)  
**Target Platform**: Linux x86_64, Windows x86_64, macOS  
**Project Type**: High-performance 3D virtual world desktop client  
**Performance Goals**: Coarse mip preview latency $< 50\text{ ms}$; $0\text{ ms}$ decode penalty on disk cache hits; 100% elimination of dynamic preset downgrades, user preset knob complexity, and re-decode loops  
**Constraints**: Strict Launch & Build Prohibitions; zero hidden allocations in high-frequency paths; zero-copy sub-buffer slicing; minimal diff footprint  
**Scale/Scope**: Texture streaming pipeline across `indra/llimage` and `indra/newview` plus removal of deprecated UI/settings controls and test suite updates  

---

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Assessment | Status |
| :--- | :--- | :---: |
| **I. Strict Launch & Build Prohibitions** | No build commands (`ninja`, `cmake`, test runners) or GUI app launches (`vayu`) will be invoked autonomously. All validation instructions remain descriptive and user-authorized. | **PASS** |
| **II. Minimal Diff & Anti-Scope Creep** | Changes are strictly confined to `vayuimageblockcompressor`, `vayubctexturecache`, `llimageworker`, `lltexturefetch`, UI/settings cleanup of the obsolete preset knob, and unit tests. Existing working tree changes are preserved and unified. | **PASS** |
| **III. Task Completeness & Convergence** | All requirements (`FR-001` through `FR-008`) and user stories (`P1` through `P3`) map directly to design artifacts, contracts, and upcoming tasks. | **PASS** |
| **IV. C++ Engineering & Zero-Copy Standards** | Sub-buffer slicing operates zero-copy via offset calculation. Shared pointers alias buffer memory without copies. No dynamic heap allocations in inner decode/downsampling loops. | **PASS** |
| **V. Clear Communication & Native Tooling** | Plain-language explanations; ripgrep (`grep_search` / `rg`) used exclusively for codebase auditing. | **PASS** |

*Post-Design Evaluation*: All gates remain fully satisfied.

---

## Project Structure

### Documentation (this feature)

```text
specs/001-progressive-mip-residency/
├── plan.md              # Implementation Plan (this file)
├── research.md          # Phase 0 Technical Decisions & Rationale
├── data-model.md        # Phase 1 Entities, State Machine & Invariants
├── quickstart.md        # Phase 1 Runnable Validation Scenarios
├── contracts/           # Phase 1 Interface Contracts
│   ├── vayubctexturecache_contract.md
│   ├── vayuimageblockcompressor_contract.md
│   └── resolution_staging_contract.md
└── checklists/
    └── requirements.md  # Specification Quality Checklist
```

### Source Code (repository root)

```text
indra/
├── llimage/
│   ├── vayuimageblockcompressor.h             # Default hybrid target preset (Mip 0 Slow, sub-mips Fast), setHybridMips debug toggle
│   ├── vayuimageblockcompressor.cpp           # Hybrid BC1/BC7 encoding, optional pure Slow toggle via debug setting
│   ├── vayubctexturecache.h                   # Public kFormatVersion = 3, readEntry without min_preset, sub-buffer slicing
│   ├── vayubctexturecache.cpp                 # kFormatVersion = 3 file header, calcSubBufferBytes, discard upgrade invariant
│   ├── llimageworker.h                        # ImageDecode ThreadPool and request structures
│   ├── llimageworker.cpp                      # Coarse preview RAM attachment, non-blocking cache writes with discard upgrade
│   └── tests/
│       ├── vayuimageblockcompressor_test.cpp  # Test Slow preset adherence and hybrid debug toggle
│       ├── vayubctexturecache_test.cpp        # Test readEntry slicing & overwrite protection with kFormatVersion 3
│       └── llimageworker_test.cpp             # Test worker decode and caching
└── newview/
    ├── app_settings/settings.xml              # Remove RenderCompressTexturesPreset; add RenderCompressTexturesHybridMips and VayuBCTextureCacheVersion
    ├── llappviewer.cpp                        # Startup cache version invalidation check (VayuBCTextureCacheVersion != kFormatVersion)
    ├── llviewercontrol.cpp                    # RenderCompressTexturesHybridMips signal listener
    ├── skins/default/xui/en/
    │   └── floater_preferences_graphics_advanced.xml # Remove CompressionPreset UI combo box
    ├── lltexturefetch.h                       # Fetcher state machine and interface
    └── lltexturefetch.cpp                     # Early cache hit check, Mip 0 queue saturation deferral
```

**Structure Decision**: Modified existing core texture streaming, block compression, and preferences files. Obsolete user-facing preset controls are cleanly removed without adding speculative abstractions.

---

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

*No violations. All design patterns align directly with core principles.*
