# Contract: CoolVL Parity Mapping (`LLDiskCache` vs `VayuBCTextureCache`)

## Purpose
Establishes the architectural invariants required to ensure `VayuBCTextureCache` strictly mirrors the proven design of `LLDiskCache` (Cool VL Viewer reference architecture).

---

## 1:1 Invariant Mapping

| Architectural Pillar | `LLDiskCache` (J2C Asset Cache) | `VayuBCTextureCache` (BC Texture Cache) |
| :--- | :--- | :--- |
| **Direct Worker Writes** | Caller threads write directly to target file path. | `writeEntry()` called on `ImageDecode` worker writes directly to `getFilePath(id)`. |
| **No User-Space Staging** | Zero staging queues in RAM. All writes buffered by OS page cache. | `mPendingWrites`, `mPendingIndex`, `mFlushing` removed. Zero staging queues. |
| **Partitioned Directories** | 16 hex subdirectories (`0`–`f`) based on UUID first char. | 16 hex subdirectories (`0`–`f`) based on UUID first char. |
| **Path Convention** | `cache_dir / <char> / <uuid>.asset` | `cache_dir / <char> / <uuid>.bc` |
| **Lock-Free Byte Accounting** | `static std::atomic<U64> sCurrentSizeBytes` via `addBytesWritten()`. | `std::atomic<U64> mCurrentSizeBytes` via `addBytesWritten()`. |
| **Lock-Free Entry Count** | N/A (tracked implicitly). | `std::atomic<U64> mEntryCount`. |
| **Worker Purge Spawning** | **NEVER**. Bails out immediately if `!is_main_thread()`. | **NEVER**. Bails out immediately if `!is_main_thread()`. |
| **Background Purge** | `LLCachePurgeThread` (subclass of `LLThread`). | `VayuBCCachePurgeThread` (subclass of `LLThread`). |
| **Purge Trigger** | Triggered from main thread ONLY (`LLAppViewer::initCache`, menu). | Triggered from main thread ONLY (`LLAppViewer::initCache`, menu). |
| **Access Time Touching** | `updateFileAccessTime(filename)` with 1800s / 60s rate limits. | `updateFileAccessTime(filename)` with 1800s / 60s rate limits. |
| **Shutdown Behavior** | `sCacheValid = false`; join and delete `sPurgeThread`. | `mCacheValid = false`; join and delete `mPurgeThread`. |
| **Multi-Instance Guard** | `second_instance` flag adjusts `sMaxSizeBytes` ceiling. | `second_instance` flag adjusts `mMaxSizeBytes` ceiling. |
| **Self-Healing Dirs** | Ensures subdirectories exist on init / recovery. | `ensureDirectoriesExist()` recreates missing subdirs on open failure. |

---

## Prohibited Patterns

1. **No Secondary Thread Pools**: `VayuBCTextureCache` must never instantiate a `ThreadPool:BCCacheWriter` or any intermediate writer pool.
2. **No Mutex Locks across File I/O**: `writeEntry()`, `readEntry()`, and `purge()` must never hold `mMutex` during file operations.
3. **No Write Dropping**: Writes must never be dropped, discarded, or rate-limited in user space due to queue backlogs.
4. **No Worker Purge Spawning**: Worker threads writing files must NEVER spawn purge threads or evaluate cache capacity limits to initiate purging.
