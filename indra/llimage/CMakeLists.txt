# -*- cmake -*-

add_library (llimage STATIC)

find_program(ISPC_EXECUTABLE ispc DOC "Intel Implicit SPMD Program Compiler")
if (ISPC_EXECUTABLE)
  set(ISPC_TARGETS "sse2-i32x4,sse4-i32x4,avx1-i32x8,avx2-i32x8")
  set(BC7E_ISPC_OUTPUTS
      ${CMAKE_CURRENT_BINARY_DIR}/bc7e/bc7e.o
      ${CMAKE_CURRENT_BINARY_DIR}/bc7e/bc7e_sse2.o
      ${CMAKE_CURRENT_BINARY_DIR}/bc7e/bc7e_sse4.o
      ${CMAKE_CURRENT_BINARY_DIR}/bc7e/bc7e_avx.o
      ${CMAKE_CURRENT_BINARY_DIR}/bc7e/bc7e_avx2.o
  )
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bc7e)
  add_custom_command(
      OUTPUT ${BC7E_ISPC_OUTPUTS} ${CMAKE_CURRENT_SOURCE_DIR}/bc7e/bc7e_ispc.h
      COMMAND ${ISPC_EXECUTABLE} -O3 --pic --opt=fast-math --target=${ISPC_TARGETS} -h ${CMAKE_CURRENT_SOURCE_DIR}/bc7e/bc7e_ispc.h -o ${CMAKE_CURRENT_BINARY_DIR}/bc7e/bc7e.o ${CMAKE_CURRENT_SOURCE_DIR}/bc7e/bc7e.ispc
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bc7e/bc7e.ispc
      COMMENT "Compiling bc7e.ispc with ISPC multi-target (SSE2, SSE4, AVX, AVX2)"
  )
endif ()

target_sources(llimage
    PRIVATE
    llimagebmp.cpp
    llimage.cpp
    llimagedimensionsinfo.cpp
    llimagedxt.cpp
    llimagefilter.cpp
    llimagej2c.cpp
    llimagejpeg.cpp
    llimagepng.cpp
    llimagetga.cpp
    llimagewebp.cpp
    llimageavif.cpp
    llimageworker.cpp
    llpngwrapper.cpp
    vayuimageblockcompressor.cpp
    vayubctexturecache.cpp
    bc7e/rgbcx.cpp
    ${BC7E_ISPC_OUTPUTS}
    )

set_source_files_properties(
    bc7e/rgbcx.cpp
    PROPERTIES
    COMPILE_FLAGS "-Wno-unused-function -Wno-unused-variable"
    )

target_sources(llimage
    PUBLIC
    FILE_SET HEADERS
    FILES
    llimage.h
    llimagebmp.h
    llimagedimensionsinfo.h
    llimagedxt.h
    llimagefilter.h
    llimagej2c.h
    llimagejpeg.h
    llimagepng.h
    llimagetga.h
    llimagewebp.h
    llimageavif.h
    llimageworker.h
    llmapimagetype.h
    llpngwrapper.h
    vayuimageblockcompressor.h
    vayubctexturecache.h
    bc7e/rgbcx.h
    bc7e/bc7e_ispc.h
    )

target_include_directories( llimage  INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})
# Libraries on which this library depends, needed for Linux builds
# Sort by high-level to low-level
if (USE_KDU)
  target_link_libraries(llimage PUBLIC llkdu)
else ()
  target_link_libraries(llimage PUBLIC llimagej2coj)
endif ()

target_link_libraries(llimage
    PUBLIC
    llfilesystem
    llmath
    llcommon
    ll::libwebp
    ll::libavif
    ll::libpng
    ll::libjpeg
    ll::libyuv
    )

target_precompile_headers(llimage REUSE_FROM llprecompiled)

# Add tests
if (BUILD_TESTING)
  SET(llimage_TEST_SOURCE_FILES
    llimageworker.cpp
    vayuimageblockcompressor.cpp
    vayubctexturecache.cpp
    )
  set_source_files_properties(
    vayubctexturecache.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_LIBRARIES "llfilesystem"
    )
  set_source_files_properties(
    vayuimageblockcompressor.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_SOURCE_FILES "bc7e/rgbcx.cpp;${BC7E_ISPC_OUTPUTS}"
    LL_TEST_ADDITIONAL_LIBRARIES "ll::libyuv"
    )
  set_source_files_properties(
    llimageworker.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_SOURCE_FILES "vayuimageblockcompressor.cpp;vayubctexturecache.cpp;bc7e/rgbcx.cpp;${BC7E_ISPC_OUTPUTS}"
    LL_TEST_ADDITIONAL_LIBRARIES "llfilesystem;ll::libyuv"
    )
  LL_ADD_PROJECT_UNIT_TESTS(llimage "${llimage_TEST_SOURCE_FILES}")
endif ()



