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

- **CMake** 3.27+
- **Git**
- **Python** 3.13+ — used for build-time scripts
- **Rust** and **.NET SDK** — only required when producing installer packages (the default; disable with `-DPACKAGE=OFF` to skip)

Install commands are platform-specific; see below.

## Platform setup

### Windows

Install the following:

- [Visual Studio 2026](https://visualstudio.microsoft.com/vs/community/) or Visual Studio 2022 — select the **Desktop development with C++** workload
- [CMake](https://cmake.org/download/) 3.27+
- [Git for Windows](https://git-scm.com/install/windows)
- [Python 3.13+](https://www.python.org/downloads/) — tick **Add Python to PATH** during install
- [Rust](https://rust-lang.org/tools/install/) — run `rustup-init.exe` and accept defaults (packaging only)
- [.NET SDK](https://dotnet.microsoft.com/en-us/download) (packaging only)

Sanity-check in a fresh terminal:

```
cmake --version
python --version
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
    libosmesa6-dev libpipewire-0.3-dev libpulse-dev libsndio-dev libtext-unidecode-perl \
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
    libosmesa6-dev libpipewire-0.3-dev libpulse-dev libsndio-dev libtext-unidecode-perl \
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
    nasm
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

Vayu vendors the [Dullahan](https://github.com/AlchemyViewer/dullahan) CEF wrapper — used by the in-world web media plugin — as a git submodule under `indra/dullahan`. It builds from source as part of the tree, so the submodule must be present before you configure. Clone with `--recurse-submodules`, then set up the Python venv and .NET tools:

```
git clone --recurse-submodules https://github.com/Shadowolf7/Vayu-Viewer.git vayu-viewer
cd vayu-viewer
python3 -m venv .venv
# Windows: .\.venv\Scripts\Activate.ps1
# Unix:    source .venv/bin/activate
pip install -r requirements.txt
dotnet tool restore        # packaging only
```

Already cloned without `--recurse-submodules`? Fetch the submodules before configuring:

```
git submodule update --init --recursive
```

After pulling upstream changes, run the same command to keep the submodule in sync with the revision the tree expects.

## Configure

Build configuration is driven by CMake presets defined in [`indra/CMakePresets.json`](../indra/CMakePresets.json). A preset selects the generator (Visual Studio, Ninja, Xcode), the target architecture, and whether proprietary components are enabled.

List all available presets:

```
cmake -S indra --list-presets
```

### Naming convention

Preset names follow the pattern `<generator>[-<arch>][-os|fmod]`:

- **`-os` suffix** — true open-source baseline. Excludes proprietary components such as KDU JPEG2000 and FMOD audio.
- **`-fmod` suffix** — enables FMOD audio while still leaving the KDU/JPEG2000 path disabled.
- **No `-os` / `-fmod` suffix** — sets `INSTALL_PROPRIETARY=ON` and enables the full proprietary set, including KDU.

Most contributors want the `-os` variants; use `-fmod` when you specifically want FMOD support without the full proprietary stack.

### Common presets

| Preset                                       | Platform | Generator          |
|:---------------------------------------------|:---------|:-------------------|
| `vs2026-os`, `vs2022-os`                     | Windows  | Visual Studio      |
| `ninja-os`                                   | Linux    | Ninja Multi-Config |
| `ninja-fmod`                                 | Linux    | Ninja Multi-Config (FMOD enabled) |
| `ninja-os-clang`                             | Linux    | Ninja Multi-Config (Clang instead of GCC) |
| `ninja-clang-fmod`                           | Linux    | Ninja Multi-Config (Clang + FMOD) |
| `ninja-os-arm64`, `ninja-os-x64`             | macOS    | Ninja Multi-Config |
| `xcode-os`, `xcode-os-arm64`, `xcode-os-x64` | macOS    | Xcode              |

Configure with:

```
cmake -S indra --preset <preset-name>
```

This creates a build tree at `build-<HostSystem>-<preset>/` next to the source — e.g. `build-Windows-vs2026-os/`, `build-Linux-ninja-os/`, `build-Darwin-xcode-os-arm64/`.

The first configure run downloads and builds every vcpkg dependency from source. Expect **30–60+ minutes** and several GB of disk; subsequent configures finish in seconds.

#### Platform notes

- **macOS** — `xcode-os` and `ninja-os` (no arch suffix) pick the host architecture. Use the explicit `-arm64` / `-x64` preset to cross-build (e.g. an arm64 bundle from an Intel Mac).
- **Linux with Clang** (often faster local-iteration builds, better diagnostics): use the `ninja-os-clang` preset (or `ninja-clang` for the proprietary variant) instead of `ninja-os`/`ninja`. Warnings are still fatal under Clang by default (`CMAKE_COMPILE_WARNING_AS_ERROR`, see `indra/cmake/00-Common.cmake:45`) — override with `-DCLANG_DISABLE_FATAL_WARNINGS=TRUE` if needed, same as the GCC/MSVC escape hatches below.
- **Linux linker** — `mold` is picked automatically via `CMAKE_LINKER_TYPE` when installed (`indra/cmake/00-Common.cmake`, `find_program(MOLD_LINKER mold)`), on both the default GCC presets and the Clang ones above. Pass `-DCMAKE_LINKER_TYPE=LLD` (or any other value CMake supports) explicitly to override it.

### Workflow presets (one-shot configure + build)

Workflow presets run configure and build as a single command. Useful for CI and one-off release builds:

```
cmake --workflow --preset ninja-os-release
cmake --workflow --preset ninja-os-clang-release
cmake --workflow --preset ninja-clang-release
cmake --workflow --preset vs2026-os-release
cmake --workflow --preset xcode-os-release
```

See `workflowPresets` in `indra/CMakePresets.json` for the full set.

## Build

After configuring, build with CMake or your IDE.

### From the command line

```
# Multi-config generators (VS, Xcode, Ninja Multi-Config)
cmake --build <build-dir> --config Release

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

> `.slnx` is the newer Visual Studio solution format. Requires VS 2022 17.10+ or VS 2026.

### Output locations

The viewer executable lands under `build-<OS>-<preset>/newview/<Config>/`:

| Platform | Path                                                        |
|:---------|:------------------------------------------------------------|
| Windows  | `build-Windows-<preset>\newview\<Config>\<ChannelName>.exe` |
| macOS    | `build-Darwin-<preset>/newview/<Config>/<ChannelName>.app`  |
| Linux    | `build-Linux-<preset>/newview/<Config>/<ChannelName>`       |

`<ChannelName>` follows `VIEWER_CHANNEL` (default `Vayu Test` → `VayuTest.exe` / `VayuTest.app`).

## Configuration types

Ninja and Xcode presets are multi-config; Visual Studio presets always are. Pick a configuration at build time with `--config <Config>`, or use a build preset that bakes it in.

| Configuration    | Libraries | Asserts | Notes                                                 |
|:-----------------|:----------|:--------|:------------------------------------------------------|
| `Debug`          | debug     | yes     | Slowest; full debugging of viewer and deps            |
| `OptDebug`       | release   | yes     | Optimized libs with debuggable viewer code            |
| `RelWithDebInfo` | release   | yes     | Default for Ninja presets; ship-adjacent with asserts |
| `Release`        | release   | no      | Ship builds                                           |

## Build options

Override any option at configure time with `-D<NAME>=<VALUE>`. For example:

```
cmake -S indra --preset ninja-os -DBUILD_TESTING=ON -DUSE_FMODSTUDIO=ON
```

Options are defined in [`indra/CMakeLists.txt`](../indra/CMakeLists.txt). The most commonly used:

### Build targets

| Option                  | Default | Description                                                           |
|:------------------------|:--------|:----------------------------------------------------------------------|
| `BUILD_VIEWER`          | ON      | Build the viewer executable                                           |
| `BUILD_APPEARANCE_UTIL` | OFF     | Build the appearance utility                                          |
| `BUILD_TESTING`         | OFF     | Build and run unit + integration tests                                |
| `PACKAGE`               | ON      | Produce installer packages after the viewer build (requires Velopack) |
| `USE_VELOPACK`          | OFF     | Use Velopack for installer packaging (instead of NSIS/DMG)            |

### Audio

| Option           | Default | Description                            |
|:-----------------|:--------|:---------------------------------------|
| `USE_OPENAL`     | ON      | OpenAL audio engine                    |
| `USE_FMODSTUDIO` | OFF     | FMOD Studio audio engine (proprietary) |

### Profiling

| Option                 | Default            | Description                               |
|:-----------------------|:-------------------|:------------------------------------------|
| `USE_TRACY`            | ON for test builds | Tracy profiler support                    |
| `USE_TRACY_ON_DEMAND`  | ON                 | Only profile when a Tracy server connects |
| `USE_TRACY_LOCAL_ONLY` | ON                 | Disallow remote Tracy profiling           |
| `USE_TRACY_GPU`        | OFF                | Tracy GPU profiling                       |

### Optimization / instrumentation

| Option                                            | Default | Description                                                        |
|:--------------------------------------------------|:--------|:-------------------------------------------------------------------|
| `USE_LTO`                                         | OFF     | Link Time Optimization                                             |
| `USE_SSE4_2`, `USE_AVX`, `USE_AVX2`               | `USE_AVX2` ON, others OFF | Target SIMD instruction sets (x86_64 only, all three platforms) — `USE_AVX2` already implies FMA/BMI1/BMI2/F16C via the `x86-64-v3` microarchitecture level, no separate flags needed. Requires a Haswell-class Intel CPU (2013+) or Excavator/Zen-class AMD CPU (2015/2017+); set `USE_AVX2=OFF` for older hardware |
| `ENABLE_ASAN`, `ENABLE_UBSAN`, `ENABLE_THREADSAN` | OFF     | Sanitizers (macOS and Linux only)                                  |
| `<COMPILER>_DISABLE_FATAL_WARNINGS`               | OFF     | Don't treat warnings as errors. `<COMPILER>` is `VS`, `GCC`, or `CLANG` |
| `DISABLE_RELEASE_DEBUG_LOGGING`                   | varies  | Strip debug-level logging from Release builds                      |

### Media plugins

| Option                   | Default     | Description                                |
|:-------------------------|:------------|:-------------------------------------------|
| `BUILD_CEF_PLUGIN`       | ON          | Chromium Embedded Framework (in-world web) |
| `BUILD_VLC_PLUGIN`       | ON          | VLC media plugin                           |
| `BUILD_GSTREAMER_PLUGIN` | ON on Linux | GStreamer media plugin (Linux only)        |
| `BUILD_EXAMPLE_PLUGIN`   | ON          | Reference/example plugin                   |

### Platform-specific

| Option           | Default     | Description                                            |
|:-----------------|:------------|:-------------------------------------------------------|
| `USE_NVAPI`      | ON          | NVIDIA NVAPI for GPU profile support (Windows only)    |
| `USE_OPENXR`     | OFF         | OpenXR VR support (experimental)                       |
| `USE_SDL_WINDOW` | ON on Linux | SDL-based window management (Linux only; Wayland path) |

### Crash reporting

| Option                        | Default | Description                                |
|:------------------------------|:--------|:-------------------------------------------|
| `USE_SENTRY`                  | OFF     | Sentry crash reporting                     |
| `RELEASE_CRASH_REPORTING`     | OFF     | Enable crash reporting in Release builds   |
| `NON_RELEASE_CRASH_REPORTING` | OFF     | Enable crash reporting in developer builds |

See [`indra/CMakeLists.txt`](../indra/CMakeLists.txt) for the complete list.

## Running tests

Enable tests at configure time:

```
cmake -S indra --preset <preset> -DBUILD_TESTING=ON
```

Build, then run with CTest:

```
cmake --build <build-dir> --config RelWithDebInfo
ctest --test-dir <build-dir> --output-on-failure
```

Unit tests live alongside the library they cover in `indra/<library>/tests/`, written against the TUT (Template Unit Test) framework. Integration tests are in `indra/integration_tests/`.

## Packaging

Release packages are produced by [Velopack](https://velopack.io). The packaging step runs automatically after a successful build when `PACKAGE=ON` (the default). To skip it during development:

```
cmake -S indra --preset <preset> -DPACKAGE=OFF
```

Velopack also requires `dotnet tool restore` to have been run so the `vpk` CLI is on PATH.

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

Vayu requires CMake 3.27+. If your distro ships something older, install a newer version via pip inside your venv:

```
pip install --upgrade cmake ninja
```

### `vpk` command not found (or packaging step fails)

Velopack needs the `vpk` .NET tool. Install it once per clone:

```
dotnet tool restore
```

If you don't intend to produce installers, disable packaging entirely with `-DPACKAGE=OFF` and skip the Rust / .NET setup.

### Rust / `cargo` missing during packaging

Packaging uses Velopack, which invokes `cargo`. Install a stable Rust toolchain:

```
rustup default stable
```

Only required when `PACKAGE=ON` (the default).

### Warnings fail the build

By default, warnings are treated as errors. New compiler releases sometimes introduce diagnostics the tree hasn't yet cleaned up. Disable fatal warnings for the affected toolchain at configure time:

```
# MSVC / Visual Studio
cmake -S indra --preset vs2026-os -DVS_DISABLE_FATAL_WARNINGS=TRUE

# GCC
cmake -S indra --preset ninja-os -DGCC_DISABLE_FATAL_WARNINGS=TRUE

# Clang
cmake -S indra --preset ninja-os -DCLANG_DISABLE_FATAL_WARNINGS=TRUE
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

### Still stuck?

- File a build bug at <https://github.com/Shadowolf7/Vayu-Viewer/issues>.

## See also

- [Contributing](../CONTRIBUTING.md)
- [Architecture](ARCHITECTURE.md)
