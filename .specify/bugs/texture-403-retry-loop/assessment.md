# Bug Assessment: Infinite Retry Loop on HTTP 403 Forbidden Texture Fetches

- **Slug**: texture-403-retry-loop
- **Created**: 2026-09-16
- **Source**: https://github.com/Shadowolf7/Vayu-Viewer/issues/95 (Host: `github.com`, Policy: `allowlisted`)
- **Verdict**: valid
- **Severity**: high

## Report (verbatim or summarized)

> **[Issue #95: [Bug]: Infinite retry loop on HTTP 403 Forbidden texture fetches](https://github.com/Shadowolf7/Vayu-Viewer/issues/95)**
>
> When a texture fetch fails with an HTTP 403 Forbidden response (e.g. invalid permissions or missing asset on the CDN), `LLViewerFetchedTexture` never marks the asset as missing. Because the asset is never marked missing and maintains high decode priority (often > 4,000,000), `LLViewerFetchedTexture::updateFetch()` immediately re-submits the request on every single frame, causing an infinite retry loop that spams the log and floods the network thread.
>
> **Root Cause**:
> In `indra/newview/llviewertexture.cpp:1984` (introduced in commit `b95a751daed99930edc80025be06f783ccb3abc0` for PR #86):
> ```cpp
> if (mLastHttpGetStatus != LLCore::HttpStatus(HTTP_FORBIDDEN))
> {
>     setIsMissingAsset();
> }
> ```
> Exempting `HTTP_FORBIDDEN` was intended to allow retrying texture fetches on region crossings where capability URLs might expire. However, there is no retry limit, exponential backoff, or terminal failure state. For truly unresolvable 403 errors, `updateFetch()` queries `!mIsMissingAsset` each frame and immediately re-enqueues the fetch with maximum priority.
>
> **Proposed Fix**:
> • Track consecutive HTTP 403 failures per texture.  
> • Enforce a maximum retry limit (e.g. 3–5 retries) or apply exponential backoff cooldown timers before retrying.  
> • If retries are exhausted, mark the asset as missing (`setIsMissingAsset()`).

## Symptom

When a texture asset produces an HTTP 403 Forbidden error from the asset CDN (e.g. unpermitted texture or missing CDN asset), `LLViewerFetchedTexture` skips `setIsMissingAsset()`, leaving the texture active with high decode priority. Consequently, `updateFetch()` re-submits the request on every single frame, generating thousands of identical log warnings per minute and saturating the viewer's network and worker threads.

Expected behavior: The viewer should retry transient authorization failures a bounded number of times with exponential backoff cooldown, and if failures persist, mark the asset as missing (`setIsMissingAsset()`) to terminate request creation and log spam.

## Reproduction

1. Log into a simulator containing a prim or surface referencing a texture ID that returns HTTP 403 Forbidden on the asset CDN (e.g. `http://asset-cdn.glb.agni.lindenlab.com/?texture_id=a548cd29-8bbe-af3b-363a-3f44c7e47e68`).
2. Keep the texture in the camera frustum so that its decode priority remains high (`> 0`).
3. Observe `~/.vayu/logs/Vayu.log` while rendering.
4. Over 1,000+ identical HTTP 403 log warnings (`Fetch failure, setting as missing... worker state 6`) are generated within minutes as the fetch request is re-submitted on every frame.

## Suspected Code Paths

- `indra/newview/llviewertexture.cpp:1984-1988`: In `LLViewerFetchedTexture::processFetchResults()`, `setIsMissingAsset()` is skipped when `mLastHttpGetStatus == LLCore::HttpStatus(HTTP_FORBIDDEN)`.
- `indra/newview/llviewertexture.cpp:2123-2204`: In `LLViewerFetchedTexture::updateFetch()`, when a fetch completes without data and `mIsMissingAsset` remains `false`, `make_request` evaluates to `true` on the very same frame or subsequent frame, immediately invoking `createRequest()`.
- `indra/newview/llviewertexture.h:460-495`: `LLViewerFetchedTexture` lacks fields to track consecutive HTTP 403 failure counts or cooldown timers.
- `indra/newview/lltexturefetch.cpp:1576-1596`: In `LLTextureFetchWorker::doWork()`, HTTP 403 handling retries once within the worker (`mRetryAttempt == 0`) or on region change, but once that worker completes, the failure is returned to `LLViewerFetchedTexture`, which spawns a brand new worker.

## Root Cause Hypothesis

In commit `b95a751daed99930edc80025be06f783ccb3abc0` (addressing issue #86: "Grey textures after teleport / region crossing"), `HTTP_FORBIDDEN` was exempted from calling `setIsMissingAsset()` in `LLViewerFetchedTexture::processFetchResults()` to prevent transient region-crossing capability token expirations from permanently blacklisting textures. However, because `LLViewerFetchedTexture` has no retry counter, backoff timer, or terminal failure state for 403 responses, `updateFetch()` queries `!mIsMissingAsset` on every frame, sees `decode_priority > 0`, and continuously re-spawns worker requests via `createRequest()` ad infinitum.
- *Confidence*: High (confirmed through code inspection of `llviewertexture.cpp:1984` and commit `b95a751dae`).

## Proposed Remediation

**Preferred**:
Introduce bounded retry tracking and exponential backoff cooldown in `LLViewerFetchedTexture`:
1. **Add state in `LLViewerFetchedTexture`** (`indra/newview/llviewertexture.h`):
   - `U8 mHttpForbiddenRetryCount;` (initialized to `0` in `init()`)
   - `LLFrameTimer mHttpForbiddenRetryTimer;` (tracks backoff cooldown)
2. **Handle 403 failure in `processFetchResults()`** (`indra/newview/llviewertexture.cpp`):
   - When a fetch completes with `mLastHttpGetStatus == LLCore::HttpStatus(HTTP_FORBIDDEN)` and `getDiscardLevel() < 0`:
     - Increment `mHttpForbiddenRetryCount`.
     - If `mHttpForbiddenRetryCount >= MAX_HTTP_FORBIDDEN_RETRIES` (e.g. 3 retries):
       - Log that HTTP 403 retries have been exhausted and call `setIsMissingAsset()`.
     - Else:
       - Compute an exponential backoff cooldown (e.g. `1.0f * (1 << (mHttpForbiddenRetryCount - 1))` -> 1.0s, 2.0s).
       - Arm `mHttpForbiddenRetryTimer.setTimerExpirySec(cooldown)`.
       - Log the retry attempt and cooldown delay.
     - For non-403 errors (e.g. 404), call `setIsMissingAsset()` immediately as before.
3. **Enforce cooldown in `updateFetch()`** (`indra/newview/llviewertexture.cpp`):
   - In `updateFetch()`, suppress `make_request` if `mHttpForbiddenRetryCount > 0 && !mHttpForbiddenRetryTimer.hasExpired()`.
4. **Reset retry state on success or cache invalidation**:
   - Reset `mHttpForbiddenRetryCount = 0` when texture raw data is successfully received, in `clearFetchedResults()`, and in `setIsMissingAsset(false)`.

**Alternatives**:
- **Alternative 1: Immediate `setIsMissingAsset()` on terminal worker failure**:
  Remove the `mLastHttpGetStatus != LLCore::HttpStatus(HTTP_FORBIDDEN)` check in `llviewertexture.cpp` completely and allow `setIsMissingAsset()` to execute whenever the worker finishes without data. `LLTextureFetchWorker` already retries once internally (`mRetryAttempt == 0`) and on region change.
  - *Trade-off*: Simpler and avoids adding timer/counter state to `LLViewerFetchedTexture`. However, if simulator capability establishment takes longer than the single worker attempt during high-latency region crossings, textures may be prematurely blacklisted as missing for the remainder of the session.
- **Alternative 2: Global retry map in `LLViewerTextureList`**:
  Track 403 failure timestamps and retry counts in a centralized hash map on `LLViewerTextureList` instead of adding member variables to `LLViewerFetchedTexture`.
  - *Trade-off*: Keeps `sizeof(LLViewerFetchedTexture)` unchanged, but introduces map lookup and thread-synchronization overhead on every texture update pass and requires explicit cleanup when textures are destroyed.

**Files likely to change**:
- `indra/newview/llviewertexture.h`
- `indra/newview/llviewertexture.cpp`

**Tests to add or update**:
- Test with known 403 texture ID (`a548cd29-8bbe-af3b-363a-3f44c7e47e68`): verify that exactly 3 fetch attempts occur with backoff delays, after which `setIsMissingAsset()` is called and all subsequent fetch requests and log spam cease.
- Verify normal texture loading remains unaffected.
- Verify sim crossings and teleports still allow valid textures to recover without triggering permanent missing asset states prematurely.

## Risks & Considerations

- **Memory footprint**: Adding `U8 mHttpForbiddenRetryCount` and `LLFrameTimer mHttpForbiddenRetryTimer` (~16 bytes) to `LLViewerFetchedTexture`. With thousands of texture instances, total memory overhead is negligible (< 100 KB).
- **Latency on high-lag crossings**: If region capability acquisition takes longer than the cumulative backoff period (~3–7 seconds), a valid texture could eventually be marked missing. 3 retries with backoff provides sufficient breathing room while preventing unbounded loops.
- **Observer logging**: Replacing per-frame `LL_WARNS()` spam with structured, throttled retry logs improves log readability and disk I/O under bad network conditions.

## Open Questions

- None. The issue description and code evidence are fully consistent, and the bounded retry mechanism directly resolves the root cause.
