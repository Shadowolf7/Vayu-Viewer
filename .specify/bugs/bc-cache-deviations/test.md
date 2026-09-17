# Bug Verification: VayuBCTextureCache Architectural Deviations from CoolVL

- **Slug**: bc-cache-deviations
- **Tested**: 2026-09-16
- **Assessment**: ./assessment.md
- **Fix**: ./fix.md
- **Result**: partial

## Summary

The fix has been compiled and verified via automated unit testing. The canonical Release build (`vayu-bin` and package tarball) built cleanly without warnings or errors. Unit tests in `PROJECT_llimage_TEST_vayubctexturecache` passed 11/11, confirming that `clear()` and `writeEntry()` self-heal missing directory structures. Per repository invariants and Guardrail 122, result is held at `partial` until an in-world session confirms zero worker thread CPU thrashing under a live grid.

## Checks Performed

| Check | Command / Action | Result | Notes |
|-------|------------------|--------|-------|
| Unit tests (`test<1>` through `test<11>`) | `./build-Linux-ninja-perf/sharedlibs/Release/bin/PROJECT_llimage_TEST_vayubctexturecache` | pass | All 11 tests passed, including new self-healing write (`test<10>`) and self-healing clear (`test<11>`) |
| Release build & link (`vayu-bin`) | `./scripts/safe-build.sh cmake --build build-Linux-ninja-perf --config Release` | pass | Clean compilation, LTO link, and packaging (`Vayu_Test_26_4_0_63853_x86_64.tar.xz`) completed with exit code 0 |
| Static code & wiring verification | Cross-module code inspection (`llappviewer.cpp`, `vayubctexturecache.h/.cpp`, `llimageworker.cpp`) | pass | Verified directory self-healing hooks, non-destructive purge delegation, write failure warning logging, and pre-decode cache bypass |
| In-world reproduction (post-fix) | Startup cache clear (`Startup cache purge requested: ONCE`) and region teleport | not-run | Requires running GUI application in-world; ready for user testing |

## Output Excerpts

### Unit Test Execution
```text
Unit test group_started name=VayuBCTextureCache
Unit test group_completed name=VayuBCTextureCache
	Total Tests:	11
	Passed Tests:	11	YAY!! \o/
```

### Build & Package Summary
```text
[1/4] Building CXX object llimage/.../tests/vayubctexturecache_test.cpp.o
[2/4] Building CXX object llimage/.../vayubctexturecache.cpp.o
[3/4] Linking CXX executable sharedlibs/Release/bin/PROJECT_llimage_TEST_vayubctexturecache
[4/4] Linking CXX executable newview/Release/bin/vayu-bin
================ Created base package  Vayu_Test_26_4_0_63853_x86_64.tar.xz
```

## Residual Risks

- **Live grid timing**: While unit tests prove directory self-healing and decode bypass logic work in isolation, live simulator testing is needed to observe viewer frame rates and fast timers when entering heavily textured regions.

## Recommendation

Hold at **partial** until the user conducts a live session test. When launching the viewer with a cache purge, verify that `bccache/` is preserved and image decode threads remain near 0% CPU on cached textures. Once confirmed, close [Issue #94](https://github.com/Shadowolf7/Vayu-Viewer/issues/94) as fully resolved.
