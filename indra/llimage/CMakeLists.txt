# -*- cmake -*-

add_library (llimage STATIC)

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
    llimageblockcompressor.cpp
    llbctexturecache.cpp
    bc7e/bc7enc.cpp
    bc7e/rgbcx.cpp
    )

set_source_files_properties(
    bc7e/bc7enc.cpp
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
    llimageblockcompressor.h
    llbctexturecache.h
    bc7e/bc7enc.h
    bc7e/rgbcx.h
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
    )

target_precompile_headers(llimage REUSE_FROM llprecompiled)

# Add tests
if (BUILD_TESTING)
  SET(llimage_TEST_SOURCE_FILES
    llimageworker.cpp
    llimageblockcompressor.cpp
    llbctexturecache.cpp
    )
  set_source_files_properties(
    llimageblockcompressor.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_SOURCE_FILES "bc7e/bc7enc.cpp;bc7e/rgbcx.cpp"
    )
  set_source_files_properties(
    llimageworker.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_SOURCE_FILES "llimageblockcompressor.cpp;llbctexturecache.cpp;bc7e/bc7enc.cpp;bc7e/rgbcx.cpp"
    )
  LL_ADD_PROJECT_UNIT_TESTS(llimage "${llimage_TEST_SOURCE_FILES}")
endif ()



