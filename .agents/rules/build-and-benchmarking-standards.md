---
description: Build commands, Ninja Multi-Config Release invariants, and profiling/benchmarking protocols for Vayu-Viewer.
globs: ["**/CMakeLists.txt", "**/*.cmake", "**/BUILD.md"]
always_on: true
---

# Vayu-Viewer Build & Benchmarking Invariants

Strictly adhere to the following build and benchmarking protocols:

## 1. Strict Canonical Build Procedures (Ninja Multi-Config)
* **Never invoke plain `ninja` without an explicit config flag.** The project uses CMake with `Ninja Multi-Config`. Plain `ninja` defaults to `RelWithDebInfo` (generating an unoptimized ~1GB debug binary and leaving `Release/` stale).
* **Canonical Build Commands:**
  - **Release component build:** `ninja -C build-Linux-ninja-perf -f build-Release.ninja <target>:Release`
    *(or `cmake --build build-Linux-ninja-perf --config Release --target <target>`)*
  - **Release full package:** `ninja -C build-Linux-ninja-perf -f build-Release.ninja vayu-bin:Release`
    *(or `cmake --build build-Linux-ninja-perf --config Release`)*
* **Never launch or benchmark from `RelWithDebInfo/` for profiling or performance verification.**

## 2. Pre-Launch & Benchmark Invariants
* Before launching any build for testing or Tracy profiling:
  1. **Verify Binary Freshness:** Run `ls -l <build_dir>/newview/Release/bin/vayu-bin` and mathematically verify the modification timestamp is newer than the latest edited source file.
  2. **Verify Process Isolation:** Ensure no other viewer instances (`vayu`, `alchemy`, `secondlife`) or background compilations (`ninja`, `cmake`) are running before launching or recording.
  3. **Launch Explicit Release Executable:** Always launch the packaged wrapper at `<build_dir>/newview/Release/vayu` (or installed baseline at `~/.vayu-install/vayu`).

## 3. Interactive Process & Launch Control Invariants
* **Never launch an interactive GUI application (the viewer) without explicit, turn-by-turn user confirmation.**
  - Building/compiling in the background is acceptable.
  - Launching the interactive viewer (`vayu`, `vayu-bin`) MUST always be explicitly requested or confirmed by the user in that specific turn.
* **Strict Prohibition on Autonomous Re-launches / Retries:**
  - If a benchmark, capture, or export fails, segfaults, or produces incomplete data, **STOP IMMEDIATELY**.
  - Report the exact error output and state to the user in plain text.
  - **Never** attempt to re-launch the application, retry a benchmark, or restart background recording without direct user instruction.

