# Features

Vayu is a Second Life viewer forked from [Alchemy Viewer](https://github.com/AlchemyViewer/Alchemy), which is itself a substantial fork of Linden Lab's official viewer. This page covers what you actually get — grouped by where it comes from, so you know what's Vayu's own work versus inherited. For in-progress and planned work, see the project's public issue tracker; for internal naming/dev conventions, see [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md).

## Inherited from Alchemy Viewer

Alchemy is already a deep fork of the official viewer, and Vayu builds on all of it:

- **More rendering options** — a choice of tonemapping styles (ACES, Reinhard, Filmic, AGX), 3D LUT-based color grading, sharpening, and a finishing pass with vignette, film grain, dithering, chromatic aberration, and colorblind-friendly compensation/preview.
- **Chat quality-of-life** — handy slash-style chat commands (jump home, set draw distance, check your position, quick math), colorized/styled nearby chat, typing and online/offline notifications, and radar alerts sent to chat.
- **Radar and minimap enhancements** — parcels for sale or with collision restrictions highlighted on the map, live nearby-agent counts, adjustable update rate, and rings showing who's currently chatting nearby.
- **Mouselook and movement** — a friend/foe overlay in mouselook, adjustable zoom timing, a "realistic" mouselook mode, and toggles for click-to-sit and mouse-steering.
- **Interface conveniences** — auto-hiding toolbars, font overrides, and other small remembered-state niceties.

This isn't exhaustive — Alchemy carries roughly 80 of its own settings beyond this summary. Anything not listed here or in the Vayu section below is Alchemy's.

## What Vayu adds

### Rendering

- Correct depth sorting between rigged attachments and world alpha (clothing/attachments no longer draw in the wrong order relative to transparent world objects).
- Attached spotlights and projector lights now stay lit regardless of the "attached lights" toggle — only point-light attachments are affected by it.
- **Block texture compression (BC7/BC1/BC4/BC5)** — textures are compressed off the main thread as they decode, cutting VRAM usage, with a persistent disk cache so they aren't re-encoded on every load. Choose a speed/quality preset (Ultrafast/Fast/Basic/Slow) and cache size in Preferences → Graphics → Advanced Hardware.

### Camera & movement

- **Mouselook eye-height offset** — dial in your first-person camera height, with a master toggle so your saved value survives being switched off. Preferences → Move → Mouse Input, also in Quick Settings.
- **Vehicle-tilt decoupling** — mouselook camera no longer tilts with the vehicle's roll/pitch while driving.

### Environment

- **Classic clouds** — a real Linden Lab feature removed in 2011, restored and modernized: a proper wind-driven 3D cloud layer instead of the flat Windlight sky-dome. Includes client-side density synthesis since modern regions mostly no longer send real cloud data.
- **Driver mode** — while seated on a vehicle, region/parcel crossings no longer swap your sky/water/day environment out from under you; snaps back to your actual location the instant you dismount.
- Both available in Quick Settings' Environment section.

### Ported from Firestorm

Pose stand, Windlight quick-select, an FPS limiter, and a VRAM-triggered draw-distance toggle.

### Content creation

- Import notecards and scripts directly from files (handles CRLF/BOM automatically), with batch/bulk upload support.
- Standalone Notecard/Script entries in the Upload menu.
- Legacy-viewer auto-sizing detection for notecards that use the old width-ruler convention.

### Linux

- Fixed FMOD audio build/packaging.
- One-stop GPU/GameMode selection (`switcherooctl`/`gamemoderun`) built into the launcher — no more manually forcing your dGPU.
- Window decorations under Wayland (GNOME/Mutter, Weston).
- Missing git submodules are checked out automatically; ccache wired in for faster rebuilds if you're building from source.

### Reliability fixes

- Mesh loading no longer hangs waiting objects when a mesh header retry runs out of attempts.
- Fixed an asset/mesh loading regression introduced by a curl upgrade.

---

Want to see what's next? Check the project's issue tracker for planned and in-progress work.
