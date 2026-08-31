# -*- cmake -*-
include_guard()
add_library(ll::libyuv INTERFACE IMPORTED)

find_package(libyuv CONFIG REQUIRED)
target_link_libraries(ll::libyuv INTERFACE yuv)
