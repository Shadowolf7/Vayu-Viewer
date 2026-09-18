# Feature Specification: Direct Worker Storage Writes for Block-Compressed Texture Cache (CoolVL Architecture Alignment)

**Feature Branch**: `002-direct-bccache-writes`

**Created**: 2026-09-17

**Status**: Draft

**Input**: User description: "https://github.com/Shadowolf7/Vayu-Viewer/issues/100"

## Architectural Context & Reference Baseline

The Vayu-Viewer disk caching subsystem previously underwent a major architectural alignment with Henri Beauchamp's reference `LLDiskCache` (Cool VL Viewer) for JPEG2000 (J2C) assets in `indra/llfilesystem/lldiskcache.h|cpp`. That architecture established the gold standard for high-performance, robust, and lock-free disk caching:

1. **Direct Worker Writes**: Decoding worker threads write completed asset data directly to disk without thread hops, staging queues, or locking a shared mutex across filesystem operations.
2. **Lock-Free Accounting**: Real-time disk cache size and entry tracking via atomic counters (`addBytesWritten`).
3. **Partitioned Storage Layout**: Assets distributed across 16 hex subdirectories (`0`–`f`) by UUID, eliminating filesystem directory lock contention.
4. **Hardware-Speed Buffering**: Relies on the operating system kernel page cache to absorb dirty file pages asynchronously into RAM at hardware speeds.
5. **Decoupled Purging**: Worker threads NEVER trigger, evaluate, or spawn cache purging. Worker threads strictly update atomic counters and return. Purging is strictly isolated to the main thread (e.g. startup checks or maintenance triggers) and executed in a dedicated background purge thread without stalling or entangling worker threads.

The block-compressed (BC) texture cache (`VayuBCTextureCache`) diverged from this proven architecture by introducing a secondary worker pool (`ThreadPool:BCCacheWriter`), in-memory pending queues (`mPendingWrites`), and attempting to trigger purging during worker writes. This feature re-aligns `VayuBCTextureCache` strictly to the proven CoolVL cache architecture established in `LLDiskCache`.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Unimpeded Texture Streaming During High-Speed Traversal and Scene Loading (Priority: P1)

As a virtual world explorer navigating dense regions, flying across sim borders, or teleporting into complex scenes, I want textures to decode and persist immediately without stalling background streaming or dropping writes under heavy burst load, so that my visual world loads completely without missing surfaces, blurred assets, or repeated texture re-downloads.

**Why this priority**: Texture streaming throughput directly dictates scene immersion and visual completeness. When texture write queues bottleneck or drop writes during rapid traversal, textures fail to persist, causing repeated network downloads, stuttering, and incomplete rendering. Aligning with the CoolVL direct-worker-write pattern ensures decoding workers flush directly to the kernel page cache without bottlenecking.

**Independent Test**: Move through dense regions or teleport across sim borders at high velocity; verify that all decoded textures persist immediately to persistent storage without dropping writes, causing decode stalls, or triggering re-download cycles.

**Acceptance Scenarios**:

1. **Given** parallel decoding workers are actively decompressing high volumes of textures during rapid scene loading, **When** decoded textures are ready to be saved, **Then** each worker writes the texture asset directly to persistent storage without funneling through a single bottleneck or queue.
2. **Given** high-frequency scene asset production across multiple worker threads, **When** burst volume exceeds past single-worker throughput limits, **Then** zero writes are dropped or discarded due to backlog overflow.

---

### User Story 2 - Stable System Memory Footprint and Elimination of Stutter / Spikes (Priority: P2)

As a resident running the viewer during long exploration sessions or in high-complexity environments, I want memory consumption to remain stable and predictable without ballooning due to buffered pending write queues, and without experiencing deadlocks or application freezes during asset saving or shutdown.

**Why this priority**: User-space write buffering previously caused memory spikes reaching several gigabytes and introduced shutdown hangs/crashes. Eliminating redundant user-space queues ensures predictable RAM usage and clean, deterministic application shutdowns matching the behavior of `LLDiskCache`.

**Independent Test**: Subject the viewer to sustained heavy texture streaming bursts and trigger an immediate shutdown; verify that memory usage does not balloon from queued writes and the viewer exits smoothly without hangs, aborts, or lingering background writer pool locks.

**Acceptance Scenarios**:

1. **Given** heavy concurrent texture decoding generating dozens of megabytes of compressed data per second, **When** assets are prepared for storage, **Then** memory is freed immediately upon persistent commit without being retained in redundant user-space staging buffers.
2. **Given** an active session with continuous texture decompression in progress, **When** the user quits the application, **Then** the viewer shuts down cleanly without hanging or terminating abruptly.

---

### User Story 3 - Instant Cache Hits and Direct Storage Reads (Priority: P3)

As a returning resident revisiting areas with previously cached textures, I want the viewer to retrieve cached texture data directly from persistent storage without navigating intermediate staging queues or suffering from write-back synchronization delays.

**Why this priority**: Cache retrieval speed governs how fast the world pops into view when revisiting familiar locations or looking around. Direct reads ensure consistent zero-latency cache hits without complex synchronization overhead.

