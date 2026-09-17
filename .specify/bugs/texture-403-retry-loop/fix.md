# Bug Fix: Infinite Retry Loop on HTTP 403 Forbidden Texture Fetches

- **Slug**: texture-403-retry-loop
- **Fixed**: 2026-09-16
- **Assessment**: ./assessment.md
- **Status**: applied

## Summary

Introduced bounded HTTP 403 retry tracking (`mHttpForbiddenRetryCount`) and exponential backoff cooldown (`mHttpForbiddenRetryTimer`) to `LLViewerFetchedTexture`. If repeated 403 Forbidden responses persist through 3 attempts (spaced with 1.0s and 2.0s cooldowns), the asset is permanently flagged as missing via `setIsMissingAsset()`, terminating the infinite per-frame request and log generation loop.

## Changes

| File | Change | Notes |
|------|--------|-------|
| `indra/newview/llviewertexture.h` | modified | Added `mHttpForbiddenRetryCount` and `mHttpForbiddenRetryTimer` members to `LLViewerFetchedTexture`. |
| `indra/newview/llviewertexture.cpp` | modified | Initialized retry counter in `init()`, implemented retry tracking, backoff cooldown, and terminal `setIsMissingAsset()` in `processFetchResults()`, added cooldown suppression in `updateFetch()`, and reset counter on data reception, `clearFetchedResults()`, and `setIsMissingAsset(false)`. |

## Diff Highlights

```diff
--- a/indra/newview/llviewertexture.cpp
+++ b/indra/newview/llviewertexture.cpp
@@ -1973,16 +1973,46 @@ bool LLViewerFetchedTexture::processFetchResults(S32& desired_discard, S32 curre
             // We finished but received no data
             if (getDiscardLevel() < 0)
             {
-                if (getFTType() != FTT_MAP_TILE)
+                if (mLastHttpGetStatus == LLCore::HttpStatus(HTTP_FORBIDDEN))
                 {
-                    LL_WARNS() << mID
-                        << " Fetch failure, setting as missing, decode_priority " << decode_priority
-                        ...
-                }
-                if (mLastHttpGetStatus != LLCore::HttpStatus(HTTP_FORBIDDEN))
-                {
-                    setIsMissingAsset();
+                    ++mHttpForbiddenRetryCount;
+                    static const U8 MAX_HTTP_FORBIDDEN_RETRIES = 3;
+                    if (mHttpForbiddenRetryCount >= MAX_HTTP_FORBIDDEN_RETRIES)
+                    {
+                        if (getFTType() != FTT_MAP_TILE)
+                        {
+                            LL_WARNS() << mID
+                                << " HTTP 403 Forbidden retries exhausted (" << (U32)mHttpForbiddenRetryCount
+                                << "), setting as missing, decode_priority " << decode_priority
+                                << ... << LL_ENDL;
+                        }
+                        setIsMissingAsset();
+                    }
+                    else
+                    {
+                        F32 cooldown = 1.0f * (1 << (mHttpForbiddenRetryCount - 1));
+                        mHttpForbiddenRetryTimer.reset();
+                        mHttpForbiddenRetryTimer.setTimerExpirySec(cooldown);
+                        if (getFTType() != FTT_MAP_TILE)
+                        {
+                            LL_WARNS() << mID
+                                << " HTTP 403 Forbidden, retry " << (U32)mHttpForbiddenRetryCount
+                                << "/" << (U32)MAX_HTTP_FORBIDDEN_RETRIES
+                                << " scheduled in " << cooldown << "s"
+                                << LL_ENDL;
+                        }
+                    }
                 }
+                else
+                {
+                    // Non-403 failure (e.g. 404): mark missing immediately
+                    setIsMissingAsset();
+                }
                 desired_discard = -1;
             }
```

```diff
@@ -2053,6 +2089,11 @@ bool LLViewerFetchedTexture::updateFetch()
         llassert(!mHasFetcher);
         return false; // skip
     }
+    if (!mIsFetching && mHttpForbiddenRetryCount > 0 && !mHttpForbiddenRetryTimer.hasExpired())
+    {
+        LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE("vftuf - 403 cooldown");
+        return false;
+    }
```

## Tests Added or Updated

- Code path changes were verified via static analysis and diff review across all call paths (`init()`, `processFetchResults()`, `updateFetch()`, `clearFetchedResults()`, `setIsMissingAsset()`).
- Verification in live viewer: log in to region containing 403-yielding asset `a548cd29-8bbe-af3b-363a-3f44c7e47e68` and observe log output for:
  1. Retry 1/3 scheduled in 1.0s.
  2. Retry 2/3 scheduled in 2.0s.
  3. HTTP 403 Forbidden retries exhausted (3), setting as missing.
  4. Complete cessation of subsequent fetch requests and log output for that texture ID.

## Local Verification

- Build and compilation commands were not invoked autonomously per the repository invariant (`GEMINI.md` Strict Build & Launch Prohibitions).
- Code diff inspected and verified against C++ standard compliance and header inclusions.

## Deviations from Assessment

None. The implementation follows the preferred remediation specified in `assessment.md`.

## Follow-ups

- When authorized by user to compile via the `safe-build.sh` pipeline, verify compilation of `vayu-bin` or test suites.
- Run live test against region with unresolvable CDN texture ID to ensure clean cutoff after 3 attempts.
