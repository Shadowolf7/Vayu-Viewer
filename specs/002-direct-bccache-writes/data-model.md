# Phase 1 Data Model: Direct Worker Storage Writes for Block-Compressed Texture Cache

## Entities & Memory Representation

```mermaid
classDiagram
    class VayuBCTextureCache {
        -std::string mCacheDir
        -U64 mNominalSizeBytes
        -U64 mMaxSizeBytes
        -std::atomic~U64~ mCurrentSizeBytes
        -std::atomic~U64~ mEntryCount
        -std::atomic~bool~ mPurging
        -bool mCacheValid
        -VayuBCCachePurgeThread* mPurgeThread
        +instance() VayuBCTextureCache&
        +initCache(cache_dir, max_size_bytes, second_instance) void
        +ensureDirectoriesExist() bool
        +clear() void
        +purge() void
        +threadedPurge() void
        +shutdown() void
        +readEntry(id, discard_level, header, buffer) bool
        +writeEntry(id, discard_level, header, buffer) void
        +getFilePath(id) string
        +getFilePath(id, discard_level) string
        +calcSubBufferBytes(...) size_t
        +updateFileAccessTime(file_path) void
        +addBytesWritten(bytes) void
    }

    class VayuBCCacheEntryHeader {
        +U8 mFormat
        +U8 mPreset
        +U8 mIsMask
        +U8 mDiscardLevel
        +S32 mMipLevels
        +U32 mWidth
        +U32 mHeight
        +S32 mComponents
        +U32 mGLInternalFormat
        +U32 mGLPrimaryFormat
    }

    class FileHeader {
        +U32 mMagic
        +U32 mVersion
        +VayuBCCacheEntryHeader mMeta
        +U64 mBufferSize
    }

    class VayuBCCachePurgeThread {
        +run() void
    }

    VayuBCTextureCache *-- VayuBCCachePurgeThread : manages
    FileHeader *-- VayuBCCacheEntryHeader : contains
    VayuBCTextureCache ..> FileHeader : reads/writes on disk
```

---

### 1. `VayuBCTextureCache` (Process Singleton)

Responsible for coordinating on-disk cache directories, lock-free size tracking, direct worker reads and writes, and main-thread maintenance purging.

| Field | Type | Access / Concurrency | Description |
| :--- | :--- | :--- | :--- |
| `mCacheDir` | `std::string` | Read-only after `initCache` | Base path for the BC cache (`~/.vayu/cache/bccache/`) |
| `mNominalSizeBytes` | `U64` | Read-only after `initCache` | Target capacity after purging (e.g. user budget) |
| `mMaxSizeBytes` | `U64` | Read-only after `initCache` | Threshold triggering auto-purge (150% of nominal) |
| `mCurrentSizeBytes` | `std::atomic<U64>` | Lock-free atomic | Real-time byte tally of cached entries on disk |
| `mEntryCount` | `std::atomic<U64>` | Lock-free atomic | Real-time count of cached files on disk |
| `mPurging` | `std::atomic<bool>` | Lock-free atomic | Flag indicating an active purge thread |
| `mCacheValid` | `bool` | Set on init/shutdown | Validity flag for cache operations |
| `mPurgeThread` | `VayuBCCachePurgeThread*` | Main thread only | Pointer to active purge thread |

> [!IMPORTANT]
> **Eliminated Fields**: `mWriterPool`, `mPendingWrites`, `mPendingIndex`, `mFlushing`, `mPendingBytes`, `mMaxPendingBytes`, `mDroppedWrites`, `mDroppedBytes`, `mDroppedWritesReported`, `mLastDropLogTime`, `mDraining`, and `PendingWrite`.

---

### 2. `VayuBCCacheEntryHeader` (Metadata)

C-compatible metadata header describing texture encoding, dimensions, and GL upload formats.

| Field | Type | Size | Description |
| :--- | :--- | :--- | :--- |
| `mFormat` | `U8` | 1 byte | Compression format (`1` = BC1, `2` = BC3, `3` = BC7) |
| `mPreset` | `U8` | 1 byte | Compression preset (`0` = Fast, `1` = Medium, `2` = Slow) |
| `mIsMask` | `U8` | 1 byte | `1` if alpha mask, `0` if alpha blend / opaque |
| `mDiscardLevel` | `U8` | 1 byte | Discard level of root mip (`0` = full resolution) |
| `mMipLevels` | `S32` | 4 bytes | Total mip levels present in the stored payload |
| `mWidth` | `U32` | 4 bytes | Width of the largest mip in pixels |
| `mHeight` | `U32` | 4 bytes | Height of the largest mip in pixels |
| `mComponents` | `S32` | 4 bytes | Color components (3 = RGB, 4 = RGBA) |
| `mGLInternalFormat` | `U32` | 4 bytes | OpenGL compressed internal format enum |
| `mGLPrimaryFormat` | `U32` | 4 bytes | OpenGL base primary format enum |

