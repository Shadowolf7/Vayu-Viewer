set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# Match the viewer's own -march (USE_AVX2, indra/cmake/00-Common.cmake) so
# vcpkg-built deps aren't compiled at the compiler's plain x86-64 baseline.
set(VCPKG_C_FLAGS "-march=x86-64-v3")
set(VCPKG_CXX_FLAGS "-march=x86-64-v3")
