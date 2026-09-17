# Bug Verification: Automated Sim-Crossing Visibility and Attachment Recovery

- **Slug**: coolvl-crossing-refresh
- **Tested**: 2026-09-11
- **Assessment**: ./assessment.md
- **Fix**: ./fix.md
- **Result**: partial

## Summary

Static code and configuration checks passed: the automated asynchronous scheduling engine, timer controls, dirty flag updates, and region-change hooks were verified across all 6 target files, and `settings.xml` syntax parsed cleanly. In-world live simulator crossing testing is downgraded to `partial` pending compilation and an in-world driving session.

## Checks Performed

| Check | Command / Action | Result | Notes |
|-------|------------------|--------|-------|
| Reproduction (post-fix) | In-world region crossing on vehicle | not-run | Requires compiled `vayu-bin` and live grid connection; pending user build instruction |
| Settings XML validation | `python3 -c "import xml.etree.ElementTree as ET; ET.parse('indra/newview/app_settings/settings.xml')"` | pass | `settings.xml` parses with valid XML syntax and well-formed elements |
| Symbol & signature cross-reference | Header/source inspection (`llviewermenu.h`, `llviewermenu.cpp`, `llagent.cpp`, `llstartup.cpp`) | pass | Enum `eRegionChangeType`, function prototypes, and callback bindings match signatures across modules |
| Regression suite | Build & test runners | not-run | Repository safety invariant prohibits autonomous compilation without explicit turn-by-turn user instruction |

## Output Excerpts

### XML Validation Check
```text
python3 -c "import xml.etree.ElementTree as ET; ET.parse('indra/newview/app_settings/settings.xml'); print('XML valid')"
XML valid
```

### Static Wiring Check
- `LLAgent::setRegion()` (`llagent.cpp`): Invokes `schedule_objects_visibility_refresh(mTeleportState == TELEPORT_NONE ? AFTER_CROSS_BORDER : AFTER_FAR_TP)` when transitioning into a new region.
- `schedule_objects_visibility_refresh()` (`llviewermenu.cpp`): Clamps delay (0.5s–10s) and defers callback via `doAfterInterval()`.
- `handle_objects_visibility()` (`llviewermenu.cpp`): Checks `LLApp::isExiting()`, logs refresh, executes `gObjectList.refreshAllObjects()`, and conditionally invokes `handle_refresh_attachments()`.
- `LLViewerObjectList::refreshAllObjects()` (`llviewerobjectlist.cpp`): Flags all live objects with `objectp->markForUpdate(true)` and marks drawables for rebuild.

## Residual Risks

- **Server-side timing variance**: Extreme region lag during handoff might exceed the 2-second default delay. Users can tune `VisibilityAutoRefreshBorder` (up to 10s) via Debug Settings if needed.
- **In-world validation needed**: Full behavioral validation requires logging into Second Life and driving across region borders to observe visual object and attachment persistence.

## Recommendation

Hold at **partial** until the user explicitly builds the viewer and conducts an in-world driving test across sim boundaries. Once in-world logging confirms `Refreshing objects visibility after sim border crossing` fires and objects/attachments properly persist, close the bug as fully verified.
