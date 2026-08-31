set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)

# Match the viewer's own /arch:AVX2 (USE_AVX2, indra/cmake/00-Common.cmake)
# so vcpkg-built deps aren't compiled at the compiler's plain baseline.
set(VCPKG_C_FLAGS "/arch:AVX2")
set(VCPKG_CXX_FLAGS "/arch:AVX2")

set(VCPKG_C_FLAGS_RELEASE "")
set(VCPKG_CXX_FLAGS_RELEASE "/std:c++20 /Zc:__cplusplus")

if(PORT MATCHES "faudio")
    set(VCPKG_LIBRARY_LINKAGE static)
endif()

if(PORT MATCHES "openal-soft")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()

if(PORT MATCHES "^webrtc$")
    set(VCPKG_BUILD_TYPE release)
endif()
