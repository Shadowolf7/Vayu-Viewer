# -*- cmake -*-
include_guard()

include(OpenGL)

add_library( ll::pluginlibraries INTERFACE IMPORTED )

if (WINDOWS)
  target_link_libraries( ll::pluginlibraries INTERFACE
      wsock32
      ws2_32
      Iphlpapi
      psapi
      advapi32
      user32
      )
endif (WINDOWS)

target_link_libraries( ll::pluginlibraries INTERFACE OpenGL::GL)

target_include_directories( ll::pluginlibraries INTERFACE ${INDRA_SOURCE_DIR}/llimage ${INDRA_SOURCE_DIR}/llrender)

# Slim per-plugin host executables (media_plugin_example, media_plugin_libvlc, ...)
# only need llplugin's child-side code (llpluginprocesschild.cpp etc). mold has an
# archive-extraction ordering bug that can still pull in llplugin's newview-only
# host-side code (llpluginclassmedia.cpp), which references newview's gSavedSettings
# and fails to link. Plugins that additionally link a substantial third-party static
# lib (CEF, GStreamer) happen to avoid the bad extraction order and are unaffected.
# Force a non-mold linker for a target as a workaround; no-op unless mold is in use.
function(ll_plugin_workaround_mold_archive_bug target)
  if (LINUX AND CMAKE_EXE_LINKER_FLAGS MATCHES "mold")
    target_link_options(${target} PRIVATE "-fuse-ld=lld")
  endif ()
endfunction()
