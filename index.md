---
layout: default
---

<p align="center">
  <img alt="Vayu Logo" src="assets/vayu_logo.png" width="200">
</p>

Vayu is a third-party client for [Second Life](https://secondlife.com), forked from [Alchemy Viewer](https://www.alchemyviewer.org), which is itself forked from the official [Linden Lab viewer](https://github.com/secondlife/viewer). See [FEATURES.md](https://github.com/Shadowolf7/Vayu-Viewer/blob/develop/FEATURES.md) for what's already shipped.

Please pardon the ugliness of this page, if this project takes off I may bother with a proper website.

My upstream, Alchemy, is moving at an incredible pace of development, so many potential features are on hold pending those changes. Vayu should be regarded as being in a state of perpetual Beta.

Vayu is primarily focused on QOL features for drivers and other vehicle enthusiasts. I've merged the best sim-crossing protection features from Firestorm and CoolVL courtesy of the amazing work by Animats and Henri Beauchamp respectively. I've also applied my own fixes towards the same. 

Perhaps my most notable unique feature is Block Compression. Textures are decoded from jpeg2000 and cached in their respective BC format according to type. This enables a radical savings on vRAM use, and so long as the textures are still in-cache prevents them from needing to be decoded from jpeg2000 again. Instead BC is already immediately consumable by the GPU, cutting down on processing needed for decoding and enabling nearly instantaneous loading of textures. By default this feature is on, and set to "basic" profile for a good balance of quality and performance. This feature will work differently on Mac OS and may be slightly slower and lower quality. This feature necessitated implementing a second texture cache. I recommend keeping the BC texture cache as large as possible, while you can keep the regular texture cache very small. Both caches have inherited the advanced and performant cache architecture of CoolVL viewer.

## Download

- **Windows** — [latest release](https://github.com/Shadowolf7/Vayu-Viewer/releases/download/Vayu_Beta_1/Vayu_Beta_26_4_0_1_x86_64_Setup.exe): `_Setup.exe` to install.
- **Windows Portable** — [latest release](https://github.com/Shadowolf7/Vayu-Viewer/releases/download/Vayu_Beta_1/Vayu_Beta_26_4_0_1_x86_64_Portable.zip): `_Portable.zip` to run without installing.
- **macOS (Apple Silicon)** — [latest release](https://github.com/Shadowolf7/Vayu-Viewer/releases/download/Vayu_Beta_1/Vayu_Beta_26_4_0_1_arm64.dmg): `_arm64.dmg` for M-series Macs.
- **macOS (Intel)** — [latest release](https://github.com/Shadowolf7/Vayu-Viewer/releases/download/Vayu_Beta_1/Vayu_Beta_26_4_0_1_x86_64.dmg): `_x86_64.dmg` for Intel Macs.
- **Linux** — [latest release](https://github.com/Shadowolf7/Vayu-Viewer/releases/download/Vayu_Beta_1/Vayu_Beta_26_4_0_1_x86_64.tar.xz): `.tar.xz` application bundle.

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
