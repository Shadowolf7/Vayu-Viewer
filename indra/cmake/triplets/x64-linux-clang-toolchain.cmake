# Chainloaded by x64-linux-alchemy-clang.cmake so vcpkg builds ports (like
# cef-bin's libcef_dll_wrapper) with the same compiler as the outer indra
# tree. Without this, vcpkg falls back to the system default compiler for
# every port it builds from source, regardless of which top-level preset
# chose Clang for the rest of the build - see GH issue #30: CEF's
# scoped_refptr is Clang-only TRIVIAL_ABI, so a GCC-built libcef_dll_wrapper
# linked against Clang-compiled callers corrupts the by-value CefApp argument
# passed to CefInitialize.
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# vcpkg normally populates CMAKE_SYSTEM_NAME/CMAKE_SYSTEM_PROCESSOR itself,
# but defers to the chainloaded toolchain file once one is set (per
# VCPKG_CHAINLOAD_TOOLCHAIN_FILE's documented contract). Without these, ports
# that branch on CMAKE_SYSTEM_PROCESSOR in their own CMakeLists.txt (e.g.
# zlib-ng's elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" AND WITH_SSE2))
# see it empty and fail to configure.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Setting CMAKE_SYSTEM_NAME above makes CMake assume cross-compiling,
# regardless of whether it matches the host - which breaks ports whose
# CMakeLists.txt uses try_run() for feature detection (e.g. curl's
# HAVE_POSIX_STRERROR_R), since CMake refuses to execute a test binary
# it thinks was built for a foreign target. This toolchain is only ever
# used for this native x64 Linux build, so it's always safe to disable.
set(CMAKE_CROSSCOMPILING OFF CACHE BOOL "")
