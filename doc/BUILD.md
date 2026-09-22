# Building Vayu

Everything you need to build Vayu from source: platform setup, presets, options, tests, packaging, and troubleshooting.

- [Prerequisites](#prerequisites)
- [Platform setup](#platform-setup) — Windows, macOS, Linux
- [Clone and bootstrap](#clone-and-bootstrap)
- [Configure](#configure)
- [Build](#build)
- [Configuration types](#configuration-types)
- [Build options](#build-options)
- [Running tests](#running-tests)
- [Packaging](#packaging)
- [Troubleshooting](#troubleshooting)

## Prerequisites

Every platform needs a C++ toolchain plus:

- **CMake** 4.0+
- **Git**
- **Rust** and **.NET SDK** — only for Velopack installers (`-DAL_USE_VELOPACK=ON`)
- **Python** 3 — only for the tests that spawn a Python peer (see [Running tests](#running-tests))

Install commands are platform-specific; see below.

## Platform setup

### Windows

Install the following:

- [Visual Studio 2026](https://visualstudio.microsoft.com/vs/community/) — select the **Desktop development with C++** workload
- [CMake](https://cmake.org/download/) 4.0+
- [Git for Windows](https://git-scm.com/install/windows)
- [Rust](https://rust-lang.org/tools/install/) — run `rustup-init.exe` and accept defaults (packaging only)
- [.NET SDK](https://dotnet.microsoft.com/en-us/download) (packaging only)

Sanity-check in a fresh terminal:

```
cmake --version
git --version
```

### macOS

Install [Xcode](https://developer.apple.com/xcode/) from the App Store, then run `xcode-select --install` to get the command-line tools.

Install [Homebrew](https://brew.sh/), then the build dependencies:

```
brew install git cmake zip unzip curl pkgconf automake autoconf autoconf-archive \
    gettext libtool rustup dotnet
```

Initialize the Rust toolchain (packaging only):

```
rustup-init -y
```

### Linux

Install system packages for your distro:

<details>
<summary>Arch</summary>

```
sudo pacman -Syu automake autoconf autoconf-archive base-devel cmake fontconfig git glib2-devel \
    gstreamer gst-plugins-base-libs libdecor ninja libglvnd libtool libvlc libx11 pkgconf python \
    wayland dotnet-sdk rustup zip nasm
```

</details>

<details>
<summary>Debian 12+</summary>

```
sudo apt install \
    autoconf autoconf-archive automake bison build-essential cmake curl flex gettext \
    libasound2-dev libaudio-dev libdbus-1-dev libdecor-0-dev libdrm-dev \
    libegl1-mesa-dev libfribidi-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev \
    libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev libibus-1.0-dev libjack-dev \
    libpipewire-0.3-dev libpulse-dev libsndio-dev libtext-unidecode-perl \
    libthai-dev libtool libudev-dev libunwind-dev liburing-dev libvlc-dev libwayland-dev \
    libx11-dev libxcursor-dev libxext-dev libxfixes-dev libxft-dev libxi-dev libxinerama-dev \
    libxkbcommon-dev libxrandr-dev libxss-dev libxtst-dev linux-libc-dev ninja-build \
    pkgconf tar tex-common texinfo unzip zip dotnet-sdk-10.0 rustup nasm
```

</details>

<details open>
<summary>Ubuntu 22.04+</summary>

```
sudo apt install \
    autoconf autoconf-archive automake bison build-essential cmake curl flex gettext \
    libasound2-dev libaudio-dev libdbus-1-dev libdecor-0-dev libdrm-dev \
    libegl1-mesa-dev libfribidi-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev \
    libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev libibus-1.0-dev libjack-dev \
    libpipewire-0.3-dev libpulse-dev libsndio-dev libtext-unidecode-perl \
    libthai-dev libtool libudev-dev libunwind-dev liburing-dev libvlc-dev libwayland-dev \
    libx11-dev libxcursor-dev libxext-dev libxfixes-dev libxft-dev libxi-dev libxinerama-dev \
    libxkbcommon-dev libxrandr-dev libxss-dev libxtst-dev linux-libc-dev ninja-build \
    pkgconf tar tex-common texinfo unzip zip dotnet-sdk-10.0 rustup nasm
```

</details>

<details>
<summary>Fedora / RHEL</summary>

**AlmaLinux 10:**

```
sudo dnf group install "Development Tools"
sudo dnf install cmake fontconfig-devel git glib2-devel gstreamer1-devel \
    gstreamer1-plugins-base-devel libdecor-devel libX11-devel mesa-libOSMesa-devel libglvnd-devel \
    ninja-build python3 vlc-devel wayland-devel dotnet-sdk-10.0 rustup
```

You may need to enable EPEL first: `sudo dnf install epel-release`

**Fedora 44+:**

```
sudo dnf install @development-tools @c-development cmake fontconfig-devel git glib-devel \
    gstreamer1-devel gstreamer1-plugins-base-devel libdecor-devel libX11-devel \
    mesa-compat-libOSMesa-devel libglvnd-devel ninja-build python3 vlc-devel \
    wayland-devel dotnet-sdk-10.0 rustup perl-IPC-Cmd perl-FindBin perl-Time-Piece \
    autoconf-archive perl-open libXcursor-devel wayland-protocols-devel dbus-devel \
    ibus-devel mesa-libGLU-devel libxkbcommon-devel mesa-libEGL-devel mesa-libGL-devel \
    libXtst-devel libXrandr-devel pipewire-devel pulseaudio-libs-devel alsa-lib-devel \
    nasm libXScrnSaver-devel
```

To build with Clang instead of GCC, also install: `sudo dnf install clang lld`

</details>

<details>
<summary>OpenSUSE Tumbleweed</summary>

```
sudo zypper in -t pattern devel_basis devel_C_C++
sudo zypper install cmake fontconfig-devel git glib2-devel gstreamer-devel \
    gstreamer-plugins-base-devel libdecor-devel libglvnd-devel libX11-devel ninja Mesa-libGL-devel \
    python3 vlc-devel wayland-devel
```

</details>

Initialize a stable Rust toolchain (packaging only):

```
rustup default stable
```

## Clone and bootstrap

Vayu vendors the [Dullahan](https://github.com/AlchemyViewer/dullahan) CEF wrapper — used by the in-world web media plugin — as a git submodule under `indra/dullahan`. It builds from source as part of the tree, so the submodule must be present before you configure. Clone with `--recurse-submodules`:

```
git clone --recurse-submodules https://github.com/Shadowolf7/Vayu-Viewer.git vayu-viewer
cd vayu-viewer
dotnet tool restore        # Velopack installers only
```

Optional: To run the unit tests that spawn a Python peer (`llleap`, `llprocess`, `llsdserialize`, `llcorehttp`), set up a virtual environment for the `llsd` peer runner:
```
python3 -m venv .venv && source .venv/bin/activate   # Windows: .\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

Already cloned without `--recurse-submodules`? Fetch the submodules before configuring:

```
git submodule update --init --recursive
```

After pulling upstream changes, run the same command to keep the submodule in sync with the revision the tree expects.

## Configure

Build configuration is driven by CMake presets. [`indra/CMakePresets.json`](../indra/CMakePresets.json) includes one file per generator under [`indra/cmake/presets/`](../indra/cmake/presets/) (`vs2026.json`, `ninja.json`, `xcode.json`), each of which includes `base.json`, the hidden bases they are composed from. `generate.py` beside them writes all five; edit its tables, not the JSON. A preset selects the generator (Visual Studio, Ninja, Xcode), the target architecture, and whether proprietary components are enabled.

List all available presets:

```
cmake -S indra --list-presets
```

### Naming convention

Preset names follow the pattern `<generator>[-<arch>][-os]`:

- **`-os` suffix** — open-source only. Excludes proprietary components (KDU JPEG2000 codec, FMOD audio, and other non-free libraries).
- **No `-os` suffix** — sets `AL_ENABLE_PROPRIETARY=ON`. Requires licensed source for the proprietary components and is only useful if you have access to them.

Most contributors want the `-os` variants.

`<generator>[-os]-fullopt` (with `-arm64` / `-x64` on macOS) is that preset with the optimizations of a shipped build: LTO on, Tracy and Release-configuration debug logging off. The channel is not part of it — pass `-DAL_CHANNEL=...` as for any preset — and neither is the Velopack update client (`-DAL_USE_VELOPACK=ON`), which CI adds. The Ninja ones default to the Release configuration. The hidden `fullopt` preset carries the three settings for a preset of your own, for example `{"name": "mine", "inherits": ["ninja-os", "fullopt", "mold"]}` in `CMakeUserPresets.json`.

### Common presets

| Preset                                       | Platform | Generator          |
|:---------------------------------------------|:---------|:-------------------|
| `vs2026-os`                                  | Windows  | Visual Studio      |
| `ninja-os`                                   | Linux    | Ninja Multi-Config |
| `ninja-os-arm64`, `ninja-os-x64`             | macOS    | Ninja Multi-Config |
| `xcode-os`, `xcode-os-arm64`, `xcode-os-x64` | macOS    | Xcode              |

Configure with:

```
cmake -S indra --preset <preset-name>
```

This creates a build tree at `build-<HostSystem>-<preset>/` next to the source — e.g. `build-Windows-vs2026-os/`, `build-Linux-ninja-os/`, `build-Darwin-xcode-os-arm64/`.

The first configure run downloads and builds every vcpkg dependency from source. Expect **30–60+ minutes** and several GB of disk; subsequent configures finish in seconds.

### vcpkg

Third-party dependencies are managed by [vcpkg](https://vcpkg.io) in manifest mode, using the ports/triplets under `indra/vcpkg/`. `indra/cmake/BootstrapVcpkg.cmake` clones and bootstraps vcpkg itself into `../vcpkg` (next to `indra/`) automatically on first configure — nothing to install by hand.

**Compiler selection doesn't automatically reach vcpkg.** Ports vcpkg builds from source (e.g. `cef-bin`'s bundled `libcef_dll_wrapper`) run through their own CMake invocation, driven by the *triplet*, not by the outer `CMAKE_CXX_COMPILER` you pass to the top-level configure. Mixing them — viewer built with Clang, vcpkg dependencies still built with the system default GCC — isn't just an inconsistency, it caused a real crash: [GH issue #30](https://github.com/Shadowolf7/Vayu-Viewer/issues/30). CEF's `scoped_refptr` uses a Clang-only `[[clang::trivial_abi]]` calling convention that GCC doesn't understand, so a GCC-built `libcef_dll_wrapper` called from Clang-compiled code silently corrupted its own arguments — no build error, just a runtime segfault in the media plugin.

This repo handles it for you: `BootstrapVcpkg.cmake` auto-selects the `x64-linux-alchemy-clang` triplet (chainloads Clang for vcpkg's own port builds) whenever `CMAKE_CXX_COMPILER` matches `clang`, so the Clang flags below are safe as-is. The catch — **switching compilers on an already-configured tree forces a full vcpkg rebuild.** The triplet name is part of vcpkg's binary-cache key, so none of the ~300 already-built packages carry over; budget the same 30–60+ minutes as a from-scratch configure.

An optional [R2 binary cache](VCPKG-R2.md) can restore matching dependencies.
The setup guide covers pipeline environment variables, read-only developer
access, retention, and rollout checks.

#### Platform notes

- **macOS** — `xcode-os` and `ninja-os` (no arch suffix) pick the host architecture. Use the explicit `-arm64` / `-x64` preset to cross-build (e.g. an arm64 bundle from an Intel Mac).
- **Linux with Clang** (faster builds): append `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_LINKER_TYPE=LLD` to the configure command. The hidden `lld` and `mold` presets set `CMAKE_LINKER_TYPE`, and `ccache` and `sccache` set the compiler launcher, for a preset of your own in `CMakeUserPresets.json`, for example `{"name": "mine", "inherits": ["ninja-os", "mold", "ccache"]}`. A compiler cache needs `/Z7`-style debug info on MSVC, which this tree does not use, so the launcher presets are for Linux and macOS. See [vcpkg](#vcpkg) above for why compiler selection is safe on a fresh configure but costly on an existing one.
- **Linux linker** — `mold` is picked automatically via `CMAKE_LINKER_TYPE` when installed (`indra/cmake/00-Common.cmake`), regardless of which compiler you select. Pass `-DCMAKE_LINKER_TYPE=LLD` (or any other value CMake supports) explicitly to override it.
- **vcpkg triplet** — chosen from the generator, the architecture and `AL_ISA_TIER`: `<arch>-<os>-alchemy[-avx2|-avx512][-release]`, where `-release` means a single-configuration tree that is not Debug and skips the debug ports. Pass `-DVCPKG_TARGET_TRIPLET=<name>` to choose one yourself; CI does, to take release-only ports under a multi-config generator.

### Workflow presets (one-shot configure + build)

Workflow presets run configure and build as a single command. Useful for CI and one-off release builds:

```
cmake --workflow --preset ninja-os-release
cmake --workflow --preset vs2026-os-release
cmake --workflow --preset xcode-os-release
cmake --workflow --preset vs2026-os-fullopt-release
```

See `workflowPresets` in the generator files under `indra/cmake/presets/` for the full set.

## Build

After configuring, build with CMake or your IDE.

### From the command line

```bash
# Multi-config generators (VS, Xcode, Ninja Multi-Config)
cmake --build <build-dir> --config Release

# Or using Ninja Multi-Config directly (Linux canonical)
ninja -C build-Linux-ninja-perf -f build-Release.ninja vayu-bin

# Or use a build preset
cmake --build --preset ninja-os-release
```

### From an IDE

```
# Visual Studio
start .\build-Windows-vs2026-os\Vayu.slnx

# Xcode
open ./build-Darwin-xcode-os-arm64/Vayu.xcodeproj
```

> `.slnx` is the newer Visual Studio solution format. Requires VS 2026.

### Output locations

The viewer executable lands under `build-<OS>-<preset>/newview/<Config>/`:

| Platform | Path                                                        |
|:---------|:------------------------------------------------------------|
| Windows  | `build-Windows-<preset>\newview\<Config>\<ChannelName>.exe` |
| macOS    | `build-Darwin-<preset>/newview/<Config>/<ChannelName>.app`  |
| Linux    | `build-Linux-<preset>/newview/<Config>/<ChannelName>`       |

`<ChannelName>` follows `AL_CHANNEL` (default `Vayu Test` → `VayuTest.exe` / `VayuTest.app`).

## Configuration types

Ninja and Xcode presets are multi-config; Visual Studio presets always are. Every configure preset has a build preset per configuration, named `<preset>-<config>` in lower case: `ninja-os-debug`, `ninja-os-optdebug`, `ninja-os-relwithdebinfo`, `ninja-os-release`, and likewise for the others. `--config <Config>` on the command line overrides the preset's configuration.

| Configuration    | Libraries | Asserts | Notes                                                 |
|:-----------------|:----------|:--------|:------------------------------------------------------|
| `Debug`          | debug     | yes     | Slowest; full debugging of viewer and deps            |
| `OptDebug`       | release   | yes     | Optimized libs with debuggable viewer code            |
| `RelWithDebInfo` | release   | yes     | Default for Ninja presets; ship-adjacent with asserts |
| `Release`        | release   | no      | Ship builds                                           |

## Build options

Override any option at configure time with `-D<NAME>=<VALUE>`. For example:

```
cmake -S indra --preset ninja-os -DAL_BUILD_TESTS=ON -DAL_USE_FMODSTUDIO=ON
```

Options are defined in [`indra/CMakeLists.txt`](../indra/CMakeLists.txt). The most commonly used:

### Build targets

| Option                  | Default | Description                                                           |
|:------------------------|:--------|:----------------------------------------------------------------------|
| `AL_BUILD_VIEWER`          | ON      | Build the viewer executable                                           |
| `AL_BUILD_APPEARANCE_UTILITY` | OFF     | Build the appearance utility                                          |
| `AL_BUILD_TESTS`         | OFF     | Build and run unit + integration tests                                |
| `AL_ENABLE_GL_TESTS`     | ON      | Run the tests that render on a hidden window; off, they are built and registered disabled (needs `AL_BUILD_TESTS`) |
| `AL_BUILD_DOCS`          | OFF     | Add the `doc` target (API documentation with Doxygen)                 |
| `AL_VCPKG_INSTALL`       | ON      | Let configure run `vcpkg install` when the manifest, the registry configuration, the triplets or the feature list changed; off leaves the ports to you |
| `AL_BUILD_PACKAGE`       | ON      | Add the `package` target: the CPack archive of the installed tree (zip, tar.xz, dmg) |
| `AL_USE_VELOPACK`        | OFF     | Add the `velopack` target, and the Velopack update client to the viewer |
| `AL_SOURCEID`            | `$sourceid` | Referring agency recorded in `settings_install.xml`                |

### Audio

| Option              | Default | Description                                                          |
|:--------------------|:--------|:---------------------------------------------------------------------|
| `AL_USE_FAUDIO`     | ON      | FAudio audio engine                                                  |
| `AL_USE_OPENAL`     | OFF     | OpenAL audio engine                                                  |
| `AL_USE_FMODSTUDIO` | OFF     | FMOD Studio audio engine (proprietary; `AL_FMODSTUDIO_SDK_DIR` names the SDK, or the Windows installer's registry entry does) |

### Proprietary SDKs

| Option           | Default | Description                                                                 |
|:-----------------|:--------|:----------------------------------------------------------------------------|
| `AL_ENABLE_PROPRIETARY` | OFF | Allow the non-free libraries below                                     |
| `AL_USE_KDU`     | ON      | Kakadu JPEG2000 codec (needs `AL_ENABLE_PROPRIETARY`)                       |
| `AL_USE_DISCORD` | OFF     | Discord presence through the Social SDK (needs `AL_ENABLE_PROPRIETARY`; `AL_DISCORD_SDK_DIR` names the SDK unpacked from the developer portal) |

FMOD isn't fetched by vcpkg — it's a separately-downloaded SDK. Set `FMODSTUDIO_SDK_DIR` to point at it, e.g.:

```
cmake -S indra --preset ninja-os -DUSE_FMODSTUDIO=ON -DFMODSTUDIO_SDK_DIR=/path/to/fmodstudioapi20314linux
```

Without it, configure fails late — after vcpkg's dependency install completes — with `CMake Error at cmake/FMODSTUDIO.cmake:59 ... The "optimized" argument must be followed by a library.` (`indra/cmake/FMODSTUDIO.cmake` falls back to a Windows registry lookup that doesn't exist on Linux/macOS, leaving the library path empty.) On Windows only, an installed FMOD Studio SDK is found automatically via the registry.

### Profiling

| Option                 | Default            | Description                               |
|:-----------------------|:-------------------|:------------------------------------------|
| `AL_USE_TRACY`            | ON for test builds | Tracy profiler support                    |
| `AL_ENABLE_TRACY_ON_DEMAND`  | ON                 | Only profile when a Tracy server connects |
| `AL_ENABLE_TRACY_LOCAL_ONLY` | ON                 | Disallow remote Tracy profiling           |
| `AL_ENABLE_TRACY_GPU`        | OFF                | Tracy GPU profiling                       |

### Optimization / instrumentation

| Option                                            | Default | Description                                                        |
|:--------------------------------------------------|:--------|:-------------------------------------------------------------------|
| `AL_USE_LTO`                     | OFF     | Link Time Optimization                                                      |
| `AL_ISA_TIER`                    | `v3`    | x86-64 level for the viewer and its vcpkg ports: `baseline`, `v2` (SSE4.2), `v3` (AVX2), `v4` (AVX-512). Ignored on macOS |
| `USE_RPMALLOC`                   | ON      | Use rpmalloc as global memory allocator (Linux and Windows)                 |
| `AL_SANITIZERS`                  | empty   | Any of `address`, `undefined`, `thread` (GCC and Clang only)                |
| `AL_ENABLE_WARNINGS_AS_ERRORS`   | ON      | Treat compiler warnings as errors                                           |
| `AL_ENABLE_RELEASE_DEBUG_LOGGING`| Test channel only | Keep debug-level logging in Release builds                        |
| `AL_USE_WEBRTC`                  | ON      | WebRTC voice (off automatically in sanitized builds)                        |

### Media plugins

| Option                   | Default     | Description                                |
|:-------------------------|:------------|:-------------------------------------------|
| `AL_BUILD_CEF_PLUGIN`       | ON          | Chromium Embedded Framework (in-world web) |
| `AL_BUILD_VLC_PLUGIN`       | ON          | VLC media plugin                           |
| `AL_BUILD_GSTREAMER_PLUGIN` | ON on Linux | GStreamer media plugin (Linux only)        |
| `AL_BUILD_EXAMPLE_PLUGIN`   | ON          | Reference/example plugin                   |

### Platform-specific

| Option           | Default     | Description                                            |
|:-----------------|:------------|:-------------------------------------------------------|
| `AL_USE_OPENXR`     | OFF         | OpenXR VR support (experimental)                       |
| `AL_USE_SDL_WINDOW` | ON on Linux | SDL-based window management (Linux only; GL through EGL on Wayland and X11 alike) |

### Crash reporting

| Option                        | Default | Description                                |
|:------------------------------|:--------|:-------------------------------------------|
| `AL_USE_SENTRY`                  | OFF     | Sentry crash reporting                     |
| `AL_ENABLE_CRASH_REPORTING`   | OFF     | Send crash reports from this build         |

Every option the project defines carries the `AL_` prefix. Booleans use one of
three verbs: `AL_BUILD_<x>` produces a target or artifact, `AL_USE_<x>` pulls
in a dependency or picks a backend, `AL_ENABLE_<x>` switches a behaviour.
Values are `AL_<NOUN>`. Configuring with a name from before this scheme
prints a warning naming the replacement.

See [`indra/CMakeLists.txt`](../indra/CMakeLists.txt) for the complete list.

## CMake style

The CMake files are formatted with [gersemi](https://github.com/BlankSpruce/gersemi) (`pip install gersemi`); the configuration is `.gersemirc` at the repository root, and it reads the project's own command definitions from `indra/cmake` so `al_add_test` and friends format like the built-ins. Format what you touched before committing:

```
gersemi -i indra/CMakeLists.txt indra/cmake/*.cmake indra/*/CMakeLists.txt
```

`gersemi --check` on the same paths reports what would change without changing it.

## Running tests

Enable tests at configure time:

```
cmake -S indra --preset <preset> -DAL_BUILD_TESTS=ON
```

Four tests drive a Python peer (`llleap`, `llprocess`, `llsdserialize`, `llcorehttp`); they need a Python 3 interpreter with the `llsd` package (`pip install -r requirements.txt`, in a venv if you like) and are registered disabled when configure finds none. Nothing else in the build runs Python.

The `llrender` suites render on a hidden SDL window with the platform's own GL -- WGL on Windows, EGL on Linux, and where Linux has no display SDL's offscreen driver over Mesa (set `LIBGL_ALWAYS_SOFTWARE=1` for llvmpipe on a machine with no GPU). A host with no GL 4.1 to give, such as a CI runner without a graphics driver, configures with `-DAL_ENABLE_GL_TESTS=OFF`: those suites still build, and CTest reports them as not run rather than failed. They carry the label `gl`, so `ctest -LE gl` skips them for one run.

Build, then run with CTest:

```
cmake --build <build-dir> --config RelWithDebInfo
ctest --test-dir <build-dir> --output-on-failure
```

Unit tests live alongside the library they cover in `indra/<library>/tests/`, written against the TUT (Template Unit Test) framework. Integration tests are in `indra/integration_tests/`.

## Editing XUI

`indra/newview/skins/xui.xsd` is the widget vocabulary: every registered tag, the attributes its parameter block answers to, the parameter elements it takes and the tags valid below it. Point an XML editor at it and a XUI file gets completion and a warning on a name no widget has.

The file is written out of the viewer's own registries, since the viewer is the only place all of them exist: run a developer build, open XUI Studio (Advanced &gt; XUI / Colors &gt; XUI Studio) and press **Schema**. `llui_libtest --schema` writes the same thing for the widgets `llui` registers, which is the part a test in that library can check.

VS Code, with the Red Hat XML extension:

```json
"xml.fileAssociations": [
  { "pattern": "**/skins/**/xui/**/*.xml", "systemId": "indra/newview/skins/xui.xsd" }
]
```

It is regenerated rather than edited, and it is permissive where XUI is ambiguous. A parameter may be written as an attribute or as a nested element, and a colour, image, font or setting name is a string whose vocabulary lives in another file. Those are for the tool's lint to check, not a schema.

A few files under `xui/` are data rather than widget trees — `strings.xml`, `mime_types.xml`, the `llsd` files, the `contents` tables. The schema has no root for those and an editor will say so on their first line; the association is by path and cannot tell them apart.

## Packaging

The install rules in `indra/cmake/ViewerInstall.cmake` are the package manifest. After every link of the viewer they stage the tree it runs from into the build directory (`newview/<Config>/`, or `newview/<Config>/<Channel>.app` on macOS). The same rules write a clean tree anywhere:

```
cmake --install build-<OS>-<preset> --config Release --prefix <dir>
```

The archive of that tree comes from CPack — a `.zip` on Windows, a `.tar.xz` on Linux, a `.dmg` on macOS — into the build directory, named `Alchemy[_<channel>]_<version>_<arch>`:

```
cpack --config build-<OS>-<preset>/CPackConfig.cmake -C Release
```

(or the `package` target). Release archives on Linux and macOS are stripped of debug information on the way. Every package is written with its SHA-256 beside it (`<package>.sha256`, in `sha256sum` form). `-DAL_BUILD_PACKAGE=OFF` leaves CPack out; the install rules stay.

The source package is the committed tree of the repository and its submodules at the checked-out commit — what `git ls-files --recurse-submodules` names, nothing the build wrote into the source tree — as `Alchemy_<version>_src.tar.xz`, every entry stamped with the commit's time:

```
cpack --config build-<OS>-<preset>/CPackSourceConfig.cmake
```

(or the `package_source` target under Ninja). Uncommitted changes are not in it, and cpack says so.

The Windows installer and the update packages come from [Velopack](https://velopack.io): configure with `-DAL_USE_VELOPACK=ON`, run `dotnet tool restore` once so the `vpk` tool is available, and build the `velopack` target. It installs into `newview/velopack/<Config>/app` and writes the installer and the update feed to `newview/velopack/<Config>/Releases`.

The third-party attribution is generated, not kept by hand: `cmake/Attribution.cmake` reads every installed port's `vcpkg.spdx.json` and `copyright` and writes `app_settings/packages-info.txt` (what the About floater's Licences tab shows) and `licenses.txt` (every licence text). What vcpkg cannot know — the pieces under `indra/externals/`, the SDKs from outside vcpkg, and a holder or licence a port's files do not state — is in `cmake/attribution.json`, as is the list of installed ports that ship nothing and are skipped: build tools, empty ports that stand for a system library, and what is built only for those. A newly added port whose `vcpkg.json` declares no `license` stops the build with its name; fix the port, add an override to the table, or, if the viewer ships none of it, skip it with the reason (and the platform, when the port is empty only on some).

On macOS the install step signs the bundle inside out — ad-hoc, or with `-DAL_ENABLE_SIGNING=ON -DAL_SIGNING_IDENTITY=<Developer ID>` — so the CEF helpers keep their sandbox entitlements. On Linux the binaries carry an `$ORIGIN`-relative RPATH and find the data one directory above the executable, so the tree runs from wherever it is unpacked.

XUI files under the viewer skins are tracked as dependencies of the packaging manifest, so editing one and rebuilding the package target refreshes the packaged UI assets rather than leaving stale XUI content behind.

## Troubleshooting

### Configure fails: `indra/dullahan` has no `CMakeLists.txt`

The Dullahan CEF wrapper is a git submodule. If you cloned without `--recurse-submodules`, `indra/dullahan` is empty and CMake configure stops with an error like:

```
CMake Error at CMakeLists.txt (add_subdirectory):
  The source directory .../indra/dullahan does not contain a CMakeLists.txt file.
```

Fetch the submodule, then re-run configure:

```
git submodule update --init --recursive
```

### First `cmake -S indra --preset ...` takes forever

Expected on the first run: vcpkg downloads and builds every C/C++ dependency from source. Budget **30–60+ minutes** and several GB of disk. Subsequent configures reuse the vcpkg cache and finish in seconds.

If the run produces no output for a very long time it usually isn't hung — check CPU and disk activity before killing it.

### A build freezes the whole system, not just the terminal

Large parallel C++ builds (RelWithDebInfo especially: debug info + `-O3`, a big precompiled header) can use more RAM per parallel job than a many-core, modest-RAM machine has to spare. Ninja and Make both default to one job per core, so on e.g. a 12-thread laptop with 13GB RAM, a full-parallelism build can overcommit memory badly enough to cause severe swap thrashing — which can make the whole desktop unresponsive, not just slow the build down, even before the kernel's OOM killer reacts.

On Linux (systemd + cgroup v2), wrap the build command with [`scripts/safe-build.sh`](../scripts/safe-build.sh):

```
scripts/safe-build.sh cmake --build --preset ninja-os-relwithdebinfo
```

This runs the build in a systemd user-scope cgroup with `MemoryHigh` set to ~70% of total system RAM (computed at run time, not hardcoded), so the kernel proactively reclaims — including swapping — from the build's cgroup specifically once it crosses that threshold, rather than letting it compete unbounded with the rest of the system. Falls back to running the command unwrapped if `systemd-run`/cgroup v2 isn't available (non-Linux, or older systems).

### CMake is too old

Vayu requires CMake 4.0+. If your distro ships something older, install a newer version via pip inside your venv:

```
pip install --upgrade cmake ninja
```

### `vpk` command not found (the `velopack` target fails)

Velopack needs the `vpk` .NET tool. Install it once per clone:

```
dotnet tool restore
```

### Rust / `cargo` missing during packaging

Velopack invokes `cargo`. Install a stable Rust toolchain:

```
rustup default stable
```

Only needed with `AL_USE_VELOPACK=ON`.

### Warnings fail the build

By default, warnings are treated as errors. New compiler releases sometimes introduce diagnostics the tree hasn't yet cleaned up. Disable fatal warnings at configure time:

```
cmake -S indra --preset vs2026-os -DAL_ENABLE_WARNINGS_AS_ERRORS=OFF
```

### Visual Studio doesn't recognize `Vayu.slnx`

`.slnx` is the newer Visual Studio solution format. Use Visual Studio 2022 17.10+ or Visual Studio 2026, or configure with the `vs2022-os` preset on an older compatible edition.

### `cmake --build --preset ninja-os-release` fails with "no such preset"

You probably configured with a proprietary preset (e.g. `ninja`, without the `-os` suffix). Build presets are tied to configure presets — use the matching build preset for whichever configure preset you used (for example `ninja-release` for `ninja`).

### Linux: missing system headers during vcpkg builds

Double-check the package list for your distro under [Platform setup → Linux](#linux). Common offenders when a package lookup produces an error like `<something>.h not found`:

- `autoconf-archive` — required by several vcpkg ports
- `libxkbcommon-dev`, `libwayland-dev`, `wayland-protocols` — required for SDL window and Wayland support
- `libgstreamer-plugins-base1.0-dev` — required for the GStreamer media plugin

### Linux: window has no titlebar/decorations on Wayland

Upstream vcpkg's `sdl3` port hardcodes `SDL_WAYLAND_LIBDECOR=OFF` since [3.4.14](https://github.com/microsoft/vcpkg/commit/8f57207a8a1b96a4a864e11f85d57b0a252b3ac3) for build reproducibility (host-installed libs shouldn't silently change SDL's capabilities). That disables client-side decorations entirely on compositors with no server-side decoration support (GNOME/Mutter, Weston), regardless of whether `libdecor` is installed.

Fixed via the `indra/vcpkg/ports/sdl3` overlay port, which re-enables the flag — safe here since `indra/cmake/SDL3.cmake` already hard-requires `libdecor-0` on the host at configure time, so this doesn't reintroduce the non-determinism upstream is guarding against. If decorations go missing again after a vcpkg baseline bump, check whether this overlay still matches upstream's portfile (a future SDL3 update could restructure the option or rename it).

### vcpkg curl build fails with "try_run() invoked in cross-compiling mode"

Seen when building the vendored `curl` overlay port (`indra/vcpkg/ports/curl`, pinned to 7.54.1 for Linden's HTTP/1.1 pipelining patch — see [#67](https://github.com/Shadowolf7/Vayu-Viewer/issues/67)) with the Clang preset:

```
CMake Error: try_run() invoked in cross-compiling mode, please set the following cache variables appropriately:
   HAVE_FSETXATTR_5 (advanced)
   HAVE_POSIX_STRERROR_R (advanced)
   HAVE_POLL_FINE_EXITCODE (advanced)
-- Configuring incomplete, errors occurred!
```

Root cause: `indra/vcpkg/triplets/x64-linux-clang-toolchain.cmake` sets `CMAKE_SYSTEM_NAME`/`CMAKE_SYSTEM_PROCESSOR` explicitly (needed so vcpkg builds every port with Clang instead of falling back to the system compiler — see [#30](https://github.com/Shadowolf7/Vayu-Viewer/issues/30)). Setting `CMAKE_SYSTEM_NAME` in a toolchain file makes CMake assume cross-compiling regardless of whether the value actually matches the host, which breaks any port whose CMake build uses `try_run()` for feature detection — curl 7.54.1's does. Most ports never hit this because they don't call `try_run()`.

Fixed by explicitly forcing `set(CMAKE_CROSSCOMPILING OFF CACHE BOOL "")` in that same toolchain file (safe here since it's only ever used for this native x64 Linux build, never a real cross-target). If you see this error, confirm that line is still present — it's easy to lose if the toolchain file gets rewritten.

### `mold` fails with "discarded COMDAT section probably due to an ODR violation" on LTO builds

Seen with `USE_LTO=ON` linking targets that pull in vcpkg static libraries (e.g. `libboost_fiber.a`, `libfmt.a`) — typically a libstdc++ inline symbol like `std::system_error::system_error(std::error_code, char const*)`.

This is a real `mold` regression, not a code or config problem: mold 2.41.0 (still the latest release as of this writing) mishandles COMDAT-group resolution when a link mixes LTO-compiled objects (our own code, compiled with `-flto=thin`) with precompiled non-LTO static libraries (everything vcpkg builds — the vcpkg triplets don't set `-flto`). It's a regression from 2.40.4, reported and fixed upstream — [rui314/mold#1613](https://github.com/rui314/mold/issues/1613), duplicate of [#1565](https://github.com/rui314/mold/issues/1565), fixed by commit [`920a5161`](https://github.com/rui314/mold/commit/920a5161) — but that fix hasn't shipped in a tagged release yet, so distro packages of mold don't have it.

You can confirm it's the false positive rather than a real conflict: the two "conflicting" COMDAT copies are byte-identical (`ar x` the object out of each archive, then `objcopy -O binary --only-section=<section> ... | cmp`).

Workarounds, in order of preference:

1. **Build mold from source** at a commit including `920a5161` (or later) and point this build at it. **Putting it first on `PATH` does not work**, even though `find_program(mold)` at configure time respects `PATH` fine: Clang resolves the bare `-fuse-ld=mold` to `<clang's own InstalledDir>/ld.mold` (verify with `clang++ -fuse-ld=mold -### -o /tmp/x /dev/null`), not via a `PATH` search — so it silently keeps using the system package's `ld.mold` regardless of what a shell-level `PATH` override points at, `systemd-run`/`safe-build.sh` or not. The only proven fix is an absolute path baked into the link flags:
   ```
   -DCMAKE_LINKER_TYPE=DEFAULT \
   -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=/absolute/path/to/fixed/mold \
   -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=/absolute/path/to/fixed/mold \
   -DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=/absolute/path/to/fixed/mold
   ```
   `CMAKE_LINKER_TYPE=DEFAULT` is required, not optional: this repo's `00-Common.cmake` sets `CMAKE_LINKER_TYPE=MOLD` whenever it finds any `mold` on `PATH`, which injects its own bare `-fuse-ld=mold` into the link line. Since Clang takes the *last* `-fuse-ld=` flag, that injected one wins over an absolute-path `CMAKE_EXE_LINKER_FLAGS` unless `CMAKE_LINKER_TYPE` is forced to `DEFAULT` to suppress it — verify with `ninja -t commands <target> | grep -o -- '-fuse-ld=[^ ]*'`, which should show exactly one occurrence, before spending a full build cycle finding out it didn't take.

   Scope the build to a local prefix (`-DCMAKE_INSTALL_PREFIX=...`) if you don't want to replace your system package — an out-of-tree `cmake --install` is enough. Point `CMAKE_EXE_LINKER_FLAGS` at the resulting binary from whatever preset needs it (a personal `CMakeUserPresets.json` entry is a natural place, since the path is local to whoever built it — not portable across machines or checkouts, and needs updating if mold is ever rebuilt elsewhere).
2. **Use LLD instead of mold** for LTO builds: `-DCMAKE_LINKER_TYPE=LLD`. LLD's LTO support is unaffected by this bug.
3. **Turn off `USE_LTO`** if neither is acceptable — not usually worth it, since LTO is the whole point of enabling it.

### Linux: Dullahan / CEF media plugin startup failures and gotchas

The in-world web browser is powered by the Chromium Embedded Framework (CEF) via Dullahan (`indra/dullahan`), built as a dedicated host executable `media_plugin_cef` alongside subprocess helper `dullahan_host`.

Several Linux-specific gotchas can cause CEF initialization to fail or crash at startup (`ContentMainRun failed with exit code 28`):

1. **Linker RPATH single-quote escaping with mold / clang**:
   Because `CMAKE_SKIP_RPATH TRUE` is set for Linux in `indra/cmake/00-Common.cmake`, executables rely on explicit linker options (`target_link_options`).
   - When specifying RPATH in CMake (e.g. `target_link_options(media_plugin_cef PRIVATE "LINKER:--build-id" "LINKER:-rpath,\$ORIGIN:\$ORIGIN/../../lib")`), **do not wrap `$ORIGIN` in single quotes** (`'$ORIGIN'`).
   - GNU `ld` strips nested quotes, but `mold` preserves them literally into the ELF `RUNPATH` header (`RUNPATH '$ORIGIN:$ORIGIN/../../lib'`). glibc's dynamic loader (`ld.so`) only expands unquoted `$ORIGIN` tokens; with literal quotes, `$ORIGIN` fails to expand and the binary falls back to system library paths (e.g. `/lib64/libcef.so`), causing ABI symbol mismatches or loader errors.
   - `dullahan_host` must also carry `RUNPATH` (`$ORIGIN:$ORIGIN/../lib:$ORIGIN/../../lib`) so that CEF child subprocesses (renderer/GPU/utility) resolve `libcef.so` from `lib/`.

2. **CEF resource staging layout contract**:
   In CEF 151+, resource files (`resources.pak`, `chrome_100_percent.pak`, `chrome_200_percent.pak`), `icudtl.dat`, and the `locales/` directory must be staged directly into `bin/llplugin/` alongside `media_plugin_cef` and `dullahan_host` (in addition to `lib/`). If they are missing from `bin/llplugin/`, CEF cannot locate its resource bundle and terminates with `RESULT_CODE_MISSING_DATA` (exit code 28).

3. **Desktop Wayland compositors & Ozone backend**:
   Dullahan uses off-screen rendering (OSR) to draw web surfaces into OpenGL textures. On desktop Linux compositors (such as GNOME Mutter), native Wayland lacks ChromeOS-specific alpha compositing protocols (`zcr_alpha_compositing_v1`), causing Wayland surface initialization to fail. When `DISPLAY` is available, Dullahan forces `--ozone-platform=x11` (XWayland) so OSR initializes cleanly via GLX/EGL.

4. **Embedded first-run and Terms of Service suppression**:
   Some Linux distributions (notably openSUSE) include distribution policies in their Chromium packaging that trigger a modal dialog titled *"Chromium Additional Terms of Service"* if a new profile is launched without embedded suppression flags. Dullahan passes `--no-first-run`, `--no-default-browser-check`, `--disable-fre`, and `--disable-search-engine-choice-screen` to keep CEF fully headless.

5. **`libcef_dll_wrapper` TRIVIAL_ABI Calling Convention**:
   CEF's `scoped_refptr` uses Clang's `[[clang::trivial_abi]]`. If the viewer is compiled with Clang, `libcef_dll_wrapper` (built from source by vcpkg) must also be built with Clang via the chainloaded toolchain file (`x64-linux-clang-toolchain.cmake`). If built with GCC, `CefApp` calling conventions mismatch across the binary boundary, causing a SIGSEGV in `CefInitialize`.

### Still stuck?

- File a build bug at <https://github.com/Shadowolf7/Vayu-Viewer/issues>.

## See also

- [Contributing](../CONTRIBUTING.md)
- [Architecture](ARCHITECTURE.md)
