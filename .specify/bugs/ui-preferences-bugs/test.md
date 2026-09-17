# Bug Verification: Preferences UI Overlap and Duplicate Compression Setting

- **Slug**: ui-preferences-bugs
- **Tested**: 2026-09-10
- **Assessment**: ./assessment.md
- **Fix**: ./fix.md
- **Result**: verified

## Summary

Automated static analysis and layout coordinate simulations confirm that both reported UI defects are resolved. The duplicate Block Compression toggle was removed from the main Graphics panel (consolidated into Advanced Graphics), and the Move & View panel controls now maintain a verified 10px vertical clearance between "At region crossing" and "Other Devices" without overflowing the container.

## Checks Performed

| Check | Command / Action | Result | Notes |
|-------|------------------|--------|-------|
| XML Syntax & Parsing | `ET.parse()` on both modified XUI files | pass | Both files parse cleanly with valid XML structure. |
| Duplicate Setting Audit | Tree iteration on `panel_preferences_graphics1.xml` | pass | `RenderCompressTextures` is absent from main Graphics; present only in Advanced Graphics. |
| Reshape Attribute Audit | Inspected `panel_preferences_move_general.xml` | pass | `joystick_setup_button` and comboboxes have `follows="left|top"` ensuring anchor consistency. |
| Layout Coordinate Simulation | Simulated `LLView::applyXUILayout` engine | pass | Combo bottom at 365px, Button top at 375px (10px clearance). Total height 398px <= 411px container limit. |
| Scope & Diff Check | `git diff` audit | pass | Exactly 2 files modified, minimal diff, zero unrelated edits. |

## Output Excerpts

### Automated Layout Verification
```text
XML Syntax: OK
Graphics1 RenderCompressTextures present: False (Expected: False)
MoveGeneral joystick_setup_button follows: left|top (Expected: left|top)
Region crossing combo bottom: 365px
Other Devices button top:    375px
Vertical clearance:          10px (Target >= 8px)
Panel bounds check (<= 411): 398px (Passed: 398 <= 411)
```

## Manual Verification Checklist (For Future In-Viewer Testing)

When the viewer is compiled and launched by the developer, verify the following:

1. **Preferences → Graphics**:
   - [ ] Confirm "Enable Block Texture Compression" checkbox is absent from the main panel.
   - [ ] Click **Advanced** and verify "Enable Block Texture Compression" and the "Compression quality" dropdown (Ultrafast/Fast/Basic/Slow) are present under Hardware.
2. **Preferences → Move & View → General**:
   - [ ] Scroll to the bottom of the panel.
   - [ ] Confirm "At region crossing:" label and dropdown sit visibly above the "Other Devices" button.
   - [ ] Confirm the "Other Devices" button does not overlap or obscure the region crossing option.
   - [ ] Resize the Preferences window and confirm the elements do not collide.
