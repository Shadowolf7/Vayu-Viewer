---
layout: default
---

<p align="center">
  <img alt="Vayu Logo" src="assets/vayu_logo.png" width="200">
</p>

Vayu is a third-party client for [Second Life](https://secondlife.com), forked from [Alchemy Viewer](https://www.alchemyviewer.org), which is itself forked from the official [Linden Lab viewer](https://github.com/secondlife/viewer). See [FEATURES.md](https://github.com/Shadowolf7/Vayu-Viewer/blob/develop/FEATURES.md) for what's already shipped.

## Download

- **Windows** — [latest release](https://github.com/Shadowolf7/Vayu-Viewer/releases/latest): `_Setup.exe` to install, or `_Portable.zip` to run without installing
- **Linux** — [latest release](https://github.com/Shadowolf7/Vayu-Viewer/releases/latest): `.tar.xz`
- **macOS** — not yet available; builds aren't signed/notarized for distribution

See the [releases page](https://github.com/Shadowolf7/Vayu-Viewer/releases) for all versions.

## Building from source

Vayu uses CMake with vcpkg for dependency management. Platform setup, presets, build options, tests, packaging, and troubleshooting all live in [doc/BUILD.md](https://github.com/Shadowolf7/Vayu-Viewer/blob/develop/doc/BUILD.md).

```
git clone --recurse-submodules https://github.com/Shadowolf7/Vayu-Viewer.git vayu-viewer
cd vayu-viewer
python3 -m venv .venv && source .venv/bin/activate   # Windows: .\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
dotnet tool restore                                  # packaging only
cmake -S indra --preset <preset>                     # see BUILD.md for presets
cmake --build build-<OS>-<preset> --config Release
```

## Contribute

File bug reports, suggest enhancements, or open a pull request — see [CONTRIBUTING](https://github.com/Shadowolf7/Vayu-Viewer/blob/develop/CONTRIBUTING.md) for guidelines.

## Acknowledgements

Vayu stands on the work of:

- [Alchemy Viewer](https://www.alchemyviewer.org), the project this is forked from
- [Linden Lab](https://www.lindenlab.com/) and the [Second Life Viewer](https://github.com/secondlife/viewer) contributors

[View on GitHub](https://github.com/Shadowolf7/Vayu-Viewer)
