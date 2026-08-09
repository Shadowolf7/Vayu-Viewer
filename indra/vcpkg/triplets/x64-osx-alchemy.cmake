set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)

set(VCPKG_OSX_DEPLOYMENT_TARGET 14.0)

# Match the viewer's own AVX2 tier (USE_AVX2, indra/cmake/00-Common.cmake,
# the BUILD_TARGET_IS_X86_64 branch) so vcpkg-built deps aren't compiled at
# the compiler's plain baseline. Not applicable to arm64-osx-alchemy — the
# viewer never sets an ISA flag on Apple Silicon.
#
# Spelled out as individual -m flags rather than "-march=x86-64-v3": that
# psABI microarchitecture-level alias is a GCC/Linux-clang convention (see
# the x64-linux-alchemy* triplets, where it works fine) that Apple's clang
# driver rejects outright ("unsupported argument 'x86-64-v3' to option
# '-march='"). The individual feature flags are recognized on every clang
# target, including Darwin.
#
# -arch x86_64 is included here too, not just via VCPKG_OSX_ARCHITECTURES
# above: that variable only reaches CMAKE_OSX_ARCHITECTURES, which CMake
# applies as a per-target property on normal compiled targets. Some ports
# (e.g. libpng's symbols.out generation) invoke the compiler directly from
# a `cmake -P` script-mode custom command with no target to attach that
# property to, so on GitHub's Apple Silicon runners (macos-26) it silently
# falls back to the host's native arm64 — which then rejects these x86-only
# -m flags. Forcing -arch x86_64 into the flags themselves makes every
# compiler invocation target x64 regardless of how the port builds.
set(VCPKG_C_FLAGS "-arch x86_64 -msse4.2 -mavx -mavx2 -mfma -mbmi -mbmi2 -mf16c -mlzcnt -mmovbe")
set(VCPKG_CXX_FLAGS "-arch x86_64 -msse4.2 -mavx -mavx2 -mfma -mbmi -mbmi2 -mf16c -mlzcnt -mmovbe")

if(PORT MATCHES "hunspell")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()

if(PORT MATCHES "openal-soft")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
