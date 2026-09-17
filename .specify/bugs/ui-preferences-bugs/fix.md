# Bug Fix: Preferences UI Overlap and Duplicate Compression Setting

- **Slug**: ui-preferences-bugs
- **Fixed**: 2026-09-10
- **Assessment**: ./assessment.md
- **Status**: applied

## Summary

Consolidated the "Enable Block Texture Compression" control into the Advanced Graphics floater (Option A), removing the duplicate toggle from the primary Graphics panel. Fixed the control overlap in the Move & View preferences panel by adding `follows="left|top"` to all bottom controls and compacting vertical padding so "Other Devices" sits comfortably below "At region crossing".

## Changes

| File | Change | Notes |
|------|--------|-------|
| `indra/newview/skins/default/xui/en/panel_preferences_graphics1.xml` | modified | Removed duplicate `RenderCompressTextures` checkbox (Option A). |
| `indra/newview/skins/default/xui/en/panel_preferences_move_general.xml` | modified | Added `follows="left|top"` to `joystick_setup_button` and comboboxes; compacted `top_pad` values to eliminate overlap and vertical clipping. |

## Diff Highlights

### 1. Removed Duplicate Block Compression Checkbox
```xml
-  <check_box
-    control_name="RenderCompressTextures"
-    follows="left|top"
-    height="16"
-    initial_value="true"
-    label="Enable Block Texture Compression"
-    layout="topleft"
-    left="30"
-    name="RenderCompressTextures"
-    tool_tip="Compresses textures in video memory using BC1/BC4/BC5/BC7 via bc7enc, saving significant VRAM."
-    top_pad="6"
-    width="300" />
```

### 2. Move & View Layout Fix
```xml
   <combo_box
    control_name="RegionCrossingMovementMode"
+   follows="left|top"
    height="23"
    layout="topleft"
    left_pad="10"
    top_delta="-6"
...
   <button
+   follows="left|top"
    height="23"
    label="Other Devices"
    layout="topleft"
    left="30"
    name="joystick_setup_button"
-   top_pad="15"
+   top_pad="10"
    width="155">
```

## Local Verification

- **Syntax Verification**: Validated XML syntax for both modified files using `xml.etree.ElementTree` (`Both XML files parsed successfully!`).
- **Layout Coordinate Simulation**: Ran exact `LLView::applyXUILayout` simulation confirming:
  - `region_crossing_movement_combo` bottom is at `y = 365px`.
  - `joystick_setup_button` top is at `y = 375px`, bottom is at `y = 398px`.
  - There is a 10px vertical clearance between the region crossing combo and the button.
  - The entire control stack easily fits inside the ~411px tab container client area with 13px of breathing room.

## Deviations from Assessment

None. Option A was selected as requested for the Block Compression control.
