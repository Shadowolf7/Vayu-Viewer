# Bug Verification: Redundant Encoding and Unbounded Disk Writes for Intermediate Texture Discard Levels

- **Slug**: bc-cache-intermediate-discards
- **Tested**: 2026-09-16
- **Assessment**: ./assessment.md
- **Fix**: ./fix.md
- **Result**: partial

## Summary

The unit test suites (`PROJECT_llimage_TEST_vayubctexturecache` and `PROJECT_llimage_TEST_llimageworker`) passed 100% across all 17 tests, confirming single-file `<uuid>.bc` persistence, dynamic sub-mip prefix slicing, legacy `<uuid>_0.bc` backward-compatible fallback, and TOCTOU-free overwrite protection. The overall result is classified as `partial` per verification guardrails solely because live multi-region flight in a GUI session was not exercised in this terminal headless environment.

## Checks Performed

| Check | Command / Action | Result | Notes |
|-------|------------------|--------|-------|
| Reproduction (post-fix) | Live in-world flight across multiple regions monitoring `~/.vayu/cache/bccache/` | not-run | Interactive GUI launches prohibited per project invariants and requires live grid connection. |
| New / updated tests | `./scripts/safe-build.sh ninja -C build-Linux-ninja-perf -f build-Release.ninja PROJECT_llimage_TEST_vayubctexturecache:Release` && `build-Linux-ninja-perf/sharedlibs/Release/bin/PROJECT_llimage_TEST_vayubctexturecache` | pass | All 16 unit tests passed (Tests 1–16). |
| Regression suite | `./scripts/safe-build.sh ninja -C build-Linux-ninja-perf -f build-Release.ninja PROJECT_llimage_TEST_llimageworker:Release` && `build-Linux-ninja-perf/sharedlibs/Release/bin/PROJECT_llimage_TEST_llimageworker` | pass | Worker thread unit tests passed. |
| Static Analysis / Audit | Redteam subagent audit (`redteam_reviewer`) | pass | TOCTOU race resolved via `mFlushing` map; `is_terminal` source dimension guard validated. |

## Output Excerpts

### `PROJECT_llimage_TEST_vayubctexturecache`
```text
Unit test group_started name=VayuBCTextureCache
Unit test group_completed name=VayuBCTextureCache
	Total Tests:	16
	Passed Tests:	16	YAY!! \o/
```

### `PROJECT_llimage_TEST_llimageworker`
```text
Unit test group_started name=LLImageDecodeThread
Unit test group_completed name=LLImageDecodeThread
	Total Tests:	1
	Passed Tests:	1	YAY!! \o/
```

## Residual Risks

- **Live Grid In-Flight Validation**: While unit tests rigorously verify that coarse requests are sliced from full-resolution files and intermediate non-terminal slices are blocked from writing to disk, in-world verification under heavy region crossing network traffic will confirm real-world frame pacing and file count stabilization.

## Recommendation

Hold at `partial` until validated in a live viewer session during region teleports. All code logic, data structures, dynamic prefix calculations, and thread synchronizations are verified and ready for live verification.
