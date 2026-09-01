# Features

Vayu is a Second Life viewer forked from [Alchemy Viewer](https://github.com/AlchemyViewer/Alchemy), which is itself a deep fork of Linden Lab's official viewer. This page covers what you get as a user — categorized by where each enhancement originated, with clear explanations of why it matters in-world.

---

## Inherited from Alchemy Viewer

Alchemy represents a major modernization of the viewer codebase. Vayu inherits Alchemy's extensive architectural work, including:

- **Modern Graphics & Pipeline Overhaul** — Major rendering pipeline modernizations, including deep PBR materials support, improved shadow mapping, GL modernizations, tonemapping options (ACES, Reinhard, Filmic, AGX), 3D LUT-based color grading, sharpening, vignette, film grain, and colorblind simulation/correction.
- **Specular Anti-Aliasing & Shading Corrections** — GPU specular anti-aliasing and lobe-widening shaders that eliminate pixel buzzing and high-contrast flickering on glossy surfaces, glass, and windows viewed at a distance. ([ec2702c042](https://github.com/Shadowolf7/Vayu-Viewer/commit/ec2702c042), [03c0860b84](https://github.com/Shadowolf7/Vayu-Viewer/commit/03c0860b84))
- **Modernized LLSD & XML Document Pipelines** — High-performance document scanning that removes legacy Expat callback overhead and uses zero-copy string parsing during scene, object, and inventory transfers. ([732b1f4af2](https://github.com/Shadowolf7/Vayu-Viewer/commit/732b1f4af2), [00ac6c47c8](https://github.com/Shadowolf7/Vayu-Viewer/commit/00ac6c47c8))
- **Chat & Communication QoL** — Slash-style chat commands (quick teleport home, draw distance adjustments, coordinates, inline math), styled/colorized nearby chat, typing and status notifications, and integrated radar alerts.
- **Radar & Minimap Enhancements** — Clear visual highlighting for parcels for sale or with collision restrictions, live nearby-agent counts, adjustable radar update rates, and proximity chat range rings.
- **Mouselook & Controls** — Friend/foe identification overlays in mouselook, customizable zoom timing, realistic mouselook inertia, and dedicated toggles for click-to-sit and mouse-steering.
- **Interface Conveniences** — Auto-hiding toolbars, custom font overrides, and comprehensive remembered UI layout states.

---

## What Vayu Adds

### Rendering & Performance

- **Massively reduced VRAM usage via Block Texture Compression (BC7/BC1/BC4/BC5)** — Textures are compressed off the main thread into native GPU formats as they stream in, freeing up gigabytes of video memory and eliminating texture thrashing in crowded events. Compressed textures are cached locally to disk so subsequent visits load instantly. Includes configurable quality presets (Ultrafast/Fast/Basic/Slow) and an in-memory safety ceiling (`VayuBCTextureCacheMaxPendingSize`) to protect system RAM. ([#76](https://github.com/Shadowolf7/Vayu-Viewer/pull/76) / [236aefa71a](https://github.com/Shadowolf7/Vayu-Viewer/commit/236aefa71a), [#82](https://github.com/Shadowolf7/Vayu-Viewer/pull/82) / [2340f4c914](https://github.com/Shadowolf7/Vayu-Viewer/commit/2340f4c914))
- **Higher, smoother framerates via AVX2 / FMA Vector Acceleration** — 256-bit AVX2 SIMD culls off-screen objects against all 6 frustum planes simultaneously, while hardware FMA3 accelerates matrix transformations and vector math on modern CPUs. ([0631f98847](https://github.com/Shadowolf7/Vayu-Viewer/commit/0631f98847), [3cb66b9848](https://github.com/Shadowolf7/Vayu-Viewer/commit/3cb66b9848))
- **Correct Transparency & Attachment Sorting** — Fixes visual sorting defects where alpha attachments or clothing would incorrectly render behind transparent world objects.
- **Two-Tier Performance Diagnostics** — Built-in low-overhead frame-time diagnostics (`VayuPerfFrameLog`) and Tracy GPU profiling for identifying lag spikes. ([#80](https://github.com/Shadowolf7/Vayu-Viewer/pull/80) / [524bb4e2ad](https://github.com/Shadowolf7/Vayu-Viewer/commit/524bb4e2ad))

### Camera & Vehicle Movement

- **Stable Mouselook in Vehicles (Horizon Decoupling)** — When driving or flying in mouselook, your camera stays level with the horizon rather than tilting wildly with vehicle roll/bank, while heading and pitch track smoothly. ([#33](https://github.com/Shadowolf7/Vayu-Viewer/pull/33) / [403b227539](https://github.com/Shadowolf7/Vayu-Viewer/commit/403b227539), [5814ec9762](https://github.com/Shadowolf7/Vayu-Viewer/commit/5814ec9762))
- **Adjustable Mouselook Eye-Height** — Customize your first-person camera height to match your avatar's true eye level (Preferences → Move → Mouse Input, or Quick Settings). ([#48](https://github.com/Shadowolf7/Vayu-Viewer/pull/48) / [db2f12ff49](https://github.com/Shadowolf7/Vayu-Viewer/commit/db2f12ff49))
- **Configurable Region-Crossing Prediction (Animats / Firestorm)** — Choose how your vehicle behaves during sim crossings under Preferences → Move → General (*At region crossing*):
  - *Predict trajectory (clamped)* (default): Smooth dead reckoning clamped to 1.0s to prevent runaway physics projections.
  - *Stop at boundary*: Clamps your position at the border and zeroes velocity, preventing vehicles from spinning or flying off into the void during laggy handoffs.
  - *Unlimited prediction*: Unconstrained legacy extrapolation.
- **Follow-Cam Sim-Crossing Damping (Animats / Firestorm)** — Eliminates the jarring 256m coordinate snap / camera rubberband when driving vehicles across sim borders. ([dfcf0e2fc8](https://github.com/Shadowolf7/Vayu-Viewer/commit/dfcf0e2fc8))
- **Vehicle Unseating Protection** — Sim crossings temporarily defer kill messages for your seat/vehicle during handoff transitions, preventing accidental unseating or being dropped into water while driving across regions. ([#73](https://github.com/Shadowolf7/Vayu-Viewer/pull/73) / [e590647298](https://github.com/Shadowolf7/Vayu-Viewer/commit/e590647298), [fa665aa70f](https://github.com/Shadowolf7/Vayu-Viewer/commit/fa665aa70f))

### Environment & World

- **Classic 3D Wind-Driven Clouds (Henri Beauchamp / Cool VL Viewer)** — Restores and modernizes the volumetric 3D cloud layer from early viewer history with client-side density synthesis and wind motion, rather than the flat Windlight sky dome. ([#26](https://github.com/Shadowolf7/Vayu-Viewer/pull/26) / [22d43eaeae](https://github.com/Shadowolf7/Vayu-Viewer/commit/22d43eaeae), [#53](https://github.com/Shadowolf7/Vayu-Viewer/pull/53) / [2130829ab1](https://github.com/Shadowolf7/Vayu-Viewer/commit/2130829ab1))
- **Driver Mode Environment Lock** — When seated on a vehicle, traveling across parcels or regions will not abruptly flash or change your environment/sky settings until you dismount.
- **Persistent Quick Settings EEP Controls** — Floating Quick Settings environment dropdowns (Sky, Water, Day Cycle) stay synchronized with active environment state, persist cleanly, and let you reset back to region defaults in one click. ([b78d5a185c](https://github.com/Shadowolf7/Vayu-Viewer/commit/b78d5a185c))

### Ported from Cool VL Viewer (Henri Beauchamp)

- **High-Performance Disk & Asset Cache (`LLDiskCache`)** — Lockless atomic size tracking, hierarchical directory distribution (preventing filesystem limits and slow directory traversals), background threaded auto-purging at 150% capacity, and safe multi-viewer instance deconfliction.
- **SIMD Flat Hash Containers & Single-Cycle UUID Hashing** — Flat hash maps (`parallel-hashmap` / `hbfastmap.h` / `hbfastset.h`) replace slow node-based maps across hot UI and rendering paths. Inlined UUID methods and single-cycle 64-bit XOR digest hashing eliminate lookup bottlenecks across millions of ID queries.
- **Avatar & UI CPU Throttling** — Distance-based update interval throttling for background avatar extents/impostors and 5 FPS rate-limiting on voice visualizer polling significantly reduce CPU load in crowded events.
- **Dynamic Immediate-Mode VBO Caching (`LLRender::mVBCache`)** — Accelerates 2D interface rendering, text flushes, and dynamic UI elements.
- **`HBExternalEditor` Seamless External Text & Script Editing** — Edit notecards and scripts in your preferred local desktop editor (Kate, Gedit, VS Code, Notepad) with zero manual configuration (automatic XDG/Explorer desktop fallback), Linux `LD_LIBRARY_PATH` environment isolation, and live 1 Hz file watching so saving in the external editor immediately updates the viewer in real time.

### Ported from Firestorm

- **Quick Settings & Utility Tools** — Integrated avatar pose stand, Windlight quick-selector, background FPS limiter, and a user-facing toggle for VRAM-triggered draw distance throttling (`FSDrawDistanceVRAMOptimization`, ported from Firestorm FIRE-35748). ([7ab47eabfcd](https://github.com/Shadowolf7/Vayu-Viewer/commit/7ab47eabfcd))

### Content Creation

- **Direct File Import & Bulk Uploads** — Import notecards and LSL scripts directly from disk (with automatic CRLF/BOM normalization) with full batch/bulk upload support. ([#2](https://github.com/Shadowolf7/Vayu-Viewer/pull/2) / [b4352e48f6](https://github.com/Shadowolf7/Vayu-Viewer/commit/b4352e48f6), [47be8d1cbc](https://github.com/Shadowolf7/Vayu-Viewer/commit/47be8d1cbc))
- **Intelligent Notecard Width-Ruler Fitting** — Unicode-native ruler detection (supporting UTF-32 block/box glyphs like `▀`, `═`, `─` and embedded directives like `EXTEND TO FIT`) that automatically switches to a monospace font and resizes the floater to fit the author's layout without awkward word wrapping.
- **Luau Script Engine Integration** — First-class Luau language support in world scripts alongside traditional LSL. ([#1](https://github.com/Shadowolf7/Vayu-Viewer/pull/1) / [f71887e3ff](https://github.com/Shadowolf7/Vayu-Viewer/commit/f71887e3ff))
- **VS Code Bidirectional Script Synchronization** — Real-time bidirectional synchronization between the viewer's script compiler and external VS Code editor instances via a local WebSocket server. ([#4](https://github.com/Shadowolf7/Vayu-Viewer/pull/4) / [5c07e0e7a2](https://github.com/Shadowolf7/Vayu-Viewer/commit/5c07e0e7a2))
- **Texture-in-Inventory Inspecting & Upload Pipeline Enhancements** — Enhanced inspection workflows for texture assets and optimized GLTF mesh/material upload pipelines.

### Linux Platform Enhancements

- **One-Click GPU & Performance Launcher** — Automatic discrete GPU switching and GameMode integration (`switcherooctl`/`gamemoderun`) built directly into the launcher script.
- **Native Wayland Window Decorations** — Full client-side window decoration support on modern Wayland compositors (GNOME/Mutter, Weston) via `libdecor`. ([d8fb15c644](https://github.com/Shadowolf7/Vayu-Viewer/commit/d8fb15c644))
- **Robust FMOD Audio Packaging** — Out-of-the-box FMOD Studio audio build and runtime packaging.

---

For technical architecture details and developer documentation, see [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md) and [doc/BUILD.md](doc/BUILD.md).

---

Want to see what's next? Check the project's issue tracker for planned and in-progress work.