**Independent Test**: Revisit an environment where textures were recently streamed; verify that textures load from local disk cache immediately without cache misses, synchronization hitches, or stale entry lookups.

**Acceptance Scenarios**:

1. **Given** a texture recently written to persistent storage by a worker thread, **When** the asset is requested for rendering, **Then** it is read directly from disk storage without intermediate queue checks.
2. **Given** concurrent worker threads accessing different texture assets simultaneously, **When** simultaneous reads and writes occur across separate assets, **Then** operations execute concurrently without serializing behind a shared lock.

---

### Edge Cases

- **Concurrent writes to independent texture assets**: Every texture is identified by a unique identifier and saved in partitioned storage paths (16 hex subdirectories `0`–`f`), ensuring concurrent worker writes never conflict or collide with one another.
- **External deletion or missing storage directory**: If a cache directory or partition is missing or deleted during runtime, the system automatically detects the missing path and recreates the required directory structure before completing the write.
- **Cache budget and purging decoupling**: Worker write operations never evaluate budget limits to spawn purge threads, preventing write stalls and disk thrashing during streaming.
- **Corrupted or partial writes due to abnormal termination or power loss**: Cached entries maintain structured integrity headers; unreadable or partial files fail validation during read operations and are safely discarded and refreshed.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST write decoded block-compressed texture assets directly to persistent storage from the decoding worker thread upon completion of compression, mirroring the lock-free direct write architecture of the reference J2C disk cache (`LLDiskCache`).
- **FR-002**: The system MUST eliminate intermediate in-memory pending write queues, ensuring dirty file data is passed immediately to the operating system's storage cache without duplicate user-space buffer allocations.
- **FR-003**: The system MUST eliminate write-drop policies and write-drop counters, guaranteeing that valid decoded texture assets are committed to persistent storage without being discarded due to internal queue backlogs.
- **FR-004**: The system MUST support concurrent, non-blocking persistent writes across independent texture assets from multiple parallel worker threads without holding a shared mutex during file I/O operations.
- **FR-005**: The system MUST maintain thread-safe, lock-free global accounting of total cached storage bytes and total entry counts as writes complete, matching the atomic accounting model of `LLDiskCache`.
- **FR-006**: The system MUST decouple cache purging entirely from worker write operations, ensuring worker threads never spawn purge threads or evaluate cache limits during write operations.
- **FR-007**: The system MUST read cached texture assets directly from persistent storage without querying intermediate pending write buffers.
- **FR-008**: The system MUST automatically detect and recreate missing cache directory structures on demand if storage directories are deleted or missing during a write attempt.
- **FR-009**: The system MUST restrict threaded cache purging strictly to the main thread (e.g. startup maintenance or explicit purge commands).
- **FR-010**: The system MUST remove obsolete user-facing and application-level configuration settings dedicated to managing pending write queue memory caps (`VayuBCTextureCacheMaxPendingSize`).
- **FR-011**: The system MUST ensure clean, deterministic application shutdown without deadlock or termination faults during active texture storage operations.

### Key Entities

- **Texture Asset**: A unique visual asset identified by an immutable UUID, consisting of image data and compression metadata.
- **Cache Entry File**: The persistent on-disk file (`<uuid>.bc`) containing the asset header and compressed mip data, partitioned across 16 storage subdirectories (`0`–`f`).
- **Storage Cache Budget**: The configured maximum on-disk capacity (nominal and maximum bytes) that limits the persistent disk footprint.
- **Worker Thread**: A parallel processing thread responsible for decompressing incoming textures and writing finished assets directly to persistent storage.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Zero percent (0%) of valid decoded texture writes are dropped or discarded across all operational conditions, including high-density region crossings and teleport bursts.
- **SC-002**: Peak user-space heap memory overhead allocated for texture write staging queues is reduced to zero (0 MB), eliminating queue-induced memory bloat.
- **SC-003**: Zero purge threads are spawned from worker threads during texture streaming operations.
- **SC-004**: Eliminates 100% of application shutdown hangs, deadlocks, and signal abort crashes related to secondary writer pool thread lifecycle and event synchronization.
- **SC-005**: Texture write throughput scales linearly with the number of parallel decoding worker threads without funnel serialization bottlenecks or queue contention.

## Assumptions

- The underlying operating system kernel page cache efficiently buffers dirty file pages in system RAM and flushes them asynchronously to physical storage without requiring manual application-level queueing.
- The reference `LLDiskCache` implementation in `indra/llfilesystem/lldiskcache.h|cpp` serves as the proven architectural reference for lock-free direct writes, atomic tracking, and decoupled main-thread purging in Vayu-Viewer.
- Storage hardware (SSD, NVMe, or HDD) and filesystem handle concurrent write operations across distinct file paths in separate directories safely and efficiently.
- Each texture asset has a globally unique identifier (UUID) ensuring that concurrent worker writes target distinct file paths, eliminating cross-thread file collisions during normal operations.
- Cache directory paths are stored on a local filesystem accessible to the viewer process with read/write permissions.
