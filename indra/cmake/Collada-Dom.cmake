# -*- cmake -*-
include_guard()
include(ZLIBNG)

add_library( ll::colladadom INTERFACE IMPORTED )

find_path(COLLADA_DOM_INCLUDE_DIRS NAMES dae.h PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/collada-dom" REQUIRED NO_DEFAULT_PATH)

target_include_directories(ll::colladadom SYSTEM INTERFACE "${COLLADA_DOM_INCLUDE_DIRS}" "${COLLADA_DOM_INCLUDE_DIRS}/1.4")

find_library(COLLADA14_LIBRARY_RELEASE
    NAMES collada14dom
    PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
    REQUIRED
    NO_DEFAULT_PATH
)

target_link_libraries(ll::colladadom INTERFACE optimized ${COLLADA14_LIBRARY_RELEASE})

find_library(COLLADA14_LIBRARY_DEBUG
    NAMES collada14dom
    PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
    NO_DEFAULT_PATH
)

if (NOT COLLADA14_LIBRARY_DEBUG STREQUAL "COLLADA14_LIBRARY_DEBUG-NOTFOUND")
    target_link_libraries(ll::colladadom INTERFACE debug ${COLLADA14_LIBRARY_DEBUG})
elseif (LINUX)
    # Release-only vcpkg triplets (VCPKG_BUILD_TYPE release, e.g. the
    # *-release-native perf triplets) never populate debug/lib. Only safe to
    # fall back to the Release archive on Linux, where there's no separate
    # debug CRT (VCPKG_CRT_LINKAGE dynamic) to mismatch against.
    target_link_libraries(ll::colladadom INTERFACE debug ${COLLADA14_LIBRARY_RELEASE})
endif()

find_package(minizip CONFIG REQUIRED)

add_library(ll::minizip INTERFACE IMPORTED)
target_link_libraries(ll::minizip INTERFACE MINIZIP::minizip ll::zlib-ng)

find_package(LibXml2 REQUIRED)

add_library(ll::libxml2 INTERFACE IMPORTED)
target_link_libraries(ll::libxml2 INTERFACE LibXml2::LibXml2)

target_link_libraries(ll::colladadom INTERFACE ll::libxml2 ll::minizip)
