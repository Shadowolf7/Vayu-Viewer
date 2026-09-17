# Contract: Resolution Staging & Decode Scheduling Contract

**Component**: `indra/llimage/llimageworker.h` / `.cpp` & `indra/newview/lltexturefetch.h` / `.cpp`
**Spec**: [spec.md](../spec.md)

## 1. Decode Queue Backlog Query

```cpp
// LLImageDecodeThread
size_t getPending();
```

### Guarantees
- Returns the current count of pending decode work items in the 8-thread worker pool (`mThreadPool->getQueue().size()`).
- Thread-safe and callable from main thread and texture fetch worker threads.

---

## 2. Decode Deferral & Mip Staging Contract

```cpp
// In LLTextureFetchWorker::doWork(S32 param) when mState == DECODE_IMAGE:
constexpr size_t kSaturationThreshold = 16; // 2x worker pool width

if (discard == 0 && mDecodedDiscard >= 0 &&
    LLAppViewer::getImageDecodeThread()->getPending() > kSaturationThreshold)
{
    // Coarse preview is active and queues are saturated:
    // Defer heavy Mip 0 resolution until queue headroom is restored.
    return false;
}
```

### Guarantees
- If `discard > 0` (coarse preview data): Never deferred. Dispatches immediately to provide rapid visual feedback.
- If `discard == 0` and no preview is active (`mDecodedDiscard < 0`): Dispatches immediately to establish initial visibility.
- If `discard == 0` and preview is active (`mDecodedDiscard >= 0`): Defers Discard 0 when worker queue exceeds `kSaturationThreshold`. Returns `false` to remain in `DECODE_IMAGE` state, cooperatively yielding worker bandwidth to unrendered coarse textures.
- Once pending queue drops $\le \text{kSaturationThreshold}$, Discard 0 proceeds to decode and resolves to full target fidelity.

---

## 3. Upgradable Discard Persistence Contract

```cpp
// In ImageRequest::processRequest() in llimageworker.cpp:
if (cacheable)
{
    VayuBCTextureCache::instance().writeEntry(mID, discard, cache_header, buffer);
}
```

### Guarantees
- Both coarse slice decodes ($\text{discard} > 0$) and full pyramids ($\text{discard} == 0$) are written to `VayuBCTextureCache` asynchronously.
- The cache's overwrite invariant ensures that if an entry already exists with `existing.mDiscardLevel <= discard`, the write is safely dropped without modifying the file.
- When a finer resolution decode (e.g. Discard 0) arrives, it seamlessly upgrades any coarser slice already on disk.
- If a user teleports or disconnects before Discard 0 completes, the coarse decode is preserved in disk cache, preventing duplicate J2C decodes across viewer restarts.
