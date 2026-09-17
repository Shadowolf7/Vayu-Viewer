# Bug Verification: Infinite Retry Loop on HTTP 403 Forbidden Texture Fetches

- **Slug**: texture-403-retry-loop
- **Tested**: 2026-09-16
- **Assessment**: ./assessment.md
- **Fix**: ./fix.md
- **Result**: partial

## Summary

Static code analysis, symbol verification, and git diff checks have passed: `mHttpForbiddenRetryCount` and `mHttpForbiddenRetryTimer` are properly wired across `init()`, `processFetchResults()`, `updateFetch()`, `clearFetchedResults()`, and `setIsMissingAsset()`. Automated compilation, unit test suites, and live in-world reproduction are downgraded to `partial` (not-run) in strict accordance with the repository build and launch prohibitions (`GEMINI.md`).

## Checks Performed

| Check | Command / Action | Result | Notes |
|-------|------------------|--------|-------|
| Reproduction (post-fix) | In-world login to region with 403 texture ID | not-run | Requires compiled `vayu-bin` and live grid session; pending explicit user build instruction |
| Git diff whitespace & format check | `git diff --check` | pass | Clean diff with zero whitespace errors or formatting regressions |
| Symbol & signature cross-reference | Header/source inspection (`llviewertexture.h`, `llviewertexture.cpp`, `lltimer.h`) | pass | Member declarations, `LLFrameTimer` methods (`reset`, `setTimerExpirySec`, `hasExpired`), and retry constants match signatures |
| Logic & state transition audit | Static code path analysis | pass | Verified that 403 responses schedule exponential backoff (1s, 2s) and terminate with `setIsMissingAsset()` on attempt 3; verified cooldown suppression in `updateFetch()` |
| Regression & test suite | Build & test runners (`ninja`, `ctest`) | not-run | Repository safety invariant prohibits autonomous compilation without explicit turn-by-turn user instruction |

## Output Excerpts

### Git Diff Check
```text
$ git diff --check
(exited with code 0 - no whitespace errors)
```

### Static Logic Verification
- **Init & Reset**:
  - `LLViewerFetchedTexture::init()` initializes `mHttpForbiddenRetryCount = 0`.
  - Reception of valid image data (`getDataSize() > 0 && mRawDiscardLevel >= 0`) resets `mHttpForbiddenRetryCount = 0`.
  - `clearFetchedResults()` and `setIsMissingAsset(false)` reset `mHttpForbiddenRetryCount = 0`.
- **403 Failure & Backoff**:
  - In `processFetchResults()`, `mLastHttpGetStatus == HTTP_FORBIDDEN` increments `mHttpForbiddenRetryCount`.
  - Attempts 1 and 2 set `mHttpForbiddenRetryTimer` expiry to `1.0s` and `2.0s` via exponential backoff and log scheduled retries.
  - Attempt 3 hits `MAX_HTTP_FORBIDDEN_RETRIES = 3`, logs retries exhausted, and invokes `setIsMissingAsset()`.
  - Non-403 errors (e.g. 404) immediately invoke `setIsMissingAsset()`.
- **Request Suppression**:
  - In `updateFetch()`, when `!mIsFetching && mHttpForbiddenRetryCount > 0 && !mHttpForbiddenRetryTimer.hasExpired()`, the function exits early (`return false;`).
  - During request generation, `make_request` is explicitly set to `false` while the 403 cooldown is unexpired.

## Residual Risks

- **Sim-crossing latency spikes**: If simulator capability propagation takes longer than the cumulative backoff window (~3–7 seconds) under heavy network congestion, a valid texture could reach the retry threshold and be marked missing. 3 attempts with backoff balances protection against infinite loops with ample time for normal capability establishment.
- **In-world verification pending**: Behavioral verification against live CDN texture `a548cd29-8bbe-af3b-363a-3f44c7e47e68` requires user authorization to compile and run the viewer.

## Recommendation

Hold at **partial** until the user explicitly authorizes a build and conducts an in-world session with a 403-yielding asset. Once in-world log inspection confirms that retries 1 and 2 delay via cooldown, attempt 3 marks the texture as missing, and subsequent per-frame logging/network requests cease completely, close [Issue #95](https://github.com/Shadowolf7/Vayu-Viewer/issues/95) as fully verified.
