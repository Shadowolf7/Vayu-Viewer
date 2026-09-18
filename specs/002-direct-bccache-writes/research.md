# Phase 0 Research & Architectural Alignment: Direct Worker Storage Writes for Block-Compressed Texture Cache

## Context & Problem Statement

In `VayuBCTextureCache`, disk writes for block-compressed textures were offloaded to a secondary single-threaded pool (`ThreadPool:BCCacheWriter`) and buffered through an in-memory queue (`mPendingWrites`, `mPendingIndex`, `mFlushing`) bounded by `VayuBCTextureCacheMaxPendingSize`.

This design was introduced in commit `18e33f5722` to prevent `ImageDecode` workers from blocking when `writeEntry()` held a coarse `mMutex` across synchronous `std::ofstream.write()` calls. Instead of simply removing the lock from the file I/O critical section, a secondary worker thread pool and an in-memory pending queue were bolted on.

This architectural deviation from Henri Beauchamp's reference `LLDiskCache` (Cool VL Viewer) spawned a cascade of severe defects:
1. **Redundant User-Space Buffering**: The operating system kernel page cache already buffers dirty disk writes asynchronously in RAM. Maintaining a custom in-memory queue (`mPendingWrites`) with duplicated memory buffers is completely redundant.
2. **Funnel Bottleneck (8-to-1)**: 8 parallel SIMD image decode workers feed into a single writer thread. Under rapid region crossings or dense scenes, the writer thread cannot keep up with production, forcing the cache to drop writes (`mDroppedWrites`) and triggering Issue #97.
3. **Fragile Concurrency History**:
   - **Shutdown SIGABRT** (`c8abb6ebf0`): `mWriterPool` was destroyed after `LLEventPumps` during static destruction.
   - **Deadlock under write bursts** (`37849f7fd3`): `writeEntry()` blocked on queue tickets while holding `mMutex`.
   - **Unbounded RAM growth** (`bedec7af83`): In-memory queue ballooned to gigabytes before a 256 MB cap was retrofitted.
   - **Silent write drops** (Issue #94): Cache directory destruction left `drainPendingWrites()` silently dropping files via `if (out.good())`.

Every single one of these problems is directly caused by the decision to spawn a secondary worker pool and intermediate queues in violation of the clean CoolVL cache architecture. By correctly implementing the CoolVL cache architecture here, every enumerated issue is eliminated at its root.

---

## Direct Root-Cause Resolution via CoolVL Alignment

| Enumerated Issue in Issue #100 | Root Cause (Architectural Deviation) | CoolVL Resolution (Architecture Restored) |
| :--- | :--- | :--- |
| **1. Redundant User-Space Buffering** | Bolted-on in-memory queues (`mPendingWrites`) duplicating buffers in heap RAM. | **Zero user-space queues.** Writes flush straight into the OS kernel page cache, which manages dirty pages in RAM asynchronously at hardware speed. |
| **2. Funnel Bottleneck (8-to-1)** | 8 parallel SIMD decoders serializing behind a single-threaded writer pool (`BCCacheWriter`). | **Direct worker writes.** Whichever `ImageDecode` thread produces the texture writes directly to disk across 16 partitioned hex subdirectories (`0`–`f`). 8 parallel decoders write concurrently with zero bottleneck. |
| **3. Dropped Writes under Bursts (#97)** | Queue capacity overflow forcing backlog trimming and file drops. | **Zero dropped writes (0%).** Direct disk writes never drop; the OS page cache absorbs bursts effortlessly. |
| **4. Shutdown SIGABRT (`c8abb6ebf0`)** | `mWriterPool` lifecycle racing against `LLEventPumps` and viewer static teardown. | **Zero secondary thread pools.** No `mWriterPool` exists; shutdown is instantaneous, simple, and deterministic. |
| **5. Deadlock under Write Bursts (`37849f7fd3`)** | `writeEntry()` blocking on queue tickets while holding coarse `mMutex`. | **No mutex held during writes.** Lock-free concurrent writes across separate file paths. |
| **6. Unbounded RAM Growth (`bedec7af83`)** | Unconstrained queue accumulation of multi-megabyte compressed mip arrays. | **Zero queue footprint.** Buffer memory is freed immediately upon completion of the direct write. |
| **7. Silent Write Drops (Issue #94)** | `drainPendingWrites()` silently discarding entries on write failures during directory destruction. | **Direct synchronous error recovery.** `writeEntry()` calls `ensureDirectoriesExist()` directly on the caller thread and retries with immediate feedback. |

---

## Architectural Reference: CoolVL `LLDiskCache`

In Cool VL Viewer (`LLDiskCache`), disk caching for J2C and general assets is simple, robust, and lock-free:
- **Direct Worker Writes**: Whichever worker thread produces the asset writes the file directly to disk (`cache_dir / hex_subdir / <uuid>.asset`) without holding any mutex.
- **Partitioned Storage (No Shared File Contention)**: Every texture UUID produces a distinct filename distributed across 16 hex subdirectories (`0`–`f`), ensuring concurrent worker writes never conflict or serialize with one another.
- **Lock-Free Accounting**: Once the write completes, the worker updates an `std::atomic<U64> sCurrentSizeBytes` counter.
- **Hardware-Speed Buffering**: The operating system's kernel page cache absorbs dirty file pages immediately into RAM, flushing them to NVMe/SSD asynchronously at hardware speeds without thread hops or context switches.
- **Strictly Decoupled Purging**: Worker threads NEVER evaluate cache limits or spawn purge threads when writing. In `LLDiskCache::addBytesWritten()`, worker threads bail out immediately via `if (!is_main_thread() || sPurgeThread) return;`. Purging is strictly restricted to the main thread (e.g. startup maintenance or explicit menu actions).
- **Zero In-Memory Queues**: No secondary thread pool, no pending list allocations, no drop heuristics, and zero risk of worker-pool deadlocks.

---

## Technical Decisions & Rationale

### Decision 1: Direct Worker Writes Without Shared Mutex

- **Decision**: Remove `ThreadPool:BCCacheWriter` and eliminate all user-space queue structures (`mPendingWrites`, `mPendingIndex`, `mFlushing`). In `writeEntry()`—which is executed directly on the `ImageDecode` worker thread upon completing block compression—write the `FileHeader` and compressed buffer directly to `getFilePath(id)` without acquiring `mMutex`.
- **Rationale**:
  - Eliminates thread hopping and context switching overhead.
  - Workers write straight into the OS kernel page cache, which buffers dirty writes in RAM asynchronously and flushes to NVMe/SSD at bus speeds.
  - Because each texture UUID is unique and files are partitioned across 16 hex subdirectories, different worker threads never contend for the same file path or directory lock.
- **Alternatives Considered**:
  - *Multi-threaded writer pool (e.g., 4 writer threads)*: Still introduces redundant context switches, thread queues, and user-space memory duplication. Overcomplicates what the OS page cache already does natively.
  - *Increasing the in-memory pending queue ceiling*: Band-aid that increases RAM usage by gigabytes without solving the throughput mismatch or shutdown race conditions.

---

### Decision 2: Real-Time Lock-Free Cache Accounting with Worker Decoupling from Purging

- **Decision**: Update cache storage metrics atomically (`mCurrentSizeBytes`, `mEntryCount`) via `addBytesWritten()`, strictly adhering to `LLDiskCache::addBytesWritten()`. Worker threads NEVER spawn purge threads or check capacity limits.
- **Rationale**:
  - `addBytesWritten(S64 bytes)` uses atomic fetch-add for positive byte deltas and compare-exchange loop for negative byte deltas.
  - In `LLDiskCache`, if `!is_main_thread()`, `addBytesWritten()` immediately returns after updating the atomic counter. Worker threads do not launch purge threads or scan directories.
  - In `VayuBCTextureCache`, we mirror this exact invariant: `if (!is_main_thread() || mPurging) return;`. Worker threads write their file, update the atomic counter, and return immediately.
- **Alternatives Considered**:
  - *Spawning purge thread from worker thread on write*: Violates CoolVL architecture. Causes worker threads to race on purge thread initialization and creates heavy disk I/O thrashing between texture decoding workers and background purge scanners.

---

### Decision 3: Direct File-Based Overwrite Protection & Discard Upgrade Check

- **Decision**: In `writeEntry()`, perform resolution upgrade checking directly against the on-disk file:
  1. Check `LLFile::isfile(path)`.
  2. If present, open `std::ifstream in(path, std::ios::binary)` and read `FileHeader`.
  3. If `existing_fh.mMeta.mDiscardLevel <= local_header.mDiscardLevel`, return immediately without writing (preventing coarser overwrites of finer resolution data).
  4. Also check legacy path `getFilePath(id, 0)` if `discard_level > 0`.
- **Rationale**:
  - Since writes hit the OS page cache immediately, any recently written entry is hot in the OS page cache and read instantaneously.
  - Eliminates the need to maintain `mPendingIndex` or `mFlushing` maps in RAM.
  - Completely self-contained within `writeEntry()` without holding locks.
- **Alternatives Considered**:
  - *Shared concurrent map of in-flight UUIDs*: Introduces fine-grained locking or lock-free map overhead for zero tangible benefit, since duplicate decodes for the same UUID at the exact same moment are already deduplicated by `LLTextureFetch`.

---

### Decision 4: Directory Self-Healing Without Mutex

- **Decision**: If `std::ofstream out(path, ...)` fails to open (`!out.good()`), invoke `ensureDirectoriesExist()` to recreate any missing hex subdirectories, then retry the open once.
- **Rationale**:
  - Protects against external directory deletion or initial missing paths (Issue #94).
  - Directory creation is idempotent (`LLFile::mkdir` ignores existing directories).
  - Standard error path handling that only executes if a directory is actually missing.
- **Alternatives Considered**:
  - *Pre-checking directory existence on every write*: Adds unnecessary filesystem stat calls to the hot path.

---

### Decision 5: Configuration & App Teardown Simplification

- **Decision**:
  1. Remove `VayuBCTextureCacheMaxPendingSize` from `indra/newview/app_settings/settings.xml`.
  2. Remove listener and references to `VayuBCTextureCacheMaxPendingSize` in `indra/newview/llappviewer.cpp` and `indra/newview/llviewercontrol.cpp`.
  3. Simplify `VayuBCTextureCache::shutdown()`: Set `mCacheValid = false`, wait for `mPurgeThread` to finish (timeout loop up to 1 second), and delete `mPurgeThread`.
- **Rationale**:
  - With no in-memory pending queues, queue ceiling settings are obsolete.
  - With no `mWriterPool`, shutdown is instantaneous and deterministic, eliminating shutdown aborts (`c8abb6ebf0`).
- **Alternatives Considered**:
  - *Retaining unused settings as deprecated stubs*: Leaves dead settings in `settings.xml` and confuses users and developers. Clean removal follows the Anti-Scope Creep and minimal diff principles.

---

## Parity Matrix: `LLDiskCache` vs `VayuBCTextureCache`

| Architectural Feature | Reference `LLDiskCache` (CoolVL) | Prior `VayuBCTextureCache` | New `VayuBCTextureCache` (Aligned) |
| :--- | :--- | :--- | :--- |
| **Worker Writes** | Direct from caller thread | Queued to `ThreadPool:BCCacheWriter` | **Direct from ImageDecode worker** |
| **User-Space Queues** | None | `mPendingWrites`, `mPendingIndex`, `mFlushing` | **None** |
| **Dropped Writes** | None (0%) | Dropped when queue hits 256 MB cap | **None (0%)** |
| **Thread Synchronization** | Lock-free atomics | `mMutex` over queue operations | **Lock-free atomics** |
| **Storage Structure** | 16 hex subdirs (`0`–`f`) | 16 hex subdirs (`0`–`f`) | **16 hex subdirs (`0`–`f`)** |
| **File I/O Lock** | No mutex during I/O | Mutex during queuing, single-thread writer | **No mutex during I/O** |
| **Worker Purge Spawning**| **NEVER** (`!is_main_thread()` returns) | Called `threadedPurge()` from worker thread | **NEVER (`!is_main_thread()` returns)** |
| **Purge Trigger** | Main thread only (`PurgeDiskCacheOnStartup`, etc.) | Writer thread triggered | **Main thread only** |
| **Teardown** | Join purge thread | Join writer pool + purge thread | **Join purge thread** |
