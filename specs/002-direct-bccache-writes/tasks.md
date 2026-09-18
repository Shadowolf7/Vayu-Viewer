# Implementation Tasks: Direct Worker Storage Writes for Block-Compressed Texture Cache (CoolVL Architecture Alignment)

**Feature**: Direct Worker Storage Writes for Block-Compressed Texture Cache  
**Branch**: `002-direct-bccache-writes` | **Date**: 2026-09-17  
**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Baseline audit of existing writer pool, pending queue, and settings references across affected files.

- [X] T001 Audit existing `BCCacheWriter`, pending queue, and settings references across `indra/llimage/vayubctexturecache.h`, `indra/llimage/vayubctexturecache.cpp`, `indra/newview/app_settings/settings.xml`, `indra/newview/llappviewer.cpp`, and `indra/newview/llviewercontrol.cpp`
- [X] T002 [P] Prepare unit test harness in `indra/llimage/tests/vayubctexturecache_test.cpp` for direct worker writes and updated `initCache` signature

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Eliminate secondary writer pool and in-memory queue declarations that all subsequent changes depend on.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [X] T003 Remove writer pool (`mWriterPool`), in-memory queue members (`mPendingWrites`, `mPendingIndex`, `mFlushing`, `mPendingBytes`, `mMaxPendingBytes`), and `PendingWrite` struct from `indra/llimage/vayubctexturecache.h`
- [X] T004 Update `VayuBCTextureCache::initCache` signature and implementation in `indra/llimage/vayubctexturecache.h` and `indra/llimage/vayubctexturecache.cpp` to remove `max_pending_bytes` parameter and eliminate writer pool creation
- [X] T005 Update `VayuBCTextureCache::shutdown` in `indra/llimage/vayubctexturecache.h` and `indra/llimage/vayubctexturecache.cpp` to remove writer pool teardown and join `mPurgeThread` cleanly

**Checkpoint**: Foundation ready - cache class compiles without secondary writer pool structures.

---

## Phase 3: User Story 1 - Direct Worker Writes & Unimpeded Streaming (Priority: P1) 🎯 MVP

**Goal**: Whichever `ImageDecode` worker produces the block-compressed texture writes directly to `getFilePath(id)` with zero mutex locks held during file I/O, lock-free atomic accounting, and strict decoupling from purge thread spawning.

**Independent Test**: Simulate concurrent worker threads encoding and writing textures directly via `writeEntry()`; verify files write directly to disk without queuing, deadlocks, or dropped writes, and atomic size and count track accurately.

- [X] T006 [US1] Implement lock-free direct worker `VayuBCTextureCache::writeEntry` in `indra/llimage/vayubctexturecache.cpp` writing `FileHeader` and compressed buffer directly to `getFilePath(id)` without holding `mMutex`
- [X] T007 [US1] Implement direct filesystem overwrite protection in `VayuBCTextureCache::writeEntry` in `indra/llimage/vayubctexturecache.cpp` to reject writes if existing on-disk file has lower or equal discard level
- [X] T008 [US1] Add directory self-healing in `VayuBCTextureCache::writeEntry` in `indra/llimage/vayubctexturecache.cpp` to invoke `ensureDirectoriesExist()` if file open fails and retry write
- [X] T009 [US1] Update `VayuBCTextureCache::addBytesWritten` in `indra/llimage/vayubctexturecache.cpp` to enforce `!is_main_thread()` immediate return, strictly decoupling worker writes from purge thread spawning
- [X] T010 [US1] Update `VayuBCTextureCache::clear` in `indra/llimage/vayubctexturecache.cpp` to remove pending queue clearing and reset atomic size and entry counters

**Checkpoint**: User Story 1 functional and independently testable. Direct worker writes execute lock-free with zero drops and atomic accounting.

---

## Phase 4: User Story 2 - Memory Stability, Deadlock Elimination & Settings Cleanup (Priority: P2)

**Goal**: Remove all user-space queue trimming, drop logic, and obsolete configuration settings (`VayuBCTextureCacheMaxPendingSize`) to guarantee zero queue memory overhead and deterministic shutdown.

**Independent Test**: Subject the cache to sustained burst writes and trigger viewer shutdown; verify 0 MB queue heap allocation, zero write-drop warnings, and clean process exit without `SIGABRT` or deadlocks.

