# Bug Assessment: Preferences UI Overlap and Duplicate Compression Setting

- **Slug**: ui-preferences-bugs
- **Created**: 2026-09-10
- **Source**: pasted text
- **Verdict**: valid
- **Severity**: medium

## Report (verbatim or summarized)

> "I noticed a bug in the UI, Block Compression is in two places and the Move and View pane has the 'at region corssing' option being covered by the Other Devices button."

## Symptom

1. **Duplicate Block Compression Control**: The "Enable Block Texture Compression" checkbox (`RenderCompressTextures`) appears in two separate places in Preferences:
   - In the main Graphics panel: Preferences → Graphics → General (`panel_preferences_graphics1.xml:396-407`).
   - In the Advanced Graphics floater: Preferences → Graphics → Advanced (`floater_preferences_graphics_advanced.xml:363-374`), alongside the quality preset combo box (`RenderCompressTexturesPreset`).

2. **UI Control Overlap in Move & View**: In Preferences → Move & View → General, the "Other Devices" button (`joystick_setup_button`) overlaps and covers the "At region crossing:" label and dropdown (`region_crossing_movement_lbl` / `region_crossing_movement_combo`).

## Reproduction

### Issue 1: Block Compression
1. Open Preferences (`Ctrl+P`) and navigate to the **Graphics** category.
2. Observe "Enable Block Texture Compression" checkbox under the display/hardware section.
3. Click **Advanced** to open the Advanced Graphics floater.
4. Observe "Enable Block Texture Compression" checkbox repeated under the Hardware section with the "Compression quality" dropdown.

### Issue 2: Move & View Overlap
1. Open Preferences (`Ctrl+P`) and navigate to **Move & View** → **General**.
2. Look at the bottom of the panel.
3. Observe that the "Other Devices" button overlaps/covers the "At region crossing:" option.

## Suspected Code Paths

- `indra/newview/skins/default/xui/en/panel_preferences_graphics1.xml:396-407`: First instance of `RenderCompressTextures` checkbox.
- `indra/newview/skins/default/xui/en/floater_preferences_graphics_advanced.xml:363-415`: Second instance of `RenderCompressTextures` checkbox and `RenderCompressTexturesPreset` combo box.
- `indra/newview/skins/default/xui/en/panel_preferences_move_general.xml:272-317`: `region_crossing_movement_lbl`, `region_crossing_movement_combo`, and `joystick_setup_button`.
- `indra/newview/skins/default/xui/en/panel_preferences_move.xml:12-35`: Tab container and panel enclosing `panel_preferences_move_general.xml`.

## Root Cause Hypothesis

- **Issue 1 (Duplicate Setting)**: Commit `8b50632aba` added `RenderCompressTextures` to the primary graphics tab (`panel_preferences_graphics1.xml`). Later, commit `af0d405e76` added texture compression presets to the Advanced Graphics floater (`floater_preferences_graphics_advanced.xml`) and duplicated the enable checkbox there without removing it from the primary panel or deciding on a single home.
  - *Confidence*: High.

- **Issue 2 (Layout Collision & Reshape Flags)**: In commit `3ef8f7ba4b3eb648a1d87bd1fd71e72badc06503`, the `region_crossing_movement` controls were added at the bottom of `panel_preferences_move_general.xml`. The `joystick_setup_button` was changed from legacy coordinate `top="30"` (anchored at bottom) to `top_pad="15"`, but lacked `follows="left|top"` and exceeded the available client height inside `moveview_tab_container` (~411px available vs ~431px required). Because `joystick_setup_button` has default `FOLLOWS_NONE` (bottom-anchored) while the preceding controls have `follows="left|top"`, reshaping/resizing the panel causes `Other Devices` to stay anchored to the bottom while the region crossing label and dropdown shift into it.
  - *Confidence*: High.

## Proposed Remediation

**Preferred**:
1. **Fix Move & View Layout**:
   - Give `joystick_setup_button` and `region_crossing_movement_combo` explicit `follows="left|top"`.
   - Compact vertical padding (`top_pad`) across the preceding checkbox/label groups in `panel_preferences_move_general.xml` (reclaiming ~25–30px total) so all controls comfortably sit above `joystick_setup_button` without clipping or colliding.
2. **Consolidate Block Compression**:
   - Determine preferred placement:
     - **Option A**: Keep the toggle in the main Graphics panel (`panel_preferences_graphics1.xml`) and move the preset dropdown there as well, removing the duplicate from Advanced Graphics.
     - **Option B**: Keep both the toggle and preset dropdown strictly in Advanced Graphics (`floater_preferences_graphics_advanced.xml`), freeing up space in the main Graphics tab.
     - **Option C**: Keep the simple toggle in main Graphics for quick user access, and keep the advanced preset tuning in Advanced Graphics (removing the redundant second checkbox).

**Files likely to change**:
- `indra/newview/skins/default/xui/en/panel_preferences_move_general.xml`
- `indra/newview/skins/default/xui/en/panel_preferences_graphics1.xml` (or `floater_preferences_graphics_advanced.xml`)

**Tests to add or update**:
- Visual inspection of Preferences → Move & View → General.
- Visual inspection of Preferences → Graphics and Advanced Graphics floaters.

## Risks & Considerations
- Low risk: Pure XUI layout changes. No binary logic, C++ data structures, or performance pipelines are modified.

## Open Questions
- For Block Compression: Which placement do you prefer (Option A: everything in Main Graphics, Option B: everything in Advanced Graphics, or Option C: toggle in Main, tuning dropdown in Advanced)?
