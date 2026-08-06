set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)

set(VCPKG_OSX_DEPLOYMENT_TARGET 14.0)

# Match the viewer's own -march (USE_AVX2, indra/cmake/00-Common.cmake, the
# BUILD_TARGET_IS_X86_64 branch) so vcpkg-built deps aren't compiled at the
# compiler's plain baseline. Not applicable to arm64-osx-alchemy — the
# viewer never sets an ISA flag on Apple Silicon.
set(VCPKG_C_FLAGS "-march=x86-64-v3")
set(VCPKG_CXX_FLAGS "-march=x86-64-v3")

if(PORT MATCHES "hunspell")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()

if(PORT MATCHES "openal-soft")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
