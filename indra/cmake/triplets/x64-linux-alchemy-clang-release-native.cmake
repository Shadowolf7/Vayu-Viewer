set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# Match the viewer's own -march (USE_MARCH_NATIVE, indra/cmake/00-Common.cmake)
# so vcpkg-built deps aren't compiled at the compiler's plain x86-64 baseline.
# Ties the resulting cache to this exact machine's CPU - do not reuse on
# another host.
set(VCPKG_C_FLAGS "-march=native -fPIC")
set(VCPKG_CXX_FLAGS "-march=native -fPIC")
set(VCPKG_CMAKE_POSITION_INDEPENDENT_CODE ON)

# Used instead of x64-linux-alchemy-release by the Clang presets (see
# BootstrapVcpkg.cmake) so vcpkg builds ports with the same compiler as the
# rest of the tree - see GH issue #30.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/x64-linux-clang-toolchain.cmake")
