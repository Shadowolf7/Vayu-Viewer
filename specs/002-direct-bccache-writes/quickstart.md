# Quickstart: Direct Worker Storage Writes Validation Guide

## Overview

This guide provides end-to-end validation scenarios for verifying direct worker storage writes in `VayuBCTextureCache` without a background writer pool or user-space pending queues.

> [!NOTE]
> All build and execution commands below are documented for test verification and must only be executed under explicit developer instruction per the Vayu-Viewer Constitution.

---

## Scenario 1: Unit Test Suite Verification

### Objective
Verify that `VayuBCTextureCache` correctly handles direct writes, read round-trips, sub-buffer slicing, self-healing directories, discard upgrade protection, and multi-threaded concurrent worker writes without an intermediate writer pool.

### Test Target
- Test binary: `llimage_test` target `PROJECT_llimage_TEST_vayubctexturecache`
- Source: `indra/llimage/tests/vayubctexturecache_test.cpp`

### Verification Steps
1. Build the unit test binary:
   ```bash
   ninja -C build-Linux-ninja-perf llimage_test
   ```
2. Execute the test runner:
   ```bash
   ./build-Linux-ninja-perf/indra/llimage/tests/llimage_test --suite=VayuBCTextureCache
   ```
3. Expected Outcome:
   - All tests pass (100% success rate).
   - Test cases verify direct worker writes, concurrent thread writes, self-healing directories, and 0 dropped writes.

---

## Scenario 2: Concurrent Multi-Threaded Direct Worker Writes

### Objective
Ensure that multiple parallel threads simulating `ImageDecode` workers can invoke `writeEntry()` simultaneously across distinct texture UUIDs without deadlocks, corruption, or serialization bottlenecks.

### Test Logic
1. Initialize cache in a temporary directory with a test budget.
2. Spawn 8 worker threads simulating parallel SIMD image decoders.
3. Each worker thread generates 50 distinct textures and writes them concurrently via `writeEntry()`.
4. Verify:
   - 400 total entries written to disk across the 16 hex subdirectories.
   - `mEntryCount == 400`.
   - `mCurrentSizeBytes` equals the sum of all written file sizes.
   - 0 writes dropped or skipped.
   - All 400 entries readable via `readEntry()`.

---

## Scenario 3: Discard Upgrade & Overwrite Protection

### Objective
Verify that coarser resolution writes (higher discard level) never overwrite finer resolution entries (lower discard level) already committed to disk.

### Test Logic
1. Write a full-resolution texture (`discard = 0`, 16x16, 3 mips) for UUID `A`.
2. Attempt to write a coarse preview (`discard = 2`, 4x4, 1 mip) for the same UUID `A`.
3. Verify that the file on disk retains `discard = 0` and full resolution.
4. Write a coarse preview (`discard = 2`) for UUID `B`.
5. Upgrade UUID `B` by writing full resolution (`discard = 0`).
6. Verify that the file on disk is successfully upgraded to `discard = 0`.

---

## Scenario 4: Clean Application Teardown

### Objective
Verify that the viewer shuts down deterministically during active texture streaming without hanging or crashing with `SIGABRT`.

### Test Logic
1. Subject the cache to continuous background writes.
2. Trigger `VayuBCTextureCache::instance().shutdown()`.
3. Verify that the shutdown method returns within 1 second.
4. Verify that `mCacheValid == false` and `mPurgeThread == nullptr`.
5. Process exits cleanly with exit code 0.
