# Bug Fix: Automated Sim-Crossing Visibility and Attachment Recovery

- **Slug**: coolvl-crossing-refresh
- **Fixed**: 2026-09-11
- **Assessment**: ./assessment.md
- **Status**: applied

## Summary

Wired up automated asynchronous post-region-crossing and post-teleport object visibility recovery and attachment re-synchronization ported from Henri Beauchamp's Cool VL Viewer. When arriving in a new region, `LLAgent::setRegion()` now automatically schedules `schedule_objects_visibility_refresh()` after a configurable delay (`VisibilityAutoRefreshBorder`, default 2 seconds), executing `refreshAllObjects()` and attachment rebuild/re-sync (`AutoRefreshAttachmentsInSL`) to prevent missing or unrezzed objects and attachment desyncs.

## Changes

| File | Change | Notes |
|------|--------|-------|
| `indra/newview/app_settings/settings.xml` | modified | Added `VisibilityAutoRefreshBorder` (default 2s), `VisibilityAutoRefreshFarTP` (default 2s), `VisibilityAutoRefreshLogin` (default 0s), and `AutoRefreshAttachmentsInSL` (default true) |
| `indra/newview/llviewermenu.h` | modified | Declared `eRegionChangeType` enum (`AFTER_LOGIN`, `AFTER_CROSS_BORDER`, `AFTER_FAR_TP`) and function prototypes `schedule_objects_visibility_refresh()`, `handle_refresh_objects()`, `handle_refresh_attachments()` |
| `indra/newview/llviewermenu.cpp` | modified | Included `llcallbacklist.h`, implemented `schedule_objects_visibility_refresh()` with `doAfterInterval()` timer, and implemented `handle_objects_visibility()` to call `refreshAllObjects()` and `handle_refresh_attachments()` |
| `indra/newview/llviewerobjectlist.cpp` | modified | Added `objectp->markForUpdate(true)` in `refreshAllObjects()` object loop |
| `indra/newview/llagent.cpp` | modified | In `LLAgent::setRegion()`, schedule automated visibility refresh on region changes (`AFTER_CROSS_BORDER` when `mTeleportState == TELEPORT_NONE`, else `AFTER_FAR_TP`) |
| `indra/newview/llstartup.cpp` | modified | Scheduled `AFTER_LOGIN` visibility refresh when startup completes in `STATE_CLEANUP` |

## Diff Highlights

### Automated Scheduling in `LLAgent::setRegion`
```cpp
if (mRegionp)
{
    // Schedule automated visibility and attachment refresh on arrival in new region (ported from Cool VL).
    U32 sim_change_type = (mTeleportState == TELEPORT_NONE) ? AFTER_CROSS_BORDER : AFTER_FAR_TP;
    schedule_objects_visibility_refresh(sim_change_type);
}
```

### Delayed Timer Callback in `llviewermenu.cpp`
```cpp
void schedule_objects_visibility_refresh(U32 type)
{
    static LLCachedControl<U32> login_delay(gSavedSettings, "VisibilityAutoRefreshLogin");
    static LLCachedControl<U32> cross_delay(gSavedSettings, "VisibilityAutoRefreshBorder");
    static LLCachedControl<U32> tp_delay(gSavedSettings, "VisibilityAutoRefreshFarTP");

    F32 delay = (type == AFTER_CROSS_BORDER) ? (F32)cross_delay :
                (type == AFTER_FAR_TP) ? (F32)tp_delay : (F32)login_delay;

    if (delay <= 0.f || !LLStartUp::isLoggedIn()) return;

    doAfterInterval(std::bind(handle_objects_visibility, (void*)((intptr_t)type)),
                    llclamp(delay, 0.5f, 10.f));
}
```

## Tests Added or Updated

- Verified setting definitions in `settings.xml` parse valid XML and define appropriate types (`U32` for delays, `Boolean` for attachment refresh).
- Verified enum and signature linkage across `llviewermenu.h`, `llagent.cpp`, and `llstartup.cpp`.
- Manual in-world crossing verification pending compilation and run:
  - Verify timer logs `Refreshing objects visibility after sim border crossing` 2.0s after crossing.
  - Verify attachment re-sync logs in `LLAttachmentsMgr`.

## Local Verification

- Static syntax and structural verification across all 6 modified files.
- Verified XML validity of `settings.xml`.
- Note: Compilation and building of `vayu-bin` strictly withheld per repository safety policy requiring explicit turn-by-turn user build instruction.

## Deviations from Assessment

None. All remediation items planned in `assessment.md` were implemented as specified.

## Follow-ups

- Once built and tested, user can run `/speckit-bug-test slug=coolvl-crossing-refresh` to validate the in-world behavior and record verification.
