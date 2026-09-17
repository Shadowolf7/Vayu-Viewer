# Contract: VayuBCTextureCache Interface

**Component**: `indra/llimage/vayubctexturecache.h` / `vayubctexturecache.cpp`
**Spec**: [spec.md](../spec.md)

## 1. `readEntry` Contract

```cpp
bool readEntry(const LLUUID& id,
               S32 discard_level,
               VayuBCCacheEntryHeader& header,
               std::vector<U8>& buffer);
```

### Preconditions
- `id.notNull()` is true.
- `discard_level >= 0`.

### Postconditions
- Returns `true` if an on-disk or flushing entry exists with `entry.mDiscardLevel <= discard_level`.
- If `entry.mDiscardLevel == discard_level`:
  - `buffer` contains the full stored mip payload.
  - `header` reflects the stored file metadata.
- If `entry.mDiscardLevel < discard_level` (higher resolution available on disk):
  - `calcSubBufferBytes()` calculates the byte size of the requested sub-pyramid.
  - `buffer` is populated with the sliced coarse sub-buffer.
  - `header.mWidth` and `header.mHeight` are shifted right by `diff = discard_level - entry.mDiscardLevel`.
  - `header.mMipLevels` is decremented by `diff`.
  - `header.mDiscardLevel` is set to `discard_level`.
- Access time of the cached file is updated via rate-limited LRU touch.
- **Preset Independence**: No rejection based on `mPreset`. Any valid compressed file is accepted.

---

## 2. `writeEntry` Contract

```cpp
void writeEntry(const LLUUID& id,
                S32 discard_level,
                const VayuBCCacheEntryHeader& header,
                std::shared_ptr<const std::vector<U8>> buffer);
```

### Invariants & Guarantees
- **File Naming**: All entries are stored under `<cache_dir>/<hex_subdir>/<uuid>.bc`.
- **Overwrite Prevention**:
  - If a file already exists at `<uuid>.bc` with `existing.mDiscardLevel <= header.mDiscardLevel`, the write is dropped immediately without modifying the file.
  - If a pending write in the write queue has `pending.mDiscardLevel <= header.mDiscardLevel`, the write is dropped.
- **Asynchronous Flush**: Writes are non-blocking and queued to `mPendingWrites`, drained by the background writer thread.

---

## 3. Sub-Buffer Slicing Helper

```cpp
static size_t calcSubBufferBytes(U8 format, U32 width, U32 height, S32 num_mips, S32 diff);
```

### Guarantees
- Computes exact block byte counts for BC1/BC4 (8 bytes per 4x4 block) and BC3/BC5/BC7 (16 bytes per 4x4 block) for all mips down to $1 \times 1$.
- Returns byte count corresponding to the sub-pyramid starting at mip level `diff`.
