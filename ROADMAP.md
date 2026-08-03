# ROADMAP.md

Forward-looking work for Vayu: what's in progress, planned, backlogged, or being watched. For what's already shipped, see [**FEATURES.md**](FEATURES.md).

Update this file as part of the PR that lands an item — move it out of here into `FEATURES.md` rather than leaving stale entries behind. This is meant to be the single source of truth for status; if a scoping note elsewhere (memory, Obsidian, etc.) disagrees with this file, this file wins — update it, don't let the disagreement stand.

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
