# Implementation Plan: Direct Worker Storage Writes for Block-Compressed Texture Cache (CoolVL Architecture Alignment)

**Branch**: `002-direct-bccache-writes` | **Date**: 2026-09-17 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/002-direct-bccache-writes/spec.md`

## Summary

Re-implement the block-compressed texture disk cache (`VayuBCTextureCache`) to strictly mirror Henri Beauchamp's reference CoolVL cache architecture (`LLDiskCache`), which was successfully implemented for J2C assets in `indra/llfilesystem/lldiskcache.h|cpp`. 

Every defect enumerated in GitHub Issue #100—redundant user-space buffering, the 8-to-1 decode funnel bottleneck, dropped writes under burst load (#97), shutdown `SIGABRT` crashes (`c8abb6ebf0`), write-burst deadlocks (`37849f7fd3`), gigabyte-scale memory leaks (`bedec7af83`), and silent directory-destruction drops (Issue #94)—was a direct consequence of the architectural mistake to spawn a secondary worker pool (`ThreadPool:BCCacheWriter`) and bolt on in-memory pending queues (`mPendingWrites`, `mPendingIndex`, `mFlushing`, `VayuBCTextureCacheMaxPendingSize`) instead of simply eliminating the lock from the file I/O critical section.

We eliminate every single one of these problems at its root by fully restoring the clean CoolVL architecture:
- Whichever `ImageDecode` worker thread produces the compressed texture writes the file directly to disk (`getFilePath(id)`) with zero mutex locks held during file I/O.
- Texture UUIDs partitioned across 16 hex subdirectories (`0`–`f`) eliminate filesystem write contention.
- Atomic size counters (`mCurrentSizeBytes`, `mEntryCount`) track usage lock-free.
- Worker writes never trigger or spawn purge threads (`if (!is_main_thread()) return;`).
- Cache maintenance purging is strictly decoupled from worker writes and isolated to the main thread.

---

## Technical Context

**Language/Version**: C++17 / C++20  
**Primary Dependencies**: `LL::ThreadPool` ("ImageDecode"), `LLFile`, `LLDirIterator`, `boost::filesystem`, `fmt`  
**Storage**: Persistent on-disk texture cache (`~/.vayu/cache/bccache/` structured as `hex_subdir/<uuid>.bc`)  
**Testing**: TUT C++ Unit Test Framework (`indra/llimage/tests/vayubctexturecache_test.cpp`)  
**Target Platform**: Linux x86_64, Windows x86_64, macOS  
**Project Type**: High-performance 3D virtual world desktop client  
**Performance Goals**: 0 dropped writes (0%); 0 MB user-space queue heap overhead; 0ms write queue lock contention on image decoders; linear write scalability across 8 parallel decoding threads; 0 purge threads spawned during worker writes  
**Constraints**: Strict Launch & Build Prohibitions; zero mutex held across file I/O; zero hidden memory allocations; 1:1 parity with `LLDiskCache` reference architecture; worker writes never spawn purge threads  
**Scale/Scope**: `indra/llimage/vayubctexturecache.h|cpp`, `indra/llimage/tests/vayubctexturecache_test.cpp`, and cleanup in `indra/newview/` (settings and budget listeners)  

---

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Assessment | Status |
| :--- | :--- | :---: |
| **I. Strict Launch & Build Prohibitions** | No build commands (`ninja`, `cmake`, test runners) or GUI app launches (`vayu`, `vayu-bin`) will be triggered autonomously. All verification scenarios in `quickstart.md` are descriptive. | **PASS** |
| **II. Minimal Diff & Anti-Scope Creep** | Changes are strictly confined to removing the obsolete writer pool and queue structures from `VayuBCTextureCache`, aligning with `LLDiskCache`, updating unit tests, and cleaning up the deprecated pending size setting. No unsolicited refactoring. | **PASS** |
| **III. Task Completeness & Convergence** | All requirements (`FR-001` through `FR-011`) and user stories (`P1` through `P3`) map directly to research decisions, data models, contracts, and upcoming tasks. | **PASS** |
| **IV. C++ Engineering & Zero-Copy Standards** | Direct disk writes eliminate duplicate in-memory buffer copies (`mPendingWrites`). Real-time byte tracking uses atomic fetch-add/compare-exchange (`addBytesWritten`). No dynamic heap allocations on high-frequency paths. | **PASS** |
| **V. Clear Communication & Native Tooling** | Clear explanations; ripgrep (`grep_search` / `rg`) used exclusively for symbol and codebase tracing. | **PASS** |

*Post-Design Evaluation*: All gates remain fully satisfied.

---

## Project Structure

### Documentation (this feature)

```text
specs/002-direct-bccache-writes/
├── plan.md              # Implementation Plan (this file)
├── research.md          # Phase 0 Technical Decisions & Rationale
├── data-model.md        # Phase 1 Entities, State Machine & Invariants
├── quickstart.md        # Phase 1 Runnable Validation Scenarios
├── contracts/           # Phase 1 Interface & Parity Contracts
│   ├── vayubctexturecache_contract.md
│   └── coolvl_parity_contract.md
└── checklists/
    └── requirements.md  # Specification Quality Checklist
```

### Source Code (repository root)

```text
indra/
├── llimage/
│   ├── vayubctexturecache.h                   # Remove mWriterPool, pending queues, drop counters; update initCache signature
│   ├── vayubctexturecache.cpp                 # Implement direct worker writeEntry without mutex; eliminate drainPendingWrites; streamline readEntry & shutdown; decouple worker writes from purging
│   └── tests/
│       └── vayubctexturecache_test.cpp        # Update unit tests for direct concurrent worker writes without writer pool or pending bounds
└── newview/
    ├── app_settings/settings.xml              # Remove deprecated VayuBCTextureCacheMaxPendingSize setting
    ├── llappviewer.cpp                        # Update applyBCTextureCacheBudgets() to remove max_pending_bytes argument
    └── llviewercontrol.cpp                    # Remove VayuBCTextureCacheMaxPendingSize setting signal listener
```

**Structure Decision**: Modified existing core texture cache implementation in `indra/llimage` to mirror `indra/llfilesystem/lldiskcache.h|cpp`. Removed dead queue configuration from `indra/newview`.

---

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

*No violations. All design patterns align directly with core principles.*