Total size: 28 bytes.

---

### 3. On-Disk Binary Format: `FileHeader`

Stored as the leading binary payload of every `<uuid>.bc` file on disk.

```text
+-----------------------+-----------------------+---------------------------------------+-----------------------+
|  mMagic (4 bytes)     |  mVersion (4 bytes)   |  mMeta (28 bytes)                     |  mBufferSize (8 bytes)|
|  0x31434256 ("VBC1")  |  0x00000003 (kVer=3)  |  (VayuBCCacheEntryHeader)             |  Payload byte length  |
+-----------------------+-----------------------+---------------------------------------+-----------------------+
|  Compressed Texture Byte Stream (mBufferSize bytes) ...                                                       |
+---------------------------------------------------------------------------------------------------------------+
```

---

## State Machine & Execution Lifecycles

### Direct Worker Write Flow (Lock-Free, No Purge Spawning)

```mermaid
sequenceDiagram
    autonumber
    participant W as ImageDecode Worker Thread
    participant C as VayuBCTextureCache
    participant FS as Local Filesystem / OS Page Cache

    W->>C: writeEntry(id, discard, header, buffer)
    activate C
    C->>FS: isfile(getFilePath(id))?
    alt File exists
        C->>FS: read FileHeader
        opt existing.mDiscardLevel <= discard
            C-->>W: Return (No-op: higher/equal resolution exists)
        end
    end
    C->>FS: open ofstream(path, binary|trunc)
    alt Open failed
        C->>C: ensureDirectoriesExist()
        C->>FS: retry open ofstream(path)
    end
    C->>FS: write(FileHeader)
    C->>FS: write(buffer)
    C->>FS: close()
    C->>C: addBytesWritten(delta) (atomic, bails out on non-main thread)
    C-->>W: Return immediately
    deactivate C
```

### Direct Read Flow

```mermaid
sequenceDiagram
    autonumber
    participant R as Reader / GL Upload Thread
    participant C as VayuBCTextureCache
    participant FS as Local Filesystem / OS Page Cache

    R->>C: readEntry(id, discard_level, header, buffer)
    activate C
    C->>FS: isfile(getFilePath(id))?
    alt Not found
        C-->>R: Return false (Cache Miss)
    end
    C->>FS: open ifstream(path, binary)
    C->>FS: read FileHeader
    alt Magic / Version invalid
        C->>FS: remove(path)
        C->>C: addBytesWritten(-size)
        C-->>R: Return false
    end
    alt Exact discard match
        C->>FS: read full buffer
        C->>C: updateFileAccessTime(path)
        C-->>R: Return true
    else Stored discard < requested discard (Slicing)
        C->>C: calcSubBufferBytes(...)
        C->>FS: read sub-buffer slice
        C->>C: updateFileAccessTime(path)
        C-->>R: Return true
    else Stored discard > requested discard
        C-->>R: Return false (Finer resolution needed)
    end
    deactivate C
```

---

## Core Invariants

1. **Zero Mutex Across File I/O**: `writeEntry()`, `readEntry()`, and `purge()` never hold `mMutex` during file opens, reads, writes, stats, or closes.
2. **Strict Worker Decoupling from Purge**: Worker writes NEVER check cache limits or launch purge threads. In `addBytesWritten()`, non-main threads bail out immediately (`if (!is_main_thread()) return;`).
3. **Resolution Staging Invariant**: An on-disk entry with lower discard level (higher resolution) is never overwritten by a write with a higher discard level (coarser resolution).
4. **Partitioned Isolation**: Textures are partitioned into `cache_dir / hex_subdir / <uuid>.bc` where `hex_subdir` is `id.asString()[0]`. Two concurrent writes for different UUIDs never touch the same file.
5. **Lock-Free Size Accounting**: `mCurrentSizeBytes` accurately reflects on-disk volume through atomic fetch-add and compare-exchange operations.