- [X] T011 [US2] Delete `trimPendingBacklog`, `queuePendingWrite`, `drainPendingWrites`, `mDroppedWrites`, and drop rate logging from `indra/llimage/vayubctexturecache.h` and `indra/llimage/vayubctexturecache.cpp`
- [X] T012 [P] [US2] Remove `VayuBCTextureCacheMaxPendingSize` setting definition from `indra/newview/app_settings/settings.xml`
- [X] T013 [P] [US2] Remove `VayuBCTextureCacheMaxPendingSize` argument from `LLAppViewer::applyBCTextureCacheBudgets` in `indra/newview/llappviewer.cpp`
- [X] T014 [P] [US2] Remove `VayuBCTextureCacheMaxPendingSize` signal listener from `settings_setup_listeners` in `indra/newview/llviewercontrol.cpp`

**Checkpoint**: User Story 2 complete. User-space queue overhead reduced to 0 MB and obsolete settings cleanly excised.

---

## Phase 5: User Story 3 - Direct Disk Reads & Unit Test Parity (Priority: P3)

**Goal**: Ensure cache reads query disk directly (hitting OS page cache) without intermediate queue lookups, and update unit tests for multi-threaded direct worker writes.

**Independent Test**: Execute `PROJECT_llimage_TEST_vayubctexturecache` unit test suite; verify concurrent multi-threaded writes, read round-trips, sub-buffer slicing, and immediate shutdown pass 100%.

- [X] T015 [US3] Streamline `VayuBCTextureCache::readEntry` in `indra/llimage/vayubctexturecache.cpp` to remove pending index and flushing lookups, reading directly from disk files via OS page cache
- [X] T016 [US3] Update Test 5 in `indra/llimage/tests/vayubctexturecache_test.cpp` to test concurrent multi-threaded direct worker writes across multiple threads without a writer pool or pending queues
- [X] T017 [US3] Update Test 6 in `indra/llimage/tests/vayubctexturecache_test.cpp` to test immediate persistence and clean deterministic shutdown without backlog drainage
- [X] T018 [US3] Remove `getPendingBytes` and `getMaxPendingBytes` calls from `indra/llimage/tests/vayubctexturecache_test.cpp` and `indra/llimage/vayubctexturecache.h`

**Checkpoint**: All user stories functional. Texture cache hits immediately with zero thrashing and unit tests validate CoolVL direct-write parity.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Repository-wide audit, lint/diagnostics verification, and strict plan alignment.

- [X] T019 [P] Run ripgrep audit across `indra/` to verify zero lingering references to `BCCacheWriter`, `mPendingWrites`, or `VayuBCTextureCacheMaxPendingSize`
- [X] T020 [P] Run clangd `get_diagnostics` across `indra/llimage/vayubctexturecache.h`, `indra/llimage/vayubctexturecache.cpp`, `indra/llimage/tests/vayubctexturecache_test.cpp`, `indra/newview/llappviewer.cpp`, and `indra/newview/llviewercontrol.cpp`
- [X] T021 Review git diff against Vayu-Viewer Constitution principles (strict build guard, minimal diff, zero-copy standards)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately.
- **Foundational (Phase 2)**: Depends on Phase 1 completion - BLOCKS all user stories.
- **User Story 1 (Phase 3)**: Depends on Phase 2 completion. Delivers the core direct worker write engine (MVP).
- **User Story 2 (Phase 4)**: Depends on Phase 2 completion. Can proceed in parallel with US1 or immediately after.
- **User Story 3 (Phase 5)**: Depends on Phase 3 completion (direct writes in place before streamlining reads and unit tests).
- **Polish (Phase 6)**: Depends on all user stories being complete.

### User Story Dependencies

- **User Story 1 (P1)**: Direct worker writes and lock-free accounting. Independent MVP.
- **User Story 2 (P2)**: Dead queue and settings deletion. Independent of US1 write logic, depends on foundational header cleanup.
- **User Story 3 (P3)**: Read streamlining and unit tests. Depends on US1 direct writes being in place.

---

## Parallel Execution Opportunities

- **Phase 1**: `T002` can be drafted while `T001` audit is conducted.
- **Phase 4**: `T012` (`settings.xml`), `T013` (`llappviewer.cpp`), and `T014` (`llviewercontrol.cpp`) modify different files and can be executed in parallel.
- **Phase 6**: `T019` (ripgrep audit) and `T020` (clangd diagnostics) can run concurrently.

---

## Implementation Strategy

### MVP First (User Story 1 Only)
1. Complete Phase 1 (Setup) and Phase 2 (Foundational).
2. Implement Phase 3 (User Story 1 - Direct Worker Writes).
3. Validate that direct worker writes successfully commit textures to disk with zero locks and atomic accounting.

### Incremental Delivery
1. Add User Story 2 (Phase 4): Cleanly excise dead queue structures and `settings.xml` knobs.
2. Add User Story 3 (Phase 5): Streamline `readEntry()` and update unit tests to validate multi-threaded concurrent worker writes.
3. Complete Phase 6 (Polish): Repository-wide declaration audit, pre-build diagnostics, and diff review.
