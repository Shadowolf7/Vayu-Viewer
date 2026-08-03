# FEATURES.md

Tracks how Vayu diverges from its two ancestors: **Linden Lab's official viewer** and **Alchemy Viewer** (`upstream` remote, `AlchemyViewer/Alchemy`), which Vayu is directly forked from. This file covers **current state only** — what's already shipped, in both Alchemy and Vayu. For in-progress work, planned items, and backlog, see [**ROADMAP.md**](ROADMAP.md).

Update this file as part of the PR that lands a feature, the same way `doc/BUILD.md` gets updated alongside build-system changes — and move the corresponding item out of `ROADMAP.md` in the same PR.

## What Alchemy Viewer adds (inherited foundation)

Before Vayu's own changes, Alchemy is already a substantial fork of Linden Lab's official viewer. Highlights, not an exhaustive list:

- **More rendering options:** a choice of tonemapping styles (ACES, Reinhard, Filmic, AGX), 3D LUT-based color grading, sharpening, and a finishing pass with vignette, film grain, dithering, chromatic aberration, and color-vision-deficiency (colorblind) compensation/preview.
- **Chat quality-of-life:** a set of slash-style chat commands (e.g. jump to your home, set draw distance, look up your position, run quick calculations), colorized/styled nearby chat, typing and online/offline notifications, and radar alerts sent to chat.
- **Radar and minimap enhancements:** parcels for sale or with collision restrictions highlighted on the map, live nearby-agent counts, adjustable update rate, and rings showing who's currently chatting nearby.
- **Mouselook and movement:** an identify-friend-or-foe overlay while in mouselook, adjustable zoom timing, a "realistic" mouselook mode, and various movement toggles (e.g. disabling click-to-sit or mouse-steering).
- **Interface conveniences:** auto-hiding toolbars, font overrides, and other small remembered-state niceties.

This list isn't exhaustive — Alchemy carries roughly 80 of its own settings beyond this summary (`indra/newview/app_settings/settings_alchemy.xml`). Anything not called out above and not listed in the Vayu sections below is Alchemy's, not Vayu's.

## Naming conventions used in this codebase

- **Settings/XUI identifiers:** a new `gSavedSettings` key or widget `name=` gets the `Vayu*` prefix if it's genuinely new work, or keeps `Alchemy*` if it's inherited from Alchemy or extends an existing `Alchemy*` feature. Provenance decides, not neighboring keys.
- **"Layer 1 vs layer 2" rebrand split:** user/OS-visible identity (window titles, icons, bundle IDs, installer names, URLs) was renamed Alchemy → Vayu. Internal plumbing (C++ identifiers, settings-key prefixes, vcpkg port names, the selectable `alchemy` skin, CI workflows tied to Alchemy's own infra) was deliberately left alone — renaming it is cosmetic churn that only grows merge-conflict surface against `upstream`.

## Shipped (merged into `develop`)

**Rebrand:** product identity (name, icons, bundle/app IDs, window class, installer/packaging scripts, per-user data directory, log/crash-dump filenames, About floater, bug-report/update URLs) changed from Alchemy to Vayu. Internal plumbing deliberately left as `Alchemy*` per the layer-1/layer-2 split above.

**Rendering:**
- BC7/BPTC texture compression support.
- `RenderPBRMaterials` toggle — fall back PBR faces to legacy Blinn-Phong shading per-face (narrower than a full global PBR/Blinn-Phong switch — see Cool VL Viewer item below).
- Ported [secondlife/viewer#5927](https://github.com/secondlife/viewer/pull/5927): interleave rigged attachment alpha into world alpha for correct depth sorting.

**Ported from Firestorm:** pose stand, windlight quick-select, FPS limiter, VRAM-triggered draw-distance toggle — plus three XUI-only follow-up fixes (FPS limiter controls and PBR Materials toggle were hidden behind other controls in Preferences; quick-settings environment dropdowns lacked labels).

**Camera:** mouselook eye-height offset (`VayuMouselookEyeHeightOffset`) and vehicle-tilt decoupling (`VayuMouselookDecoupleVehicleTilt`), new Preferences > Move > Mouse Input controls.

**Content creation:** notecard/script file import with CRLF/BOM handling, batch/bulk upload support for notecards and scripts, standalone Notecard/Script entries in the Upload menu, legacy-viewer auto-sizing via width-ruler convention detection.

**Linux platform:**
- Fixed Linux FMOD build/packaging; consolidated GPU/GameMode selection (`switcherooctl`/`gamemoderun`) into the main launcher wrapper.
- `OPENSSL_CONF=/dev/null` workaround for an openSUSE crypto-policy TLS bug (see project memory `project_login_oom_bug_fixed`).
- Window decorations on Wayland (GNOME/Mutter, Weston): `libdecor` build prerequisite documented for all supported distros, plus a hard CMake configure-time check so a missing dev package fails loudly instead of silently shipping an undecorated window.
- Auto-checkout of missing git submodules at configure time; ccache wired up for faster incremental rebuilds.

**Reliability fixes:** mesh header retry exhaustion now notifies waiting objects instead of hanging; asset/mesh loading regression from the curl 8.21.0 upgrade; texture compression disabled on `setSubImageFromFrameBuffer` targets (was producing corrupt output).

---

For what's in progress, planned, backlogged, or being watched, see [**ROADMAP.md**](ROADMAP.md).
