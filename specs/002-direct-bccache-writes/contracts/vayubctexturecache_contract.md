# Contract: `VayuBCTextureCache` Interface

## Class Declaration & API

```cpp
class VayuBCTextureCache
{
public:
    static VayuBCTextureCache& instance();
    ~VayuBCTextureCache();

    VayuBCTextureCache(const VayuBCTextureCache&) = delete;
    VayuBCTextureCache& operator=(const VayuBCTextureCache&) = delete;

    static constexpr U32 kMagic = 0x31434256; // "VBC1"
    static constexpr U32 kFormatVersion = 3;

    // Initializes the cache directory structure and disk budgets.
    // If cache_dir already initialized, updates budgets safely without rescanning.
    void initCache(const std::filesystem::path& cache_dir, S64 max_size_bytes,
                   bool second_instance = false);

    // Verifies cache_dir and all 16 hex subdirectories ('0'-'f') exist on disk.
    // Recreates missing directories if deleted externally.
    bool ensureDirectoriesExist();

    // Clears the cache by removing all cached files in all 16 subdirectories.
    void clear();

    // Purges oldest items so total cache size <= mNominalSizeBytes.
    // Must be called from the main thread or a dedicated maintenance thread.
    void purge();

    // Threaded cache purging via background VayuBCCachePurgeThread.
    // Must be called from the main thread only.
    void threadedPurge();

    // Shuts down the cache and joins the purge thread cleanly.
    void shutdown();

    // Direct synchronous disk read. Slices sub-buffer if discard_level > stored discard.
    // Returns true on hit, false on miss or corruption.
    bool readEntry(const LLUUID& id, S32 discard_level,
                   VayuBCCacheEntryHeader& header, std::vector<U8>& buffer);

    // Direct worker write. Writes FileHeader + buffer directly to getFilePath(id).
    // Lock-free with respect to other worker writes. Never triggers purge threads.
    void writeEntry(const LLUUID& id, S32 discard_level,
                    const VayuBCCacheEntryHeader& header,
                    std::shared_ptr<const std::vector<U8>> buffer);

    // File path generation: cache_dir / hex_subdir / <id>.bc
    std::string getFilePath(const LLUUID& id) const;
    std::string getFilePath(const LLUUID& id, S32 discard_level) const;

    // Sub-buffer byte size calculation for mip slicing
    static size_t calcSubBufferBytes(U8 format, U32 width, U32 height, S32 num_mips, S32 diff);

    // Rate-limited touch of last write time to preserve LRU ordering
    void updateFileAccessTime(const std::string& file_path);

    // Lock-free atomic size tracking.
    // When called from worker threads (!is_main_thread()), bails out immediately after atomic update.
    void addBytesWritten(S64 bytes);

    S64 getCurrentSize() const;
    S64 getMaxSize() const;
    S64 getNominalSize() const;
    size_t getEntryCount() const;
    bool isInitialized() const;
    const std::string getCacheInfo() const;
};
```

---

## Method Contracts

### `writeEntry`
- **Caller Context**: `ImageRequest::processRequest()` executing on an `ImageDecode` worker thread.
- **Preconditions**: `buffer != nullptr`, `buffer->size() > 0`.
- **Concurrency**: Fully thread-safe across concurrent workers. No shared mutex held during file I/O operations.
- **Behavior**:
  1. Resolves destination path: `getFilePath(id)`.
  2. If file already exists, reads header; if existing `mDiscardLevel <= discard_level`, immediately returns.
  3. Opens file with `std::ofstream(path, std::ios::binary | std::ios::trunc)`.
  4. If open fails, invokes `ensureDirectoriesExist()` and retries once.
  5. Writes `FileHeader` followed by `buffer->data()`.
  6. Closes stream and invokes `addBytesWritten(delta)`.
  7. If new file, increments `mEntryCount`.
  8. Immediately returns to caller. Never spawns or evaluates purge threads.
- **Postconditions**: Texture asset is persisted in the OS page cache / filesystem; atomic cache totals updated.

### `readEntry`
- **Caller Context**: Main thread, texture worker threads, or background loader.
- **Concurrency**: Thread-safe concurrent reads across distinct files.
- **Behavior**:
  1. Checks `LLFile::isfile(getFilePath(id))`.
  2. Opens `std::ifstream(path, std::ios::binary)`.
  3. Reads and validates `FileHeader` (`mMagic == kMagic`, `mVersion == kFormatVersion`).
  4. If invalid/corrupt: removes file, adjusts `addBytesWritten(-st_size)`, decrements `mEntryCount`, returns `false`.
  5. If `mDiscardLevel == discard_level`: reads full buffer, touches access time, returns `true`.
  6. If `mDiscardLevel < discard_level`: computes sub-slice bytes, reads partial buffer, touches access time, returns `true`.
  7. If `mDiscardLevel > discard_level`: returns `false` (finer detail requested than available).

### `threadedPurge`
- **Caller Context**: Main thread ONLY (e.g. startup maintenance in `LLAppViewer::initCache()` or UI menu commands).
- **Concurrency**: Guarded by `mPurgeThread` status check; polls completion via `doAfterInterval` on the main thread event loop.
- **Postconditions**: Reclaims disk space down to `mNominalSizeBytes` in background.

### `shutdown`
- **Caller Context**: Viewer teardown in `LLAppViewer::cleanup()`.
- **Behavior**: Sets `mCacheValid = false`, waits for `mPurgeThread` to finish (up to 1.0s), and deletes `mPurgeThread`.
- **Postconditions**: Zero running background threads; zero dangling writer pool references.
