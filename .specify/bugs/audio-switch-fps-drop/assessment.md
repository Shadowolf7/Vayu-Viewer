# Bug Assessment: Framerate drops to ~1 fps after switching the audio output device

- **Slug**: audio-switch-fps-drop
- **Created**: 2026-09-21
- **Source**: pasted text
- **Verdict**: likely valid, needs reproduction
- **Severity**: medium

## Report (verbatim or summarized)

Report from Tron Udal on Vayu Beta 26.4.0.1 (release line 26.4.0, matches current `indra/newview/VIEWER_VERSION.txt` = `26.4.0`):

> Switched my audio device … framerate dropped to 1 fps. Mostly running great otherwise.

Single-symptom report: after changing the audio output device, the viewer degrades to ~1 frame per second and, per the wording, stays there (not a one-frame hitch). No accompanying crash, no logs.

**Environment (clarified 2026-09-21):** Windows user; the beta was built **without FMOD**. In these builds `AL_USE_OPENAL=OFF` as well (`build-Linux-ninja-perf/CMakeCache.txt`), so the **only** compiled audio backend is Vayu's custom FAudio engine (`llaudioengine_faudio`). The FMOD code paths identified below are therefore irrelevant to this report and are kept only as an audit note.

## Symptom

After the user switches the audio output device (either via the Sound preferences output-device dropdown or via an OS-level default-device change), the main loop collapses to ~1 fps. Expected: the audio swap completes with, at most, a brief audio glitch and the framerate is unaffected.

## Reproduction

Unknown; the report is a single user observation. Suspected trigger paths:

1. Open Sound → output-device combo, pick a different device (commits via `gAudiop->setOutputDevice(id)`), observe framerate. [NEEDS CLARIFICATION: was the switch in-app or OS-level?]
2. Change the OS default audio device while the viewer is running (plug/unplug HDMI or headphones, or set a new default sink/endpoint). [NEEDS CLARIFICATION]
3. Platform is Windows (confirmed). Engine is FAudio (only backend compiled).

## Suspected Code Paths

- `indra/newview/llappviewer.cpp:5950` — `gAudiop->idle()` runs on the main thread every frame (`LLAudioEngine::idle()` at `indra/llaudio/llaudioengine.cpp:251`: per-channel `updateBuffer()` / `update3DPosition()` / `updateLoop()`). Any per-frame audio call that now blocks ~1 s collapses the framerate to ~1 fps.
- `indra/llaudio/llaudioengine_faudio.cpp:828` (`setOutputDevice`) → `releaseFAudioDevice()` (766) + `initFAudioDevice()` (280). Vayu's custom FAudio backend tears down and rebuilds the *entire* voice graph (all channels, wind, reverb, mastering voice) synchronously on the main thread. The Windows `FAudio_PlatformQuit` path (`vcpkg/buildtrees/faudio/src/26.06-26cb67ac0b.clean/src/FAudio_platform_win32.c:477-481`) does `SetEvent(stopEvent)` then `WaitForSingleObject(audioThread, INFINITE)`.
- `indra/llaudio/llaudioengine_faudio.cpp` — the FAudio engine has **no `idle()` override and none of the device-loss machinery** that the OpenAL backend has. Its Windows WASAPI audio thread exits silently when the endpoint is invalidated (`FAudio_platform_win32.c:187-196` — `AUDCLNT_E_DEVICE_INVALIDATED` → `break`), leaving the engine to keep calling into a dead device with no recovery, no callback, no poll.
- `vcpkg/buildtrees/faudio/src/26.06-26cb67ac0b.clean/src/FAudio_platform_win32.c:213` (`FAudio_DefaultDeviceIndex`) — device enumeration on Windows walks the WASAPI device collection (`actives`/`unplugged`); a mid-switch OS device state can make these COM calls take far longer than normal, and they are invoked from the main thread by `enumerateOutputDevices()` (`llaudioengine_faudio.cpp:196`) and again by `setOutputDevice()`.
- `indra/llaudio/llaudioengine_faudio.cpp:1102` (`updateWind`) — per-frame main-thread wind chunk top-up (`while (depth < MAX_WIND_QUEUED)`) with `windGenerate()` synthesizing each 0.05 s stereo chunk inline. Bounded and cheap in the steady state, but if the wind voice is recreated across a device reset and the OnBufferEnd callback chain is interrupted, this loop re-fires every frame.

## Root Cause Hypothesis

Confidence: **low–medium**. The decisive gap is asymmetry between the backends:

