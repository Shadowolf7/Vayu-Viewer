# FEATURES.md

Tracks how Vayu diverges from its two ancestors: **Linden Lab's official viewer** and **Alchemy Viewer** (`upstream` remote, `AlchemyViewer/Alchemy`), which Vayu is directly forked from. Alchemy's own divergence from LL is out of scope here — this file only tracks what's specific to Vayu, on top of Alchemy.

Update this file as part of the PR that lands a feature, the same way `doc/BUILD.md` gets updated alongside build-system changes. Move items between sections as they progress; don't just append.

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

**Camera:** mouselook eye-height offset (`AlchemyMouselookEyeHeightOffset`) and vehicle-tilt decoupling (`AlchemyMouselookDecoupleVehicleTilt`), new Preferences > Move > Mouse Input controls.

**Content creation:** notecard/script file import with CRLF/BOM handling, batch/bulk upload support for notecards and scripts, standalone Notecard/Script entries in the Upload menu, legacy-viewer auto-sizing via width-ruler convention detection.

**Linux platform:**
- Fixed Linux FMOD build/packaging; consolidated GPU/GameMode selection (`switcherooctl`/`gamemoderun`) into the main launcher wrapper.
- `OPENSSL_CONF=/dev/null` workaround for an openSUSE crypto-policy TLS bug (see project memory `project_login_oom_bug_fixed`).
- Window decorations on Wayland (GNOME/Mutter, Weston): `libdecor` build prerequisite documented for all supported distros, plus a hard CMake configure-time check so a missing dev package fails loudly instead of silently shipping an undecorated window.
- Auto-checkout of missing git submodules at configure time; ccache wired up for faster incremental rebuilds.

**Reliability fixes:** mesh header retry exhaustion now notifies waiting objects instead of hanging; asset/mesh loading regression from the curl 8.21.0 upgrade; texture compression disabled on `setSubImageFromFrameBuffer` targets (was producing corrupt output).

## In progress (not yet merged)

- **Cool VL Viewer Lua scripting** (`feature/coolvl-lua-port`, local-only branch, not yet pushed) — `HBPreprocessor` ported, partial `HBViewerAutomation` core engine, resident script lifecycle, `OnRegionChange`/`OnPositionChange` events, Lua wired as a vcpkg dependency. ~3,075 lines across 6 commits. Not built/tested yet.
- **Standalone notecard/script editor** — vendoring Scintilla/Lexilla via CMake (dropped the vcpkg feature in favor of a direct vendor). Early scaffolding only.

## Roadmap (scoped, not started)

Roughly cheapest-to-largest:

- **Firestorm LSL preprocessor** (`fslslpreproc*.cpp/h`) — check overlap with the already-ported `HBPreprocessor` above before starting; both want the same `#include`/`#define` macro layer.
- **Cool VL Viewer classic cloud layer** — real 3D wind-driven cloud puffs with settable altitude, distinct from Vayu's current flat Windlight sky-dome clouds. Well de-risked: the simulator already sends cloud-density packets (`llvlmanager.cpp`, `CLOUD_LAYER_CODE`), but the client-side `unpackData()` cloud branch is a literal empty stub — only the consumer is missing, and Cool VL has a complete one to port.
- **Cool VL Viewer PBR → Blinn-Phong global toggle** — broader than the already-shipped per-face `RenderPBRMaterials`; open decision whether the narrower version already suffices.
- **GameControl/gamepad support** — large, multi-year upstream effort (LL's own PR alone is 93 files/9k+ lines). Not present in Vayu at all yet. SDL2→SDL3 compatibility unconfirmed since Vayu is on SDL3.
- **Login screen rework** — use secondlife.com's background animation; the login flow already embeds a CEF browser panel, not a static image, so this is more feasible than it might first look.
- **Adwaita/GNOME-styled skin** — exploratory only, not committed. Scoped from a recolor-only pass (~1-2 days) up through true native libadwaita accent-color following (would need new C++, not just a skin).

## Post-rebrand backlog (raised, not yet scoped/prioritized)

- App icon/logo art — current art in `indra/newview/branding/` and `doc/vayu_logo.png` is placeholder.
- GitHub Pages project website.
- Build distribution — plan: GitHub Releases + GitHub Actions + Velopack's built-in GitHub update source (Velopack currently wired for Windows/macOS only; Linux needs its own integration). Code signing plan: SignPath Foundation (free, OSS) or Azure Trusted Signing for Windows; Apple Developer Program ($99/yr) for macOS notarization.
- Chat log import tool (likely from Firestorm's format) + eventual cloud-backup integration (OwnCloud/Dropbox/OneDrive/iCloud) + fix for chat transcripts breaking continuity on avatar rename.
- Vendor mimalloc as the allocator (next up, per user's own prioritization), following the precedent of Firestorm bundling jemalloc.
- Windows AMD OpenGL-via-D3D12 workaround (`GALLIUM_DRIVER=d3d12` via `mesa-dist-win`'s `libgallium_wgl.dll`) — AMD's native Windows GL driver is weak; D3D12 is the API every vendor validates hardest against. Zink (already used on Linux) as a fallback toggle.
- Register a Vayu Discord application — Rich Presence is still hardcoded to Alchemy's own Discord App ID and image asset; needs Roger to register a new app before this can be swapped over.
- `.github/` CI/release workflows still reference Alchemy's own infra (secrets, signing keys) — needs its own decision pass once distribution is ready.

## Watching (not itemized as work)

- Linden Lab's multi-year OpenGL → Vulkan/Metal/D3D12 abstraction-layer transition (LL keeping OpenGL as a "legacy renderer" for years alongside new backends). Relevant to Vayu's long-term rendering architecture; watch for merge-conflict timing against `upstream`.
