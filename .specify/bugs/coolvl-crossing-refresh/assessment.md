# Bug Assessment: Missing Automated Sim-Crossing Visibility and Attachment Recovery

- **Slug**: coolvl-crossing-refresh
- **Created**: 2026-09-11
- **Source**: pasted text
- **Verdict**: valid
- **Severity**: high

## Report (verbatim or summarized)

> In earlier development and public documentation (`gh-pages/index.md`), Vayu stated: *"I've merged the best sim-crossing protection features from Firestorm and CoolVL courtesy of the amazing work by Animats and Henri Beauchamp respectively."*
>
> While Firestorm's adaptive velocity filter and follow-cam damping were integrated, Cool VL's automated sim-crossing recovery mechanisms (Henri Beauchamp's `VisibilityAutoRefreshBorder` and `AutoRefreshAttachmentsInSL`) were never fully ported. In commit `3ef8f7ba4b`, `gObjectList.refreshAllObjects()` was added solely as a manual menu action (**Advanced → Refresh Objects**), leaving the automated post-crossing trigger entirely absent.

## Symptom

When crossing region boundaries (especially in vehicles or at speed), objects in the arrival simulator frequently fail to rez, remain invisible, or get trapped in stale spatial partitions until a manual camera zoom or LOD shift forces a partition update. Additionally, worn avatar attachments can desync or become detached ("ghost attachments") upon arrival. 

The expected behavior (matching Cool VL Viewer) is an automatic, timed refresh 2 seconds after crossing that forces partition updates on all drawables and resynchronizes worn attachments without requiring manual user menu interventions.

## Reproduction

1. Log into Second Life in a vehicle (car, boat, or aircraft).
2. Drive or fly across a region border into an adjacent populated region.
3. Observe that several objects, obstacles, or prims in the arrival region fail to render or pop in late until the camera moves significantly or the user manually clicks **Advanced → Refresh Objects**.
4. Observe occasional attachment desyncs on the avatar or passengers.

## Suspected Code Paths

- `indra/newview/llagent.cpp:1076-1130`: `LLAgent::setRegion()` handles the transition into a new region. It updates origins and camera offsets but lacks any invocation of `schedule_objects_visibility_refresh()` when `mTeleportState == TELEPORT_NONE`.
- `indra/newview/llviewermenu.cpp:2333-2345`: Contains `handle_refresh_attachments()` and `handle_refresh_objects()`, but these are only exposed as static manual menu callbacks rather than scheduled asynchronous events.
- `indra/newview/llviewerobjectlist.cpp:1793-1807`: `LLViewerObjectList::refreshAllObjects()` performs `repartitionObjects()` and drawable extents/partition updates, but is never invoked automatically on crossing.
- `indra/newview/app_settings/settings.xml`: Missing `VisibilityAutoRefreshBorder` (delay in seconds) and `AutoRefreshAttachmentsInSL` (enable toggle) settings.

## Root Cause Hypothesis

In commit `3ef8f7ba4b`, the author ported Henri Beauchamp's partition-rebuilding logic into `LLViewerObjectList::refreshAllObjects()` and exposed it under the **Advanced** menu as a manual diagnostic tool. However, the author failed to wire `schedule_objects_visibility_refresh(AFTER_CROSS_BORDER)` into `LLAgent::setRegion()`, omitted the configurable timer (`VisibilityAutoRefreshBorder`), and omitted the post-crossing attachment recovery (`AutoRefreshAttachmentsInSL`). Consequently, what was supposed to be an automated background recovery system across sim boundaries was left as a dead-end manual menu button.
- *Confidence*: High (verified directly against Cool VL's `llagent.cpp:1101-1106`, `llviewermenu.cpp:3070-3099`, and `settings.xml`).

## Proposed Remediation

**Preferred**:
1. **Define Settings in `settings.xml`**:
   - `VisibilityAutoRefreshBorder`: U32, default `2` (range 0–10 seconds, 0 = disabled). Controls the delay after crossing before refreshing object visibility.
   - `AutoRefreshAttachmentsInSL`: Boolean, default `true`. Re-syncs worn attachments after border crossings and teleports.
   - `VisibilityAutoRefreshFarTP`: U32, default `2` (range 0–10 seconds, 0 = disabled). Same refresh for teleports beyond draw distance.
2. **Implement Async Scheduling in `llviewermenu.cpp` / `llagent.cpp`**:
   - Add `schedule_objects_visibility_refresh(U32 type)` with types `AFTER_CROSS_BORDER` and `AFTER_FAR_TP`.
   - Use `doAfterInterval()` from `llcallbacklist.h` to fire after the configured `VisibilityAutoRefreshBorder` seconds (clamped to 10s max).
3. **Trigger from `LLAgent::setRegion()`**:
   - Inside `LLAgent::setRegion()`, when `mRegionp != regionp` and `mRegionp != nullptr` (i.e. changing regions, not initial login):
     - Check `mTeleportState == TELEPORT_NONE ? AFTER_CROSS_BORDER : AFTER_FAR_TP` and call `schedule_objects_visibility_refresh()`.
4. **Execute Post-Crossing Refresh Callback**:
   - Call `gObjectList.refreshAllObjects()` to update spatial partitions and trigger drawable rebuilds.
   - If `AutoRefreshAttachmentsInSL` is enabled, call `handle_refresh_attachments()` to refresh attachment state.

**Alternatives**:
- *Immediate execution without timer*: Triggering `refreshAllObjects()` instantaneously inside `setRegion()` is prone to race conditions because the simulator object stream for the new region has only just begun. A 2-second delay allows incoming object packets and capabilities to settle first.

**Files likely to change**:
- `indra/newview/app_settings/settings.xml`
- `indra/newview/llagent.cpp`
- `indra/newview/llviewermenu.h`
- `indra/newview/llviewermenu.cpp`

**Tests to add or update**:
- Test timer triggering: Cross region boundary in vehicle; verify via debug logging that `schedule_objects_visibility_refresh` queues and executes 2.0s later.
- Verify setting `VisibilityAutoRefreshBorder = 0` cleanly disables the automated refresh.
- Verify setting persistence in `settings.xml`.

## Risks & Considerations

- **Frame hitch during batch update**: Running `refreshAllObjects()` iterates all live objects. In crowded regions with 10,000+ objects, `repartitionObjects()` could cause a minor frame hitch. However, running it 2 seconds *after* the crossing moves it outside the critical handoff transition window.
- **Safety**: `doAfterInterval()` must check that the agent is still logged in and alive when the callback fires to avoid accessing invalid pointers if the user crashes or disconnects during the 2-second window.

## Open Questions

- Should `VisibilityAutoRefreshBorder` also be exposed as a slider in Preferences (**Preferences → Move → General** alongside the other region crossing settings), or kept in the Advanced/Debug Settings?