- The **OpenAL** engine (`indra/llaudio/llaudioengine_openal.cpp:488`) has a device-invalidation model: `idle()` polls `ALC_CONNECTED` (once per ~2 s) and a device-changed event (`mDefaultDeviceChanged` → `reopenOnDefaultDevice()`) triggers full recovery — but it is **not compiled** in these builds.
- The **FAudio** engine, the only backend in a no-FMOD build, has *nothing* — no `idle()` override, no device-change callback registration (no `FAudio_RegisterForCallbacks`/`OnCriticalError` wiring anywhere in `indra/`), and the underlying WASAPI thread silently exits when the endpoint is invalidated. On an OS-level device switch this leaves the engine playing into a dead device with no recovery; on the in-app path `setOutputDevice` performs a heavy synchronous teardown+rebuild on the main thread.

A sustained ~1 fps is the signature of the main thread blocking ~1 s on *every* frame, i.e. the per-frame `gAudiop->idle()` re-entering a blocking backend call after the swap. The most defensible candidate mechanism on the confirmed Windows+FAudio configuration is that **FAudio's WASAPI audio thread exits on device invalidation while the engine keeps calling into it per frame** — and on Windows a device switch (OS-level default change, or a USB/HDMI endpoint appearing or vanishing) also perturbs the WASAPI device enumeration that `enumerateOutputDevices()`/`setOutputDevice()` walk synchronously on the main thread. This needs a profile or a log to confirm — the report alone can't distinguish the exact blocking call.

## Proposed Remediation

**Preferred**: reproduce first, then harden. Concretely, in order:

1. Confirm with a log/trace: add an explicit `LL_INFOS` of which backend won at `indra/newview/llstartup.cpp:820-880` (that line range exists in the FMOD-gated block; ensure an equivalent unconditional log for FAudio), and ask the reporter whether switching back / restarting recovers it.
2. Give the FAudio engine the device-loss detection + recovery the OpenAL backend already has: add an `idle()` override that (a) detects the invalidated device (e.g. re-check the live default endpoint on a ~2 s poll, or hook a device-change notification via the Windows `IMMNotificationClient` if exposed by the platform layer) and (b) automatically re-runs `releaseFAudioDevice()` + `initFAudioDevice()` rather than leaving the engine pointed at a dead endpoint. Also verify the sync teardown in `setOutputDevice` isn't the blocker under Tracy.
3. Bound the per-frame audio budget: log/sample `gAudiop->idle()` cost so an anomalous spike is diagnosable from `LogOut.txt` in the field — the reported ~1s/frame stall would then be attributable to a specific backend call.

**Files likely to change**:
- `indra/llaudio/llaudioengine_faudio.{h,cpp}` — add `idle()` device-loss detection/recovery (OpenAL parity).
- `indra/llaudio/llaudioengine_openal.cpp` — reference implementation to mirror.
- `indra/newview/llstartup.cpp` — engine-selection log line for diagnosability.

**Tests to add or update**:
- A unit/integration check that `setOutputDevice` toggling between enumerated devices does not change the main-thread frame cost beyond a bounded budget (guards against a regression to a blocking idle path).
- A scripted backend test (if feasible under CI audio devices) that simulates endpoint invalidation and asserts FAudio auto-recovers by re-initing within a few seconds.

## Risks & Considerations

- The fix touches Vayu's flagship custom audio backend (FAudio); a heavy `idle()` poll could itself cost frames if implemented naively — the OpenAL pattern (2 s interval, debounced, edge-detected) is the safe template.
- `setOutputDevice` currently does a full voice-graph teardown on the main thread; making that async or deferred changes observable behavior (sound momentarily stops) and needs care with the wind/loop-resume bookkeeping already present (`prepareForDeviceReset` → rebuild).
- Since FMOD is not built in the affected beta, FMOD-side workarounds are out of scope for this report; the FAudio engine is the entire surface.
- No data-loss / crash risk identified; worst case user restarts the viewer.

## Open Questions

- [NEEDS CLARIFICATION: was the device switched in the Sound prefs dropdown or in the OS (default device change / plug-unplug)?]
- [NEEDS CLARIFICATION: does switching back to the previous device (or restarting the viewer) restore the framerate?]
- [NEEDS CLARIFICATION: does the 1 fps drop persist indefinitely, or recover after some seconds/minutes?]
- [NEEDS CLARIFICATION: does any audio still play after the switch (dead-device case), or does it go silent / stutter?]
- [NEEDS CLARIFICATION: which specific device was involved (HDMI / USB DAC / onboard / virtual cable), if known?]